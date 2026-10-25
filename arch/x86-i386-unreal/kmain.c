/* tizix x86-i386-unreal : カーネル入口(crt0 から呼ばれる)
 *   M2: 共通 src/ の init() → sh() へ。PIT/スケジューラは M3。
 *   QEMU 脱出: 端末 Ctrl-C(SIGINT で QEMU 終了)。
 */
void con_init(void);
void init(void);
extern unsigned xmem_init(void);
extern unsigned x86_nslots(void);
extern unsigned char x86_nslot_rt;     /* crt0.s */
extern void xmem_write(unsigned lo, unsigned hi, const void *src, unsigned n);
extern void xmem_read(unsigned lo, unsigned hi, void *dst, unsigned n);
/* アンリアルモード(#186): 0=未対応/失敗、1=拡張メモリを読み書きできた(2MB 付近でパターン往復一致) */
unsigned char x86_xmem_ok;
static void xmem_selftest(unsigned ext_kb_hi, unsigned ext_kb_lo)
{
	static unsigned char pat[16] = {0x55,0xAA,0x01,0xFE,0x10,0xEF,0x7F,0x80,0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
	unsigned char back[16];
	unsigned i, hi;

	if (!xmem_init())
		return;
	/* 往復試験の場所: 拡張メモリが 3MB 以上あれば 2MB 地点、128KB〜3MB なら 1MB+64KB 地点、それ未満は試さない */
	if (ext_kb_hi || ext_kb_lo >= 3072)
		hi = 0x0020;
	else if (ext_kb_lo >= 128)
		hi = 0x0011;
	else
		return;
	xmem_write(0x0000, hi, pat, 16);
	xmem_read(0x0000, hi, back, 16);
	for (i = 0; i < 16; i++)
		if (back[i] != pat[i])
			return;
	x86_xmem_ok = 1;
}

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
extern unsigned x86_cpu_type(void);
extern unsigned x86_pool_blocks(void);

static void put_ul(unsigned long v)
{
	char b[11];
	int i = 10;
	b[i] = 0;
	do { b[--i] = (char)('0' + (unsigned)(v % 10UL)); v /= 10UL; } while (v);
	con_puts(b + i);
}

/* カーネル本体(slot 0 = シェル/init)のスタックの使用量。bss の終わり(__bss_end)から 0xFFFE までがスタック領域で、
 * 起動時に今の SP の手前までをパターンで埋め、底から連続して残るパターンの長さ = 余裕(free が表示)。
 * カーネルの 64KB セグメントはほぼ満杯なので、機能を足したあとの余裕を見張る。 */
extern unsigned char __bss_end[];
static unsigned char *kstk_lo;
static void kstk_fill(void)
{
	unsigned char *sp, *p;
	__asm__ volatile ("mov %%sp, %0" : "=r"(sp));
	kstk_lo = __bss_end;
	for (p = kstk_lo; p + 96 < sp; p++)
		*p = 0xA5;
}
unsigned x86_kstack_headroom(void)
{
	unsigned i = 0;
	const unsigned char *p = kstk_lo;
	while (p[i] == 0xA5 && (unsigned)(p + i) < 0xFF00u)
		i++;
	return i;
}

/* 起動の順序(#186): ①CPU 判定とアンリアルモードの可否 → ②メモリ量(従来 + 拡張)→ ③使えるメモリからスロット数を決める。 */
/* 最低条件(TK 2026-10-24): 386 以降で、メモリが 4MB 以上。QEMU の試験は 128MB。満たさなければ理由を出して止める。
 *   4MB の機械でも、BIOS が報告する拡張メモリ(1MB より上)は 3072KB ちょうどとは限らない(QEMU -m 4 の SeaBIOS は上端の
 *   128KB を自分用に取って 2944KB。実機もシャドウ RAM などで減る)。3MB の機械は多くても 2048KB なので、その間の 2560KB を
 *   下限にして「3MB 以下は止める・4MB は通す」にする。
 *   アンリアルモードの往復試験の失敗は、実機での調べものに起動を続けたいので表示だけにする。 */
#define MIN_EXT_KB 2560UL
static void halt_with(const char *why)
{
	con_puts("tizix x86-i386-unreal needs ");
	con_puts(why);
	con_puts(": halted\n");
	for (;;)
		__asm__ volatile ("cli; hlt");
}

void kmain(void)
{
	unsigned cpu, ext_lo, ext_hi, a88;
	unsigned long ext_kb;

	con_init();
	scstk_fill();
	kstk_fill();

	/* ② のうち拡張メモリの量は ① の往復試験の場所決めにも要るので、先に stage2 の値を読む */
	ext_lo = x86_lowword(0x516);            /* E801: 1〜16MB の KB */
	ext_hi = x86_lowword(0x518);            /* E801: 16MB 超の 64KB 単位 */
	a88 = x86_lowword(0x514);               /* INT 15h/88h(64MB 弱で頭打ち) */
	if (ext_lo == 0 && ext_hi == 0)
		ext_lo = a88;
	ext_kb = (unsigned long)ext_lo + ((unsigned long)ext_hi << 6);

	/* ① CPU とアンリアルモード */
	cpu = x86_cpu_type();
	xmem_selftest(ext_hi, ext_lo);
	con_puts("cpu: ");
	con_puts(cpu >= 3 ? "386+" : cpu == 2 ? "286" : "8086/186");
	con_puts(", unreal mode: ");
	con_puts(x86_xmem_ok ? "ok" : cpu < 3 ? "n/a" : ext_kb < 128 ? "no extended memory" : "failed");
	con_puts("\n");

	/* ② メモリ */
	con_puts("memory: conventional ");
	put_ul(x86_lowword(0x512));
	con_puts("KB, extended ");
	put_ul(ext_kb);
	con_puts("KB\n");
	if (cpu < 3)
		halt_with("a 386 or later CPU");
	if (ext_kb < MIN_EXT_KB)
		halt_with("4MB of memory or more");

	/* ③ スロット数(使えるメモリ分) */
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
