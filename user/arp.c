/* user/arp.c - ARP 表の表示(#109)。gcc 系アーキ(TZ_SYSCALL)だけ。
 *   値はカーネルの lwIP の表(user/netcli.h の net_info、NI_ARP)。表示だけで、足し引きはしない。
 *
 *   使い方: arp
 */
#include "stdio.h"
#include "netcli.h"

static void pr_hex2(unsigned v)
{
        const char *d = "0123456789abcdef";
        putchar(d[(v >> 4) & 0xF]);
        putchar(d[v & 0xF]);
}

int main(int argc, char **argv)
{
        unsigned i, n = 0;
        unsigned long ip, hi, lo;
        (void)argc;
        (void)argv;

        if (!(net_info(NI_FLAGS) & 1)) {
                printf("arp: no network interface\n");
                return 1;
        }
        printf("Address          HWaddress          Iface\n");
        for (i = 0; ; i++) {
                ip = net_info(NI_ARP + i * 4);
                if (ip == 0xFFFFFFFFUL)
                        break;
                if (!ip)
                        continue;
                hi = net_info(NI_ARP + i * 4 + 1);
                lo = net_info(NI_ARP + i * 4 + 2);
                net_pr_ip(ip);
                /* 桁をそろえる(アドレスは最長 15 文字) */
                {
                        unsigned w = 0;
                        unsigned long a = ip;
                        int k;
                        for (k = 0; k < 4; k++, a >>= 8)
                                w += (a & 0xFF) >= 100 ? 3 : (a & 0xFF) >= 10 ? 2 : 1;
                        w += 3;
                        while (w++ < 17) putchar(' ');
                }
                pr_hex2((unsigned)(hi >> 8)); putchar(':');
                pr_hex2((unsigned)hi);         putchar(':');
                pr_hex2((unsigned)(lo >> 24)); putchar(':');
                pr_hex2((unsigned)(lo >> 16)); putchar(':');
                pr_hex2((unsigned)(lo >> 8));  putchar(':');
                pr_hex2((unsigned)lo);
                printf("  wl0\n");
                n++;
        }
        if (!n)
                printf("(empty)\n");
        return 0;
}
