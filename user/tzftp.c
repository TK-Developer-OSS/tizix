/* user/tzftp.c - net.bin(常駐デーモン)+ netcli.h を使う簡易ファイル転送コマンド。
 *   本物の FTP(RFC959)ではない(コマンド名を紛らわしくないよう ftp から
 *   tzftp へ改名済み)。python/tzftp_srv.py(host側)との専用の極小
 *   プロトコル(1行ヘッダ + 生バイト列、1リクエスト=1TCP接続)。
 *
 *   使い方: `net &` を先に起動しておくこと。
 *     tzftp put [file]   ローカル(tizix側)の file を host の tzftp_root/ へ送る
 *     tzftp get [file]   host の tzftp_root/file を取得してローカルへ書く
 *   file を省略すると "TZFTPFILE" を使う。
 *
 *   接続先は python/tzftp_srv.py の既定 127.0.0.1:2121 固定。
 *
 *   既知の制限: net_close() は KW_NETCMD にフラグを立てるだけで実際には
 *   host 側ソケットを閉じない(user/net.c 参照)。切断は host 側
 *   (tzftp_srv.py)が処理後に能動的に close する前提の設計にしてある。
 *
 *   掟: fseek 巻き戻し禁止 → put のサイズ計算は 2 パス fopen。
 *
 *   2026-09-17: #51(iy_reg_claude.py の JP_COND/CALL_COND バグ)・#54
 *   (その修正自体のフラグ破壊バグ)・iosim.c の graceful close 対応、
 *   の3点をすべて修正した上で書き直した安定版(旧 ftp.c)。以前 argv 経由の
 *   fopen が原因かと誤診断した時期があったが実際は無関係だった
 *   ([[ftp-command-attempt]] 参照)。同日、本物のFTPプロトコルではない
 *   ことを名前で明示するため ftp → tzftp へ改名。 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "netcli.h"

#define TZFTP_HOST "127.0.0.1:2121"
#define DEFAULT_FILE "TZFTPFILE"

extern unsigned int getticks(void);

static void net_write_all(const char *buf, unsigned int len)
{
    unsigned int sent, n;

    sent = 0;
    while (sent < len) {
        n = (unsigned int)net_write(buf + sent, (int)(len - sent));
        if (n == 0)
            continue;
        sent += n;
    }
}

/* ~5秒無通信でタイムアウトし 0 を返す */
static unsigned int net_read_byte(char *out)
{
    unsigned int t0;

    t0 = getticks();
    for (;;) {
        if (net_read(out, 1) == 1)
            return 1;
        if ((unsigned int)(getticks() - t0) > 500)
            return 0;
    }
}

static void do_put(const char *fname)
{
    FILE *fp;
    char hdr[64];
    char numbuf[8];
    unsigned int size, total;
    int c;

    /* 1パス目: サイズを数えるだけ(fseek 巻き戻し禁止の掟) */
    fp = fopen(fname, "r");
    if (!fp) {
        puts("tzftp: cannot open local file\n");
        return;
    }
    size = 0;
    for (;;) {
        c = fgetc(fp);
        if (c < 0)
            break;
        size++;
    }
    fclose(fp);

    if (net_connect(TZFTP_HOST) != 0) {
        puts("tzftp: connect failed (net daemon down, or tzftp_srv.py not listening)\n");
        return;
    }

    /* 2パス目: 開き直して実送信 */
    fp = fopen(fname, "r");
    if (!fp) {
        puts("tzftp: cannot reopen local file\n");
        net_close();
        return;
    }

    strcpy(hdr, "PUT ");
    strcat(hdr, fname);
    strcat(hdr, " ");
    itoa((int)size, numbuf, 10);
    strcat(hdr, numbuf);
    strcat(hdr, "\n");
    net_write_all(hdr, strlen(hdr));

    total = 0;
    for (;;) {
        c = fgetc(fp);
        if (c < 0)
            break;
        {
            char ch = (char)c;
            net_write_all(&ch, 1);
        }
        total++;
    }
    fclose(fp);

    /* net.c daemon は TX リングバッファを「1ループにつき1バイト」relay
     * する設計(user/net.c 参照)。ここで即座に net_close() すると、
     * daemon がまだ全バイトを吐き出し切っていないうちに NETCMD_CLOSE
     * を検知して即 IDLE に戻ってしまい、キューに積んだだけで実際には
     * 送信されていない末尾データが失われる(2026-09-17 発見、
     * task.md #53。put が「N bytes」と成功報告するのに host 側に
     * 何も届かない、という形で顕在化した)。daemon に確実に何ティックか
     * 回してもらってから close する。 */
    {
        unsigned int t0 = getticks();
        while ((unsigned int)(getticks() - t0) < 20) {
        }
    }
    net_close();

    puts("tzftp: put ");
    puts(fname);
    puts(" -> host (");
    itoa((int)total, numbuf, 10);
    puts(numbuf);
    puts(" bytes)\n");
}

static void do_get(const char *fname)
{
    FILE *fp;
    char hdr[64];
    char numbuf[8];
    char rb[16];
    unsigned char ri, k;
    unsigned int size, total;
    char ch;

    if (net_connect(TZFTP_HOST) != 0) {
        puts("tzftp: connect failed (net daemon down, or tzftp_srv.py not listening)\n");
        return;
    }

    strcpy(hdr, "GET ");
    strcat(hdr, fname);
    strcat(hdr, "\n");
    net_write_all(hdr, strlen(hdr));

    /* host は "SIZE <n>\n" を返す */
    ri = 0;
    for (;;) {
        if (net_read_byte(&ch) == 0) {
            rb[0] = '\0';
            break;
        }
        if (ch == '\n') {
            rb[ri] = '\0';
            break;
        }
        if (ri < 15)
            rb[ri++] = ch;
    }

    k = 0;
    while (rb[k] != '\0' && (rb[k] < '0' || rb[k] > '9'))
        k++;
    size = 0;
    while (rb[k] >= '0' && rb[k] <= '9') {
        size = size * 10 + (unsigned int)(rb[k] - '0');
        k++;
    }

    if (size == 0) {
        puts("tzftp: get: file not found on host, or empty\n");
        net_close();
        return;
    }

    fp = fopen(fname, "w");
    if (!fp) {
        puts("tzftp: cannot create local file\n");
        net_close();
        return;
    }

    total = 0;
    while (total < size) {
        if (net_read_byte(&ch) == 0)
            break;
        fputc(ch, fp);
        total++;
    }
    fclose(fp);
    net_close();

    puts("tzftp: get ");
    puts(fname);
    puts(" <- host (");
    itoa((int)total, numbuf, 10);
    puts(numbuf);
    puts("/");
    itoa((int)size, numbuf, 10);
    puts(numbuf);
    puts(" bytes)\n");
}

int main(int argc, char **argv)
{
    char *mode;
    char fname[40];

    if (argc < 1 || argv[0] == 0) {
        puts("tzftp: usage: tzftp <get|put> [filename]\n");
        return 1;
    }
    mode = argv[0];

    strcpy(fname, DEFAULT_FILE);
    if (argc >= 2 && argv[1])
        strcpy(fname, argv[1]);

    if (strcmp(mode, "put") == 0) {
        do_put(fname);
    } else if (strcmp(mode, "get") == 0) {
        do_get(fname);
    } else {
        puts("tzftp: mode must be get or put\n");
        return 1;
    }

    return 0;
}
