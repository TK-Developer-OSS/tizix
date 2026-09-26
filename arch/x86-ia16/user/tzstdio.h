/* tzstdio.h - x86-ia16 で tizix 共通 user/*.c(tzcc 向け書式)を "そのまま"
 *   ビルドするための互換 stdio.h。
 *
 *   共通 user/*.c(echo.c/cat.c/head.c/...)は tzcc(tizix/tzcc/)の
 *   include/tizix.h をヘッダとして使う前提で書かれている。x86 は tzcc の
 *   対象外(ia16 は 8086 セグメント、tzcc は Z80 IY 相対 PIC 専用)なので、
 *   同じ関数名・シグネチャを x86 の低レベル syscall(open/read/write/close、
 *   opendir/readdir/closedir、putchar/getchar)の上に実装しなおす。
 *
 *   使い方: Makefile が共通 user/<cmd>.c を obj/tzport/ へコピーし、この
 *   ファイルも obj/tzport/stdio.h としてコピーする。#include "stdio.h" は
 *   クォート付きインクルードなので同じディレクトリのこちらを優先して拾う
 *   (x86 ネイティブコマンド用の user/stdio.h には触れない)。
 *
 *   全部 static ── x86 ネイティブコマンド(ls.c 等、user/stdio.h + ulibc.o
 *   を使う)とシンボルが衝突しないようにするため。ulibc.o からは
 *   putchar/getchar/syscall5 だけを extern で借りる。
 *
 *   未対応のまま呼ぶと何もしない/失敗を返すもの(実装先の syscall が無い):
 *     fseek/ftell(x86 に seek syscall が無い)
 *     mkdir/rename(x86 に該当 syscall が無い)
 *     proc_block 系 / krun_wait / callovl / getxbase 系
 *       (tizix 固有の高度機能。基本コマンドは使わない前提)
 *   これらを使うコマンドは動かない可能性が高い。動くのは fopen/fread/fwrite/
 *   fgetc/fputc/fgets/fputs/opendir/readdir/readdir_size/closedir/unlink
 *   止まりの範囲(readdir_size は AH=12 で 2026-09-12 に実装した)。
 */
#ifndef _TZSTDIO_H
#define _TZSTDIO_H

#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FS_DENIED 0xFF

typedef unsigned int size_t;

extern int putchar(int c);      /* ulibc.o */
extern int getchar(void);       /* ulibc.o */
extern unsigned syscall5(unsigned func, unsigned al, unsigned bx,
                         unsigned cx, unsigned si, unsigned di);

/* ---- puts/prs/prnum/prnuml(tzcc 互換。puts は改行を付ける) ---- */
static int puts(const char *s)
{
        while (*s)
                putchar((unsigned char)*s++);
        putchar('\n');
        return 0;
}

static void prs(const char *s)
{
        while (*s)
                putchar((unsigned char)*s++);
}

static void prnum(unsigned v)
{
        char b[6];
        int i = 0;
        do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (i) putchar(b[--i]);
}

static void prnuml(unsigned long v)
{
        char b[11];
        int i = 0;
        do { b[i++] = (char)('0' + (unsigned)(v % 10)); v /= 10; } while (v);
        while (i) putchar(b[--i]);
}

/* printf: %s %d %u %c %x %% だけの簡易版(ulibc.c の実装と同等)。
 * hello.c 以外の canonical コマンドはほぼ prs/puts/prnum を使うので、
 * printf を実際に踏むのは a.c/b.c(printf("A")等)くらいの想定。 */
static int printf(const char *fmt, ...)
{
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        for (; *fmt; fmt++) {
                if (*fmt != '%') { putchar((unsigned char)*fmt); continue; }
                fmt++;
                switch (*fmt) {
                case 's': prs(__builtin_va_arg(ap, const char *)); break;
                case 'd': {
                        int d = __builtin_va_arg(ap, int);
                        if (d < 0) { putchar('-'); d = -d; }
                        prnum((unsigned)d);
                        break;
                }
                case 'u': prnum(__builtin_va_arg(ap, unsigned)); break;
                case 'c': putchar(__builtin_va_arg(ap, int)); break;
                case 'x': {
                        unsigned v = __builtin_va_arg(ap, unsigned);
                        char b[4]; int i = 0;
                        do {
                                unsigned d = v & 0xF;
                                b[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                                v >>= 4;
                        } while (v);
                        while (i) putchar(b[--i]);
                        break;
                }
                case '%': putchar('%'); break;
                default: putchar('%'); putchar((unsigned char)*fmt); break;
                }
        }
        __builtin_va_end(ap);
        return 0;
}

/* ---- FILE(open/read/write/close の上に被せる薄いラッパ) ---- */
/* ★2026-09-12: FILE スロットの空き判定に fd<0 を使っていたが、tz_files[] は
 * 静的配列で(BSS)ゼロ初期化される ── 未使用スロットは fd==0(負数ではない)
 * から始まる。「fd<0 なら空き」は一度も使われていないスロットを「使用中」と
 * 誤判定し、プロセス起動直後の最初の fopen が必ず空きスロット無しで NULL を
 * 返す(cat 等、fopen をすぐ呼ぶコマンドが軒並み "cannot open" になっていた
 * 実バグ)。fd の値に意味を持たせず、明示の used フラグで管理する。 */

/* klog(msg): /var/log/message へ 1 行追記(syscall 18)。番号は
 * m68k-mega と揃えてある。user/rsyslog.c が使う。 */
static void klog(const char *msg)
{
        syscall5(18, 0, 0, 0, (unsigned)msg, 0);
}
#define TZ_NFILE 4
typedef struct { int fd; int eof; int err; int used; } FILE;
static FILE tz_files[TZ_NFILE];

static FILE *fopen(const char *path, const char *mode)
{
        unsigned flags = (mode && mode[0] == 'w') ? 1u : 0u;
        unsigned r = syscall5(4, flags, 0, 0, (unsigned)path, 0);
        int i;

        if (r == 0xFFFF)
                return NULL;
        for (i = 0; i < TZ_NFILE; i++) {
                if (!tz_files[i].used) {
                        tz_files[i].fd   = (int)r;
                        tz_files[i].eof  = 0;
                        tz_files[i].err  = 0;
                        tz_files[i].used = 1;
                        return &tz_files[i];
                }
        }
        syscall5(5, r, 0, 0, 0, 0);    /* 空きスロットが無い → 開いた fd を閉じて諦める */
        return NULL;
}

static int fclose(FILE *f)
{
        if (!f || !f->used)
                return -1;
        syscall5(5, (unsigned)f->fd, 0, 0, 0, 0);
        f->used = 0;
        return 0;
}

static int fgetc(FILE *f)
{
        unsigned char c;
        int n;

        if (!f || !f->used)
                return EOF;
        n = (int)syscall5(6, 0, (unsigned)f->fd, 1, 0, (unsigned)&c);
        if (n <= 0) { f->eof = 1; return EOF; }
        return c;
}

static int fputc(int c, FILE *f)
{
        unsigned char ch = (unsigned char)c;
        int n;

        if (!f || !f->used)
                return EOF;
        n = (int)syscall5(7, 0, (unsigned)f->fd, 1, 0, (unsigned)&ch);
        return (n == 1) ? c : EOF;
}

static size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
        unsigned want, got;

        if (!f || !f->used || size == 0)
                return 0;
        want = (unsigned)(size * nmemb);
        got  = syscall5(6, 0, (unsigned)f->fd, want, 0, (unsigned)ptr);
        if (got < want)
                f->eof = 1;
        return got / size;
}

static size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
        unsigned want, got;

        if (!f || !f->used || size == 0)
                return 0;
        want = (unsigned)(size * nmemb);
        got  = syscall5(7, 0, (unsigned)f->fd, want, 0, (unsigned)ptr);
        return got / size;
}

static char *fgets(char *buf, int n, FILE *f)
{
        int i = 0, c;

        if (!f || n <= 1)
                return NULL;
        while (i < n - 1) {
                c = fgetc(f);
                if (c == EOF) {
                        if (i == 0) return NULL;
                        break;
                }
                buf[i++] = (char)c;
                if (c == '\n') break;
        }
        buf[i] = 0;
        return buf;
}

static int fputs(const char *s, FILE *f)
{
        while (*s)
                if (fputc((unsigned char)*s++, f) == EOF)
                        return EOF;
        return 0;
}

static int feof(FILE *f)  { return f ? f->eof : 1; }
static int ferror(FILE *f) { return f ? f->err : 1; }
static int fflush(FILE *f) { (void)f; return 0; }

/* x86 に seek syscall が無いので未対応。呼ぶと失敗を返す(呼び出し元が
 * これを前提に fopen し直す設計のコマンドなら壊れずに動く)。 */
static long ftell(FILE *f) { (void)f; return -1L; }
static int fseek(FILE *f, long off, int whence)
{
        (void)f; (void)off; (void)whence;
        return -1;
}

/* ---- ディレクトリ(tzcc 規約: 現在1個だけ開ける前提) ---- */
static int tz_cur_dh = -1;

static int opendir(const char *path)
{
        unsigned r = syscall5(8, 0, 0, 0, (unsigned)path, 0);
        if (r == 0xFFFF)
                return -1;
        tz_cur_dh = (int)r;
        return 0;
}

/* x86 の readdir syscall はファイル/ディレクトリを区別しない(0=終端/1=有)。
 * tzcc 規約は 0=終端/1=ファイル/2=ディレクトリだが、ここでは常に 1 を返す
 * (ディレクトリを "ファイル" 扱いする ── ls 相当は動くが -l 的な種別表示は
 * できない)。 */
static int readdir(char *name13)
{
        if (tz_cur_dh < 0)
                return 0;
        return (int)syscall5(9, 0, (unsigned)tz_cur_dh, 0, 0, (unsigned)name13);
}

static void closedir(void)
{
        if (tz_cur_dh >= 0)
                syscall5(10, (unsigned)tz_cur_dh, 0, 0, 0, 0);
        tz_cur_dh = -1;
}

/* ★2026-09-12: AH=12(sysfile.c)で実装。直前 readdir エントリのサイズ下位
 * 16bit を返す(64KB 未満ファイル前提。z80 側と同じ約束)。 */
static unsigned long readdir_size(void)
{
        return (unsigned long)syscall5(12, 0, 0, 0, 0, 0);
}

/* ---- FS 書込み ---- */
static int unlink(const char *path)
{
        return (syscall5(11, 0, 0, 0, (unsigned)path, 0) == 0xFFFF) ? 1 : 0;
}
static int mkdir(const char *path)  { (void)path; return -1; }        /* 未対応 */
static int rename(const char *a, const char *b) { (void)a; (void)b; return -1; }      /* 未対応 */

/* ---- プロセス wait/wake・共有メモリ・オーバーレイ・追加ブロック ----
 * tizix 固有の高度機能。x86 側にまだ土台が無いので no-op/失敗スタブのみ。
 * これらを実際に使うコマンド(sh 等)は移植対象外。 */
static void proc_block(void) {}
static void proc_wake(unsigned char b) { (void)b; }
static unsigned char krun_wait(const char *f, const char *pack, unsigned char argc)
{ (void)f; (void)pack; (void)argc; return 0; }
static unsigned getbase(void) { return 0; }
static unsigned char peek(unsigned addr) { (void)addr; return 0; }
static void poke(unsigned addr, unsigned char v) { (void)addr; (void)v; }
static unsigned peekw(unsigned addr) { (void)addr; return 0; }
static void pokew(unsigned addr, unsigned v) { (void)addr; (void)v; }
static unsigned callovl(unsigned addr, unsigned arg) { (void)addr; (void)arg; return 0; }
static unsigned getxbase(void) { return 0; }
static unsigned getxsize(void) { return 0; }

#endif
