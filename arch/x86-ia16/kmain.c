/* tizix x86-ia16 : カーネル入口(crt0 から呼ばれる)
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
static void xmem_selftest(void)
{
	static unsigned char pat[16] = {0x55,0xAA,0x01,0xFE,0x10,0xEF,0x7F,0x80,0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
	unsigned char back[16];
	unsigned i;

	if (!xmem_init())
		return;
	xmem_write(0x0000, 0x0020, pat, 16);   /* 物理 0x00200000(2MB) */
	xmem_read(0x0000, 0x0020, back, 16);
	for (i = 0; i < 16; i++)
		if (back[i] != pat[i])
			return;
	x86_xmem_ok = 1;
}

void kmain(void)
{
	con_init();
	x86_nslot_rt = (unsigned char)x86_nslots();   /* スケジューラが巡回するスロット数 */
	xmem_selftest();
	init();                 /* src/init.c: kernel_init → "tizix" → sh() */

	for (;;)
		__asm__ volatile ("hlt");
}

/* plat.h の IRQ_OFF/IRQ_ON(入れ子対応)の状態 */
unsigned char x86_irq_depth, x86_irq_was;
