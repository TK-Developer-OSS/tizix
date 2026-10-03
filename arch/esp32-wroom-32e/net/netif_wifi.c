/* arch/esp32-wroom-32e/net/netif_wifi.c(#109): lwIP(NO_SYS)と WiFi の STA を繋ぐ。
 *   送信: lwIP の linkoutput → esp_wifi_internal_tx。
 *   受信: WiFi ドライバのコールバック(WiFi のスレッド = slot0 の中)→ pbuf に写して lwIP へ。
 *   タイマ: init の待ちループ(net/wifi.c の plat_idle)が sys_check_timeouts を回す。
 *   lwIP の中も WiFi のスレッドも slot0 の協調スレッドなので、lwIP を取り合うことは無い。 */
#include <stdint.h>
#include <string.h>
#include "osal.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/apps/sntp.h"
#include "lwip/dhcp.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"

extern void net_log(const char *fmt, ...);   /* net/wifi.c: /var/log/message へ */

static struct netif sta;
static int lwip_up;
static uint8_t txbuf[1600];
static uint32_t rx_pkts, tx_pkts;

extern void knet_poll(void);
extern void time_set(unsigned long sec);
static char ntp_server[40];
static int sntp_started, sntp_done;

/* SNTP が時刻を得たとき(lwipopts.h の SNTP_SET_SYSTEM_TIME) */
void net_sntp_set(unsigned long sec)
{
    time_set(sec);
    if (!sntp_done) {
        sntp_done = 1;
        net_log("net: clock set by SNTP (%s)", ntp_server);
    }
}
extern void kquery_poll(void);

/* lwIP の OS 層(NO_SYS で要るもの) */
u32_t sys_now(void) { return os_ticks(); }
sys_prot_t sys_arch_protect(void) { return irq_off(); }
void sys_arch_unprotect(sys_prot_t ps) { irq_restore(ps); }

static err_t low_output(struct netif *n, struct pbuf *p)
{
    uint16_t len;
    (void)n;
    if (p->tot_len > sizeof txbuf) return ERR_BUF;
    len = pbuf_copy_partial(p, txbuf, p->tot_len, 0);
    if (esp_wifi_internal_tx(WIFI_IF_STA, txbuf, len) != ESP_OK)
        return ERR_IF;
    tx_pkts++;
    return ERR_OK;
}

static esp_err_t wifi_rx(void *buffer, uint16_t len, void *eb)
{
    struct pbuf *p;

    if (lwip_up && buffer && len) {
        p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
        if (p) {
            rx_pkts++;
            memcpy(p->payload, buffer, len);
            if (sta.input(p, &sta) != ERR_OK)
                pbuf_free(p);
        }
    }
    if (eb) esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

static err_t sta_init(struct netif *n)
{
    n->name[0] = 'w'; n->name[1] = 'l';
    n->mtu = 1500;
    n->hwaddr_len = 6;
    esp_wifi_get_mac(WIFI_IF_STA, n->hwaddr);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    n->output = etharp_output;
    n->linkoutput = low_output;
    return ERR_OK;
}

static int use_dhcp, dns_fixed;
static ip_addr_t dns_conf;

/* アドレスが付いた / 変わった(lwIP の status callback。DHCP で取れたときもここ) */
static void on_status(struct netif *n)
{
    char sa[16], sm[16];                    /* ip4addr_ntoa は 1 つの静的領域を使い回す */

    if (ip4_addr_isany_val(*netif_ip4_addr(n)))
        return;
    if (dns_fixed)
        dns_setserver(0, &dns_conf);        /* dns= を書いたら DHCP の DNS より優先 */
    strcpy(sa, ip4addr_ntoa(netif_ip4_addr(n)));
    strcpy(sm, ip4addr_ntoa(netif_ip4_netmask(n)));
    net_log("net: %s ip %s mask %s gw %s", use_dhcp ? "dhcp" : "static", sa, sm,
            ip4addr_ntoa(netif_ip4_gw(n)));
}

/* lwIP を起こす。ip が空か "dhcp" なら DHCP、そうでなければ固定("192.168.0.10" の形)。
 * dns が空なら DHCP が教えるもの(固定 IP なら 8.8.8.8)。1 = 成功 */
int net_lwip_start(const char *ip, const char *mask, const char *gw, const char *dns, const char *ntp)
{
    ip4_addr_t a, m, g;

    strncpy(ntp_server, ntp[0] ? ntp : "pool.ntp.org", sizeof ntp_server - 1);
    use_dhcp = !ip[0] || !strcmp(ip, "dhcp");
    ip4_addr_set_zero(&a);
    ip4_addr_set_zero(&m);
    ip4_addr_set_zero(&g);
    if (!use_dhcp) {
        if (!ip4addr_aton(ip, &a)) { net_log("net: bad ip=%s in /etc/wifi", ip); return 0; }
        if (!mask[0] || !ip4addr_aton(mask, &m)) IP4_ADDR(&m, 255, 255, 255, 0);
        if (!gw[0] || !ip4addr_aton(gw, &g)) ip4_addr_set_zero(&g);
    }
    dns_fixed = dns[0] && ipaddr_aton(dns, &dns_conf);
    if (!dns_fixed) IP_ADDR4(&dns_conf, 8, 8, 8, 8);
    lwip_init();
    dns_setserver(0, &dns_conf);
    netif_add(&sta, &a, &m, &g, NULL, sta_init, ethernet_input);
    netif_set_status_callback(&sta, on_status);
    netif_set_default(&sta);
    netif_set_up(&sta);                     /* 固定 IP ならここで on_status が呼ばれる */
    esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx);
    lwip_up = 1;
    if (use_dhcp)
        net_log("net: waiting for DHCP");
    return 1;
}

/* つながった / 切れた(net/wifi.c のイベントから) */
void net_link(int up)
{
    if (!lwip_up) return;
    if (up) {
        /* ESP-IDF(wifi_default.c)と同じく、つながった時点で受信口を登録し直して IP を持ったことを知らせる */
        esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx);
        esp_wifi_internal_set_sta_ip();
        netif_set_link_up(&sta);
        if (use_dhcp)
            dhcp_start(&sta);               /* つながるたびに取り直す(別の AP かもしれない) */
        else
            etharp_gratuitous(&sta);
        if (!sntp_started) {            /* 時計合わせ(以後 1 時間おき)。TLS の証明書の検査に要る */
            sntp_started = 1;
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, ntp_server);
            sntp_setservername(1, "time.google.com");
            sntp_init();
        }
    } else {
        if (use_dhcp)
            dhcp_release_and_stop(&sta);
        netif_set_link_down(&sta);
    }
}

void net_poll(void)
{
    if (!lwip_up) return;
    sys_check_timeouts();
    knet_poll();
    kquery_poll();
}

/* net/knet.c から */
struct netif *net_sta(void) { return &sta; }
int net_is_up(void)
{
    return lwip_up && netif_is_link_up(&sta) && !ip4_addr_isany_val(*netif_ip4_addr(&sta));
}

/* arp 用: sel = 表の番号 * 4 + k(k: 0 = IP / 1 = MAC 先頭 2 バイト / 2 = MAC 残り 4 バイト)。
 * 空きの番号は IP が 0、表の外は 0xFFFFFFFF。lwIP の表を読むだけ(表示用。書き換えの途中を
 * 読んでも 1 行が古いか新しいかの違いで済む)。 */
unsigned long net_arpstat(unsigned sel)
{
    ip4_addr_t *ip;
    struct netif *n;
    struct eth_addr *e;
    unsigned i = sel / 4, k = sel % 4;

    if (i >= ARP_TABLE_SIZE)
        return 0xFFFFFFFFUL;
    if (!lwip_up || !etharp_get_entry(i, &ip, &n, &e))
        return 0;
    switch (k) {
    case 0: return ip4_addr_get_u32(ip);
    case 1: return ((unsigned long)e->addr[0] << 8) | e->addr[1];
    case 2: return ((unsigned long)e->addr[2] << 24) | ((unsigned long)e->addr[3] << 16) |
                   ((unsigned long)e->addr[4] << 8) | e->addr[5];
    }
    return 0;
}

/* ifconfig 用の値(番号は user/netcli.h の NI_*。アドレスは lwIP の並び = 先頭のオクテットが下位バイト) */
unsigned long net_ifstat(unsigned sel)
{
    const uint8_t *h = sta.hwaddr;

    if (!lwip_up)
        return sel == 0 ? 0 : 0xFFFFFFFFUL;
    switch (sel) {
    case 0: return 1u | (netif_is_link_up(&sta) ? 2u : 0u) | (use_dhcp ? 4u : 0u);
    case 1: return ip4_addr_get_u32(netif_ip4_addr(&sta));
    case 2: return ip4_addr_get_u32(netif_ip4_netmask(&sta));
    case 3: return ip4_addr_get_u32(netif_ip4_gw(&sta));
    case 4: return ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)));
    case 5: return ((unsigned long)h[0] << 8) | h[1];
    case 6: return ((unsigned long)h[2] << 24) | ((unsigned long)h[3] << 16) | ((unsigned long)h[4] << 8) | h[5];
    case 7: return rx_pkts;
    case 8: return tx_pkts;
    case 9: return sta.mtu;
    }
    return 0xFFFFFFFFUL;
}
