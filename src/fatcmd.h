/* fatcmd.h - FatFs backed builtins */
#ifndef FATCMD_H
#define FATCMD_H

int  fat_init(void);            /* f_mount driveb。戻り 0=FR_OK。call once at shell start */
int  fat_isdir(const char *path);/* path がディレクトリなら 1(sh の cd 用)。"/" は常に 1 */

#ifndef ARCH_X86_IA16
/* ---- カレントディレクトリ(カーネル所有)。実装と設計の理由は fatcmd.c 冒頭 ----
 *   パスを受け取るカーネル入口は先頭で kpath() を通す。これにより sh は
 *   「どの引数がパスか」を推測しなくてよくなり、コマンドのオプションが
 *   増えても壊れない。 */
const char *kpath(const char *in, unsigned char slot) __sdcccall(0); /* slot=0/1、ブロック毎 */
int  kchdir(const char *path) __sdcccall(0);     /* 0=ok / -1=no such dir。drv_tbl[42] */
void kgetcwd(char *out) __sdcccall(0);           /* out は KW_CWD_MAX。drv_tbl[43] */
void kpath_init(void);                           /* cwd="/root"。fat_init から 1 回 */
#endif

/* kdir: backend 透過のディレクトリ反復(外部 ls 用、drv_tbl[21..23])。同時 1 個。 */
int  kdir_open(const char *path) __sdcccall(0);  /* 0=ok / -1=fail。cwd 起点で解決 */
int  kdir_open_abs(const char *path) __sdcccall(0); /* 解決済み絶対パス用(内部) */
int  kdir_read(char *name) __sdcccall(0);        /* 1=name あり / 0=終端。name>=13B */
void kdir_close(void) __sdcccall(0);
unsigned long kdir_size(void) __sdcccall(0);     /* 直前 kdir_read のサイズ(dir/dev=0)。drv_tbl[38] */

/* cat / echo は外部化(Step 8)、rm / mkdir / mv も外部化(Step 9)。
 * rm/mkdir/mv 用カーネル入口(drv_tbl[24..26])。パスは vfs_resolve 経由・
 * 非 FAT は拒否。戻り 0=OK / 0xFF=permission denied / 1..19=FatFs FRESULT。
 * sh も TMP.PIP 掃除に kfs_unlink を使う(戻り無視)。 */
int  kfs_mkdir(const char *path) __sdcccall(0);
int  kfs_unlink(const char *path) __sdcccall(0);
int  kfs_rename(const char *src, const char *dst) __sdcccall(0);

#ifndef ARCH_X86_IA16
/* rsyslog: /var/log/message へ 1 行追記(drv_tbl[46])。init() のブート文言と
 * 外部コマンド rsyslog(user/rsyslog.c)の両方から呼ばれる。 */
void klog_write(const char *msg) __sdcccall(0);
#endif

/* df(#56): sel=0 総 KB / sel=1 空き KB。取れなければ 0xFFFFFFFF。drv_tbl[47] */
unsigned long kfs_df(unsigned sel) __sdcccall(0);
int  redir_begin(const char *fname, unsigned char append);  /* > / >> file: 出力先をファイルへ。append!=0 で追記 */
void redir_end(void);
int  in_begin(const char *fname);     /* < file: 入力元をファイルへ */
void in_end(void);
int  in_src(void);                    /* io.c の getchar から直接 call。-1=EOF */

#if defined(ARCH_X86_IA16)
/* シェル行ヒストリ(z80 の user/sh.c #45 と同じ /root/history リング形式を
 * カーネル側(src/io.c の readline)へ移植。z80 は外部コマンドとして自前で
 * FatFs を叩いていたが、こちらはカーネル内蔵シェルなので fatcmd.c に置く。
 * ARCH ガード付き ── z80pack はサイズに敏感なので巻き込まない。 */
void     hist_init(void);
void     hist_add(const char *line);
unsigned hist_get(unsigned char back, char *buf);
unsigned char hist_count(void);
#endif

#endif
