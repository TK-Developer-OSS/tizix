/* arch/x86-i386-unreal/user/pciio.h -- PCI 設定空間(I/O 0xCF8/0xCFC)と 32bit MMIO(FS 経由、アンリアルモード)
 *   386 命令は gas が .code16 の中で 0x66/0x67 プレフィックスを付けて出す。ia16-gcc 側の .arch は触らないよう
 *   各 asm の前後で i386 ⇄ i8086,jumps を切り替える。
 *   FS はカーネルが起動時に「ベース 0・リミット 4GB」にしてあり、スケジューラも触らない(xmem.s)。
 */
#define ASM386_ON  ".arch i386\n\t"
#define ASM386_OFF "\n\t.arch i8086,jumps"

/* 32bit の入出力を (下位16,上位16) で扱う。ia16-gcc の long は DX:AX なので asm 内で明示的に分ける */
static unsigned long inl_(unsigned port)
{
    unsigned lo, hi;
    __asm__ volatile (ASM386_ON "inl %%dx,%%eax\n\tmov %%eax,%%edx\n\tshr $16,%%edx" ASM386_OFF
                      : "=a"(lo), "=d"(hi) : "1"(port) : "memory");
    return ((unsigned long)hi << 16) | lo;
}
static void outl_(unsigned port, unsigned long v)
{
    unsigned lo = (unsigned)v, hi = (unsigned)(v >> 16);
    __asm__ volatile (ASM386_ON "movzwl %%ax,%%eax\n\tmovzwl %%cx,%%ecx\n\tshl $16,%%ecx\n\tor %%ecx,%%eax\n\toutl %%eax,%%dx" ASM386_OFF
                      : : "a"(lo), "c"(hi), "d"(port) : "memory");
}

static unsigned long pci_cfg_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{
    outl_(0xCF8, 0x80000000UL | ((unsigned long)bus << 16) | ((unsigned long)dev << 11) | ((unsigned long)fn << 8) | (reg & 0xFCu));
    return inl_(0xCFC);
}
static void pci_cfg_write(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned long v)
{
    outl_(0xCF8, 0x80000000UL | ((unsigned long)bus << 16) | ((unsigned long)dev << 11) | ((unsigned long)fn << 8) | (reg & 0xFCu));
    outl_(0xCFC, v);
}

/* 物理アドレス a(32bit)の 32bit を読む/書く(FS = 4GB リミット) */
static unsigned long mmio_read32(unsigned long a)
{
    unsigned lo = (unsigned)a, hi = (unsigned)(a >> 16), rlo, rhi;
    __asm__ volatile (ASM386_ON
                      "movzwl %%bx,%%edi\n\tmovzwl %%cx,%%ecx\n\tshl $16,%%ecx\n\tor %%ecx,%%edi\n\t"
                      "movl %%fs:(%%edi),%%eax\n\tmov %%eax,%%edx\n\tshr $16,%%edx" ASM386_OFF
                      : "=a"(rlo), "=d"(rhi) : "b"(lo), "c"(hi) : "di", "memory", "cc");
    return ((unsigned long)rhi << 16) | rlo;
}
static void mmio_write32(unsigned long a, unsigned long v)
{
    unsigned lo = (unsigned)a, hi = (unsigned)(a >> 16), vlo = (unsigned)v, vhi = (unsigned)(v >> 16);
    __asm__ volatile (ASM386_ON
                      "movzwl %%bx,%%edi\n\tmovzwl %%cx,%%ecx\n\tshl $16,%%ecx\n\tor %%ecx,%%edi\n\t"
                      "movzwl %%ax,%%eax\n\tshl $16,%%edx\n\tor %%edx,%%eax\n\tmovl %%eax,%%fs:(%%edi)" ASM386_OFF
                      : : "b"(lo), "c"(hi), "a"(vlo), "d"(vhi) : "di", "memory", "cc");
}

static void phex(unsigned long v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) { unsigned d = (unsigned)((v >> (4 * i)) & 15UL); putchar((char)(d < 10 ? '0' + d : 'a' + d - 10)); }
}
static unsigned long parse_hex(const char *s)
{
    unsigned long v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}
