/* arch/x86-i386-unreal/user/lspci.c -- PCI デバイスの一覧(ベンダー:デバイス、クラス、BAR)
 *   lspci          バス 0〜7 を走査
 *   lspci -v       BAR(メモリ/IO、アドレス)と割り込み線も表示
 *   実機の NIC の BAR 位置確認用(MMIO が 4GB 手前だとリアルモードの 16bit では届かない → アンリアルモード)。 */
#include "tzstdio.h"
#include "pciio.h"

int main(int argc, char **argv)
{
    unsigned bus, dev, fn, n = 0, verbose = argc > 0 && argv[0][0] == '-' && argv[0][1] == 'v';   /* tizix の argv[0] は最初の引数 */
    for (bus = 0; bus < 8; bus++)
        for (dev = 0; dev < 32; dev++)
            for (fn = 0; fn < 8; fn++) {
                unsigned long id = pci_cfg_read(bus, dev, fn, 0);
                unsigned long cls, hdr;
                if ((id & 0xFFFFUL) == 0xFFFFUL) { if (fn == 0) break; continue; }
                cls = pci_cfg_read(bus, dev, fn, 8);
                hdr = pci_cfg_read(bus, dev, fn, 0x0C);
                phex(bus, 2); putchar(':'); phex(dev, 2); putchar('.'); phex(fn, 1); putchar(' ');
                phex(id & 0xFFFFUL, 4); putchar(':'); phex(id >> 16, 4);
                prs("  class "); phex(cls >> 8, 6); prs(" rev "); phex(cls & 0xFFUL, 2);
                putchar('\n'); n++;
                if (verbose) {
                    unsigned b, nb = ((hdr >> 16) & 0x7FUL) == 0 ? 6 : 2;
                    for (b = 0; b < nb; b++) {
                        unsigned long bar = pci_cfg_read(bus, dev, fn, 0x10 + 4 * b);
                        if (bar == 0 || bar == 0xFFFFFFFFUL) continue;
                        prs("     BAR"); prnum(b); prs(bar & 1 ? " io  " : " mem ");
                        phex(bar & (bar & 1 ? 0xFFFFFFFCUL : 0xFFFFFFF0UL), 8);
                        if (!(bar & 1) && ((bar >> 1) & 3) == 2) { prs(" (64bit)"); b++; }
                        putchar('\n');
                    }
                    if (((hdr >> 16) & 0x7FUL) == 0) { unsigned long il = pci_cfg_read(bus, dev, fn, 0x3C); prs("     irq "); prnum((unsigned)(il & 0xFF)); putchar('\n'); }
                }
                if (fn == 0 && !(hdr & 0x00800000UL)) break;    /* 単機能デバイス */
            }
    prnum(n); puts(" devices");
    return 0;
}
