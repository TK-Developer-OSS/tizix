/* arch/x86-i386-unreal/user/netmodem16.c -- netcli.h(net_connect / net_read / net_write / net_close ほか)の
 *   RS-232C モデム(Hayes AT)版。tizix のネットワークの原則(TK 2026-10-23)。ESP-AT は一時的な代用(netcli16.c、NET=esp)。
 *
 *   ダイヤル: "ATDT host:port"(WiFi モデム = Zimodem / WiFi232 などが host:port を TCP につなぐ。昔ながらのモデムなら電話番号)
 *             → "CONNECT ..." で以降は**生のバイト列**(データモード)。
 *   切断:     ガードタイム(約 1.1 秒の無通信)+ "+++" + ガードタイム → コマンドモードへ → "ATH"。
 *             相手が切ったら "NO CARRIER"(データ中に出るので末尾を見張る)か DCD の落ち。
 *   UART:     既定 COM2(0x2F8、115200bps 8N1)。-DNET_UART=0x3F8 / -DNET_BAUD_DIV=12(9600)で変えられる。
 *   モデムの下では DNS 照会・ping・インタフェース情報・待ち受けは**できない**(それらは lwIP 級の機器の機能)。
 *   gcc 系と同じ API 名は揃えてあるが、net_resolve は数字の IP だけ、net_ping は失敗を返す。net_listen は着信待ち(RING → ATA)。
 *   時計合わせ: RFC 868 の time(port 37。32bit・1900 年からの秒数)を ATDT で取る(net_sntp_time)。
 *   シリアルはカーネルの割り込み受信ドライバ経由(user/uart16.h)。待つ間は寝る(u_wait)。
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

#include "uart16.h"

#define LINEMAX 64
#define RINGSZ  512

static unsigned char inited, modem_ok, connected, data_mode, carrier_seen, echo_lines, dcd_seen, listening;
static unsigned char f_ok, f_err, f_connect, f_nocarrier, f_ring;
static unsigned char ring[RINGSZ];
static unsigned rh, rt, rcount;
static char line[LINEMAX];
static unsigned llen;
/* データ中の "\r\nNO CARRIER"(相手が切った知らせ)を、受信データに混ぜないための見張り。この形の頭に合う間は
 * hold に溜めて渡さず、全部そろえば切断、途中で外れたら溜めた分をデータとして渡す。 */
static const char nc_pat[] = "\r\nNO CARRIER";
#define NC_LEN 12
static char hold[NC_LEN];
static unsigned hlen;
static unsigned long i_tx, i_rx;

static int uart_init(void) { return u_open(); }        /* DTR/RTS を立てる(モデムに「端末が居る」と知らせる) */
static void uart_puts(const char *s) { u_write(s, (unsigned)strlen(s)); }

static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void handle_line(void)
{
    line[llen] = 0;
    if (echo_lines && line[0]) { prs(line); putchar('\n'); }
    if (same(line, "OK")) f_ok = 1;
    else if (same(line, "ERROR")) f_err = 1;
    else if (starts(line, "CONNECT")) { f_connect = 1; data_mode = 1; carrier_seen = 1; }   /* 以降は生データ */
    else if (same(line, "RING")) f_ring = 1;                                    /* 着信(net_listen 中なら ATA で出る) */
    else if (same(line, "NO CARRIER") || same(line, "BUSY") || same(line, "NO ANSWER") || same(line, "NO DIALTONE")) f_nocarrier = 1;
}

static void put_data(unsigned char c)
{
    if (rcount < RINGSZ) { ring[rh] = c; rh = (rh + 1) % RINGSZ; rcount++; i_rx++; }
}

static void flush_hold(void)                  /* 見張りに溜めていた分をデータとして渡す */
{
    unsigned k;
    for (k = 0; k < hlen; k++) put_data((unsigned char)hold[k]);
    hlen = 0;
}

static void feed(unsigned char c)
{
    if (data_mode) {
        unsigned k, ok;
        hold[hlen++] = (char)c;
        for (;;) {                            /* hold が nc_pat の頭に合うまで、先頭から 1 バイトずつデータとして渡す */
            for (ok = 1, k = 0; k < hlen; k++) if (hold[k] != nc_pat[k]) { ok = 0; break; }
            if (ok) break;
            put_data((unsigned char)hold[0]);
            for (k = 1; k < hlen; k++) hold[k - 1] = hold[k];
            hlen--;
        }
        if (hlen == NC_LEN) { hlen = 0; data_mode = 0; connected = 0; }    /* 相手が切った(この知らせはデータに入れない) */
        return;
    }
    if (c == '\r') return;
    if (c == '\n') { handle_line(); llen = 0; return; }
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
    if (connected) {                            /* DCD を出すモデムだけ: 一度 DCD が立ってから落ちたら相手が切った */
        if (u_msr() & 0x80) dcd_seen = 1;
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
        u_wait(1);                              /* 応答が来るまで寝る(来れば即座に起きる) */
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
    while ((unsigned)(getticks() - t0) < ticks) { pump(); u_wait(1); }
}

static void ensure_init(void)
{
    if (inited && modem_ok) return;             /* 見つかっていなければ毎回試し直す(モデムが後から電源を入れられることもある) */
    inited = 1;
    llen = 0; data_mode = 0; rh = rt = rcount = 0;
    if (uart_init() != 0) { modem_ok = 0; return; }  /* そのシリアルポートが無い: AT を送らずに諦める */
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
    hlen = 0;
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
    if (!rcount && connected) {                 /* 何も無ければ 1 tick まで寝る(読みのループが CPU を使い切らない) */
        if (u_wait(1) == 0) {
            /* 1 tick 何も来なかった: 見張りに溜めていた "\r\n" などは NO CARRIER の頭ではなくデータだった。
             * (何か来て u_wait が早く戻ったときは吐き出さない。NO CARRIER が途中まで届いたところかもしれない) */
            if (hlen) flush_hold();
        } else
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
    int k;
    if (!connected) return 0;
    for (k = 0; k < len; k += 64) {                     /* 受信はカーネルが貯めるので、送る間に取り込まなくてよい */
        u_write(buf + k, (unsigned)(len - k > 64 ? 64 : len - k));
        pump();
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
    connected = 0; carrier_seen = 0; listening = 0;
    rh = rt = rcount = 0;
    hlen = 0;
}

int net_state(void)
{
    pump();
    if (connected) return NET_CONNECTED;
    if (!listening) return NET_IDLE;
    if (f_ring) {                               /* 着信: ATA で出る */
        f_ring = 0;
        rh = rt = rcount = 0;
        hlen = 0;
        if (cmd_line("ATA", W_CONNECT, 500) == 0) {
            connected = 1; listening = 0; dcd_seen = 0;
            return NET_CONNECTED;
        }
        data_mode = 0;
        return NET_LISTEN;
    }
    u_wait(1);                                  /* 待ち受け中のポーリング(telnetd / mcpd / nc -l)は 1 tick まで寝る */
    return NET_LISTEN;
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

/* 着信を待つ(一般のモデムと同じ: RING が来たら ATA で出る)。port はモデム側の設定(Pi の at_modem.py --listen)で決まり、
 * ここでは使わない(電話番号がモデムの回線で決まるのと同じ)。 */
int net_listen(unsigned port)
{
    (void)port;
    ensure_init();
    if (!modem_ok || connected) return -1;
    f_ring = 0;
    cmd_line("ATS0=0", W_OK, 100);              /* 自動応答はしない(こちらが ATA で出る) */
    listening = 1;
    return 0;
}

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
