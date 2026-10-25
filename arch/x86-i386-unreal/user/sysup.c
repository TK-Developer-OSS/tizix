/* arch/x86-i386-unreal/user/sysup.c -- 起動ディスクの予約領域(ブートセクタ + stage2 + カーネル)を書き換える。
 *   sysup FILE   FILE = 新しいディスク像の先頭(予約領域ぶん、512 バイト単位)。xmodem(rx)で送り込んでから使う。
 *   書く前に、FILE の先頭セクタの BPB(ディスクの形式)が今のディスクと同じかを確かめ、違えば何も書かない
 *   (形式の違う像の予約領域を書くと FAT が読めなくなる)。各セクタは書いたあと読み戻して照合する。
 *   予約領域の外(FAT 以降)はカーネル(syscall 61)が拒否する。新しいカーネルは次の reboot から使われる。 */
#include "tzstdio.h"

static unsigned char cur[512], buf[512], back[512];

static int bootrd(unsigned lba, void *b) { return _syscall5_asm(63, 0, lba, 0, 0, (unsigned)b) == 0xFFFFu ? -1 : 0; }
static int bootwr(unsigned lba, const void *b) { return _syscall5_asm(61, 0, lba, 0, 0, (unsigned)b) == 0xFFFFu ? -1 : 0; }

static int same(const unsigned char *a, const unsigned char *b, unsigned from, unsigned to)
{
    unsigned i;
    for (i = from; i < to; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned lba = 0, i;
    if (argc < 1) { puts("usage: sysup FILE   (the first sectors of a new disk image)"); return 1; }
    f = fopen(argv[0], "r");
    if (!f) { printf("sysup: cannot open %s\n", argv[0]); return 1; }
    if (fread(buf, 1, 512, f) != 512 || bootrd(0, cur) != 0) { puts("sysup: cannot read sector 0"); fclose(f); return 1; }
    /* BPB(11..61)と署名 55 AA が今のディスクと同じこと */
    if (!same(buf, cur, 11, 62) || buf[510] != 0x55 || buf[511] != 0xAA) {
        puts("sysup: disk format (BPB) differs from this disk: nothing written");
        fclose(f);
        return 1;
    }
    for (;;) {
        if (bootwr(lba, buf) != 0) { printf("sysup: write error at sector %u (stopped here)\n", lba); fclose(f); return 1; }
        if (bootrd(lba, back) != 0 || !same(buf, back, 0, 512)) { printf("sysup: verify error at sector %u\n", lba); fclose(f); return 1; }
        lba++;
        if ((lba & 7) == 0) putchar('.');
        for (i = 0; i < 512; i++) buf[i] = 0;
        if (fread(buf, 1, 512, f) != 512) break;
    }
    fclose(f);
    printf("\nsysup: %u sectors written and verified. reboot to use them.\n", lba);
    return 0;
}
