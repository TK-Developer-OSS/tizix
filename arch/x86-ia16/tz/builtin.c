/* tizix x86-ia16 : shell builtins and FS bring-up -- tzcc version of ../../src/builtin.c (x86 branch).
 * (cd / pwd are handled by the external shell; only ps / kill / exit and builtin_init live here.) */
#include "kw.h"
#include "fsx.h"
int fat_init(void);
void vfs_init(void);
int strcmp(char *a, char *b);
void kprintf(char *fmt, ...);
char *tz_kwork(void);
char kload_driver(void);
void tz_halt(void);

int cmd_exit(char *arg)
{
    kprintf("bye\n");
    return 2;
}

int cmd_ps(char *arg)
{
    char *kw;
    char *state;
    char *tag;
    char n;
    char cur;

    kw = tz_kwork();
    cur = kw[KW_CUR_O];
    kprintf("BLK ST\n");
    for (n = 0; n < PROC_BLOCK_MAX + 1; n++) {
        if (kw[n] == 0) continue;
        state = "rdy";
        if (n == cur) state = "run";
        tag = "";
        if (n == 0) tag = " (sh)";
        kprintf("%u %s%s\n", n, state, tag);
    }
    return 1;
}

int cmd_kill(char *arg)
{
    char *kw;
    unsigned n;

    kw = tz_kwork();
    n = 0;
    while (*arg == ' ')
        arg = arg + 1;
    if (*arg < '0' || *arg > '9') {
        kprintf("usage: kill <1..7>\n");
        return 1;
    }
    while (*arg >= '0' && *arg <= '9') {
        n = (n * 10 + (*arg - '0')) & 0xFF;
        arg = arg + 1;
    }
    if (n < 1 || n > 7) {
        kprintf("bad block %u\n", n);
        return 1;
    }
    if (kw[n] == 0) {
        kprintf("%u not running\n", n);
        return 1;
    }
    kw[n] = 0;
    kprintf("killed %u\n", n);
    return 1;
}

void builtin_init(void)
{
    if (fat_init() == 0)
        kprintf("FAT Drive DETECTED\n");
    else
        kprintf("FAT Drive FAILED (continuing)\n");
    vfs_init();
    /* the resident DRIVER, after the mount and before the prompt (as on the z80: no fallback, a missing driver is
     * a broken setup) */
    if (kload_driver() == 0) {
        kprintf("Driver LOADED\n");
    } else {
        kprintf("Driver FAILED\n");
        for (;;)
            tz_halt();
    }
}

int builtin_try(char *cmd, char *arg)
{
    if (strcmp(cmd, "ps") == 0) return cmd_ps(arg);
    if (strcmp(cmd, "kill") == 0) return cmd_kill(arg);
    if (strcmp(cmd, "exit") == 0) return cmd_exit(arg);
    return 0;
}

int builtin_is(char *cmd)
{
    if (strcmp(cmd, "cd") == 0) return 1;
    if (strcmp(cmd, "pwd") == 0) return 1;
    if (strcmp(cmd, "ps") == 0) return 1;
    if (strcmp(cmd, "kill") == 0) return 1;
    if (strcmp(cmd, "exit") == 0) return 1;
    return 0;
}
