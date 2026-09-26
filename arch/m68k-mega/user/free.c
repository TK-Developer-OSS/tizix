/* arch/m68k-mega/user/free.c - m68k-mega 専用 free
 *   共有 user/free.c は z80 の pid 表の固定番地(0x8400)と 4KB ブロック 2..7 を
 *   前提にしているので、date.c と同じく arch 側で丸ごと差し替える
 *   (Makefile の $(TZPORT_DIR)/free.c 個別ルール)。
 *
 *   m68k はスロット 1..N-1 が各 32KB(src/kexec.c の PROC_SIZE)。状態は
 *   syscall 20(slot(n) = pid 表の n 番)で読む。表示の形は z80 版と揃える:
 *
 *     slots 1-30: 30 x 32KB = 960KB
 *     used 2 (64KB)  free 28 (896KB)  largest free run 28 (896KB)
 *     1:# 2:# 3:. …
 *
 *   記号: . = 空き / # = プロセス / p = パイプのバッファ枠。
 */
#include "stdio.h"

#define SLOT_KB      32
#define PID_PIPEBUF  0xFC

static unsigned long slot_pid(unsigned n)
{
        return syscall5(20, (unsigned long)n, 0, 0, 0);
}

static void kb(unsigned n)
{
        prs(" (");
        prnum(n * SLOT_KB);
        prs("KB)");
}

int main(int argc, char **argv)
{
        unsigned n, nslot = 0, used = 0, fre = 0, run = 0, best = 0;
        unsigned long p;

        (void)argc;
        (void)argv;
        for (n = 1; ; n++) {
                p = slot_pid(n);
                if (p == 0xFFFFFFFFUL)
                        break;
                nslot++;
                if (p == 0) {
                        fre++;
                        run++;
                        if (run > best) best = run;
                } else {
                        used++;
                        run = 0;
                }
        }

        prs("slots 1-");
        prnum(nslot);
        prs(": ");
        prnum(nslot);
        prs(" x 32KB = ");
        prnum(nslot * SLOT_KB);
        prs("KB\n");
        prs("used ");
        prnum(used);
        kb(used);
        prs("  free ");
        prnum(fre);
        kb(fre);
        prs("  largest free run ");
        prnum(best);
        kb(best);
        putchar('\n');

        for (n = 1; n <= nslot; n++) {
                p = slot_pid(n);
                prnum(n);
                putchar(':');
                if (p == 0) putchar('.');
                else if (p == PID_PIPEBUF) putchar('p');
                else putchar('#');
                putchar(n % 10 == 0 ? '\n' : ' ');
        }
        if (nslot % 10) putchar('\n');
        return 0;
}
