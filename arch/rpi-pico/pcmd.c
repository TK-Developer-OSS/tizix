/* arch/rpi-pico/pcmd.c - SD も外部コマンド(plat_load が未実装)も無くても使える、最小のファイル操作。
 *   ls / mkdir / rm / mv / cp / touch / df / sync / uptime をカーネル内蔵(builtin)で持つ。
 *   builtin_try から pico_builtin() が先に呼ばれる(src/builtin.c)。echo / cat は builtin.c 側
 *   (/dev/gpio への出入りも持つため)。出力は kprintf なので、`ls > file` などのリダイレクトも効く。 */
#include "../../src/ff.h"
#include "../../src/io.h"
#include "../../src/kernel.h"
#include "../../src/fatcmd.h"
#include "../../src/builtin.h"
#include "../../src/vfs.h"
#include "../../src/diskio.h"
#include <string.h>

extern int disk_is_sd(void);

/* 先頭の語を a に、残りを b に分ける(空白区切り)。a / b は arg の中を指し、arg を書き換える */
static char argbuf[96];
static void two_args(const char *arg, char **a, char **b)
{
    unsigned i = 0;
    char *p;
    while (arg[i] && i < sizeof argbuf - 1) { argbuf[i] = arg[i]; i++; }
    argbuf[i] = 0;
    p = argbuf;
    while (*p == ' ') p++;
    *a = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    while (*p == ' ') p++;
    *b = p;
    for (p = *b; *p && *p != ' '; p++) ;
    *p = 0;
}

static int cmd_ls(const char *arg)
{
    char name[KNAME_MAX];
    int t;
    char *a, *b;

    two_args(arg, &a, &b);
    if (kdir_open(a) != 0) {
        kprintf("ls: %s: cannot open\n", a[0] ? a : ".");
        return BUILTIN_OK;
    }
    while ((t = kdir_read(name)) > 0) {
        if (t == 2)
            kprintf("%s/\n", name);
        else
            kprintf("%s  %u\n", name, (unsigned)kdir_size());
    }
    kdir_close();
    return BUILTIN_OK;
}

static int cmd_mkdir(const char *arg)
{
    char *a, *b;
    two_args(arg, &a, &b);
    if (!a[0]) { kprintf("usage: mkdir DIR\n"); return BUILTIN_OK; }
    if (kfs_mkdir(a) != 0)
        kprintf("mkdir: %s: failed\n", a);
    return BUILTIN_OK;
}

static int cmd_rm(const char *arg)
{
    char *a, *b;
    two_args(arg, &a, &b);
    if (!a[0]) { kprintf("usage: rm FILE\n"); return BUILTIN_OK; }
    if (kfs_unlink(a) != 0)
        kprintf("rm: %s: failed\n", a);
    return BUILTIN_OK;
}

static int cmd_mv(const char *arg)
{
    char *a, *b;
    two_args(arg, &a, &b);
    if (!a[0] || !b[0]) { kprintf("usage: mv SRC DST\n"); return BUILTIN_OK; }
    if (kfs_rename(a, b) != 0)
        kprintf("mv: %s: failed\n", a);
    return BUILTIN_OK;
}

static int cmd_touch(const char *arg)
{
    static FIL fp;
    char *a, *b;
    const char *path;
    two_args(arg, &a, &b);
    if (!a[0]) { kprintf("usage: touch FILE\n"); return BUILTIN_OK; }
    path = kpath(a, 0);
    if (vfs_resolve(path) != VFS_FAT ||
        f_open(&fp, path, FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) {
        kprintf("touch: %s: failed\n", a);
        return BUILTIN_OK;
    }
    f_close(&fp);
    return BUILTIN_OK;
}

static int cmd_cp(const char *arg)
{
    static FIL src, dst;
    static char buf[256];
    char *a, *b;
    char sp[64], dp[64];
    const char *p;
    UINT br, bw;
    unsigned i;
    int ok = 1;

    two_args(arg, &a, &b);
    if (!a[0] || !b[0]) { kprintf("usage: cp SRC DST\n"); return BUILTIN_OK; }
    p = kpath(a, 0); for (i = 0; p[i] && i < sizeof sp - 1; i++) sp[i] = p[i]; sp[i] = 0;
    p = kpath(b, 1); for (i = 0; p[i] && i < sizeof dp - 1; i++) dp[i] = p[i]; dp[i] = 0;
    if (vfs_resolve(sp) != VFS_FAT || vfs_resolve(dp) != VFS_FAT ||
        f_open(&src, sp, FA_READ) != FR_OK) {
        kprintf("cp: %s: cannot open\n", a);
        return BUILTIN_OK;
    }
    if (f_open(&dst, dp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        f_close(&src);
        kprintf("cp: %s: cannot create\n", b);
        return BUILTIN_OK;
    }
    while (f_read(&src, buf, sizeof buf, &br) == FR_OK && br) {
        if (f_write(&dst, buf, br, &bw) != FR_OK || bw != br) { ok = 0; break; }
    }
    f_close(&src);
    f_close(&dst);
    if (!ok)
        kprintf("cp: %s: write failed\n", b);
    return BUILTIN_OK;
}

static int cmd_df(const char *arg)
{
    FATFS *fp;
    DWORD nclst;
    (void)arg;
    if (f_getfree("", &nclst, &fp) != FR_OK) {
        kprintf("df: no volume\n");
        return BUILTIN_OK;
    }
    kprintf("%s: total %uKB free %uKB\n", disk_is_sd() ? "sd" : "flash",
            (unsigned)((fp->n_fatent - 2) * fp->csize / 2), (unsigned)(nclst * fp->csize / 2));
    return BUILTIN_OK;
}

static int cmd_sync(const char *arg)
{
    (void)arg;
    if (disk_ioctl(0, CTRL_SYNC, 0) != RES_OK)
        kprintf("sync: failed\n");
    return BUILTIN_OK;
}

static int cmd_uptime(const char *arg)
{
    unsigned s = uptime_sec();
    (void)arg;
    kprintf("up %u s\n", s);
    return BUILTIN_OK;
}

static const struct { const char *name; int (*fn)(const char *); } tbl[] = {
    { "ls", cmd_ls }, { "mkdir", cmd_mkdir }, { "rm", cmd_rm }, { "rmdir", cmd_rm },
    { "mv", cmd_mv }, { "cp", cmd_cp }, { "touch", cmd_touch }, { "df", cmd_df },
    { "sync", cmd_sync }, { "uptime", cmd_uptime },
};

int pico_builtin_is(const char *cmd)
{
    unsigned i;
    for (i = 0; i < sizeof tbl / sizeof tbl[0]; i++)
        if (strcmp(cmd, tbl[i].name) == 0)
            return 1;
    return 0;
}

int pico_builtin(const char *cmd, const char *arg)
{
    unsigned i;
    for (i = 0; i < sizeof tbl / sizeof tbl[0]; i++)
        if (strcmp(cmd, tbl[i].name) == 0)
            return tbl[i].fn(arg);
    return BUILTIN_NONE;
}
