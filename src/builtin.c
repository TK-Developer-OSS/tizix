/* builtin.c - VFS(飾り)を全廃した builtin.c + VFS 名前空間(器)の復活
 *
 *   このセッションの追加:
 *     - builtin_init() で vfs_init() を呼ぶ(VFS ツリーを 0x8419 に初期化)。
 *     - 検証用 "tree" コマンドを追加(vfs_dump)。実機で VFS 配置を目視
 *       確認するための一時コマンド。確認が済んだら本 builtin ごと外して可。
 *
 *   前セッションからの継続メモ:
 *     削除したもの(旧 VFS の飾り):
 *       vfs[] 静的テーブル / resolve() / ppath() / cmd_cd() / cmd_pwd()
 *     失う機能: cd / pwd(実 FAT と無関係の偽ツリー移動だった)
 *     <string.h> の strcmp は builtin_try が使い続けるので残る。
 *
 *   ※ 今回復活させた VFS(vfs.c)は上記の飾りとは別物。endpoint 名前空間の
 *     器であり、ls/cat 等の既存コマンドの経路は一切変えていない(既存は
 *     従来どおり FAT を直に見る)。VFS は socket 段(パイプ)で初めて使う。
 */
#include <string.h>

#include "io.h"
#include "kernel.h"
#include "kmem.h"
#include "builtin.h"
#include "fatcmd.h"
#include "vfs.h"
#include "kexec.h"      /* kload_driver: DRIVER.BIN を block1 へ常駐ロード */

/* 方針: ビルトインは OS の心臓部(ブート必須 + プロセス操作 = exit/ps/kill、
 * sh 直処理の cd/pwd)だけ。それ以外は外部コマンドにする(task.md #56)。
 * uptime / tree / df は外部コマンド化までビルトインを無効化(コードは残す)。
 * z80board の ROM 32KB 上限対策でもある。 */
#define BUILTIN_UPTIME_TREE 0
#define BUILTIN_DF          0          /* 実体 fat_df は fatcmd.c の FAT_DF */

#if BUILTIN_UPTIME_TREE
static int cmd_uptime(const char *arg)
{
    (void)arg;
    kprintf("up %u sec\n", uptime_sec());
    return BUILTIN_OK;
}
#endif

static int cmd_exit(const char *arg)
{
    (void)arg;
    kprintf("bye\n");
    return BUILTIN_EXIT;          /* sh() が return し、init() が停止/将来 respawn */
}

#if BUILTIN_UPTIME_TREE
/* 検証用: VFS ツリーをダンプ。確認専用。socket 段が入ったら不要。 */
static int cmd_tree(const char *arg)
{
    (void)arg;
    vfs_dump();
    return BUILTIN_OK;
}
#endif

/* z80board の SD_DEBUG ビルドは ROM 容量を作るため ps/kill/df も落とす
 * (2026-09-19 実機ブリングアップ。溢れの根本対策は task.md #56)。 */
#ifndef SD_DEBUG

/* ---- プロセス表(PCB)アクセス ---- */
#define PIDTBL   ((volatile unsigned char *)KW_PIDTAB)   /* pid_tbl[block] 0=free */
#define KCURRENT (*(volatile unsigned char *)KW_CURRENT) /* 現走行ブロック         */

/* ps : 走行中プロセス一覧。PCB(pid_tbl/current)を舐めるだけ。
 *   block0 は idle/シェル。block1 は DRIVER(飛び地)。block2..7 が外部プロセス。
 *   pid_tbl[n]=n(kexec.c)なので PID 列はブロック番号に一致(block0 のみ 1)。
 *   状態: run=現走行 / rdy=runnable 待ち。
 *   CMD/ARGS 列は kmem.h KW_CMDNAME/KW_CMDARGS(中央表)を直に覗く。 */
/* #61: スロット数はアーキで違う。m68k-mega は src/kmem.h の M68K_NSLOT が
 * 唯一の定義場所(現在 62 = slot0 + 外部 61)。ここを 8 固定にしていたため、
 * スロット 8 以降で走っているプロセスが ps に出てこなかった。 */
/* #78: 表示は z80 と m68k-mega で 1 本。違うのは走査範囲だけで、
 *   z80 は block1 が DRIVER(名前枠なし)なので 2 から、m68k は 1 から。
 *   名前表は両方とも kexec.c の ps_note が書く。x86-ia16 は表を持たないので
 *   旧来の BLK ST 表示のまま(#77 で当面リリース外)。 */
#if defined(ARCH_M68K_MEGA)
#define PS_FIRST   1
#define PS_NSLOT   M68K_NSLOT
#else
#define PS_FIRST   2
#define PS_NSLOT   8
#endif
static int cmd_ps(const char *arg)
{
    unsigned char n;
    (void)arg;
#if !defined(ARCH_X86_IA16)
    kprintf("BLK ST CMD ARGS\n");
    /* slot 0 はカーネル + init(z80 / m68k とも sh は外部プロセスで、自分の行に出る)。 */
    kprintf("0 %s (init)\n", (KCURRENT == 0) ? "run" : "rdy");
    for (n = PS_FIRST; n < PS_NSLOT; n++) {
        /* PID_CONT は z80 の継続ブロック(m68k では立たない)。 */
        if (PIDTBL[n] == 0 || PIDTBL[n] == PID_CONT) continue;  /* free/継続block */
        kprintf("%u %s %s %s\n", (unsigned int)n,
               (n == KCURRENT) ? "run" : "rdy",
               (const char *)(KW_CMDNAME + (unsigned)n * KW_CMDNAME_LEN),
               (const char *)(KW_CMDARGS + (unsigned)n * KW_CMDARGS_LEN));
    }
#else
    kprintf("BLK ST\n");
    for (n = 0; n < PS_NSLOT; n++) {
        if (PIDTBL[n] == 0) continue;              /* free */
        kprintf("%u %s%s\n", (unsigned int)n,
               (n == KCURRENT) ? "run" : "rdy",
               (n == 0) ? " (sh)" : "");
    }
#endif
    return BUILTIN_OK;
}

/* kill <block> : 指定ブロックのプロセスを解放。pid_tbl[block]=0 にするだけ。
 *   次の tick でスケジューラが pick しなくなり消える。文脈/メモリは破棄。
 *   外部プロセスは FAT/fd を持たない(当面)ので資源後始末は不要。
 *   z80: block0(sh)/block1(DRIVER)は対象外 → block2..7。
 *   x86: slot0=kernel/idle のみ対象外 → slot1..7(DRIVER 枠が無い)。 */
#if defined(ARCH_M68K_MEGA)
#define KILL_MIN   1
#define KILL_MAX   (M68K_NSLOT - 1)      /* src/kmem.h が唯一の定義場所 */
#elif defined(ARCH_X86_IA16)
#define KILL_MIN   1
#define KILL_MAX   7
#define KILL_RANGE "1..7"
#else
#define KILL_MIN   2
#define KILL_MAX   7
#define KILL_RANGE "2..7"
#endif
static int cmd_kill(const char *arg)
{
    unsigned char n = 0;

    while (*arg == ' ') arg++;
    if (*arg < '0' || *arg > '9') {
#if defined(ARCH_M68K_MEGA)
        /* m68k は枠数が多く、しかも kmem.h の M68K_NSLOT 次第で変わるので、
         * 文字列リテラルに焼かず数値で出す。 */
        kprintf("usage: kill <1..%u>\n", (unsigned int)KILL_MAX);
#else
        kprintf("usage: kill <" KILL_RANGE ">\n");   /* #59: ROM 節約で短縮 */
#endif
        return BUILTIN_OK;
    }
    while (*arg >= '0' && *arg <= '9') {
        n = (unsigned char)(n * 10 + (*arg - '0'));
        arg++;
    }
    if (n < KILL_MIN || n > KILL_MAX) {
        kprintf("bad block %u\n", (unsigned int)n);
        return BUILTIN_OK;
    }
    if (PIDTBL[n] == 0) {
        kprintf("%u not running\n", (unsigned int)n);
        return BUILTIN_OK;
    }
    PIDTBL[n] = 0;                 /* 単一バイト store は atomic。di 不要 */
    kprintf("killed %u\n", (unsigned int)n);
    return BUILTIN_OK;
}
#endif /* !SD_DEBUG */

/* ================================================================== */
/* ディスパッチ                                                        */
/*   旧版は関数ポインタ表だったが、SDCC は fptr 呼び出しを jp (iy) 系に   */
/*   コンパイルする。sh を外部コマンド化して builtin_try を drv_tbl 経由で  */
/*   呼ぶようにすると、コマンド文脈の IY=base が飛び先になって暴走する。    */
/*   → strcmp + 直接 call の連鎖にして、どの文脈から呼ばれても安全にする。  */
/* ================================================================== */

void builtin_init(void)
{
    /* ブートログ。ドライバはまだ載っていないが、kprintf は物理層を内包
     * しているのでここで普通に出せる(ドライバ非依存)。認識・ロードの
     * 各段を1行ずつ吐き、何が生きて何が死んだかを起動時に見せる。 */
    if (fat_init() == 0) {                     /* FR_OK */
        kprintf("FAT Drive DETECTED\n");
    } else {
#if defined(ARCH_X86_IA16) || defined(ARCH_M68K_MEGA)
        /* x86: マウント失敗でもプロンプトは出す(デバッグ継続用)。 */
        kprintf("FAT Drive FAILED (continuing)\n");
#else
        kprintf("FAT Drive FAILED\n");   /* 未マウントでは以降が無意味 */
        for (;;)
            ;
#endif
    }

    vfs_init();          /* VFS 名前空間を KW_VTREE に初期化(/, /dev, ...) */

#if defined(ARCH_X86_IA16) || defined(ARCH_M68K_MEGA)
    /* x86 に常駐 DRIVER の概念は無い(コマンドは int 0x80 で直接カーネルへ)。 */
#else
    /* DRIVER.BIN を block1(0x9000) へ常駐ロード。FAT マウント後に行う。
     * ユーザーコードは DRIVER 経由で出力する前提なので、プロンプト(#)が
     * 出る前に確実に載せる。失敗は構成不備 ── フォールバックは持たず、
     * kprintf(物理層内包で生存)でエラーを出して停止する。 */
    if (kload_driver() == 0) {
        kprintf("Driver LOADED\n");
    } else {
        kprintf("Driver FAILED\n");
        for (;;)
            ;
    }
#endif
}

int builtin_try(const char *cmd, const char *arg)
{
#if BUILTIN_DF && !defined(SD_DEBUG)
    if (strcmp(cmd, "df")     == 0) return fat_df(arg);
#endif
#ifndef SD_DEBUG
    if (strcmp(cmd, "ps")     == 0) return cmd_ps(arg);
    if (strcmp(cmd, "kill")   == 0) return cmd_kill(arg);
#endif
#if BUILTIN_UPTIME_TREE
    if (strcmp(cmd, "uptime") == 0) return cmd_uptime(arg);
    if (strcmp(cmd, "tree")   == 0) return cmd_tree(arg);
#endif
    if (strcmp(cmd, "exit")   == 0) return cmd_exit(arg);
    return BUILTIN_NONE;
}

/* builtin_is: cmd が builtin(下記 + sh 直処理の cd/pwd)なら 1。
 *   sh のパイプ判定用 ── 両側が非 builtin のときだけカーネルパイプを張る
 *   (片側 builtin は従来の一時ファイル方式)。実行はしない。 */
int builtin_is(const char *cmd)
{
    return (strcmp(cmd, "cd")     == 0 ||
            strcmp(cmd, "pwd")    == 0 ||
#if BUILTIN_DF && !defined(SD_DEBUG)
            strcmp(cmd, "df")     == 0 ||
#endif
#ifndef SD_DEBUG
            strcmp(cmd, "ps")     == 0 ||
            strcmp(cmd, "kill")   == 0 ||
#endif
#if BUILTIN_UPTIME_TREE
            strcmp(cmd, "uptime") == 0 ||
            strcmp(cmd, "tree")   == 0 ||
#endif
            strcmp(cmd, "exit")   == 0);
}
