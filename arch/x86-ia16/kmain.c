/* tizix x86-ia16 : カーネル入口(crt0 から呼ばれる)
 *   M2: 共通 src/ の init() → sh() へ。PIT/スケジューラは M3。
 *   QEMU 脱出: 端末 Ctrl-C(SIGINT で QEMU 終了)。
 */
void con_init(void);
void init(void);
extern unsigned x86_nslots(void);
extern unsigned char x86_nslot_rt;     /* crt0.s */

/* スロットごとの syscall スタック(crt0.s の sc_stack)の使用量測定。起動時にパターンで埋めておき、
 * 底から連続して残っているパターンの長さ = そのスロットの余裕。全スロットの最小値を返す(free が表示)。 */
extern unsigned char sc_stack[];
#define SCSTK_SZ ((unsigned)1 << X86_SCSTK_SHIFT)
static void scstk_fill(void)
{
	unsigned i, n = (unsigned)X86_MAXSLOT << X86_SCSTK_SHIFT;
	for (i = 0; i < n; i++)
		sc_stack[i] = 0xA5;
}
unsigned x86_scstk_headroom(void)
{
	unsigned k, i, best = SCSTK_SZ;
	for (k = 0; k < X86_MAXSLOT; k++) {
		const unsigned char *b = sc_stack + k * SCSTK_SZ;
		for (i = 0; i < SCSTK_SZ && b[i] == 0xA5; i++)
			;
		if (i < best)
			best = i;
	}
	return best;
}

extern void con_puts(const char *s);
extern unsigned x86_lowword(unsigned off);
extern unsigned x86_pool_blocks(void);

static void put_ul(unsigned long v)
{
	char b[11];
	int i = 10;
	b[i] = 0;
	do { b[--i] = (char)('0' + (unsigned)(v % 10UL)); v /= 10UL; } while (v);
	con_puts(b + i);
}

/* 起動の順序(#186): メモリ量(従来 + 拡張、BIOS が報告する値)→ 使えるメモリからスロット数を決める。
 * 拡張メモリ(1MB 超)は 8086 のリアルモードからは使えないので、ここでは量を表示するだけ。 */
void kmain(void)
{
	unsigned ext_lo, ext_hi;
	unsigned long ext_kb;

	con_init();
	scstk_fill();

	ext_lo = x86_lowword(0x516);            /* E801: 1〜16MB の KB */
	ext_hi = x86_lowword(0x518);            /* E801: 16MB 超の 64KB 単位 */
	if (ext_lo == 0 && ext_hi == 0)
		ext_lo = x86_lowword(0x514);        /* INT 15h/88h(64MB 弱で頭打ち) */
	ext_kb = (unsigned long)ext_lo + ((unsigned long)ext_hi << 6);
	con_puts("memory: conventional ");
	put_ul(x86_lowword(0x512));
	con_puts("KB, extended ");
	put_ul(ext_kb);
	con_puts("KB (unused)\n");

	x86_nslot_rt = (unsigned char)x86_nslots();   /* スケジューラが巡回するスロット数 */
	con_puts("blocks ");
	put_ul(x86_pool_blocks());
	con_puts(" x 4KB, slots ");
	put_ul(x86_nslot_rt);
	con_puts("\n");
	init();                 /* src/init.c: kernel_init → "tizix" → sh() */

	for (;;)
		__asm__ volatile ("hlt");
}

/* plat.h の IRQ_OFF/IRQ_ON(入れ子対応)の状態 */
unsigned char x86_irq_depth, x86_irq_was;
