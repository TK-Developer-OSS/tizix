/* arch/x86-ia16/user/netmodem16.c -- netcli.h(net_connect / net_read / net_write / net_close ほか)の
 *   RS-232C モデム(Hayes AT)版。tizix のネットワークの原則(TK 2026-10-23)。ESP-AT は一時的な代用(netcli16.c、NET=esp)。
 *
 *   ダイヤル: "ATDT host:port"(WiFi モデム = Zimodem / WiFi232 などが host:port を TCP につなぐ。昔ながらのモデムなら電話番号)
 *             → "CONNECT ..." で以降は**生のバイト列**(データモード)。
 *   切断:     ガードタイム(約 1.1 秒の無通信)+ "+++" + ガードタイム → コマンドモードへ → "ATH"。
 *             相手が切ったら "NO CARRIER"(データ中に出るので末尾を見張る)か DCD の落ち。
 *   UART:     既定 COM2(0x2F8、115200bps 8N1)。-DNET_UART=0x3F8 / -DNET_BAUD_DIV=12(9600)で変えられる。
 *   モデムの下では DNS 照会・ping・インタフェース情報・待ち受けは**できない**(それらは lwIP 級の機器の機能)。
 *   gcc 系と同じ API 名は揃えてあるが、net_resolve は数字の IP だけ、net_ping / net_listen は失敗を返す。
 *   時計合わせ: RFC 868 の time(port 37。32bit・1900 年からの秒数)を ATDT で取る(net_sntp_time)。
 *   受信は割り込みなしのポーリング(16550 の FIFO は 16 バイト)。実機では 9600bps か RTS/CTS が現実的。
 */
#include "tzstdio.h"
#include "netcli.h"

#ifndef NET_UART
#define NET_UART 0x2F8
#endif
#ifndef NET_BAUD_DIV
#define NET_BAUD_DIV 1
#endif
#ifndef NET_TIME_HOST
#define NET_TIME_HOST "time.nist.gov:37"
#endif

#define LINEMAX 64
#define RINGSZ  512

static unsigned char inited, modem_ok, connected, data_mode, carrier_seen, echo_lines, dcd_seen;
static unsigned char f_ok, f_err, f_connect, f_nocarrier;
static unsigned char ring[RINGSZ];
static unsigned rh, rt, rcount;
static char line[LINEMAX];
static unsigned llen;
static char tail[10];                         /* データ中の "NO CARRIER" 検出用に直近 10 バイト */
static unsigned long i_tx, i_rx;

static unsigned char inb_(unsigned p) { unsigned char v; __asm__ volatile ("inb %%dx,%%al" : "=a"(v) : "d"(p) : "memory"); return v; }
static void outb_(unsigned p, unsigned char v) { __asm__ volatile ("outb %%al,%%dx" : : "a"(v), "d"(p) : "memory"); }

static void uart_init(void)
{
    outb_(NET_UART + 1, 0);
    outb_(NET_UART + 3, 0x80);
    outb_(NET_UART + 0, (unsigned char)(NET_BAUD_DIV & 0xFF));
    outb_(NET_UART + 1, (unsigned char)(NET_BAUD_DIV >> 8));
    outb_(NET_UART + 3, 0x03);                 /* 8N1 */
    outb_(NET_UART + 2, 0xC7);                 /* FIFO 有効・クリア */
    outb_(NET_UART + 4, 0x03);                 /* DTR/RTS(モデムに「端末が居る」と知らせる) */
    while (inb_(NET_UART + 5) & 1) inb_(NET_UART);
}
static void uart_putc(unsigned char c)
{
    unsigned long g = 0;
    while (!(inb_(NET_UART + 5) & 0x20) && ++g < 200000UL) ;
    outb_(NET_UART, c);
}
static void uart_puts(const char *s) { while (*s) uart_putc((unsigned char)*s++); }

static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void handle_line(void)
{
    line[llen] = 0;
    if (echo_lines && line[0]) { prs(line); putchar('\n'); }
    if (same(line, "OK")) f_ok = 1;
    else if (same(line, "ERROR")) f_err = 1;
    else if (starts(line, "CONNECT")) { f_connect = 1; data_mode = 1; carrier_seen = 1; }   /* 以降は生データ */
    else if (same(line, "NO CARRIER") || same(line, "BUSY") || same(line, "NO ANSWER") || same(line, "NO DIALTONE")) f_nocarrier = 1;
}

static void feed(unsigned char c)
{
    if (data_mode) {
        unsigned k;
        if (rcount < RINGSZ) { ring[rh] = c; rh = (rh + 1) % RINGSZ; rcount++; i_rx++; }
        for (k = 0; k < 9; k++) tail[k] = tail[k + 1];
        tail[9] = (char)c;
        if (starts(tail, "NO CARRIER")) { data_mode = 0; connected = 0; }   /* 相手が切った */
        return;
    }
    if (c == '\r') return;
    if (c == '\n') { handle_line(); llen = 0; return; }
    if (llen < LINEMAX - 1) line[llen++] = (char)c;
}

static void pump(void)
{
    while (inb_(NET_UART + 5) & 1) feed(inb_(NET_UART));
    if (connected) {                            /* DCD を出すモデムだけ: 一度 DCD が立ってから落ちたら相手が切った */
        if (inb_(NET_UART + 6) & 0x80) dcd_seen = 1;
        else if (dcd_seen) connected = 0;
    }
}

static void clear_flags(void) { f_ok = f_err = f_connect = f_nocarrier = 0; }

#define W_OK      1
#define W_CONNECT 2
static int wait_for(unsigned mask, unsigned ticks)
{
    unsigned t0 = getticks();
    for (;;) {
        pump();
        if (f_err || f_nocarrier) return -1;
        if (((mask & W_OK) && f_ok) || ((mask & W_CONNECT) && f_connect)) return 0;
        if ((unsigned)(getticks() - t0) > ticks) return -2;
    }
}

static int cmd_line(const char *cmd, unsigned mask, unsigned ticks)
{
    clear_flags();
    uart_puts(cmd);
    uart_puts("\r");
    return wait_for(mask, ticks);
}

static void guard(unsigned ticks)            /* 何も送らず待つ(その間の受信は取り込む) */
{
    unsigned t0 = getticks();
    while ((unsigned)(getticks() - t0) < ticks) pump();
}

static void ensure_init(void)
{
    if (inited) return;
    uart_init();
    inited = 1;
    llen = 0; data_mode = 0; rh = rt = rcount = 0;
    modem_ok = cmd_line("AT", W_OK, 100) == 0;
    if (!modem_ok) modem_ok = cmd_line("AT", W_OK, 100) == 0;
    if (modem_ok) { cmd_line("ATE0", W_OK, 100); cmd_line("ATQ0", W_OK, 100); }
}

int net_connect_flags(const char *hostport, unsigned flags)
{
    char cmd[LINEMAX + 8];
    unsigned i = 0;
    const char *p;
    if (connected || (flags & NET_TLS)) return -1;      /* モデム越しに TLS は無い */
    ensure_init();
    if (!modem_ok) return -1;
    for (p = "ATDT"; *p; p++) cmd[i++] = *p;
    for (p = hostport; *p && i < LINEMAX + 4; p++) cmd[i++] = *p;
    cmd[i] = 0;
    rh = rt = rcount = 0;
    { unsigned k; for (k = 0; k < 10; k++) tail[k] = 0; }
    if (cmd_line(cmd, W_CONNECT, 3000) != 0) { data_mode = 0; return -1; }   /* 最大 30 秒(ダイヤル + 接続) */
    connected = 1; dcd_seen = 0;
    return 0;
}
int net_connect(const char *hostport) { return net_connect_flags(hostport, 0); }
int net_connect_tls(const char *hostport, int verify) { (void)hostport; (void)verify; return -1; }

int net_read(char *buf, int max)
{
    int n = 0;
    pump();
    while (n < max && rcount) {
        buf[n++] = (char)ring[rt];
        rt = (rt + 1) % RINGSZ; rcount--;
    }
    return n;
}

int net_write(const char *buf, int len)
{
    int k;
    if (!connected) return 0;
    for (k = 0; k < len; k++) {
        uart_putc((unsigned char)buf[k]);
        if ((k & 7) == 7) pump();                       /* 送りながら受信も取り込む(FIFO 16 バイト) */
    }
    i_tx += (unsigned long)len;
    return len;
}

void net_close(void)
{
    if (!inited) return;
    if (connected && data_mode) {
        guard(110);                                      /* +++ の前後は 1 秒以上の無通信(S12 の既定 50/50 秒) */
        uart_puts("+++");
        guard(110);
    }
    data_mode = 0;
    if (connected || carrier_seen) cmd_line("ATH", W_OK, 300);
    connected = 0; carrier_seen = 0;
    rh = rt = rcount = 0;
}

int net_state(void)
{
    pump();
    return connected ? NET_CONNECTED : NET_IDLE;
}

unsigned long net_info(unsigned sel)
{
    ensure_init();
    pump();
    switch (sel) {
    case NI_FLAGS:  return 0UL;                          /* モデムには IP のインタフェースが無い(ifconfig は "no network interface") */
    case NI_STATE:  return (unsigned long)(connected ? NET_CONNECTED : NET_IDLE);
    case NI_RXBYTE: case NI_RXIN: return i_rx;
    case NI_TXBYTE: return i_tx;
    case NI_PEND:   return (unsigned long)rcount;
    case NI_MTU:    return 0UL;
    }
    return 0xFFFFFFFFUL;                                 /* アドレス類・ARP・ポートはモデムには無い */
}

int net_listen(unsigned port) { (void)port; return -1; }      /* 着信待ち(ATA / S0=1)は未対応 */

static unsigned long parse_ip(const char *p)
{
    unsigned long v = 0;
    unsigned k, n;
    for (k = 0; k < 4; k++) {
        n = 0;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') n = n * 10 + (unsigned)(*p++ - '0');
        v |= (unsigned long)(n & 0xFF) << (8 * k);
        if (k < 3) { if (*p != '.') return 0; p++; }
    }
    return v;
}
int net_resolve(const char *name, unsigned long *addr, unsigned long *ms)
{
    unsigned long a = parse_ip(name);                    /* 名前はモデムが引く(ATDT に名前のまま渡す)。ここでは数字だけ */
    if (!a) return -1;
    *addr = a; *ms = 0;
    return 0;
}
int net_ping_send(unsigned long addr, unsigned seq, unsigned len) { (void)addr; (void)seq; (void)len; return -1; }
unsigned long net_ping_poll(unsigned seq, unsigned long *ttl) { (void)seq; if (ttl) *ttl = 0; return 0; }

void net_pr_ip(unsigned long a)
{
    printf("%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF), (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
}

/* AT コマンドを 1 行送り、応答(OK / ERROR まで)を端末へ出す(wifi コマンド用)。 */
int net_atcmd(const char *cmd, unsigned ticks)
{
    int r;
    ensure_init();
    echo_lines = 1;
    r = cmd_line(cmd, W_OK, ticks);
    echo_lines = 0;
    return r;
}

/* 時計合わせ: RFC 868 の time(32bit、1900-01-01 からの秒数)を ATDT で取る。tizix は表示にそのまま使える値(JST 加算済み)を持つ。 */
unsigned long net_sntp_time(void)
{
    unsigned long v = 0;
    unsigned got = 0, t0;
    char b;
    if (net_connect(NET_TIME_HOST) != 0) return 0;
    t0 = getticks();
    while (got < 4 && (unsigned)(getticks() - t0) < 1000) {
        if (net_read(&b, 1) == 1) { v = (v << 8) | (unsigned char)b; got++; }
    }
    net_close();
    if (got < 4 || v < 2208988800UL) return 0;
    return v - 2208988800UL + 9UL * 3600UL;
}
