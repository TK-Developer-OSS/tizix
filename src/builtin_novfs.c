/* builtin_novfs.c - VFS(飾り)を全廃した builtin.c
 *
 *   使い方:  cp builtin_novfs.c builtin.c   (Makefile は無変更)
 *
 *   削除したもの:
 *     vfs[] 静的テーブル / resolve() / ppath() / cmd_cd() / cmd_pwd()
 *   失う機能:
 *     cd / pwd  ... 実 FAT と一切対応しない偽ツリー上の移動だった。
 *                   ls は fat_ls(実 FAT)なので、cd で移動しても ls の
 *                   結果は変わらないという不整合状態だった。
 *   副作用:
 *     <string.h> の strcmp は builtin_try が使い続けるので残る。
 */
#include <string.h>

#include "io.h"
#include "kernel.h"
#include "builtin.h"
#include "fatcmd.h"

static int cmd_uptime(const char *arg)
{
    (void)arg;
    printf("up %u sec\n", uptime_sec());
    return BUILTIN_OK;
}

static int cmd_exit(const char *arg)
{
    (void)arg;
    printf("shutdown\n");
    return BUILTIN_EXIT;          /* sh() が return し、init() が停止/将来 respawn */
}

/* ================================================================== */
/* ディスパッチテーブル                                                */
/* ================================================================== */

typedef int (*builtin_fn)(const char *arg);

struct builtin {
    const char *name;
    builtin_fn  fn;
};

/* 注: 関数ポインタ間接呼び出しは SDCC が jp (iy) にコンパイルする。
 *     builtin は block0(シェル文脈)専用なので現状は無害だが、コマンド
 *     文脈から呼ぶ設計に変えた瞬間に iy=base 破壊が再発する。 */
static const struct builtin builtins[] = {
    { "ls",     fat_ls     },
    { "cat",    fat_cat    },
    { "cp",     fat_cp     },
    { "rm",     fat_rm     },
    { "mv",     fat_mv     },
    { "echo",   fat_echo   },
    { "uptime", cmd_uptime },
    { "exit",   cmd_exit   },
};
#define BUILTIN_N (sizeof(builtins) / sizeof(builtins[0]))

void builtin_init(void)
{
    fat_init();          /* mount driveb (FAT) */
}

int builtin_try(const char *cmd, const char *arg)
{
    unsigned char i;

    for (i = 0; i < BUILTIN_N; i++)
        if (strcmp(cmd, builtins[i].name) == 0)
            return builtins[i].fn(arg);

    return BUILTIN_NONE;
}
