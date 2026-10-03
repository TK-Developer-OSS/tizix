/* user/netstat.c - TCP 接続の表示(#109)。
 *   tizix の TCP は同時 1 接続(user/netcli.h)なので、表示も高々 1 行。
 *   値はカーネルに聞く(net_info、syscall 41)。gcc 系アーキ(TZ_SYSCALL)だけ。
 *
 *   使い方: netstat
 */
#include "stdio.h"
#include "netcli.h"

static void pr_ipport(unsigned long a, unsigned long port)
{
        printf("%u.%u.%u.%u:%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
               (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF), (unsigned)port);
}

/* lwIP の tcp_state の名前(lwip/tcpbase.h の順) */
static const char *tcpst(unsigned long s)
{
        switch (s) {
        case 0: return "CLOSED";
        case 1: return "LISTEN";
        case 2: return "SYN_SENT";
        case 3: return "SYN_RCVD";
        case 4: return "ESTABLISHED";
        case 5: return "FIN_WAIT_1";
        case 6: return "FIN_WAIT_2";
        case 7: return "CLOSE_WAIT";
        case 8: return "CLOSING";
        case 9: return "LAST_ACK";
        case 10: return "TIME_WAIT";
        }
        return "?";
}

int main(int argc, char **argv)
{
        unsigned long st;
        (void)argc;
        (void)argv;

        if (!(net_info(NI_FLAGS) & 1)) {
                printf("netstat: no network interface\n");
                return 1;
        }
        printf("Proto Local Address          Foreign Address        State        RX     TX\n");
        st = net_info(NI_STATE);
        if (st == NET_IDLE)
                return 0;
        if (st == NET_LISTEN) {
                printf("tcp   0.0.0.0:%u  0.0.0.0:*  LISTEN\n", (unsigned)net_info(NI_LPORT));
                return 0;
        }
        printf("tcp   ");
        pr_ipport(net_info(NI_ADDR), net_info(NI_LPORT));
        printf("  ");
        pr_ipport(net_info(NI_RADDR), net_info(NI_RPORT));
        printf("  %s  %u  %u\n",
               st == NET_CONNECTED ? tcpst(net_info(NI_TCPST)) :
               st == NET_CONNECTING ? "CONNECTING" : "ERROR",
               (unsigned)net_info(NI_RXBYTE), (unsigned)net_info(NI_TXBYTE));
        return 0;
}
