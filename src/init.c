#include "io.h"
#include "kernel.h"

#include "builtin.h"
#include "kmem.h"
#include "fatcmd.h"     /* klog_write: 起動文言を /var/log/message へも記録 */

#if defined(ARCH_X86_IA16)
#include "sh.h"
#else
#include "kexec.h"
#endif

/* arch が差し込める場所(既定は何もしない / 自分から譲るだけ)。
 *   PLAT_LATE_INIT: FAT をマウントした後、sh を起こす前(esp32: /etc/wifi を読んで WiFi を起こす)
 *   PLAT_IDLE:      init が sh の終了を待つ間の 1 回分(esp32: WiFi のスレッドに順番を回してから譲る) */
#ifndef PLAT_LATE_INIT
#define PLAT_LATE_INIT()
#endif
#ifndef PLAT_IDLE
#define PLAT_IDLE()  KYIELD()
#endif

void init(void)
{
    kernel_init();

#if defined(ARCH_X86_IA16)
    /* x86: sh はカーネル常駐(共有 src/sh.c 直呼び)。z80 / m68k-mega と違い
     * /bin/sh.bin へは外に出ていないが、**起動の骨格は z80 分岐と同じ順序に
     * 揃えてある** ── builtin_init()(FAT mount + vfs_init)→ バナー →
     * klog_write → シェル。klog_write は「シェルが起動できない障害でも
     * ログが残る」ことに意味があるので、シェル側ではなく必ずここで呼ぶ。 */
    builtin_init();                 /* FAT mount + vfs_init(旧 sh() 冒頭) */
    kprintf("tizix\n");
    klog_write("tizix boot");       /* /var/log/message にも記録 */
    sh();
    for (;;)
        ;
#else
    /* z80 / m68k-mega: sh は外部コマンド(/bin/sh.bin)。init が PID 1 として起動し、
     *   終了したら respawn する(本物の init/getty の最小形)。
     *   FAT マウント / VFS 初期化 / DRIVER 常駐ロードは旧 sh() 冒頭の
     *   builtin_init() がやっていた ── sh が外に出たのでここで行う。 */
    {
        volatile unsigned char *pid = (volatile unsigned char *)KW_PIDTAB;
        unsigned char n;

        builtin_init();                 /* FAT mount + vfs_init + kload_driver */
        kprintf("tizix\n");
        klog_write("tizix boot");       /* /var/log/message にも記録(/etc, /var/log
                                          * が無い旧ディスクでは klog_write が黙って何もしない) */
        PLAT_LATE_INIT();

        /* z80board 実機プローブ [i1]〜[i3](2026-09-19 のブリングアップで使用)。
         * 再度使う時は 1 に(src/kexec.c の KEXEC_PROBE と併用)。 */
#define INIT_PROBE 0
        for (;;) {
#if INIT_PROBE
            kprintf("[i1]");             /* kexec_file 呼び出し直前 */
#endif
            n = kexec_file("/bin/sh.bin", "");
#if INIT_PROBE
            kprintf("[i2 n=%u]", (unsigned)n);   /* 戻り値 = 先頭ブロック番号 */
#endif
            if (n == 0 || n == 0xFF) {
                kprintf("init: cannot start /bin/sh.bin (%u)\n", n);
                break;
            }
#if INIT_PROBE
            kprintf("[i3 pid=%u]\n", (unsigned)pid[n]);  /* sh 起動待ちへ入る */
#endif
            while (pid[n] != 0)          /* sh の終了を待って respawn */
                PLAT_IDLE();             /* 既定は KYIELD(z80board: タイマ未結線なので自分から譲る #54) */
        }
    }
    for (;;)
        ;
#endif
}
