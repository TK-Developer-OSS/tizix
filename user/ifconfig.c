/* user/ifconfig.c - ネットワークインタフェースの表示(#109)。
 *   値はカーネルに聞く(user/netcli.h の net_info、syscall 41)。設定の変更はしない
 *   (固定 IP は /etc/wifi の ip= / mask= / gw= / dns=、起動時に読まれる)。
 *   gcc 系アーキ(TZ_SYSCALL)だけ。z80 は net.bin + ESP-AT の側にインタフェースがある。
 *
 *   使い方: ifconfig
 */
#include "stdio.h"
#include "netcli.h"

static void pr_ip(unsigned long a)
{
        printf("%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
               (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
}

static void pr_hex2(unsigned v)
{
        const char *d = "0123456789abcdef";
        putchar(d[(v >> 4) & 0xF]);
        putchar(d[v & 0xF]);
}

int main(int argc, char **argv)
{
        unsigned long fl = net_info(NI_FLAGS), hi, lo;
        (void)argc;
        (void)argv;

        if (!(fl & 1)) {
                printf("ifconfig: no network interface (WiFi off, or no ip= in /etc/wifi)\n");
                return 1;
        }
        printf("wl0: flags=<UP%s%s>  mtu %u\n", (fl & 2) ? ",RUNNING" : "", (fl & 4) ? ",DHCP" : "",
               (unsigned)net_info(NI_MTU));
        printf("        inet ");
        pr_ip(net_info(NI_ADDR));
        printf("  netmask ");
        pr_ip(net_info(NI_MASK));
        printf("  gateway ");
        pr_ip(net_info(NI_GW));
        printf("\n        dns ");
        pr_ip(net_info(NI_DNS));
        printf("\n        ether ");
        hi = net_info(NI_MACHI);
        lo = net_info(NI_MACLO);
        pr_hex2((unsigned)(hi >> 8)); putchar(':');
        pr_hex2((unsigned)hi);        putchar(':');
        pr_hex2((unsigned)(lo >> 24)); putchar(':');
        pr_hex2((unsigned)(lo >> 16)); putchar(':');
        pr_hex2((unsigned)(lo >> 8));  putchar(':');
        pr_hex2((unsigned)lo);
        printf("\n        RX packets %u  TX packets %u\n",
               (unsigned)net_info(NI_RXPKT), (unsigned)net_info(NI_TXPKT));
        return 0;
}
