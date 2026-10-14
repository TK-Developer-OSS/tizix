#include "console.h"

/* ------------------------------------------------------------------
 * 層1 実体: PC-8801 テキスト VRAM への直書き(出力のみ)
 *
 *   テキスト VRAM は 0xF3C8 起点で 1 行 120B(文字 80 + 属性 40)、
 *   ファーム(擬似 BIOS / N88)が CRTC と DMA を設定済みなので、ここは
 *   文字コードを書くだけ。0xF000 以降(block7)は crt0.s がカーネルから
 *   取り上げてある。上 5 行は IPL と将来のメッセージ用に空けてある。
 *
 *   入力(キーボード)は未実装: con_getc は戻らない。
 *   2026-10-04: 骨組み。cpmsim 流用(z80pack)はこのファイルを使わない。
 * ------------------------------------------------------------------ */
#define TVRAM  0xF3C8u
#define PITCH  120u
#define COLS   80
#define ROW0   5            /* 先頭行(0 起点) */
#define ROWS   20            /* 擬似 BIOS は 80x20 表示。最下行(19)はファンクションキーの帯だった */

__sfr __at (0x50) CRTC_PRM;
__sfr __at (0x51) CRTC_CMD;

static unsigned char *crow;     /* 現在行の先頭 */
static unsigned char ccol;
static unsigned char crn;       /* 現在行番号 */

/* 1 行スクロール: 6 行目以降を 1 行上へ(文字も属性も 120B ずつまとめて LDIR)、最下行の文字を消す。
 *   定数は TVRAM=0xF3C8 PITCH=120 ROW0=5 ROWS=20 から:
 *     移動元 0xF3C8+6*120=0xF698 / 移動先 0xF3C8+5*120=0xF620 / 長さ 14*120=0x0690 /
 *     最下行(19)= 0xF3C8+19*120=0xFCB0(LDIR 後の DE がちょうどここ) */
static void kscroll(void) __naked
{
    __asm
        ld      hl, #0xF698
        ld      de, #0xF620
        ld      bc, #0x0690
        ldir
        ld      h, d
        ld      l, e
        ld      (hl), #0x20
        inc     de
        ld      bc, #79
        ldir
        ret
    __endasm;
}

static void newline(void)
{
    if (++crn < ROWS) {
        crow += PITCH;
        return;
    }
    crn = ROWS - 1;
    kscroll();
}

static unsigned char in_esc;
static char esc_buf[8];
static unsigned char esc_len;

void con_putc(char c)
{
    unsigned char i;

    if (crow == 0) {
        crow = (unsigned char *)TVRAM + (unsigned)ROW0 * PITCH;
        crn = ROW0;
        for (i = 0; i < 120; i++)
            crow[(unsigned)(ROWS - 1 - ROW0) * PITCH + i] = i < 80 ? ' ' : (i & 1) ? 0 : 0x80;
    }

    if (in_esc) {
        if (in_esc == 1) {
            if (c == '[') {
                in_esc = 2;
                esc_len = 0;
            } else {
                in_esc = 0;
                con_putc(c);
            }
            return;
        } else if (in_esc == 2) {
            if (c >= 0x40 && c <= 0x7E) {
                in_esc = 0;
                if (esc_len < 7) esc_buf[esc_len] = 0;
                if (esc_len == 2 && esc_buf[0] == '2' && c == 'J') {
                    unsigned char *p = (unsigned char *)TVRAM + (unsigned)ROW0 * PITCH;
                    for (i = ROW0; i < ROWS; i++) {
                        unsigned char j;
                        for (j = 0; j < COLS; j++) p[j] = ' ';
                        p += PITCH;
                    }
                    crow = (unsigned char *)TVRAM + (unsigned)ROW0 * PITCH;
                    crn = ROW0;
                    ccol = 0;
                } else if (esc_len == 0 && c == 'H') {
                    crn = ROW0;
                    ccol = 0;
                    crow = (unsigned char *)TVRAM + (unsigned)ROW0 * PITCH;
                } else if (esc_len == 0 && c == 'K') {
                    for (i = ccol; i < COLS; i++) crow[i] = ' ';
                } else if (c == 'H') {
                    unsigned char r = 0, col = 0;
                    char *p = esc_buf;
                    while (*p >= '0' && *p <= '9') { r = r * 10 + (*p - '0'); p++; }
                    if (*p == ';') p++;
                    while (*p >= '0' && *p <= '9') { col = col * 10 + (*p - '0'); p++; }
                    if (r) r--;
                    if (col) col--;
                    if (r >= ROWS - ROW0) r = ROWS - ROW0 - 1;
                    if (col >= COLS) col = COLS - 1;
                    crn = ROW0 + r;
                    crow = (unsigned char *)TVRAM + (unsigned)crn * PITCH;
                    ccol = col;
                }
            } else {
                if (esc_len < 7) esc_buf[esc_len++] = c;
            }
            return;
        }
    }

    if (c == 27) {
        in_esc = 1;
        return;
    }

    if (c == 13) ccol = 0;
    else if (c == 10) newline();
    else if (c == 8) { if (ccol) ccol--; }
    else if (c >= 32) {
        crow[ccol] = c;
        if (++ccol >= COLS) {
            ccol = 0;
            newline();
        }
    }
    /* 点滅カーソルを入力位置へ(µPD3301 のカーソル位置設定: 0x81 のあとに X, Y) */
    CRTC_CMD = 0x81;
    CRTC_PRM = ccol;
    CRTC_PRM = crn;
}

/* ------------------------------------------------------------------
 * キーボード: ポート 0x00-0x0A のマトリクスを読む(押すと該当ビットが 0)。
 *   割り込みは使わずポーリング。con_rx_ready / con_getc から走査する。
 *   配列は cisc 氏の擬似 BIOS(bios.asm の KeyTable)から読み取った。
 *     0x01 bit7 = RETURN   0x08 bit3 = DEL(BS)  bit6 = SHIFT  bit7 = CTRL
 *     0x09 bit0 = STOP(Ctrl+C 扱い) bit6 = SPACE bit7 = ESC   0x0A bit0 = TAB bit5 = テンキーの /
 *     0x02-0x07 = 文字(8 キー/ポート)
 *   カナ・GRPH・ファンクションキー・テンキーは未対応。
 * ------------------------------------------------------------------ */
static const char kn[] = "@abcdefghijklmnopqrstuvwxyz[\\]^-0123456789:;,./_";   /* \\ を落とすと以降が 1 つずれる */
static const char ks[] = "`ABCDEFGHIJKLMNOPQRSTUVWXYZ{|}~=0!\"#$%&'()*+<>?_";

static unsigned char kprev[11];
static unsigned char kq[32];         /* 受信リング(VRTC 割り込みが積み、con_getc が取る) */
static volatile unsigned char kqh, kqt;

static unsigned char kbin(unsigned char p) __naked
{
    p;
    __asm
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        in      a, (c)
        ld      l, a
        ret
    __endasm;
}

static unsigned char kmap(unsigned char p, unsigned char i)
{
    unsigned char sh, k;

    if (p == 1) return i == 7 ? 13 : 0;
    if (p == 8) return i == 3 ? 8 : 0;
    if (p == 9) return i == 0 ? 3 : i == 6 ? ' ' : i == 7 ? 27 : 0;
    if (p == 10) return i == 0 ? 9 : i == 5 ? 47 : 0;      /* TAB / テンキーの / */
    if (p < 2 || p > 7) return 0;
    k = (unsigned char)((p - 2) * 8 + i);
    sh = kbin(8);
    if (!(sh & 0x80) && kn[k] >= 'a' && kn[k] <= 'z')
        return (unsigned char)(kn[k] & 0x1F);          /* Ctrl+英字 */
    return (unsigned char)((sh & 0x40) ? kn[k] : ks[k]);   /* SHIFT 押下で bit6=0 */
}

static void kscan(void)
{
    unsigned char p, b, d, i, m, k;

    /* 初回は kprev が 0(M88 は RAM 0 起動)なので押下は検出されず、そのまま基準になる */
    for (p = 1; p < 11; p++) {
        b = kbin(p);
        d = (unsigned char)(kprev[p] & ~b);        /* 離 → 押 になったビット */
        kprev[p] = b;
        if (!d) continue;
        for (i = 0, m = 1; i < 8; i++, m = (unsigned char)(m << 1)) {
            if (d & m) {
                k = kmap(p, i);
                if (k && ((kqh + 1) & 31) != kqt) { kq[kqh] = k; kqh = (kqh + 1) & 31; }
            }
        }
    }
}

/* crt0.s の VRTC 割り込み(60Hz)から呼ばれる。コマンド実行中のキーも取りこぼさない。 */
void kb_isr(void)
{
    kscan();
}

int con_rx_ready(void)
{
    return kqh != kqt;
}

unsigned char con_getc(void)
{
    unsigned char c;

    while (kqh == kqt)
        ;
    c = kq[kqt];
    kqt = (kqt + 1) & 31;
    return c;
}
