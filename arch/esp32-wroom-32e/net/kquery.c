/* arch/esp32-wroom-32e/net/kquery.c(#109): 名前引き(dig)と ICMP echo(ping)。src/knet.h の続き。
 *
 *   形は net/knet.c と同じ: syscall(コマンドのスロット)は要求を置いて結果を見るだけで、
 *   lwIP を呼ぶのは slot0 の待ちループ(net_poll → kquery_poll)とそのコールバックだけ。
 *   どちらも同時に 1 件(後から来た要求は前のものを打ち切る)。
 *     knet_dns_start(name)          名前(数字の IP でもよい)を引き始める
 *     knet_dns_poll(&addr, &ms)     0=引いている / 1=引けた / 2=引けない
 *     knet_ping_send(addr, seq, n)  echo request を 1 つ(データ n バイト)
 *     knet_ping_poll(seq, &ttl)     0=まだ / 1+ = 応答までのミリ秒 + 1
 *   アドレスは lwIP の並び(先頭のオクテットが下位バイト)。
 */
#include <stdint.h>
#include <string.h>
#include "osal.h"
#include "lwip/dns.h"
#include "lwip/raw.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"
#include "knet.h"

extern int net_is_up(void);

#define PING_ID   0x7A58            /* "tX" */
#define PING_MAX  1024

/* ---- コマンドのスロットと slot0 の間で受け渡すもの ---- */
static volatile uint8_t  dns_req, dns_st;           /* dns_st: 0=引いている 1=引けた 2=引けない */
static char              dns_name[64];
static volatile uint32_t dns_addr, dns_t0, dns_ms;

static volatile uint8_t  ping_req;
static volatile uint32_t ping_dst;
static volatile uint16_t ping_seq, ping_len;
static volatile uint16_t ping_got_seq;               /* 最後に応答が来た seq */
static volatile uint32_t ping_t0, ping_rtt;          /* ping_rtt: 応答までの ms + 1(0 = まだ) */
static volatile uint8_t  ping_ttl;

static struct raw_pcb *icmp_pcb;

/* ---------------- syscall 側 ---------------- */

int knet_dns_start(const char *name)
{
    unsigned i;

    if (!net_is_up())
        return -1;
    for (i = 0; i < sizeof dns_name - 1 && name[i]; i++)
        dns_name[i] = name[i];
    dns_name[i] = 0;
    dns_st = 0;
    dns_t0 = os_ticks();
    dns_req = 1;
    return 0;
}

int knet_dns_poll(unsigned long *addr, unsigned long *ms)
{
    if (dns_st == 1) {
        if (addr) *addr = dns_addr;
        if (ms) *ms = dns_ms;
    }
    return dns_st;
}

int knet_ping_send(unsigned long addr, unsigned seq, unsigned len)
{
    if (!net_is_up())
        return -1;
    if (len > PING_MAX) len = PING_MAX;
    ping_dst = addr;
    ping_seq = (uint16_t)seq;
    ping_len = (uint16_t)len;
    ping_rtt = 0;
    ping_req = 1;
    return 0;
}

unsigned long knet_ping_poll(unsigned seq, unsigned long *ttl)
{
    if (ping_rtt && ping_got_seq == (uint16_t)seq) {
        if (ttl) *ttl = ping_ttl;
        return ping_rtt;
    }
    return 0;
}

/* ---------------- slot0 側 ---------------- */

static void on_dns(const char *name, const ip_addr_t *a, void *arg)
{
    (void)arg;
    if (strcmp(name, dns_name))
        return;                     /* 打ち切られた前の要求の答え */
    if (a) {
        dns_addr = ip4_addr_get_u32(ip_2_ip4(a));
        dns_ms = os_ticks() - dns_t0;
        dns_st = 1;
    } else
        dns_st = 2;
}

static u8_t on_icmp(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    uint8_t ip[IP_HLEN];
    struct icmp_echo_hdr e;
    unsigned hl;

    (void)arg; (void)pcb; (void)addr;
    if (p->tot_len < IP_HLEN + sizeof e || pbuf_copy_partial(p, ip, IP_HLEN, 0) != IP_HLEN)
        return 0;
    hl = (unsigned)(ip[0] & 0x0F) * 4;
    if (pbuf_copy_partial(p, &e, sizeof e, (u16_t)hl) != sizeof e)
        return 0;
    if (e.type != ICMP_ER || e.id != PP_HTONS(PING_ID))
        return 0;                   /* 自分の echo の応答でなければ lwIP の icmp に任せる */
    if (lwip_ntohs(e.seqno) == ping_seq && !ping_rtt) {
        ping_ttl = ip[8];
        ping_got_seq = ping_seq;
        ping_rtt = os_ticks() - ping_t0 + 1;
    }
    pbuf_free(p);
    return 1;                       /* 食べた */
}

static void do_ping(void)
{
    struct pbuf *p;
    struct icmp_echo_hdr *e;
    ip_addr_t dst;
    unsigned i, n = ping_len;

    if (!icmp_pcb) {
        icmp_pcb = raw_new(IP_PROTO_ICMP);
        if (!icmp_pcb) return;
        raw_recv(icmp_pcb, on_icmp, NULL);
        raw_bind(icmp_pcb, IP_ADDR_ANY);
    }
    p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof *e + n), PBUF_RAM);
    if (!p) return;
    e = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(e, ICMP_ECHO);
    ICMPH_CODE_SET(e, 0);
    e->id = PP_HTONS(PING_ID);
    e->seqno = lwip_htons(ping_seq);
    e->chksum = 0;
    for (i = 0; i < n; i++)
        ((uint8_t *)(e + 1))[i] = (uint8_t)('a' + i % 23);
    e->chksum = inet_chksum(e, (u16_t)(sizeof *e + n));
    ip_addr_set_ip4_u32(&dst, ping_dst);
    ping_t0 = os_ticks();
    raw_sendto(icmp_pcb, p, &dst);
    pbuf_free(p);
}

void kquery_poll(void)
{
    if (dns_req) {
        ip_addr_t a;
        err_t r;

        dns_req = 0;
        if (ipaddr_aton(dns_name, &a))
            r = ERR_OK;
        else
            r = dns_gethostbyname(dns_name, &a, on_dns, NULL);
        if (r == ERR_OK) {
            dns_addr = ip4_addr_get_u32(ip_2_ip4(&a));
            dns_ms = os_ticks() - dns_t0;
            dns_st = 1;
        } else if (r != ERR_INPROGRESS)
            dns_st = 2;
    }
    if (ping_req) {
        ping_req = 0;
        do_ping();
    }
}
