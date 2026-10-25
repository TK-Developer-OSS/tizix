/* arch/x86-i386-unreal/user/netpoc.c -- ネットワーク PoC(#187): NE2000(ISA、I/O 0x300)+ ARP + ICMP ping
 *
 *   QEMU: -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=3
 *   ユーザーモード NAT(SLIRP)の既定: ゲスト 10.0.2.15 / ゲートウェイ 10.0.2.2 / DNS 10.0.2.3。
 *   リアルモードのプロセスは I/O ポートを直接叩ける(保護なし)ので、ドライバをユーザー側に置いて試す。
 *   使い方: netpoc [a.b.c.d]   (既定 10.0.2.2 へ ARP → ICMP echo を 4 回)
 */
#include "tzstdio.h"

#define NE  0x300
static unsigned char inb_(unsigned p) { unsigned char v; __asm__ volatile ("inb %%dx,%%al" : "=a"(v) : "d"(p) : "memory"); return v; }
static void outb_(unsigned p, unsigned char v) { __asm__ volatile ("outb %%al,%%dx" : : "a"(v), "d"(p) : "memory"); }
static unsigned inw_(unsigned p) { unsigned v; __asm__ volatile ("inw %%dx,%%ax" : "=a"(v) : "d"(p) : "memory"); return v; }
static void outw_(unsigned p, unsigned v) { __asm__ volatile ("outw %%ax,%%dx" : : "a"(v), "d"(p) : "memory"); }

static unsigned char mac[6];
static const unsigned char myip[4] = {10, 0, 2, 15};
static unsigned char tgt[4] = {10, 0, 2, 2};
static unsigned char tmac[6];
static unsigned char pkt[200];
static unsigned char rxb[1600];

static void wait_ticks(unsigned n)       /* getticks は 100Hz */
{
    unsigned t0 = getticks();
    while ((unsigned)(getticks() - t0) < n) ;
}

/* リモート DMA: NIC の内部 RAM(アドレス a)から n バイトを buf へ / buf から書く */
static void dma_read(unsigned a, unsigned char *buf, unsigned n)
{
    unsigned i;
    outb_(NE + 0x00, 0x22);                       /* page0(page1 のまま ISR=reg7 に書くと CURR を壊す) */
    outb_(NE + 0x07, 0x40);                       /* ISR: RDC クリア */
    outb_(NE + 0x0A, n & 0xFF); outb_(NE + 0x0B, n >> 8);
    outb_(NE + 0x08, a & 0xFF); outb_(NE + 0x09, a >> 8);
    outb_(NE + 0x00, 0x0A);                       /* remote read, start */
    for (i = 0; i + 1 < n; i += 2) { unsigned w = inw_(NE + 0x10); buf[i] = (unsigned char)w; buf[i + 1] = (unsigned char)(w >> 8); }
    if (n & 1) buf[n - 1] = (unsigned char)inw_(NE + 0x10);
}
static void dma_write(unsigned a, const unsigned char *buf, unsigned n)
{
    unsigned i;
    outb_(NE + 0x00, 0x22);
    outb_(NE + 0x07, 0x40);
    outb_(NE + 0x0A, n & 0xFF); outb_(NE + 0x0B, n >> 8);
    outb_(NE + 0x08, a & 0xFF); outb_(NE + 0x09, a >> 8);
    outb_(NE + 0x00, 0x12);                       /* remote write, start */
    for (i = 0; i + 1 < n; i += 2) outw_(NE + 0x10, (unsigned)buf[i] | ((unsigned)buf[i + 1] << 8));
    if (n & 1) outw_(NE + 0x10, buf[n - 1]);
    {   unsigned long g = 0; while (!(inb_(NE + 0x07) & 0x40) && ++g < 60000UL) ; }
}

static int ne_init(void)
{
    unsigned char prom[32];
    unsigned i;
    inb_(NE + 0x1F); outb_(NE + 0x1F, inb_(NE + 0x1F));   /* リセット */
    wait_ticks(5);
    outb_(NE + 0x00, 0x21);                       /* page0, stop */
    outb_(NE + 0x0E, 0x49);                       /* DCR: 16bit, FIFO 8 */
    outb_(NE + 0x0A, 0); outb_(NE + 0x0B, 0);
    outb_(NE + 0x0C, 0x20);                       /* RCR: monitor */
    outb_(NE + 0x0D, 0x02);                       /* TCR: loopback */
    dma_read(0, prom, 32);
    for (i = 0; i < 6; i++) mac[i] = prom[i * 2];
    outb_(NE + 0x04, 0x40);                       /* TPSR */
    outb_(NE + 0x01, 0x46);                       /* PSTART */
    outb_(NE + 0x02, 0x80);                       /* PSTOP */
    outb_(NE + 0x03, 0x7F);                       /* BNRY = PSTOP-1(Linux と同じ流儀) */
    outb_(NE + 0x07, 0xFF);                       /* ISR クリア */
    outb_(NE + 0x0F, 0x00);                       /* IMR: 割り込み無し(ポーリング) */
    outb_(NE + 0x00, 0x61);                       /* page1, stop */
    for (i = 0; i < 6; i++) outb_(NE + 0x01 + i, mac[i]);   /* PAR0..5 */
    outb_(NE + 0x07, 0x46);                       /* CURR = PSTART */
    for (i = 0; i < 8; i++) outb_(NE + 0x08 + i, 0xFF);     /* MAR: 全マルチキャスト */
    outb_(NE + 0x00, 0x22);                       /* page0, start */
    outb_(NE + 0x0D, 0x00);                       /* TCR: 通常 */
    outb_(NE + 0x0C, 0x14);                       /* RCR: PRO(全部受ける)+ ブロードキャスト */
    return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

static unsigned char txb[128];
static void ne_send(const unsigned char *f, unsigned n)
{
    unsigned long g = 0;
    unsigned i;
    for (i = 0; i < 60 || i < n; i++) if (i < 128) txb[i] = i < n ? f[i] : 0;
    if (n < 60) n = 60;
    dma_write(0x4000, txb, n);
    outb_(NE + 0x04, 0x40);
    outb_(NE + 0x05, n & 0xFF); outb_(NE + 0x06, n >> 8);
    outb_(NE + 0x00, 0x26);                       /* transmit */
    while ((inb_(NE + 0x00) & 0x04) && ++g < 60000UL) ;
}

/* 受信: 1 フレームを rxb へ。戻り = 長さ / 0=無し */
static unsigned ne_recv(void)
{
    unsigned char hdr[4], cur, bnry, next;
    unsigned len;
    outb_(NE + 0x00, 0x62); cur = inb_(NE + 0x07); outb_(NE + 0x00, 0x22);   /* page1: CURR を読む */
    bnry = inb_(NE + 0x03);
    next = (unsigned char)(bnry + 1); if (next >= 0x80) next = 0x46;
    if (next == cur) return 0;
    {   /* ヘッダ: status, next, len(2B)。受信直後に 0 のことがあるので、無効なら短く待って読み直す */
        unsigned tries = 0;
        for (;;) {
            dma_read((unsigned)next << 8, hdr, 4);
            len = ((unsigned)hdr[3] << 8 | hdr[2]);
            if (hdr[1] != 0 && len >= 4 && len <= sizeof rxb + 4) break;
            if (++tries >= 5) {          /* 諦めて受信リングを捨てる(再同期) */
                outb_(NE + 0x03, (unsigned char)(cur == 0x46 ? 0x7F : cur - 1));
                return 0;
            }
            wait_ticks(1);
        }
    }
    len -= 4;
    dma_read(((unsigned)next << 8) + 4, rxb, len);
    { unsigned char nb = (unsigned char)(hdr[1] - 1); if (hdr[1] == 0x46) nb = 0x7F; outb_(NE + 0x03, nb); }
    outb_(NE + 0x07, 0x01);
    return len;
}

static unsigned csum(const unsigned char *p, unsigned n)
{
    unsigned long s = 0; unsigned i;
    for (i = 0; i + 1 < n; i += 2) s += ((unsigned)p[i] << 8) | p[i + 1];
    if (n & 1) s += (unsigned)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (unsigned)~s;
}

static void put_ip(const unsigned char *a) { prnum(a[0]); putchar('.'); prnum(a[1]); putchar('.'); prnum(a[2]); putchar('.'); prnum(a[3]); }

static int arp_resolve(void)
{
    unsigned i, t;
    for (i = 0; i < 6; i++) pkt[i] = 0xFF;
    for (i = 0; i < 6; i++) pkt[6 + i] = mac[i];
    pkt[12] = 0x08; pkt[13] = 0x06;
    pkt[14] = 0; pkt[15] = 1; pkt[16] = 0x08; pkt[17] = 0; pkt[18] = 6; pkt[19] = 4; pkt[20] = 0; pkt[21] = 1;
    for (i = 0; i < 6; i++) pkt[22 + i] = mac[i];
    for (i = 0; i < 4; i++) pkt[28 + i] = myip[i];
    for (i = 0; i < 6; i++) pkt[32 + i] = 0;
    for (i = 0; i < 4; i++) pkt[38 + i] = tgt[i];
    for (t = 0; t < 30; t++) {
        unsigned n, w;
        if (t % 10 == 0) ne_send(pkt, 42);
        for (w = 0; w < 10; w++) {
            n = ne_recv();
            if (n >= 42 && rxb[12] == 0x08 && rxb[13] == 0x06 && rxb[21] == 2 &&
                rxb[28] == tgt[0] && rxb[29] == tgt[1] && rxb[30] == tgt[2] && rxb[31] == tgt[3]) {
                for (i = 0; i < 6; i++) tmac[i] = rxb[22 + i];
                return 1;
            }
            wait_ticks(1);
        }
    }
    return 0;
}

static int ping_once(unsigned seq)
{
    unsigned i, w, c;
    unsigned char *ip = pkt + 14, *ic = pkt + 34;
    for (i = 0; i < 6; i++) pkt[i] = tmac[i];
    for (i = 0; i < 6; i++) pkt[6 + i] = mac[i];
    pkt[12] = 0x08; pkt[13] = 0;
    ip[0] = 0x45; ip[1] = 0; ip[2] = 0; ip[3] = 28 + 8; ip[4] = 0; ip[5] = (unsigned char)seq; ip[6] = 0; ip[7] = 0;
    ip[8] = 64; ip[9] = 1; ip[10] = 0; ip[11] = 0;
    for (i = 0; i < 4; i++) ip[12 + i] = myip[i];
    for (i = 0; i < 4; i++) ip[16 + i] = tgt[i];
    c = csum(ip, 20); ip[10] = (unsigned char)(c >> 8); ip[11] = (unsigned char)c;
    ic[0] = 8; ic[1] = 0; ic[2] = 0; ic[3] = 0; ic[4] = 0x12; ic[5] = 0x34; ic[6] = 0; ic[7] = (unsigned char)seq;
    for (i = 0; i < 8; i++) ic[8 + i] = 'a' + i;
    c = csum(ic, 16); ic[2] = (unsigned char)(c >> 8); ic[3] = (unsigned char)c;
    ne_send(pkt, 14 + 20 + 16);
    for (w = 0; w < 200; w++) {
        unsigned n = ne_recv();
        if (n >= 42 && rxb[12] == 0x08 && rxb[13] == 0 && rxb[23] == 1 && rxb[34] == 0 && rxb[41] == (unsigned char)seq)
            return 1;
        wait_ticks(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned i, ok = 0;
    if (argc > 1) {       /* a.b.c.d */
        const char *s = argv[1]; unsigned v = 0, k = 0;
        for (;; s++) { if (*s >= '0' && *s <= '9') v = v * 10 + (*s - '0'); else { if (k < 4) tgt[k++] = (unsigned char)v; v = 0; if (!*s) break; } }
    }
    if (!ne_init()) { puts("netpoc: NE2000 not found (QEMU: -device ne2k_isa,iobase=0x300)"); return 1; }
    prs("NE2000 MAC "); for (i = 0; i < 6; i++) { prnum(mac[i]); if (i < 5) putchar(':'); } putchar('\n');
    prs("ARP "); put_ip(tgt); prs(" ... ");
    if (!arp_resolve()) { puts("no reply"); return 1; }
    puts("ok");
    for (i = 0; i < 4; i++) {
        unsigned t0 = getticks();
        prs("ping "); put_ip(tgt); prs(": ");
        if (ping_once(i + 1)) { ok++; prs("reply "); prnum((unsigned)(getticks() - t0) * 10); puts(" ms"); }
        else puts("timeout");
    }
    prnum(ok); puts(" / 4 replies");
    return ok ? 0 : 1;
}
