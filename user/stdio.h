/* user/stdio.h - tizix ユーザーコード用 stdio */
#ifndef _STDIO_H
#define _STDIO_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifndef _DRV_FUNC_T_DEFINED
#define _DRV_FUNC_T_DEFINED
typedef void (*drv_func_t)(void);
#endif
extern drv_func_t const drv_tbl[];

typedef int (*fn_putchar_t)(int) __sdcccall(0);
typedef int (*fn_getchar_t)(void) __sdcccall(0);
typedef int (*fn_printf_t)(const char*, ...) __sdcccall(0);
typedef void *(*fn_fopen_t)(const char*, const char*) __sdcccall(0);
typedef size_t (*fn_fwrite_t)(const void*, size_t, size_t, void*) __sdcccall(0);
typedef int (*fn_fclose_t)(void*) __sdcccall(0);
typedef size_t (*fn_fread_t)(void*, size_t, size_t, void*) __sdcccall(0);
typedef int (*fn_fputs_t)(const char*, void*) __sdcccall(0);
typedef char *(*fn_fgets_t)(char*, int, void*) __sdcccall(0);
typedef int (*fn_kbhit_t)(void) __sdcccall(0);
typedef int (*fn_getc_timeout_t)(int) __sdcccall(0);
typedef int (*fn_fseek_t)(void*, long, int) __sdcccall(0);
typedef long (*fn_ftell_t)(void*) __sdcccall(0);
typedef int (*fn_feof_t)(void*) __sdcccall(0);
typedef int (*fn_ferror_t)(void*) __sdcccall(0);
typedef int (*fn_fflush_t)(void*) __sdcccall(0);
typedef int (*fn_fgetc_t)(void*) __sdcccall(0);
typedef int (*fn_fputc_t)(int, void*) __sdcccall(0);
typedef int (*fn_puts_t)(const char*) __sdcccall(0);
typedef void (*fn_proc_block_t)(void) __sdcccall(0);
typedef void (*fn_proc_wake_t)(unsigned char) __sdcccall(0);
typedef int  (*fn_opendir_t)(const char*) __sdcccall(0);
typedef int  (*fn_readdir_t)(char*) __sdcccall(0);
typedef void (*fn_closedir_t)(void) __sdcccall(0);
typedef unsigned long (*fn_readdir_size_t)(void) __sdcccall(0);
typedef int  (*fn_mkdir_t)(const char*) __sdcccall(0);
typedef int  (*fn_unlink_t)(const char*) __sdcccall(0);
typedef int  (*fn_rename_t)(const char*, const char*) __sdcccall(0);
typedef void (*fn_klog_t)(const char*) __sdcccall(0);
typedef void (*fn_conraw_t)(unsigned char) __sdcccall(0);

#define putchar(c)         (((fn_putchar_t)drv_tbl[0])(c))
#define getchar()          (((fn_getchar_t)drv_tbl[1])())
#define printf(...)        (((fn_printf_t)drv_tbl[2])(__VA_ARGS__))
#define fopen(p, m)        (((fn_fopen_t)drv_tbl[3])(p, m))
#define fwrite(p, s, n, f) (((fn_fwrite_t)drv_tbl[4])(p, s, n, f))
#define fclose(f)          (((fn_fclose_t)drv_tbl[5])(f))
#define fread(p, s, n, f)  (((fn_fread_t)drv_tbl[6])(p, s, n, f))
#define fputs(s, f)        (((fn_fputs_t)drv_tbl[7])(s, f))
#define fgets(s, n, f)     (((fn_fgets_t)drv_tbl[8])(s, n, f))
#define kbhit()            (((fn_kbhit_t)drv_tbl[9])())
#define getc_timeout(t)    (((fn_getc_timeout_t)drv_tbl[10])(t))
#define fseek(f, o, w)     (((fn_fseek_t)drv_tbl[11])(f, o, w))
#define ftell(f)           (((fn_ftell_t)drv_tbl[12])(f))
#define feof(f)            (((fn_feof_t)drv_tbl[13])(f))
#define ferror(f)          (((fn_ferror_t)drv_tbl[14])(f))
#define fflush(f)          (((fn_fflush_t)drv_tbl[15])(f))
#define fgetc(f)           (((fn_fgetc_t)drv_tbl[16])(f))
#define fputc(c, f)        (((fn_fputc_t)drv_tbl[17])(c, f))
#define puts(s)            (((fn_puts_t)drv_tbl[18])(s))

/* プロセス wait/wake (5a)。カーネル本体を直接指す(drv_tbl[19..20])。
 *   proc_block(): 自プロセスを parked にし、proc_wake まで戻らない。
 *   proc_wake(n): ブロック n を runnable に戻す。 */
#define proc_block()       (((fn_proc_block_t)drv_tbl[19])())
#define proc_wake(n)       (((fn_proc_wake_t)drv_tbl[20])((unsigned char)(n)))

/* backend 透過のディレクトリ反復(FAT / /dev)。同時 1 個。drv_tbl[21..23]。
 *   opendir(path): 0=ok / -1=fail
 *   readdir(name13): 0=終端 / 1=ファイル / 2=ディレクトリ
 *   closedir(): 反復終了。 */
#define opendir(p)         (((fn_opendir_t)drv_tbl[21])(p))
#define readdir(nm)        (((fn_readdir_t)drv_tbl[22])(nm))
#define closedir()         (((fn_closedir_t)drv_tbl[23])())
/* 直前 readdir が返したエントリのサイズ(ディレクトリ/dev は 0)。drv_tbl[38]。
 * FF_FS_TINY=1 で「反復中に fopen」は窓破壊 → ls -l はこれでサイズを得る。 */
#define readdir_size()     (((fn_readdir_size_t)drv_tbl[38])())

/* FS 書込み(rm / mkdir / mv)。drv_tbl[24..26]。パスは vfs_resolve 経由・
 * 非 FAT(/dev 配下)は拒否。戻り 0=OK / 0xFF=permission denied / 1..19=FatFs FRESULT。
 * (0xFF 番兵は掟の符号付き比較禁止を避けるため。コマンドは == で判別する) */
#define FS_DENIED          0xFF
#define mkdir(p)           (((fn_mkdir_t)drv_tbl[24])(p))
#define unlink(p)          (((fn_unlink_t)drv_tbl[25])(p))
#define rename(a, b)       (((fn_rename_t)drv_tbl[26])(a, b))

/* rsyslog: /var/log/message へ 1 行追記。drv_tbl[46]。
 * "YYYY-MM-DD HH:MM:SS [pid] <msg>\n" (256 文字上限、超過分は切り捨て)。 */
#define klog(msg)          (((fn_klog_t)drv_tbl[46])(msg))

/* 端末の生モード(tty の ISIG 無効に相当)。drv_tbl[48]。on=1 の間 Ctrl+C(0x03)を
 * sh の割り込み検出に取られず、データとして読める。プロセス終了で自動解除。rx が使う。 */
#define con_raw(on)        (((fn_conraw_t)drv_tbl[48])((unsigned char)(on)))

typedef void FILE;

#endif
