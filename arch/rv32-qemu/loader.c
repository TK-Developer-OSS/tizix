/* arch/rv32-qemu loader.c - loading external commands (plat_load / plat_ctx of src/loader.h)
 *
 *   layout of a .bin (little endian; must match tools/mkcmd.py):
 *     +0  u32 magic 'TZR1'   +4 u32 image size in bytes (a multiple of 4)
 *     +8  u32 number of relocations   +12 u32 base address at link time
 *     followed by the image (the first 32 bytes are the process header (src/phdr.h); crt0cmd's _start is at +0x20),
 *     the relocation table (u16 x count = the index of a word inside the image).
 *   The code is PC-relative thanks to -mcmodel=medany -mno-relax, so what needs fixing is only "the words that
 *   hold an address itself" (pointer tables and so on). Add (load address - link-time base) to the words in the table.
 *   Instructions are not rewritten.
 *
 *   the shape of a slot (addresses are in include/plat.h):
 *     base                  the image
 *     base + IMG_BUDGET     argv[] and the argument strings (written by src/kexec.c)
 *     ...                   the stack (from the top downwards)
 *     base + PLAT_SLOT_SIZE the top. The fake context sits just below it
 */
#include "loader.h"
#include "kmem.h"
#include "phdr.h"

#define IMG_BUDGET   0x8000UL		/* limit for image + bss. Must match the ASSERT in user/cmd.ld */
#define CTX_SIZE     144UL		/* the context of crt0.S (x1..x31, mepc, mstatus, padding) */
#define CMD_MAGIC    0x31525A54UL	/* 'T','Z','R','1' */

typedef char argv_area_fits[(IMG_BUDGET + KEXEC_ARGV_BYTES + CTX_SIZE + 0x1000UL
                             <= PLAT_SLOT_SIZE) ? 1 : -1];

unsigned char plat_need(FIL *fp)
{
	(void)fp;
	return 1;
}

unsigned char plat_load(FIL *fp, unsigned char n, unsigned char k, struct kimage *im)
{
	unsigned long base = PLAT_SLOT_ADDR(n);
	unsigned long hdr[4], delta, i, left;
	unsigned short tab[128];
	UINT br;

	(void)k;
	if (f_read(fp, hdr, sizeof hdr, &br) != FR_OK || br != sizeof hdr || hdr[0] != CMD_MAGIC)
		return 0xFF;
	if (hdr[1] > IMG_BUDGET || (hdr[1] & 3UL))
		return 0;
	if (f_read(fp, (void *)base, (UINT)hdr[1], &br) != FR_OK || br != hdr[1])
		return 0xFF;

	delta = base - hdr[3];
	for (left = hdr[2]; left; ) {
		unsigned long c = left > 128 ? 128 : left;
		if (f_read(fp, tab, (UINT)(c * 2), &br) != FR_OK || br != c * 2)
			return 0xFF;
		for (i = 0; i < c; i++)
			*(unsigned long *)(base + (unsigned long)tab[i] * 4) += delta;
		left -= c;
	}
	__asm__ volatile (".word 0x0000100f" ::: "memory");	/* fence.i */

	im->entry = base + PH_SIZE;
	im->argv  = base + IMG_BUDGET;
	im->top   = base + PLAT_SLOT_SIZE;
	return 1;
}

/* must match the layout that trap_entry of crt0.S pushes (x(i) is at (i-1)*4, mepc at +124, mstatus at +128).
 * mstatus = MPP (M mode) | MPIE (interrupts come back on at mret) = 0x1880. */
unsigned long plat_ctx(const struct kimage *im, unsigned long argc, unsigned long argv)
{
	unsigned long sp = im->top - CTX_SIZE;
	unsigned long *ctx = (unsigned long *)sp;
	unsigned k;

	for (k = 0; k < CTX_SIZE / 4; k++)
		ctx[k] = 0;
	ctx[1]  = im->top;		/* x2 = sp */
	ctx[9]  = argc;			/* x10 = a0 */
	ctx[10] = argv;			/* x11 = a1 */
	ctx[31] = im->entry;		/* mepc */
	ctx[32] = 0x1880UL;		/* mstatus */
	return sp;
}
