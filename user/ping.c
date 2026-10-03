/* user/ping.c - ICMP echo(#109)。gcc 系アーキ(TZ_SYSCALL)だけ。
 *   名前引きと echo の送受はカーネル(user/netcli.h の net_resolve / net_ping_*、syscall 42..45)。
 *
 *   使い方: ping [-c 回数] [-s バイト数] host     (既定 4 回・32 バイト、1 秒おき。Ctrl+C で止まる)
 */
#include "stdio.h"
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
        const char *host = 0;
        unsigned long cnt = 4, len = 32, addr, ttl, r, ms;
        unsigned long sent = 0, recv = 0, tmin = 0xFFFFFFFFUL, tmax = 0, tsum = 0;
        unsigned int t0;
        int i;

        for (i = 0; i < argc; i++) {
                if (argv[i][0] == '-' && argv[i][1] == 'c' && i + 1 < argc)
                        cnt = num(argv[++i]);
                else if (argv[i][0] == '-' && argv[i][1] == 's' && i + 1 < argc)
                        len = num(argv[++i]);
                else
                        host = argv[i];
        }
        if (!host) {
                printf("usage: ping [-c count] [-s size] host\n");
                return 1;
        }
        if (net_resolve(host, &addr, &ms) != 0) {
                printf("ping: %s: cannot resolve (or no network)\n", host);
                return 1;
        }
        printf("PING %s (", host);
        net_pr_ip(addr);
        printf("): %u data bytes\n", (unsigned)len);

        for (sent = 0; sent < cnt; ) {
                net_ping_send(addr, (unsigned)sent, (unsigned)len);
                t0 = getticks();
                r = 0;
                while ((unsigned)(getticks() - t0) < 100) {          /* 1 秒待つ(その間に来なければ失敗) */
                        if (!r && (r = net_ping_poll((unsigned)sent, &ttl)) != 0) {
                                ms = r - 1;
                                printf("%u bytes from ", (unsigned)(len + 8));
                                net_pr_ip(addr);
                                printf(": icmp_seq=%u ttl=%u time=%u ms\n", (unsigned)sent, (unsigned)ttl, (unsigned)ms);
                                recv++;
                                tsum += ms;
                                if (ms < tmin) tmin = ms;
                                if (ms > tmax) tmax = ms;
                        }
                }
                if (!r)
                        printf("Request timeout for icmp_seq %u\n", (unsigned)sent);
                sent++;
        }

        printf("--- %s ping statistics ---\n", host);
        printf("%u packets transmitted, %u packets received, %u%% packet loss\n",
               (unsigned)sent, (unsigned)recv, (unsigned)(sent ? (sent - recv) * 100 / sent : 0));
        if (recv)
                printf("round-trip min/avg/max = %u/%u/%u ms\n",
                       (unsigned)tmin, (unsigned)(tsum / recv), (unsigned)tmax);
        return recv ? 0 : 1;
}
