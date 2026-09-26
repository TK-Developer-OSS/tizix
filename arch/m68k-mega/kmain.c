/* arch/m68k-mega kmain.c
 * S2: 共有 src/ カーネル(init.c → kernel_init → sh())へ結線。
 *   src/init.c の ARCH_M68K_MEGA 分岐(x86-ia16 と同じく sh() をカーネル
 *   常駐で直接呼ぶ)を通る。
 * #47: kexec 実装。スケジューラ slot0(kernel/shell)を起動時から
 *   常時 runnable にする(x86-ia16 の `kwork[0]=1` と同じ役回り)。これが
 *   無いと sched_tick_sp/sched_exit_sp が pid_tbl 全滅で異常系に落ちる。
 */
#include "kmem.h"

extern void init(void);        /* src/init.c */

void kmain(void)
{
    *(volatile unsigned char *)KW_PIDTAB = PID_IDLE;   /* slot0 = kernel/shell */

    init();

    /* init() は本来戻らない(sh の shutdown で戻ってきたら idle)。
     * ★元は `stop #0x2000` で割込み待ちしていたが、m68ksim(rocket68)の
     * STOP 実装が呼出1回あたり数秒単位でホストへ制御を返さない(内部で
     * 割込み到着を長々とポーリングし、CYCLES_PER_SLICE を守らない)ことが
     * 判明した。ホストの stdin_fill()(Ctrl+] 検知)がその間ずっと回って
     * こなくなり、「shutdown 後 Ctrl+] が長時間効かない」実害があった
     * (#47)。ここは shutdown 後の idle に過ぎず低消費電力性は問わないので、
     * 素朴な NOP スピンに置き換える(kgetchar() の待ちループと同じ流儀)。 */
    for (;;) {
        __asm__ volatile ("nop");
    }
}
