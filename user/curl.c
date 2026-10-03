/* user/curl.c - 簡易 curl(#109)。HTTP/1.0 の GET を 1 回。http と https。
 *   TCP は user/netcli.h(z80 と同じ net_connect / net_read / net_write / net_close)。
 *   https は net_connect_tls ── TLS はカーネル(esp32 は mbedtls、証明書は CA バンドルで検証)が
 *   被せるので、こちらから見た読み書きは平文のまま。
 *   終わりは相手が閉じたこと(net_state が idle に戻る)で知る(Connection: close)。
 *   gcc 系アーキ(TZ_SYSCALL)だけ ── 終わりの判定に net_state を使うため。
 *
 *   使い方: curl [-i] [-k] [-o file] [http[s]://]host[:port][/path]
 *     -i   応答ヘッダも出す
 *     -k   https で証明書を検証しない
 *     -o   本文をファイルへ(既定は端末。名前は 8.3)
 */
#include "stdio.h"
#include "string.h"
#include "netcli.h"

static int prefix(const char *s, const char *pre)
{
        while (*pre)
                if (*s++ != *pre++)
                        return 0;
        return 1;
}

static void send_all(const char *s)
{
        int len = (int)strlen(s), n;
        unsigned int t0 = getticks();

        while (len > 0) {
                n = net_write(s, len);
                s += n;
                len -= n;
                if (n == 0 && (unsigned)(getticks() - t0) > 1000)
                        return;
        }
}

int main(int argc, char **argv)
{
        const char *url = 0, *ofile = 0, *p, *path = "/";
        char host[64], hp[72], req[256], buf[512];
        int i, n, hdr = 0, inbody = 0, crlf = 0, tls = 0, insecure = 0, r;
        unsigned long total = 0;
        unsigned int t0;
        FILE *out = 0;

        for (i = 0; i < argc; i++) {
                if (!strcmp(argv[i], "-i"))
                        hdr = 1;
                else if (!strcmp(argv[i], "-k"))
                        insecure = 1;
                else if (!strcmp(argv[i], "-o") && i + 1 < argc)
                        ofile = argv[++i];
                else
                        url = argv[i];
        }
        if (!url) {
                printf("usage: curl [-i] [-k] [-o file] [http[s]://]host[:port][/path]\n");
                return 1;
        }
        if (prefix(url, "https://")) {
                tls = 1;
                url += 8;
        } else if (prefix(url, "http://"))
                url += 7;

        /* host[:port] と path に分ける */
        for (p = url, i = 0; *p && *p != '/' && i < (int)sizeof host - 1; p++)
                host[i++] = *p;
        host[i] = 0;
        if (*p == '/')
                path = p;
        strcpy(hp, host);
        if (!strchr(host, ':'))
                strcat(hp, tls ? ":443" : ":80");
        else
                *strchr(host, ':') = 0;          /* Host: ヘッダはポート無しで */

        r = tls ? net_connect_tls(hp, !insecure) : net_connect(hp);
        if (r != 0) {
                printf("curl: cannot connect to %s%s\n", hp,
                       tls ? " (TLS: see the tls: line above; -k skips certificate checks)" : "");
                return 1;
        }
        /* 繋がってから開く(失敗したときに空のファイルを残さない) */
        if (ofile && !(out = fopen(ofile, "w"))) {
                printf("curl: cannot open %s\n", ofile);
                net_close();
                return 1;
        }

        strcpy(req, "GET ");
        strcat(req, path);
        strcat(req, " HTTP/1.0\r\nHost: ");
        strcat(req, host);
        strcat(req, "\r\nUser-Agent: tizix-curl\r\nAccept: */*\r\nConnection: close\r\n\r\n");
        send_all(req);

        t0 = getticks();
        for (;;) {
                n = net_read(buf, sizeof buf);
                if (n > 0) {
                        t0 = getticks();
                        for (i = 0; i < n && !inbody; i++) {
                                char c = buf[i];
                                /* ヘッダの終わり = 空行(\r\n\r\n。\n\n も受ける) */
                                if (c == '\n') { if (++crlf == 2) inbody = 1; }
                                else if (c != '\r') crlf = 0;
                                if (hdr) putchar(c);
                        }
                        /* 本文はまとめて書く(1 バイトずつだと syscall が 1 バイトごとになって遅い) */
                        if (i < n) {
                                total += (unsigned long)(n - i);
                                if (out) fwrite(buf + i, 1, (size_t)(n - i), out);
                                else for (; i < n; i++) putchar(buf[i]);
                        }
                        continue;
                }
                if (net_state() != NET_CONNECTED)
                        break;                      /* 相手が閉じて、受信も読み切った */
                if ((unsigned)(getticks() - t0) > 1500) {
                        printf("\ncurl: timeout\n");
                        break;
                }
        }
        net_close();
        if (out) {
                fclose(out);
                printf("curl: %u bytes -> %s\n", (unsigned)total, ofile);
        }
        return 0;
}
