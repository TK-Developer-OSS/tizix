/* tizix x86-ia16 : init (PID 1) -- tzcc version of ../../src/init.c, X86_EXTSH branch only.
 *   mount FAT, print the banner, start /bin/sh.bin and respawn it when it exits.
 * kexec_file() returns an unsigned char in AL (gcc leaves AH undefined), so n is declared char. */
#include "fsx.h"
void kernel_init(void);
void builtin_init(void);
void klog_write(char *msg);
void kprintf(char *fmt, ...);
char kexec_file(char *fname, char *arg);
char *tz_kwork(void);
void tz_kyield(void);
void tz_halt(void);

void init(void)
{
    char *pid;
    char n;

    kernel_init();
    pid = tz_kwork();                   /* pid_tbl[] is at the start of kwork */
    builtin_init();                     /* FAT mount + vfs_init */
    kprintf("tizix\n");
    klog_write("tizix boot");
    for (;;) {
        n = kexec_file("/bin/sh.bin", "");
        if (n == 0 || n == 0xFF) {
            kprintf("init: cannot start /bin/sh.bin (%u)\n", n);
            break;
        }
        while (pid[n] != 0)             /* wait for the shell to exit, then respawn it */
            tz_kyield();
    }
    for (;;)
        tz_halt();
}
