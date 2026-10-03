/* arch/esp32-wroom-32e/net/knet.c(#109): TCP クライアント 1 本(src/knet.h の実体)。
 *
 *   z80 の「常駐デーモン net.bin + 共有リング KW_NET*」と同じ形をカーネルの中に持つ:
 *     ・コマンドの syscall(knet_*)は要求を置くこととリングの読み書きだけをする。
 *       lwIP には触らない(コマンドのスロットはタイマで slot0 の途中に割り込めるので、
 *       ここから lwIP を呼ぶと取り合いになる)。
 *     ・slot0 の待ちループ(net/wifi.c の plat_idle → net_poll → knet_poll)が要求を拾い、
 *       lwIP の raw API で接続・送受信する。z80 の net.bin の主ループに当たる。
 *   コマンド側(user/netcli.h)から見た約束は z80 と同じ:
 *     net_connect("host:port") … host は数字の IP でも名前でもよい(名前は DNS で引く)
 *     net_read / net_write     … 非ブロッキング。リングが空 / 満杯なら 0 / 途中まで
 *     net_close                … 閉じて idle へ。相手が閉じたときも、受信を読み切ったら idle に戻る
 *   同時に 1 接続(z80 と同じ)。
 */
#include <stdint.h>
#include <string.h>
#include "osal.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "knet.h"
/* 状態の値は z80 の kmem.h NETSTATE_* と同じ(あちらは z80 の区画の中にあるのでここで持つ)。
 * kmem.h で「未使用」の 1 を接続中に使う。コマンド側の名前は user/netcli.h の NET_*。 */
#define NETSTATE_IDLE       0
#define NETSTATE_CONNECTING 1
#define NETSTATE_CONNECTED  2
#define NETSTATE_ERROR      3

#define RXSZ 2048
#define TXSZ 2048
#define ERRHOLD_MS 2000             /* error を呼び出し側に見せておく時間(z80 の netesp と同じ考え) */

extern int kprintf(const char *fmt, ...);
extern struct netif *net_sta(void);
extern int net_is_up(void);
extern unsigned long net_ifstat(unsigned sel);
extern unsigned long net_arpstat(unsigned sel);

enum { KC_NONE, KC_CONNECT, KC_CLOSE, KC_LISTEN };
#define NETSTATE_LISTEN     4       /* 待ち受け中(net_listen)。繋がってきたら CONNECTED */

static volatile uint8_t  k_cmd, k_state;
static volatile uint16_t k_lport;   /* 待ち受けるポート */
static struct tcp_pcb   *lpcb;      /* 待ち受けの pcb(slot0 だけが触る) */
static char              k_host[64];
static volatile uint16_t rx_h, rx_t, tx_h, tx_t;
static uint8_t           rxbuf[RXSZ], txbuf[TXSZ];

/* ここから下は slot0(knet_poll と lwIP のコールバック)だけが触る */
static struct tcp_pcb *pcb;
static struct pbuf    *pend;        /* リングに入りきらなかった受信 */
static uint16_t        pend_off;
static int             peer_closed;
static uint16_t        k_port;
static ip_addr_t       k_addr;
static uint32_t        err_at;
static uint32_t        rx_bytes, tx_bytes;
static uint32_t        rx_in;      /* lwIP から on_recv で受け取ったバイト数(rx_bytes = リングへ移した数) */
static volatile uint8_t k_tls;      /* KNET_TLS / KNET_INSECURE(src/knet.h) */
static int             hs;          /* TLS の握手中 */
static uint32_t        hs_t0;
static char            k_sni[64];   /* 証明書のホスト名と照らす名前 */

extern unsigned long time_get(void);
extern int  ktls_begin(const char *host, int verify);
extern int  ktls_handshake(void);
extern int  ktls_read(uint8_t *buf, int len);
extern int  ktls_write(const uint8_t *buf, int len);
extern int  ktls_pending(void);
extern void ktls_end(int notify);

#define TIME_VALID   1700000000UL   /* 2023-11 より前なら時計がまだ合っていない */
#define HS_TIMEOUT   20000          /* 握手(時計合わせ待ちを含む)の上限 ms */

/* ---------------- syscall 側(コマンドのスロットから) ---------------- */

int knet_connect(const char *hostport, unsigned flags)
{
    unsigned i;

    if (!net_is_up() || k_state != NETSTATE_IDLE || k_cmd != KC_NONE)
        return -1;
    k_tls = (uint8_t)(flags & (KNET_TLS | KNET_INSECURE));
    for (i = 0; i < sizeof k_host - 1 && hostport[i]; i++)
        k_host[i] = hostport[i];
    k_host[i] = 0;
    k_state = NETSTATE_CONNECTING;
    k_cmd = KC_CONNECT;
    return 0;
}

/* 待ち受け(TCP サーバ側)。1 本だけ受けて、受けたら待ち受けを閉じる(同時 1 接続は z80 と同じ)。
 * 繋がってきたら state が CONNECTED になり、あとは net_read / net_write / net_close で同じように扱う。
 * もう一度受けるには、閉じた(idle に戻った)後でまた呼ぶ。 */
int knet_listen(unsigned port)
{
    if (!net_is_up() || k_state != NETSTATE_IDLE || k_cmd != KC_NONE || !port || port > 65535)
        return -1;
    k_tls = 0;
    k_lport = (uint16_t)port;
    k_state = NETSTATE_LISTEN;
    k_cmd = KC_LISTEN;
    return 0;
}

int knet_state(void) { return k_state; }

int knet_read(char *buf, int max)
{
    int n = 0;
    uint16_t t = rx_t;

    while (n < max && t != rx_h) {
        buf[n++] = (char)rxbuf[t];
        t = (uint16_t)((t + 1) % RXSZ);
    }
    rx_t = t;
    return n;
}

int knet_write(const char *buf, int len)
{
    int n = 0;
    uint16_t h = tx_h, nh;

    if (k_state != NETSTATE_CONNECTED)
        return 0;
    while (n < len) {
        nh = (uint16_t)((h + 1) % TXSZ);
        if (nh == tx_t)
            break;
        txbuf[h] = (uint8_t)buf[n++];
        h = nh;
    }
    tx_h = h;
    return n;
}

void knet_close(void)
{
    if (k_state != NETSTATE_IDLE)
        k_cmd = KC_CLOSE;
}

/* ifconfig / netstat 用。番号は user/netcli.h の NI_* と同じ */
unsigned long knet_info(unsigned sel)
{
    if (sel < 16)
        return net_ifstat(sel);
    if (sel >= 32)
        return net_arpstat(sel - 32);
    switch (sel) {
    case 16: return k_state;
    case 17: return (k_state == NETSTATE_CONNECTED || k_state == NETSTATE_CONNECTING) ? ip4_addr_get_u32(ip_2_ip4(&k_addr)) : 0;
    case 18: return (k_state != NETSTATE_IDLE && k_state != NETSTATE_LISTEN) ? k_port : 0;
    case 19: return k_state == NETSTATE_LISTEN ? k_lport
                  : (pcb && k_state == NETSTATE_CONNECTED) ? pcb->local_port : 0;
    case 20: return rx_bytes;
    case 21: return tx_bytes;
    case 22: return (pcb && k_state == NETSTATE_CONNECTED) ? pcb->state : 0;   /* lwIP の tcp_state */
    case 23: return rx_in;
    case 24: return pend ? (unsigned long)(pend->tot_len - pend_off) : 0;      /* lwIP から受けてまだリングへ移していない分 */
    }
    return 0xFFFFFFFFUL;
}

/* ---------------- slot0 側 ---------------- */

static void reset_rings(void)
{
    rx_h = rx_t = 0;
    tx_h = tx_t = 0;
}

/* ---- 待たせている接続(#110)----
 * 待ち受け中に、今の接続を処理している間に来た接続は切らずに QMAX 本まで待たせる(受信は pbuf で溜める)。
 * 受け手が次に net_listen したときに 1 本ずつ渡す(MCP のクライアントは依頼を並行して投げる)。
 * 待ち受けの pcb も、受けている間は閉じずに開けておく。誰も受けなくなったら QIDLE_MS で片付ける。 */
#define QMAX      3
#define QIDLE_MS  10000
static struct tcp_pcb *qpcb[QMAX];
static struct pbuf    *qbuf[QMAX];
static uint8_t         qclosed[QMAX];
static uint32_t        q_t0[QMAX];
static uint32_t        idle_at;     /* 待ち受けを開けたまま IDLE になった時刻(誰も受けなければ片付ける) */

static err_t on_recv_q(void *arg, struct tcp_pcb *tp, struct pbuf *p, err_t e)
{
    int k = (int)(intptr_t)arg;
    (void)tp; (void)e;
    if (!p) { qclosed[k] = 1; return ERR_OK; }
    if (qbuf[k]) pbuf_cat(qbuf[k], p); else qbuf[k] = p;
    return ERR_OK;
}

static void on_err_q(void *arg, err_t e)
{
    int k = (int)(intptr_t)arg;
    (void)e;
    qpcb[k] = NULL;                 /* lwIP がもう解放している */
    if (qbuf[k]) { pbuf_free(qbuf[k]); qbuf[k] = NULL; }
    qclosed[k] = 0;
}

static void q_drop(int k)
{
    if (qpcb[k]) {
        tcp_arg(qpcb[k], NULL);
        tcp_recv(qpcb[k], NULL);
        tcp_err(qpcb[k], NULL);
        tcp_abort(qpcb[k]);
        qpcb[k] = NULL;
    }
    if (qbuf[k]) { pbuf_free(qbuf[k]); qbuf[k] = NULL; }
    qclosed[k] = 0;
}

static void drop_listen(void)
{
    int k;
    if (lpcb) {
        tcp_accept(lpcb, NULL);
        tcp_close(lpcb);                    /* listen の pcb の close は失敗しない */
        lpcb = NULL;
    }
    for (k = 0; k < QMAX; k++)
        q_drop(k);
}

/* 今の接続だけを閉じる(待ち受けと、待たせている接続はそのまま) */
static void drop_pcb(int abort)
{
    if (k_tls)
        ktls_end(pcb && !abort && !hs);  /* 握手が済んでいれば close_notify を送る */
    hs = 0;
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_sent(pcb, NULL);
        if (abort || tcp_close(pcb) != ERR_OK)
            tcp_abort(pcb);
        pcb = NULL;
    }
    if (pend) { pbuf_free(pend); pend = NULL; }
    pend_off = 0;
    peer_closed = 0;
}

static void set_error(void)
{
    drop_pcb(1);
    drop_listen();
    k_state = NETSTATE_ERROR;
    err_at = os_ticks();
}

static void on_err(void *arg, err_t e)
{
    (void)arg; (void)e;
    pcb = NULL;                     /* lwIP がもう解放している */
    if (pend) { pbuf_free(pend); pend = NULL; }
    if (k_state == NETSTATE_CONNECTING) {
        k_state = NETSTATE_ERROR;
        err_at = os_ticks();
    } else
        peer_closed = 1;            /* RST: 受信を読み切ったら idle へ */
}

static err_t on_recv(void *arg, struct tcp_pcb *tp, struct pbuf *p, err_t e)
{
    (void)arg; (void)tp; (void)e;
    if (!p) {
        peer_closed = 1;
        return ERR_OK;
    }
    rx_in += p->tot_len;
    if (pend)
        pbuf_cat(pend, p);
    else {
        pend = p;
        pend_off = 0;
    }
    return ERR_OK;
}

static err_t on_connected(void *arg, struct tcp_pcb *tp, err_t e)
{
    (void)arg; (void)tp;
    if (e != ERR_OK) {
        set_error();
        return ERR_OK;
    }
    reset_rings();
    rx_bytes = tx_bytes = rx_in = 0;
    if (k_tls) {
        /* TLS: TCP が繋がっても、握手が済むまでは「接続中」のまま(knet_poll が進める) */
        if (ktls_begin(k_sni, !(k_tls & KNET_INSECURE)) != 0) {
            set_error();
            return ERR_ABRT;            /* set_error が pcb を abort した */
        }
        hs = 1;
        hs_t0 = os_ticks();
        return ERR_OK;
    }
    k_state = NETSTATE_CONNECTED;
    return ERR_OK;
}

/* ---- ktls.c の下回り(TCP の生バイト)。slot0 から ---- */
int knet_raw_send(const uint8_t *buf, int len)
{
    unsigned room;

    if (!pcb)
        return -1;
    room = tcp_sndbuf(pcb);
    if (!room || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN)
        return 0;
    if ((unsigned)len > room)
        len = (int)room;
    if (tcp_write(pcb, buf, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK)
        return 0;
    tcp_output(pcb);
    return len;
}

int knet_raw_recv(uint8_t *buf, int len)
{
    int n = 0;

    while (pend && n < len) {
        u16_t c = pbuf_copy_partial(pend, buf + n, (u16_t)(len - n), pend_off);
        n += c;
        pend_off += c;
        while (pend && pend_off >= pend->len) {
            struct pbuf *next = pend->next;
            pend_off -= pend->len;
            if (next) pbuf_ref(next);
            pbuf_free(pend);
            pend = next;
        }
        if (!c) break;
    }
    if (n) {
        if (pcb) tcp_recved(pcb, (u16_t)n);
        return n;
    }
    return (peer_closed || !pcb) ? -1 : 0;
}

/* np を今の接続にする(待ち受けに来た接続、または待たせていた接続) */
static void take_conn(struct tcp_pcb *np, struct pbuf *buf, int closed)
{
    pcb = np;
    tcp_arg(pcb, NULL);
    tcp_err(pcb, on_err);
    tcp_recv(pcb, on_recv);
    k_addr = np->remote_ip;
    k_port = np->remote_port;
    reset_rings();
    pend = buf;                             /* 待たせている間に届いた分 */
    pend_off = 0;
    peer_closed = closed;
    rx_bytes = tx_bytes = 0;
    rx_in = buf ? buf->tot_len : 0;
    k_state = NETSTATE_CONNECTED;
}

/* 待ち受けに繋がってきた。受け手が待っていれば渡し、今の接続を処理中なら QMAX 本まで待たせる */
static err_t on_accept(void *arg, struct tcp_pcb *np, err_t e)
{
    int k;
    (void)arg;
    if (e != ERR_OK || !np)
        return ERR_VAL;
    if (k_state == NETSTATE_LISTEN && !pcb) {
        take_conn(np, NULL, 0);
        return ERR_OK;
    }
    for (k = 0; k < QMAX; k++)
        if (!qpcb[k]) {
            qpcb[k] = np;
            qbuf[k] = NULL;
            qclosed[k] = 0;
            q_t0[k] = os_ticks();
            tcp_arg(np, (void *)(intptr_t)k);
            tcp_recv(np, on_recv_q);
            tcp_err(np, on_err_q);
            return ERR_OK;
        }
    tcp_abort(np);                          /* 待たせる枠も一杯 */
    return ERR_ABRT;
}

/* 待たせている一番古い接続を今の接続にする。戻り: 1 = 渡した */
static int take_queued(void)
{
    int k, best = -1;
    for (k = 0; k < QMAX; k++)
        if (qpcb[k] && (best < 0 || (int32_t)(q_t0[k] - q_t0[best]) < 0))
            best = k;
    if (best < 0)
        return 0;
    take_conn(qpcb[best], qbuf[best], qclosed[best]);
    qpcb[best] = NULL;
    qbuf[best] = NULL;
    qclosed[best] = 0;
    return 1;
}

static void do_listen(void)
{
    struct tcp_pcb *p;

    drop_pcb(1);
    if (lpcb && lpcb->local_port == k_lport) {
        /* 同じポートで開けたまま: 待たせていた接続があればすぐ渡す */
        take_queued();
        return;
    }
    drop_listen();
    p = tcp_new();
    if (!p) { set_error(); return; }
    ip_set_option(p, SOF_REUSEADDR);       /* 前の接続の TIME_WAIT が同じポートを握っていても張れるように(#110) */
    if (tcp_bind(p, IP_ANY_TYPE, k_lport) != ERR_OK) {
        tcp_abort(p);
        set_error();
        return;
    }
    lpcb = tcp_listen(p);                   /* p は解放され、小さな listen 用の pcb が返る */
    if (!lpcb) {
        tcp_abort(p);
        set_error();
        return;
    }
    tcp_accept(lpcb, on_accept);
}

static void start_tcp(const ip_addr_t *a)
{
    k_addr = *a;
    pcb = tcp_new();
    if (!pcb) { set_error(); return; }
    tcp_arg(pcb, NULL);
    tcp_err(pcb, on_err);
    tcp_recv(pcb, on_recv);
    if (tcp_connect(pcb, a, k_port, on_connected) != ERR_OK)
        set_error();
}

static void on_dns(const char *name, const ip_addr_t *a, void *arg)
{
    (void)name; (void)arg;
    if (k_state != NETSTATE_CONNECTING)
        return;                     /* その間に close された */
    if (!a) { set_error(); return; }
    start_tcp(a);
}

static void do_connect(void)
{
    char *colon = strrchr(k_host, ':');
    ip_addr_t a;
    unsigned long port = 0;
    const char *p;
    err_t e;

    drop_pcb(1);
    drop_listen();                  /* 出ていく接続を張るときは待ち受けをやめる(TCP は全体で 1 本) */
    if (!colon) { set_error(); return; }
    *colon = 0;
    for (p = colon + 1; *p >= '0' && *p <= '9'; p++)
        port = port * 10 + (unsigned long)(*p - '0');
    if (!port || port > 65535) { set_error(); return; }
    k_port = (uint16_t)port;
    strcpy(k_sni, k_host);
    if (ipaddr_aton(k_host, &a)) {
        start_tcp(&a);
        return;
    }
    e = dns_gethostbyname(k_host, &a, on_dns, NULL);
    if (e == ERR_OK)
        start_tcp(&a);
    else if (e != ERR_INPROGRESS)
        set_error();
}

/* 溜まった受信をリングへ。リングが空いた分だけ窓を開ける(tcp_recved) */
static void pump_rx(void)
{
    unsigned moved = 0;

    while (pend) {
        uint16_t h = rx_h, nh = (uint16_t)((h + 1) % RXSZ);
        if (nh == rx_t)
            break;                  /* リング満杯: 読み手を待つ */
        rxbuf[h] = pbuf_get_at(pend, pend_off);
        rx_h = nh;
        moved++;
        if (++pend_off >= pend->len) {
            struct pbuf *next = pend->next;
            if (next) pbuf_ref(next);
            pbuf_free(pend);
            pend = next;
            pend_off = 0;
        }
    }
    if (moved) {
        rx_bytes += moved;
        if (pcb) tcp_recved(pcb, (u16_t)moved);
    }
}

/* 送信リングから送れるだけ tcp_write */
static void pump_tx(void)
{
    int sent = 0;

    while (pcb && tx_t != tx_h) {
        uint16_t t = tx_t;
        unsigned run = (tx_h > t) ? (unsigned)(tx_h - t) : (unsigned)(TXSZ - t);
        unsigned room = tcp_sndbuf(pcb);
        if (!room || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN)
            break;
        if (run > room) run = room;
        if (tcp_write(pcb, &txbuf[t], (u16_t)run, TCP_WRITE_FLAG_COPY) != ERR_OK)
            break;
        tx_t = (uint16_t)((t + run) % TXSZ);
        tx_bytes += run;
        sent = 1;
    }
    if (sent && pcb)
        tcp_output(pcb);
}

/* TLS: 復号した平文をリングへ */
static void pump_rx_tls(void)
{
    uint8_t tmp[256];

    for (;;) {
        uint16_t h = rx_h, t = rx_t;
        unsigned room = (t > h) ? (unsigned)(t - h - 1) : (unsigned)(RXSZ - h + t - 1);
        int n, i;
        if (!room)
            return;
        if (room > sizeof tmp) room = sizeof tmp;
        n = ktls_read(tmp, (int)room);
        if (n == 0)
            return;
        if (n < 0) {                /* close_notify / EOF / エラー: 読み切ったら idle へ */
            peer_closed = 1;
            if (pend) { pbuf_free(pend); pend = NULL; }
            return;
        }
        for (i = 0; i < n; i++) {
            rxbuf[h] = tmp[i];
            h = (uint16_t)((h + 1) % RXSZ);
        }
        rx_h = h;
        rx_bytes += (uint32_t)n;
    }
}

/* TLS: 送信リングの平文を暗号化して送る */
static void pump_tx_tls(void)
{
    while (tx_t != tx_h) {
        uint16_t t = tx_t;
        unsigned run = (tx_h > t) ? (unsigned)(tx_h - t) : (unsigned)(TXSZ - t);
        int n = ktls_write(&txbuf[t], (int)run);
        if (n <= 0) {
            if (n < 0) peer_closed = 1;
            return;
        }
        tx_t = (uint16_t)((t + (unsigned)n) % TXSZ);
        tx_bytes += (uint32_t)n;
    }
}

void knet_poll(void)
{
    uint8_t cmd = k_cmd;

    if (cmd == KC_CLOSE) {
        drop_pcb(0);                        /* 待ち受けは開けたまま(受け手がすぐ net_listen し直す) */
        reset_rings();
        k_state = NETSTATE_IDLE;
        idle_at = os_ticks();
        k_cmd = KC_NONE;
        return;
    }
    if (cmd == KC_CONNECT) {
        k_cmd = KC_NONE;
        reset_rings();
        do_connect();
        return;
    }
    if (cmd == KC_LISTEN) {
        k_cmd = KC_NONE;
        reset_rings();
        do_listen();
        return;
    }
    switch (k_state) {
    case NETSTATE_CONNECTING:
        if (!hs)
            break;
        if ((int32_t)(os_ticks() - hs_t0) > HS_TIMEOUT) {
            if (!(k_tls & KNET_INSECURE) && time_get() < TIME_VALID)
                kprintf("tls: clock is not set yet (SNTP), cannot check the certificate (-k skips)\n");
            else
                kprintf("tls: handshake timeout\n");
            set_error();
            break;
        }
        /* 証明書の有効期限を見るので、検証するときは時計(SNTP)が合うまで待つ */
        if (!(k_tls & KNET_INSECURE) && time_get() < TIME_VALID)
            break;
        switch (ktls_handshake()) {
        case 0:  k_state = NETSTATE_CONNECTED; hs = 0; break;
        case -1: set_error(); break;
        }
        break;
    case NETSTATE_CONNECTED:
        if (k_tls) {
            pump_rx_tls();
            pump_tx_tls();
        } else {
            pump_rx();
            pump_tx();
        }
        /* 相手が閉じた: 受信を全部読ませてから idle へ(z80 の net.bin と同じく自動で戻る) */
        if (peer_closed && !pend && rx_h == rx_t && !(k_tls && ktls_pending())) {
            drop_pcb(0);
            tx_h = tx_t = 0;
            k_state = NETSTATE_IDLE;
            idle_at = os_ticks();
        }
        break;
    case NETSTATE_ERROR:
        if ((int32_t)(os_ticks() - err_at) >= ERRHOLD_MS)
            k_state = NETSTATE_IDLE;
        break;
    }

    /* 片付け: 誰も受けなくなった待ち受け(受け手が終わった)と、待たせすぎた接続 */
    if (lpcb && k_state == NETSTATE_IDLE && (int32_t)(os_ticks() - idle_at) > QIDLE_MS)
        drop_listen();
    {
        int k;
        for (k = 0; k < QMAX; k++)
            if (qpcb[k] && (int32_t)(os_ticks() - q_t0[k]) > QIDLE_MS)
                q_drop(k);
    }
}
