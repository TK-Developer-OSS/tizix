/* tzstdio.h - m68k-mega で tizix 共通 user/*.c(tzcc 向け書式)を "そのまま"
 *   ビルドするための互換 stdio.h。arch/x86-ia16/user/tzstdio.h の m68k 版。
 *
 *   m68k はフラットアドレス空間なので farcpy 相当は不要。syscall5(usyscall.s)
 *   経由で TRAP #0 を叩く(戻り値は unsigned long、エラーは 0xFFFFFFFF)。
 *
 *   未対応(該当 syscall が無い): fseek/ftell / mkdir/rename /
 *   proc_block 系・krun_wait・callovl・getxbase 系(tizix 固有の高度機能)。
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

typedef unsigned long size_t;

extern unsigned long syscall5(unsigned long func, unsigned long a1,
                              unsigned long a2, unsigned long a3,
                              unsigned long a4);

static int putchar(int c)
{
        syscall5(1, (unsigned long)(unsigned char)c, 0, 0, 0);
        return c;
}

static int getchar(void)
{
        /* #82: カーネルは EOF(パイプの writer 終了)を 0xFFFFFFFF で返す */
        unsigned long r = syscall5(2, 0, 0, 0, 0);
        return (r == 0xFFFFFFFFUL) ? EOF : (int)(unsigned char)r;
}

/* unsigned int(32bit、long と同じ幅)で宣言する: a.c/sleep.c が
 * `extern unsigned int getticks(void) __sdcccall(0);` を自前で再宣言して
 * おり(__sdcccall は plat.h のシムで無害化されるが、戻り値の型が違うと
 * "conflicting types" になる)、型を合わせて衝突を避ける。
 *
 * ★倍率を掛けている理由: sleep.c/a.c は共有コードで「getticks は 100Hz」を
 * 前提にハードコードしている(z80/x86-ia16 の実タイマは実際に100Hz)。
 * m68k-mega のタイマ周期は Makefile の TICK_HZ(既定 100、実機 Mega Timer5
 * が 1Hz のままなら 1)なので、生の tick に 100 / TICK_HZ を掛けて「論理
 * 100Hz tick」にそろえる(#47 で 1Hz のとき `sleep 1` が約 100 秒かかって
 * 発覚。#83 で TICK_HZ 可変に)。TICK_HZ=100 なら倍率 1。 */
#ifndef TICK_HZ
#error "TICK_HZ が未定義(arch/m68k-mega/Makefile の UCFLAGS で渡す)"
#endif
static unsigned int getticks(void)
{
        return (unsigned int)(syscall5(3, 0, 0, 0, 0) * (100UL / TICK_HZ));
}

/* klog(msg): /var/log/message へ 1 行追記(syscall 18)。z80 では
 * drv_tbl[46] 経由。user/rsyslog.c が使う。 */
static void klog(const char *msg)
{
        syscall5(18, (unsigned long)msg, 0, 0, 0);
}

/* kfs_df(sel): 0 = 総容量 KB / 1 = 空き KB(syscall 19)。z80 は drv_tbl[47]。/bin/df が使う。 */
static unsigned long kfs_df(unsigned sel)
{
        return syscall5(19, (unsigned long)sel, 0, 0, 0);
}

static int puts(const char *s)
{
        while (*s) putchar((unsigned char)*s++);
        putchar('\n');
        return 0;
}

static unsigned long strlen(const char *s)
{
        unsigned long n = 0;
        while (*s++) n++;
        return n;
}

static int strcmp(const char *a, const char *b)
{
        while (*a && *a == *b) { a++; b++; }
        return (unsigned char)*a - (unsigned char)*b;
}

static char *strcpy(char *dst, const char *src)
{
        char *d = dst;
        while ((*d++ = *src++) != 0) ;
        return dst;
}

static char *strchr(const char *s, int c)
{
        for (;; s++) {
                if (*s == (char)c) return (char *)s;
                if (!*s) return 0;
        }
}

static char *strcat(char *dst, const char *src)
{
        char *d = dst;
        while (*d) d++;
        while ((*d++ = *src++) != 0) {}
        return dst;
}

static char *strstr(const char *hay, const char *needle)
{
        const char *h, *n;
        if (!*needle) return (char *)hay;
        for (; *hay; hay++) {
                h = hay; n = needle;
                while (*h && *n && *h == *n) { h++; n++; }
                if (!*n) return (char *)hay;
        }
        return 0;
}

static void prs(const char *s)
{
        while (*s) putchar((unsigned char)*s++);
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
                        char b[8]; int i = 0;
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
#define TZ_NFILE 4
typedef struct { long fd; int eof; int err; int used; long pos; } FILE;
static FILE tz_files[TZ_NFILE];

static FILE *fopen(const char *path, const char *mode)
{
        /* flags: 0=読み / 1=書き(作成・切り詰め)/ 2=読み書き(既存のみ、"r+")。
         * "r+" は sh のヒストリ(/root/history のリング)がスロット上書きに使う。 */
        unsigned long flags = (mode && mode[0] == 'w') ? 1UL
                            : (mode && mode[0] == 'r' && mode[1] == '+') ? 2UL : 0UL;
        unsigned long r = syscall5(4, (unsigned long)path, flags, 0, 0);
        int i;

        if (r == 0xFFFFFFFFUL)
                return NULL;
        for (i = 0; i < TZ_NFILE; i++) {
                if (!tz_files[i].used) {
                        tz_files[i].fd   = (long)r;
                        tz_files[i].eof  = 0;
                        tz_files[i].err  = 0;
                        tz_files[i].used = 1;
                        tz_files[i].pos  = 0;
                        return &tz_files[i];
                }
        }
        syscall5(5, r, 0, 0, 0);
        return NULL;
}

static int fclose(FILE *f)
{
        if (!f || !f->used) return -1;
        syscall5(5, (unsigned long)f->fd, 0, 0, 0);
        f->used = 0;
        return 0;
}

static int fgetc(FILE *f)
{
        unsigned char c;
        long n;
        if (!f || !f->used) return EOF;
        n = (long)syscall5(6, (unsigned long)f->fd, (unsigned long)&c, 1, 0);
        if (n <= 0) { f->eof = 1; return EOF; }
        return c;
}

static int fputc(int c, FILE *f)
{
        unsigned char ch = (unsigned char)c;
        long n;
        if (!f || !f->used) return EOF;
        n = (long)syscall5(7, (unsigned long)f->fd, (unsigned long)&ch, 1, 0);
        return (n == 1) ? c : EOF;
}

static size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
        unsigned long want, got;
        if (!f || !f->used || size == 0) return 0;
        want = (unsigned long)(size * nmemb);
        got  = syscall5(6, (unsigned long)f->fd, (unsigned long)ptr, want, 0);
        if (got < want) f->eof = 1;
        return (size_t)(got / size);
}

static size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
        unsigned long want, got;
        if (!f || !f->used || size == 0) return 0;
        want = (unsigned long)(size * nmemb);
        got  = syscall5(7, (unsigned long)f->fd, (unsigned long)ptr, want, 0);
        return (size_t)(got / size);
}

static char *fgets(char *buf, int n, FILE *f)
{
        int i = 0, c;
        if (!f || n <= 1) return NULL;
        while (i < n - 1) {
                c = fgetc(f);
                if (c == EOF) { if (i == 0) return NULL; break; }
                buf[i++] = (char)c;
                if (c == '\n') break;
        }
        buf[i] = 0;
        return buf;
}

static int fputs(const char *s, FILE *f)
{
        while (*s)
                if (fputc((unsigned char)*s++, f) == EOF) return EOF;
        return 0;
}

static int feof(FILE *f)  { return f ? f->eof : 1; }
static int ferror(FILE *f) { return f ? f->err : 1; }
static int fflush(FILE *f) { (void)f; return 0; }
/* seek(a1=fd,a2=offset,a3=whence) -> 絶対位置(syscall5.c #47 追加分)。
 * SEEK_CUR は現在位置をカーネル側で追跡していないため非対応。 */
static long ftell(FILE *f) { return (f && f->used) ? f->pos : -1L; }
static int fseek(FILE *f, long off, int whence)
{
        unsigned long r;
        if (!f || !f->used) return -1;
        r = syscall5(13, (unsigned long)f->fd, (unsigned long)off, (unsigned long)whence, 0);
        if (r == 0xFFFFFFFFUL) return -1;
        f->pos = (long)r;
        f->eof = 0;
        return 0;
}

/* ---- ディレクトリ(tzcc 規約: 現在1個だけ開ける前提) ---- */
static long tz_cur_dh = -1;

static int opendir(const char *path)
{
        unsigned long r = syscall5(8, (unsigned long)path, 0, 0, 0);
        if (r == 0xFFFFFFFFUL) return -1;
        tz_cur_dh = (long)r;
        return 0;
}

static int readdir(char *name13)
{
        if (tz_cur_dh < 0) return 0;
        return (int)syscall5(9, (unsigned long)tz_cur_dh, (unsigned long)name13, 0, 0);
}

static void closedir(void)
{
        if (tz_cur_dh >= 0) syscall5(10, (unsigned long)tz_cur_dh, 0, 0, 0);
        tz_cur_dh = -1;
}

static unsigned long readdir_size(void)
{
        return syscall5(12, 0, 0, 0, 0);
}

/* date.c(共有)が使う。z80/x86-ia16 は KW_EPOCH_SEC を直接 peek するが、
 * m68k-mega では kwork[] 内の可変オフセットで固定番地の保証が無いため
 * syscall #16(kernel.c time_get())経由にする。 */
static unsigned long m68k_get_epoch(void)
{
        return syscall5(16, 0, 0, 0, 0);
}

static int unlink(const char *path)
{
        return (int)syscall5(11, (unsigned long)path, 0, 0, 0);   /* 0 / 1 / FS_DENIED */
}
static int mkdir(const char *path)
{
        return (int)syscall5(14, (unsigned long)path, 0, 0, 0);
}
static int rename(const char *a, const char *b)
{
        return (int)syscall5(15, (unsigned long)a, (unsigned long)b, 0, 0);
}

/* pipe_tail: パイプの後段の tail(syscall 21)。カーネル(src/pipe.c)が writer の完了を
 * 待って 4KB 窓の末尾 want 行を出す。戻り 1 = パイプの後段として処理した / 0 = 後段ではない。
 * #82 で m68k にもカーネルパイプが入ったので、以前の「常に 0」のスタブを置き換えた
 * (`cat f | tail` が "needs FILE or pipe" になっていた)。 */
static unsigned char pipe_tail(unsigned want)
{
        return (unsigned char)syscall5(21, (unsigned long)want, 0, 0, 0);
}

static void proc_block(void) {}
static void proc_wake(unsigned char b) { (void)b; }
static unsigned char krun_wait(const char *f, const char *pack, unsigned char argc)
{ (void)f; (void)pack; (void)argc; return 0; }
static unsigned getbase(void) { return 0; }

/* peek/poke/peekw/pokew: 絶対番地への生アクセス(vi.c の undo 記録用)。
 * m68k は奇数アドレスへの word アクセスでアドレスエラー例外になるため、
 * peekw/pokew は **バイト単位**で組み立てる(z80/x86 のような直接
 * *(unsigned short*)addr は不可)。エンディアンは vi.c 内で書いて読むだけの
 * 自己完結値なので任意でよい。 */
static unsigned char peek(unsigned addr) { return *(volatile unsigned char *)addr; }
static void poke(unsigned addr, unsigned char v) { *(volatile unsigned char *)addr = v; }
static unsigned peekw(unsigned addr)
{
        unsigned char *p = (unsigned char *)addr;
        return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static void pokew(unsigned addr, unsigned v)
{
        unsigned char *p = (unsigned char *)addr;
        p[0] = (unsigned char)v;
        p[1] = (unsigned char)(v >> 8);
}
static unsigned callovl(unsigned addr, unsigned arg) { (void)addr; (void)arg; return 0; }

/* absmove/absmovd: vi.c が使う絶対番地間コピー(z80 LDIR/LDDR 相当)。
 * absmove  = 前方から(dst <= src の重なりで安全、memcpy 相当)
 * absmovd  = 後方から(dst >= src の重なりで安全。ins_at の右シフトに必要) */
static void absmove(unsigned dst, unsigned src, unsigned n)
{
        unsigned char *d = (unsigned char *)dst;
        unsigned char *s = (unsigned char *)src;
        unsigned i;
        for (i = 0; i < n; i++) d[i] = s[i];
}
static void absmovd(unsigned dst, unsigned src, unsigned n)
{
        unsigned char *d = (unsigned char *)dst;
        unsigned char *s = (unsigned char *)src;
        unsigned i = n;
        while (i > 0) { i--; d[i] = s[i]; }
}

/* absscan/absrscan: vi.c が使う絶対番地スキャン(z80 tzcc の tzcblk.s の
 * CPIR/CPDR 版と同じ戻り値規約に厳密に合わせてある。tzcc/tzcblk.s の
 * コメント参照):
 *   absscan(p,ch,n)  : [p,p+n) を前方に探し、見つかれば **一致位置そのもの**、
 *                       無ければ p+n を返す。
 *   absrscan(p,ch,n) : [p-n,p) を後方(p-1 から)に探し、見つかれば
 *                       **一致位置の 1 つ次**、無ければ p-n を返す。
 * vi.c の line_head/line_tail はこの戻り値をそのまま使う設計なので、
 * 1 バイトでもずれるとカーソル/行境界が壊れる。 */
static unsigned absscan(unsigned p, unsigned ch, unsigned n)
{
        unsigned char *s = (unsigned char *)p;
        unsigned i;
        for (i = 0; i < n; i++)
                if (s[i] == (unsigned char)ch) return p + i;
        return p + n;
}
static unsigned absrscan(unsigned p, unsigned ch, unsigned n)
{
        unsigned char *s = (unsigned char *)(p - 1);
        unsigned i;
        for (i = 0; i < n; i++)
                if (s[-(int)i] == (unsigned char)ch) return p - i;
        return p - n;
}

/* getxbase/getxsize: z80 の「追加ブロック」(#38、.BIN ヘッダに刻んだ枚数だけ
 * kexec が像とは別に確保する仕組み)は m68k-mega の kexec には無い
 * (m68k は元々プロセス毎に 32KB 固定スロットで、24KB の IMG_BUDGET を
 * 超えない範囲なら普通の static 配列で十分足りる)。vi.c 側は
 * getxbase()/getxsize() の返り値だけで本文サイズを決める設計になっている
 * ので、ここでその場限りの static バッファを返せば vi.c は無改造で動く。 */
#define VI_XBUF_SIZE 8192
static char vi_xbuf[VI_XBUF_SIZE];
static unsigned getxbase(void) { return (unsigned)vi_xbuf; }
static unsigned getxsize(void) { return (unsigned)VI_XBUF_SIZE; }

/* input_ready/getc_timeout: vi.c の getkey() が ESC シーケンス(矢印キー)を
 * 判定するための非ブロッキング読み。syscall #17 でコンソール RX-ready を
 * 覗いてから getchar() する(ブロックする getchar() だけでは ESC 単体の
 * 押下と ESC[A のような複数バイトシーケンスを区別できない)。 */
static unsigned input_ready(void)
{
        return (unsigned)syscall5(17, 0, 0, 0, 0);
}
/* kbhit: z80 の stdio.h と同じ名前(rx が受信待ちのポーリングに使う)。 */
static int kbhit(void)
{
        return (int)input_ready();
}
/* con_raw(on): 端末の生モード(syscall 35、z80 は drv_tbl[48])。rx が使う。 */
static void con_raw(int on)
{
        syscall5(35, (unsigned long)on, 0, 0, 0);
}
static int getc_timeout(unsigned wait_ticks)
{
        unsigned t0 = getticks();
        for (;;) {
                if (input_ready()) return getchar();
                if ((unsigned)(getticks() - t0) >= wait_ticks) return -1;
        }
}

#endif
