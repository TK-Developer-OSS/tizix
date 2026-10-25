/* arch/x86-i386-unreal/user/mmio.c -- 物理アドレスの 32bit を読む/書く(アンリアルモードの FS 経由)
 *   mmio ADDR [N]        ADDR(16進)から N 個(既定 1)の dword を表示
 *   mmio -w ADDR VALUE   ADDR へ書く
 *   デバイスのレジスタ確認用。誤った書き込みでハングし得る(保護なし)。 */
#include "tzstdio.h"
#include "pciio.h"

int main(int argc, char **argv)
{
    if (argc >= 3 && argv[0][0] == '-' && argv[0][1] == 'w') {
        unsigned long a = parse_hex(argv[1]);
        mmio_write32(a, parse_hex(argv[2]));
        phex(a, 8); prs(" <- "); phex(parse_hex(argv[2]), 8); putchar('\n');
        return 0;
    }
    if (argc >= 1) {
        unsigned long a = parse_hex(argv[0]);
        unsigned n = argc > 1 ? (unsigned)parse_hex(argv[1]) : 1, i;
        for (i = 0; i < n; i++, a += 4) {
            if ((i & 3) == 0) { if (i) putchar('\n'); phex(a, 8); putchar(':'); }
            putchar(' '); phex(mmio_read32(a), 8);
        }
        putchar('\n');
        return 0;
    }
    puts("usage: mmio ADDR [N] | mmio -w ADDR VALUE   (ADDR は 16 進)");
    return 1;
}
