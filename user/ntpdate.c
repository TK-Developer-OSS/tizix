/* user/ntpdate.c - JST 時刻を取得してシステム時計を書き換える。
 *
 *   本物の NTP(UDP/123)は net.bin(`net &`)が TCP 専用中継のため使えない
 *   ([[net-relay-daemon]] 系の制約)。代わりに atcli.c と同じ手口で
 *   python/at_modem.py(ESP-AT テストベッド、127.0.0.1:8080)へ AT+CIPSTART
 *   させ、その先で python/ntpdate_srv.py(既定 127.0.0.1:2123)が接続直後に
 *   "TIME <epoch>\n" を能動的に送ってくるのを 1 行受信して time_set() する。
 *
 *   epoch はサーバ側で JST(UTC+9)を加算済みの値。tizix にタイムゾーン概念は
 *   無く、KW_EPOCH_SEC は「date でそのまま表示すれば正しく見える」値を
 *   保持する方針なので、ここでも同じ約束に合わせる。
 *
 *   使い方: `net &` を起動し、host 側で at_modem.py と ntpdate_srv.py を
 *   立てておいてから実行する。
 *     ntpdate [host] [port]   at_modem.py にダイヤルさせる先
 *                             (既定 127.0.0.1 2123 = ntpdate_srv.py)
 *
 *   time_set(0x004D) は FIXED_SYMS で全コマンド共通に結線済み
 *   (di/ei はカーネル側 kernel.c の実装内で閉じる。[[kernel-owns-cwd]] 系と
 *   同様、ユーザー側は素の呼び出しでよい)。
 */
#include "stdio.h"
#include "string.h"
#include "netcli.h"

#define AT_MODEM_HOST "127.0.0.1:8080"

extern unsigned int getticks(void);
extern void time_set(unsigned long sec) __sdcccall(0);

static void write_all(const char *s)
{
    unsigned int len, sent, n;

    len = strlen(s);
    sent = 0;
    while (sent < len) {
        n = (unsigned int)net_write(s + sent, (int)(len - sent));
        if (n == 0)
            continue;
        sent += n;
    }
}

/* timeout(1/100秒)無通信でタイムアウトし 0 を返す */
static unsigned int net_read_byte(char *out, unsigned int timeout)
{
    unsigned int t0 = getticks();
    for (;;) {
        if (net_read(out, 1) == 1)
            return 1;
        if ((unsigned int)(getticks() - t0) > timeout)
            return 0;
    }
}

int main(int argc, char **argv)
{
    char host[40];
    char port[8];
    char cmd[96];
    char rb[40];
    unsigned char ri, k, p, found;
    char ch;
    unsigned long epoch;

    strcpy(host, "127.0.0.1");
    strcpy(port, "2123");
    if (argc >= 1 && argv[0])
        strcpy(host, argv[0]);
    if (argc >= 2 && argv[1])
        strcpy(port, argv[1]);

    if (net_connect(AT_MODEM_HOST) != 0) {
        puts("ntpdate: connect failed (net daemon down, or at_modem.py not listening)\n");
        return 1;
    }

    strcpy(cmd, "AT+CIPSTART=\"TCP\",\"");
    strcat(cmd, host);
    strcat(cmd, "\",");
    strcat(cmd, port);
    strcat(cmd, "\r\n");
    write_all(cmd);

    /* CIPSTART 自体の応答("\r\nCONNECT\r\n\r\nOK\r\n" 等)は行単位で読み捨て、
     * "TIME" を含む行を拾う(ntpdate_srv.py が接続直後に能動送信)。
     * at_modem.py は受信データに "+IPD,<len>:" を前置して relay するので
     * 行頭一致ではなく部分一致で探す(atcli での実地確認で判明: 実際には
     * "+IPD,16:TIME 1789752244" のような形で届く)。 */
    ri = 0;
    found = 0;
    for (;;) {
        if (net_read_byte(&ch, 500) == 0) { rb[0] = '\0'; break; }
        if (ch == '\n') {
            rb[ri] = '\0';
            p = 0;
            while (rb[p]) {
                if (rb[p] == 'T' && rb[p + 1] == 'I' && rb[p + 2] == 'M' && rb[p + 3] == 'E') {
                    found = 1;
                    break;
                }
                p++;
            }
            if (found) { ri = p; break; }
            ri = 0;
            continue;
        }
        if (ri < 39)
            rb[ri++] = ch;
        else
            ri = 0;          /* 行が長すぎる(想定外の応答) → 捨てて次の行へ */
    }

    write_all("AT+CIPCLOSE\r\n");
    net_read_byte(&ch, 100); /* 応答は深追いしない */
    net_close();

    if (!found) {
        puts("ntpdate: no TIME response (at_modem.py / ntpdate_srv.py down?)\n");
        return 1;
    }

    k = ri + 4;                /* "TIME" の直後から数字を探す */
    while (rb[k] == ' ')
        k++;
    if (rb[k] < '0' || rb[k] > '9') {
        puts("ntpdate: no TIME response (at_modem.py / ntpdate_srv.py down?)\n");
        return 1;
    }
    epoch = 0;
    while (rb[k] >= '0' && rb[k] <= '9') {
        /* epoch*10 = epoch*8 + epoch*2 (シフトのみ、__mullong を引かない) */
        epoch = (epoch << 3) + (epoch << 1) + (unsigned long)(rb[k] - '0');
        k++;
    }

    time_set(epoch);
    puts("ntpdate: JST time set\n");
    return 0;
}
