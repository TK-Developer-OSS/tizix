/* user/nc.c - 簡易 netcat(#112 の前段: TCP の待ち受けを試す)。gcc 系アーキ(TZ_SYSCALL)だけ。
 *   相手から来たものは端末へ、打ったものは相手へ(1 行ずつではなく 1 文字ずつ。Enter は CRLF)。
 *   相手が閉じたら終わる。Ctrl+C でも終わる。
 *
 *   使い方: nc host port      こちらから繋ぐ
 *           nc -l port        待ち受けて 1 本だけ受ける
 */
#include "stdio.h"
#include "string.h"
#include "netcli.h"

static unsigned long num(const char *s)
{
        unsigned long v = 0;
        while (*s >= '0' && *s <= '9')
                v = v * 10 + (unsigned long)(*s++ - '0');
        return v;
}

int main(int argc, char **argv)
{
        char hp[72], buf[128], c;
        int n, i;

        if (argc >= 2 && !strcmp(argv[0], "-l")) {
                if (net_listen((unsigned)num(argv[1])) != 0) {
                        printf("nc: cannot listen on %s\n", argv[1]);
                        return 1;
                }
                printf("nc: listening on port %s\n", argv[1]);
                while (net_state() == NET_LISTEN) {
                        if (kbhit() && getchar() == 0x03) {
                                net_close();
                                return 1;
                        }
                }
                if (net_state() != NET_CONNECTED) {
                        printf("nc: listen failed\n");
                        return 1;
                }
                printf("nc: connected from ");
                net_pr_ip(net_info(NI_RADDR));
                printf(":%u\n", (unsigned)net_info(NI_RPORT));
        } else if (argc >= 2) {
                strcpy(hp, argv[0]);
                strcat(hp, ":");
                strcat(hp, argv[1]);
                if (net_connect(hp) != 0) {
                        printf("nc: cannot connect to %s\n", hp);
                        return 1;
                }
        } else {
                printf("usage: nc host port | nc -l port\n");
                return 1;
        }

        for (;;) {
                n = net_read(buf, sizeof buf);
                for (i = 0; i < n; i++)
                        putchar(buf[i]);
                if (n == 0 && net_state() != NET_CONNECTED)
                        break;                          /* 相手が閉じた */
                if (kbhit()) {
                        c = (char)getchar();
                        if (c == 0x03)
                                break;
                        if (c == '\r' || c == '\n') {
                                putchar('\n');
                                net_write("\r\n", 2);
                        } else {
                                putchar(c);
                                net_write(&c, 1);
                        }
                }
        }
        net_close();
        printf("\nnc: closed\n");
        return 0;
}
