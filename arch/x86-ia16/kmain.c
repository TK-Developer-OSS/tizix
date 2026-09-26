/* tizix x86-ia16 : カーネル入口(crt0 から呼ばれる)
 *   M2: 共通 src/ の init() → sh() へ。PIT/スケジューラは M3。
 *   QEMU 脱出: 端末 Ctrl-C(SIGINT で QEMU 終了)。
 */
void con_init(void);
void init(void);

void kmain(void)
{
	con_init();
	init();                 /* src/init.c: kernel_init → "tizix" → sh() */

	for (;;)
		__asm__ volatile ("hlt");
}
