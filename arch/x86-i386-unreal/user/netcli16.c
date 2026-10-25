/* arch/x86-i386-unreal/user/netcli16.c -- netcli.h(net_connect / net_read / net_write / net_close)の x86-ia16 実装
 *   ESP-AT(UART 経由。純正 ESP-AT でも python/at_modem.py でもよい)を 16550 で直接話す。
 *   z80board の user/netesp.c(常駐デーモン + 共有リング)とは違い、**コマンドのプロセスの中**で完結する
 *   (常駐デーモンも syscall も要らない)。リアルモードのプロセスは I/O ポートを直接叩ける。
 *   使うコマンド(telnet / ntpdate / atcli / nettest / tzftp)は user/ の共通ソースのまま変更なし。
 *
 *   UART: 既定 COM2(0x2F8、115200bps 8N1)。-DNET_UART=0x3F8 / -DNET_BAUD_DIV=12(9600)で変えられる。
 *     COM1 はコンソールが使うので、QEMU では 2 番目のシリアルに at_modem.py をつなぐ:
 *       python3 python/at_modem.py &   /   qemu ... -serial tcp:127.0.0.1:8080
 *   受信は割り込みなしのポーリング(16550 の FIFO は 16 バイト)。net_read / net_write / 待ちの間だけ UART を
 *   読む。115200bps で長く別の処理をすると取りこぼすので、実機では 9600bps か CTS フロー制御が現実的。
 */
#include "tzstdio.h"
#include "netcli.h"

#ifndef NET_UART
#define NET_UART 0x2F8
#endif
#ifndef NET_BAUD_DIV
#define NET_BAUD_DIV 1
#endif

#include "uart16.h"

#define LINEMAX 64
#define RINGSZ  512            /* 2 のべき乗でなくてよい。+IPD のペイロードを溜める */

static unsigned char inited, connected, esp_ok, mux_on, server_on, listening;     /* esp_ok: AT に OK が返った(ESP が居る) */
/* ifconfig / dig / ping / curl 用: ESP-AT から読んだ情報(アドレスは先頭オクテットが下位バイト = gcc 側 net_info と同じ) */
static unsigned long i_ip, i_mask, i_gw, i_dns, i_mac_hi, i_mac_lo, i_raddr;
static unsigned i_rport, i_link, i_loaded;
static unsigned long i_tx, i_rx, i_txn, i_rxn;
static unsigned long dom_ip, ping_ms;
static unsigned char f_dom, f_ping, f_pingto;
static char sntp_line[40];
static unsigned char echo_lines;      /* net_atcmd 中: 受信した行をそのまま端末へ出す */
static unsigned char f_ok, f_err, f_conn, f_sendok, f_prompt;
static unsigned char ring[RINGSZ];
static unsigned rh, rt, rcount;
static unsigned ipd;             /* +IPD の残ペイロード長 */
static char line[LINEMAX];
static unsigned llen;

static int uart_init(void) { return u_open(); }        /* 溜まっていた受信はカーネルが捨てる */
static void uart_puts(const char *s) { u_write(s, (unsigned)strlen(s)); }

static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void ring_push(unsigned char c)
{
    if (rcount >= RINGSZ) return;              /* あふれた分は捨てる */
    ring[rh] = c; rh = (rh + 1) % RINGSZ; rcount++; i_rx++;
}

/* "a.b.c.d" を 32bit に(先頭オクテットが下位バイト)。失敗は 0。 */
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
static const char *quoted(const char *p)        /* 最初の " の次を返す(無ければ空) */
{
    while (*p && *p != '"') p++;
    return *p ? p + 1 : p;
}
static unsigned hexv(char c) { return c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 0; }

static void handle_info(void)
{
    if (starts(line, "+CIPSTA:ip:")) i_ip = parse_ip(quoted(line));
    else if (starts(line, "+CIPSTA:netmask:")) i_mask = parse_ip(quoted(line));
    else if (starts(line, "+CIPSTA:gateway:")) i_gw = parse_ip(quoted(line));
    else if (starts(line, "+CIFSR:STAIP,")) { unsigned long a = parse_ip(quoted(line)); if (a) i_ip = a; }
    else if (starts(line, "+CIFSR:STAMAC,")) {
        const char *q = quoted(line);
        unsigned k;
        unsigned char m[6];
        for (k = 0; k < 6 && q[0] && q[1]; k++, q += 3) m[k] = (unsigned char)(hexv(q[0]) * 16 + hexv(q[1]));
        i_mac_hi = ((unsigned long)m[0] << 8) | m[1];
        i_mac_lo = ((unsigned long)m[2] << 24) | ((unsigned long)m[3] << 16) | ((unsigned long)m[4] << 8) | m[5];
    }
    else if (starts(line, "+CIPDNS:")) { const char *q = quoted(line); if (*q) i_dns = parse_ip(q); }
    else if (starts(line, "+CWSTATE:")) i_link = (line[9] == '2');
    else if (starts(line, "+CWJAP:")) i_link = 1;
    else if (starts(line, "+CIPSNTPTIME:")) { unsigned k; for (k = 0; k < 39 && line[13 + k]; k++) sntp_line[k] = line[13 + k]; sntp_line[k] = 0; }
    else if (starts(line, "+CIPDOMAIN:")) { dom_ip = parse_ip(quoted(line)); f_dom = 1; }
    else if (starts(line, "+PING:")) {
        if (line[6] >= '0' && line[6] <= '9') { unsigned long v = 0; const char *q = line + 6; while (*q >= '0' && *q <= '9') v = v * 10 + (unsigned long)(*q++ - '0'); ping_ms = v; f_ping = 1; }
        else f_pingto = 1;
    }
}

static void handle_line(void)
{
    line[llen] = 0;
    if (echo_lines && line[0]) { prs(line); putchar('\n'); }
    if (line[0] == '+') handle_info();
    if (same(line, "OK")) f_ok = 1;
    else if (same(line, "ERROR") || same(line, "FAIL") || same(line, "SEND FAIL")) f_err = 1;
    else if (same(line, "SEND OK")) f_sendok = 1;
    else if (same(line, "CONNECT") || same(line, "ALREADY CONNECTED")) f_conn = 1;
    else if (same(line, "CLOSED")) connected = 0;
    else if (same(line, "0,CONNECT")) { connected = 1; listening = 0; i_raddr = 0; i_rport = 0; }   /* 待ち受けに接続が来た */
    else if (same(line, "0,CLOSED")) connected = 0;
}

static void feed(unsigned char c)
{
    if (ipd) { ring_push(c); ipd--; return; }
    if (c == '\r') return;
    if (c == '\n') { handle_line(); llen = 0; return; }
    if (c == '>' && llen == 0) { f_prompt = 1; return; }
    if (c == ' ' && llen == 0) return;
    if (c == ':' && llen >= 6 && starts(line, "+IPD,")) {
        unsigned n = 0, i;
        for (i = 5; i < llen && line[i] >= '0' && line[i] <= '9'; i++) n = n * 10 + (unsigned)(line[i] - '0');
        if (i < llen && line[i] == ',') {              /* +IPD,<link id>,<len>: (CIPMUX=1) */
            n = 0;
            for (i++; i < llen && line[i] >= '0' && line[i] <= '9'; i++) n = n * 10 + (unsigned)(line[i] - '0');
        }
        ipd = n; llen = 0;
        return;
    }
    if (llen < LINEMAX - 1) line[llen++] = (char)c;
}

static void pump(void)
{
    unsigned char b[64];
    unsigned n, i;
    do {
        n = u_read(b, sizeof b);
        for (i = 0; i < n; i++) feed(b[i]);
    } while (n == sizeof b);
}

static void clear_flags(void) { f_ok = f_err = f_conn = f_sendok = f_prompt = 0; }

/* 条件(ok / err / conn / sendok / prompt のどれか)が立つまで待つ。tick は 100Hz。 */
#define W_OK 1
#define W_SENDOK 2
#define W_PROMPT 4
#define W_CONN 8
static int wait_for(unsigned mask, unsigned ticks)
{
    unsigned t0 = getticks();
    for (;;) {
        pump();
        if (f_err) return -1;
        if (((mask & W_OK) && f_ok) || ((mask & W_SENDOK) && f_sendok) ||
            ((mask & W_PROMPT) && f_prompt) || ((mask & W_CONN) && f_conn && f_ok))
            return 0;
        if ((unsigned)(getticks() - t0) > ticks) return -2;
        u_wait(1);                              /* 応答が来るまで寝る(来れば即座に起きる) */
    }
}

static int at_cmd(const char *cmd, unsigned mask, unsigned ticks)
{
    clear_flags();
    uart_puts(cmd);
    uart_puts("\r\n");
    return wait_for(mask, ticks);
}

static void put_uint(char *d, unsigned v)
{
    char t[6]; int i = 0, j = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) d[j++] = t[--i];
    d[j] = 0;
}

static void ensure_init(void)
{
    if (inited) return;
    inited = 1;
    llen = 0; ipd = 0; rh = rt = rcount = 0;
    if (uart_init() != 0) { esp_ok = 0; return; }    /* そのシリアルポートが無い: AT を送らずに諦める */
    esp_ok = at_cmd("AT", W_OK, 100) == 0;      /* 起動直後の取りこぼし対策に 2 回(1 回目は ERROR でもよい) */
    if (!esp_ok) esp_ok = at_cmd("AT", W_OK, 100) == 0;
    at_cmd("ATE0", W_OK, 100);
    at_cmd("AT+CIPSERVER=0", W_OK, 100);        /* 前のプロセス(mcpd / nc -l)が残した待ち受けと CIPMUX=1 を片付けてから単一接続へ */
    at_cmd("AT+CIPMUX=0", W_OK, 100);
}

int net_connect_flags(const char *hostport, unsigned flags)
{
    char cmd[LINEMAX + 24];
    const char *colon = 0, *p;
    unsigned i = 0, hl;

    if (connected) return -1;                       /* 前の接続が片付いていない */
    ensure_init();
    if (mux_on) {                                   /* 待ち受け(CIPMUX=1)から単一接続へ戻す */
        at_cmd("AT+CIPSERVER=0", W_OK, 200);
        at_cmd("AT+CIPMUX=0", W_OK, 200);
        mux_on = server_on = listening = 0;
    }
    for (p = hostport; *p; p++) if (*p == ':') colon = p;
    if (!colon || colon == hostport) return -1;
    hl = (unsigned)(colon - hostport);
    if (hl > 48) return -1;
    if (flags & NET_TLS)                            /* 証明書の検証: 0 = しない / 2 = サーバ証明書を CA で検証 */
        at_cmd((flags & NET_INSECURE) ? "AT+CIPSSLCCONF=0" : "AT+CIPSSLCCONF=2", W_OK, 100);
    for (p = (flags & NET_TLS) ? "AT+CIPSTART=\"SSL\",\"" : "AT+CIPSTART=\"TCP\",\""; *p; p++) cmd[i++] = *p;
    { unsigned k; for (k = 0; k < hl; k++) cmd[i++] = hostport[k]; }
    cmd[i++] = '"'; cmd[i++] = ',';
    for (p = colon + 1; *p; p++) cmd[i++] = *p;
    cmd[i] = 0;
    rh = rt = rcount = 0; ipd = 0;
    i_raddr = parse_ip(hostport);                   /* 数字の IP のときだけ(名前は引かない) */
    i_rport = 0;
    for (p = colon + 1; *p >= '0' && *p <= '9'; p++) i_rport = i_rport * 10 + (unsigned)(*p - '0');
    if (at_cmd(cmd, W_CONN, (flags & NET_TLS) ? 2500 : 1500) != 0) return -1;   /* DNS + TCP(TLS は握手込みで長め) */
    connected = 1;
    return 0;
}

int net_connect(const char *hostport) { return net_connect_flags(hostport, 0); }

int net_connect_tls(const char *hostport, int verify)
{
    return net_connect_flags(hostport, verify ? NET_TLS : (NET_TLS | NET_INSECURE));
}

int net_read(char *buf, int max)
{
    int n = 0;
    pump();
    if (!rcount && connected) {                 /* 何も無ければ 1 tick まで寝る(読みのループが CPU を使い切らない) */
        u_wait(1);
        pump();
    }
    while (n < max && rcount) {
        buf[n++] = (char)ring[rt];
        rt = (rt + 1) % RINGSZ; rcount--;
    }
    return n;
}

int net_write(const char *buf, int len)
{
    int sent = 0;
    char cmd[24];
    if (!connected) return 0;
    while (sent < len) {
        int n = len - sent, k;
        if (n > 64) n = 64;
        cmd[0] = 0;
        { const char *p = mux_on ? "AT+CIPSEND=0," : "AT+CIPSEND="; int i = 0; while (*p) cmd[i++] = *p++; put_uint(cmd + i, (unsigned)n); }
        if (at_cmd(cmd, W_PROMPT, 300) != 0) break;
        u_write(buf + sent, (unsigned)n);
        (void)k;
        f_sendok = 0;
        if (wait_for(W_SENDOK, 500) != 0) break;
        sent += n; i_tx += (unsigned long)n; i_txn++;
    }
    return sent;
}

void net_close(void)
{
    if (!inited) return;
    at_cmd(mux_on ? "AT+CIPCLOSE=0" : "AT+CIPCLOSE", W_OK, 300);
    connected = 0;
    rh = rt = rcount = 0;
}

/* ---------- gcc 側(カーネル TCP)と同じ API を ESP-AT で ---------- */
static void info_load(void)
{
    if (i_loaded) return;
    ensure_init();
    at_cmd("AT+CIPSTA?", W_OK, 200);
    at_cmd("AT+CIFSR", W_OK, 200);
    at_cmd("AT+CIPDNS?", W_OK, 200);
    at_cmd("AT+CWSTATE?", W_OK, 200);
    if (i_ip) i_link = 1;
    i_loaded = 1;
}

int net_state(void)
{
    pump();
    if (!connected && listening && server_on && !rcount) {    /* 待ち受け中のポーリング(mcpd / nc -l)は 1 tick まで寝る */
        u_wait(1);
        pump();
    }
    if (connected) return NET_CONNECTED;
    return (listening && server_on) ? NET_LISTEN : NET_IDLE;
}

unsigned long net_info(unsigned sel)
{
    pump();
    if (sel >= NI_ARP) return 0xFFFFFFFFUL;            /* ARP 表は ESP-AT から見えない */
    if (sel == NI_STATE) return (unsigned long)net_state();
    if (sel == NI_RADDR) return connected ? i_raddr : 0xFFFFFFFFUL;
    if (sel == NI_RPORT) return connected ? (unsigned long)i_rport : 0xFFFFFFFFUL;
    if (sel == NI_LPORT) return 0xFFFFFFFFUL;
    if (sel == NI_RXBYTE || sel == NI_RXIN) return i_rx;
    if (sel == NI_TXBYTE) return i_tx;
    if (sel == NI_PEND) return (unsigned long)rcount;
    if (sel == NI_TCPST) return connected ? 4UL : 0UL;  /* 4 = ESTABLISHED(lwIP の値に合わせた近似) */
    if (sel == NI_RXPKT) return i_rxn ? i_rxn : (i_rx ? 1UL : 0UL);
    if (sel == NI_TXPKT) return i_txn;
    if (sel == NI_MTU) return 1500UL;
    info_load();
    switch (sel) {
    case NI_FLAGS: return 1UL | (i_link ? 2UL : 0UL) | (i_ip ? 4UL : 0UL);
    case NI_ADDR:  return i_link ? (i_ip ? i_ip : 0xFFFFFFFFUL) : 0xFFFFFFFFUL;
    case NI_MASK:  return i_mask ? i_mask : 0xFFFFFFFFUL;
    case NI_GW:    return i_gw ? i_gw : 0xFFFFFFFFUL;
    case NI_DNS:   return i_dns ? i_dns : 0xFFFFFFFFUL;
    case NI_MACHI: return i_mac_hi;
    case NI_MACLO: return i_mac_lo;
    }
    return 0xFFFFFFFFUL;
}

/* 待ち受け: ESP-AT は CIPMUX=1 + AT+CIPSERVER=1,<port>。来た接続は "0,CONNECT"(link id 0)で知らせてくる。
 *   待ち受けは閉じずに残す(接続が終わったあとにもう一度呼ばれたら、listening を立て直すだけ)。 */
int net_listen(unsigned port)
{
    char cmd[32];
    ensure_init();
    if (!esp_ok || connected) return -1;
    if (!mux_on) {
        if (at_cmd("AT+CIPMUX=1", W_OK, 200) != 0) return -1;
        mux_on = 1;
    }
    if (!server_on) {
        const char *p = "AT+CIPSERVER=1,"; unsigned i = 0;
        while (*p) cmd[i++] = *p++;
        put_uint(cmd + i, port);
        if (at_cmd(cmd, W_OK, 200) != 0) return -1;
        server_on = 1;
    }
    listening = 1;
    return 0;
}

/* 名前(数字の IP でもよい)を引く。0=引けた(*addr、*ms)/ -1=引けない */
int net_resolve(const char *name, unsigned long *addr, unsigned long *ms)
{
    char cmd[LINEMAX + 20];
    unsigned i = 0, t0;
    const char *p;
    unsigned long a = parse_ip(name);
    if (a) { *addr = a; *ms = 0; return 0; }
    ensure_init();
    for (p = "AT+CIPDOMAIN=\""; *p; p++) cmd[i++] = *p;
    for (p = name; *p && i < LINEMAX + 16; p++) cmd[i++] = *p;
    cmd[i++] = '"'; cmd[i] = 0;
    f_dom = 0; dom_ip = 0;
    t0 = getticks();
    if (at_cmd(cmd, W_OK, 1000) != 0 || !f_dom || !dom_ip) return -1;
    *addr = dom_ip;
    *ms = (unsigned long)(unsigned)(getticks() - t0) * 10UL;
    return 0;
}

/* ICMP echo。ESP-AT の AT+PING は 1 回ぶんを同期で返す。send で実行して結果を持ち、poll で渡す(0=まだ/失敗、n=ms+1) */
static unsigned long ping_result;
int net_ping_send(unsigned long addr, unsigned seq, unsigned len)
{
    char cmd[40];
    unsigned i = 0, k;
    (void)seq; (void)len;
    ensure_init();
    for (k = 0; k < 4; k++) {
        unsigned o = (unsigned)((addr >> (8 * k)) & 0xFF);
        char t[4]; unsigned n = 0;
        if (k == 0) { const char *p = "AT+PING=\""; while (*p) cmd[i++] = *p++; }
        do { t[n++] = (char)('0' + o % 10); o /= 10; } while (o);
        while (n) cmd[i++] = t[--n];
        if (k < 3) cmd[i++] = '.';
    }
    cmd[i++] = '"'; cmd[i] = 0;
    f_ping = f_pingto = 0; ping_ms = 0; ping_result = 0;
    at_cmd(cmd, W_OK, 500);
    if (f_ping) ping_result = ping_ms + 1;
    return 0;
}
unsigned long net_ping_poll(unsigned seq, unsigned long *ttl)
{
    (void)seq;
    if (ttl) *ttl = 64;
    return ping_result;
}

void net_pr_ip(unsigned long a)
{
    printf("%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF), (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
}

/* ESP-AT の SNTP で現在時刻(Unix 秒)を得る。AT+CIPSNTPCFG=1,9,"pool.ntp.org" → AT+CIPSNTPTIME? の
 *   "+CIPSNTPTIME:Thu Oct 22 14:00:00 2026" を読む。tizix は「表示にそのまま使える値(タイムゾーン加算済み)」を
 *   保持する約束なので、ESP-AT の時刻(タイムゾーン 9 = JST で設定)をそのまま秒にする。0 = 取れない。 */
static unsigned long sntp_epoch;
unsigned long net_sntp_time(void)
{
    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static const unsigned mdays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    unsigned t0, y = 0, d = 0, h = 0, mi = 0, sec = 0, m = 0, k;
    const char *p;
    ensure_init();
    if (!esp_ok) return 0;                              /* ESP が居なければすぐ諦める(起動時の sntp & が長居しない) */
    at_cmd("AT+CIPSNTPCFG=1,9,\"pool.ntp.org\"", W_OK, 300);
    /* 応答は 1 行: +CIPSNTPTIME:... を handle_info では持たない(ここだけで使う)ので、受信行を直接見る */
    sntp_epoch = 0;
    for (k = 0; k < 20; k++) {                          /* 同期するまで最大 ~10 秒 */
        clear_flags();
        uart_puts("AT+CIPSNTPTIME?\r\n");
        t0 = getticks();
        while ((unsigned)(getticks() - t0) < 100 && !f_ok && !f_err) { pump(); u_wait(1); }
        if (sntp_line[0]) break;
        t0 = getticks();
        while ((unsigned)(getticks() - t0) < 50) { pump(); u_wait(1); }
    }
    p = sntp_line;
    if (!p[0]) return 0;
    /* "Thu Oct 22 14:00:00 2026": 曜日 月 日 時:分:秒 年 */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    for (k = 0; k < 12; k++) if (p[0] == mon[3 * k] && p[1] == mon[3 * k + 1] && p[2] == mon[3 * k + 2]) { m = k; break; }
    p += 3; while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') d = d * 10 + (unsigned)(*p++ - '0');
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') h = h * 10 + (unsigned)(*p++ - '0');
    if (*p == ':') p++;
    while (*p >= '0' && *p <= '9') mi = mi * 10 + (unsigned)(*p++ - '0');
    if (*p == ':') p++;
    while (*p >= '0' && *p <= '9') sec = sec * 10 + (unsigned)(*p++ - '0');
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') y = y * 10 + (unsigned)(*p++ - '0');
    if (y < 2000) return 0;                             /* 同期前は 1970 が返る */
    {
        unsigned long days = (unsigned long)(y - 1970) * 365UL + (unsigned long)((y - 1969) / 4) - (unsigned long)((y - 1901) / 100) + (unsigned long)((y - 1601) / 400);
        days += mdays[m] + (d - 1);
        if (m >= 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
        sntp_epoch = days * 86400UL + (unsigned long)h * 3600UL + (unsigned long)mi * 60UL + sec;
    }
    return sntp_epoch;
}

/* AT コマンドを 1 行送り、応答(OK / ERROR まで)を端末へ出す。戻り: 0 = OK / 負 = ERROR・タイムアウト。
 *   wifi / esp コマンド用(AP への参加、IP の確認、任意の AT コマンド)。 */
int net_atcmd(const char *cmd, unsigned ticks)
{
    int r;
    ensure_init();
    echo_lines = 1;
    r = at_cmd(cmd, W_OK, ticks);
    echo_lines = 0;
    return r;
}
