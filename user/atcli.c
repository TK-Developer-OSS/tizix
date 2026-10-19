/* user/atcli.c - python/at_modem.py(ESP-AT テストベッド)を実際に AT
 *   コマンドで叩く実証用コマンド。net.bin(`net &`)経由で 127.0.0.1:8080
 *   (at_modem.py 自身)へダイヤルし、接続確立後の生バイトストリームへ
 *   本物の AT コマンド列(AT / AT+CWJAP / AT+CIPSTART / AT+CIPSEND)を
 *   流し込む。
 *
 *   使い方: atcli [host] [port]
 *     host/port は AT+CIPSTART で指定する「モデムに繋がせたい相手」
 *     (省略時は 127.0.0.1 9100、テスト用のローカル待受を想定)。
 *     実インターネットの到達確認をしたければ例えば `atcli example.com 80`。
 *     ペイロードは host 宛の簡単な HTTP GET(実サーバでなくても
 *     ERROR/CLOSED になるだけで atcli 自体は落ちない)。
 *
 *   nettest.c との違い: nettest は net.c/netcli.c 自体の配線検証用の
 *   固定テキスト("hello from tizix\n")を送るだけで AT コマンドは一切
 *   話さない。at_modem.py へ ATD で直結した後の生ストリームへ何を
 *   流すかは呼び出し側の自由であり、将来 z80board で ESP-AT ドライバに
 *   なる部分のプロトタイプがこの atcli.c にあたる。 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "netcli.h"

#define AT_MODEM_HOST "127.0.0.1:8080"

extern unsigned int getticks(void);

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

/* wait_ticks(1/100秒)だけ無通信になるまで受信バイトをそのまま表示する */
static void read_and_print(unsigned int wait_ticks)
{
    char ch;
    int n;
    unsigned int t0;

    t0 = getticks();
    for (;;) {
        n = net_read(&ch, 1);
        if (n == 1) {
            putchar(ch);
            t0 = getticks();
        } else if ((unsigned int)(getticks() - t0) > wait_ticks) {
            break;
        }
    }
}

int main(int argc, char **argv)
{
    char host[40];
    char port[8];
    char cmd[96];
    char req[96];
    char numbuf[8];
    unsigned int reqlen;

    strcpy(host, "127.0.0.1");
    strcpy(port, "9100");
    if (argc >= 1 && argv[0])
        strcpy(host, argv[0]);
    if (argc >= 2 && argv[1])
        strcpy(port, argv[1]);

    puts("atcli: dialing at_modem.py...\n");
    if (net_connect(AT_MODEM_HOST) != 0) {
        puts("atcli: connect failed (net daemon down, or at_modem.py not listening)\n");
        return 1;
    }
    puts("atcli: connected\n");

    puts("atcli: >> AT\n");
    write_all("AT\r\n");
    read_and_print(100);

    puts("\natcli: >> AT+CWJAP=\"test\",\"test\"\n");
    write_all("AT+CWJAP=\"test\",\"test\"\r\n");
    read_and_print(100);

    strcpy(cmd, "AT+CIPSTART=\"TCP\",\"");
    strcat(cmd, host);
    strcat(cmd, "\",");
    strcat(cmd, port);
    strcat(cmd, "\r\n");
    puts("\natcli: >> ");
    puts(cmd);
    write_all(cmd);
    read_and_print(300);

    strcpy(req, "GET / HTTP/1.0\r\nHost: ");
    strcat(req, host);
    strcat(req, "\r\n\r\n");
    reqlen = strlen(req);

    strcpy(cmd, "AT+CIPSEND=");
    itoa((int)reqlen, numbuf, 10);
    strcat(cmd, numbuf);
    strcat(cmd, "\r\n");
    puts("\natcli: >> ");
    puts(cmd);
    write_all(cmd);
    read_and_print(100);
    write_all(req);
    read_and_print(400);

    puts("\natcli: >> AT+CIPCLOSE\n");
    write_all("AT+CIPCLOSE\r\n");
    read_and_print(100);

    net_close();
    puts("\natcli: done\n");
    return 0;
}
