/* driver.c - tizix block1 常駐 I/O ドライバ本体 + ファイルAPI */
#include <stdarg.h>
#include "ff.h"
#include "kmem.h"       /* KW_RXHEAD/KW_RXTAIL (#59 実タイマ割り込み化) */

/* ユーザー公開APIプロトタイプ（__sdcccall(0) 属性明示） */
int drv_putc(int c) __sdcccall(0);
int drv_getc(void) __sdcccall(0);
int drv_printf(const char *fmt, ...) __sdcccall(0);
void *drv_fopen(const char *path, const char *mode) __sdcccall(0);
int drv_fwrite(const void *ptr, unsigned int size, unsigned int nmemb, void *fp) __sdcccall(0);
int drv_fclose(void *fp) __sdcccall(0);
int drv_fread(void *ptr, unsigned int size, unsigned int nmemb, void *fp) __sdcccall(0);
int drv_fputs(const char *s, void *fp) __sdcccall(0);
char *drv_fgets(char *s, int size, void *fp) __sdcccall(0);
int drv_kbhit(void) __sdcccall(0);
int drv_getc_timeout(int timeout_ticks) __sdcccall(0);
int drv_fseek(void *fp, long offset, int whence) __sdcccall(0);
long drv_ftell(void *fp) __sdcccall(0);
int drv_feof(void *fp) __sdcccall(0);
int drv_ferror(void *fp) __sdcccall(0);
int drv_fflush(void *fp) __sdcccall(0);
int drv_fgetc(void *fp) __sdcccall(0);
int drv_fputc(int c, void *fp) __sdcccall(0);
int drv_puts(const char *s) __sdcccall(0);

/* string.h / stdlib.h 互換の純粋関数群は user/string.c, user/stdlib.c へ分離した
 * (driver 実体 4KB 超過の恒久対策。コマンドが string.rel / stdlib.rel をリンクする)。
 * driver 内部で必要な strlen/strchr 相当だけ下に private 実装として残す。 */

extern void kputchar(int c);
extern int  kgetchar(void);
extern unsigned int getticks(void);
extern int  con_pending(void);   /* io.c: 戻しバッファに 1 バイト有り(#33) */

/* VFS 一本化: コマンドのファイル操作もカーネルの vfs_resolve を通す。
 * #32 以降、その呼び出しは kdev_open の中(カーネル側)へ移した ── DRIVER は
 * 4096B しか載らないので、判定はできる限り向こうでやる。 */

/* #32: DEVFS ノードの実体へ降りる入口(src/dev.c)。コマンドは生パスも
 * FDC ポートも触らず、ここ(= syscall)だけを通る。
 *   **判定・位置管理・32bit 換算は全部カーネル側**に置いてある。DRIVER は
 *   0x9000 から 4096B しかロードされず(src/kexec.c kload_driver)、ここに
 *   32bit 演算を 1 つ足しただけで枠を溢れて末尾の drv_printf が載らなくなり、
 *   sh が無言でハングした。driver 側は薄い分岐だけに留めること。 */
extern int  kdev_open(const char *abspath, unsigned char fd);
extern int  kdev_stream(unsigned char kind, unsigned char op, void *buf,
                        unsigned count, unsigned char fd);
extern int  kdev_seek(unsigned char kind, long off, unsigned char fd);
extern long kdev_tell(unsigned char fd);

/* カレントディレクトリはカーネルが持つ(src/fatcmd.c)。コマンドは相対パスを
 * そのまま fopen に渡してよく、ここで cwd 起点に解決する。sh が「どの引数が
 * パスか」を推測する必要が無くなった理由。アドレスは FS_SYMS が解決する。
 * 解決先は「実行中ブロック専用」の枠(slot 0)なので、パイプや & で 2 つの
 * プロセスが同時に fopen しても互いのパスを踏まない。 */
extern const char *kpath(const char *in, unsigned char slot) __sdcccall(0);


#if defined(ARCH_Z80BOARD)
/* #59: RXF# の直接ポーリングは ISR(crt0.s)専用。ここは ISR が埋める
 * KW_RXBUF リング(kmem.h、src/io.c PHYS_RXRDY と同じ)の有無だけを見る。
 * 直接ポートを叩くと ISR の読み出しと競合してバイトを取りこぼす。 */
#define CON_RXRDY()  (*(volatile unsigned char *)KW_RXHEAD != \
                      *(volatile unsigned char *)KW_RXTAIL)
#else
__sfr __at 0x00 CONSTAT;
#define CON_RXRDY()  (CONSTAT)
#endif

static int drv_getc_wrapper(void *fp);

/* ---- driver 内部専用の簡易文字列関数（libc非依存 / static） ----
 *   公開版は user/string.c へ移設済み。ここは drv_fopen / drv_fputs が使う分だけ。 */
static unsigned int my_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static char *my_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return 0;
}

/* ---- ファイルハンドル管理 ---- */
#define MAX_FD  6
typedef struct {
    unsigned char used;
    unsigned char eof;
    unsigned char error;
    FIL fil;
} fd_entry_t;

/* ---- fd_table / fd_inited は block0 常駐 RAM に固定配置する ----
 * 理由: driver 実体は 4KB を超え(l__CODE=0x1469)、_DATA が 0xA469 =
 * block2 プロセス空間に落ちる。ここに fd_table を置くと、1129B(=0x469)を
 * 超えるコマンドがロード時に自分のコードで fd_table を踏み潰し、その後の
 * f_open が FIL をコマンドのコード領域へ書き込んで自己破壊 → exit 付近で
 * Op-code trap する(rwtest/cp で再現、trap 0xA468 ≒ fd_table[0])。
 * 対策として kmem.h の block0 空き帯(0x8529-0x8DFF)へ絶対番地固定で逃がす。
 * これで踏まれず常駐する(PCB/VFS/KW_* と同じ流儀)。
 *   0x8600-0x86E5  fd_table[6]  (sizeof(fd_entry_t)=37B → 222B)
 *   0x86F0         fd_inited    (マジック 0x5A で初期化済み判定)
 * driver は 0x9000 固定リンクで iy_reg 変換対象外のため、絶対番地アクセスで正。 */
#define fd_table   ((fd_entry_t *)0x8600)
#define fd_inited  (*(volatile unsigned char *)0x86F0)

/* #32: 生ブロックデバイス fd の種別。fd_table を広げると直後の
 * fd_inited(0x86F0)に当たるので、kmem.h が別枠で確保した帯に置く。
 *   dev_kind[fd] : 0=通常の FAT ファイル / 1=/dev/null / 2=fda / 3=fdb
 * 位置(セクタ番号)はカーネル(src/dev.c の dev_pos[])が fd 番号で持つ。 */
#define dev_kind   ((volatile unsigned char *)0x8C50)

static void fd_init_if_needed(void)
{
    if (fd_inited != 0x5A) {
        for (int i = 0; i < MAX_FD; i++) {
            fd_table[i].used = 0;
            fd_table[i].eof = 0;
            fd_table[i].error = 0;
            dev_kind[i] = 0;

        }
        fd_inited = 0x5A;
    }
}

static int alloc_fd(void)
{
    int fd = -1;
    __asm__("di");
    fd_init_if_needed();
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].eof = 0;
            fd_table[i].error = 0;
            dev_kind[i] = 0;               /* #32: 既定は通常の FAT ファイル */

            fd = i;
            break;
        }
    }
    __asm__("ei");
    return fd;
}

static void free_fd(int fd)
{
    if (fd >= 0 && fd < MAX_FD) {
        __asm__("di");
        fd_table[fd].used = 0;
        fd_table[fd].eof = 0;
        fd_table[fd].error = 0;
        dev_kind[fd] = 0;
        __asm__("ei");
    }
}

/* ---- 低レベルファイルAPI（カーネルFatFsを呼ぶ） ---- */
int drv_open(const char *name, unsigned char mode)
{
    int fd;
    int kind;

    name = kpath(name, 0);              /* 相対パスを cwd 起点で絶対化(ブロック毎の枠) */

    fd = alloc_fd();
    if (fd < 0) return -1;

    /* #32: 以前はここで非 FAT を一律に弾いていた(「当面」の暫定ガード)ので
     * /dev/null も /dev/fda も開けなかった。判定はカーネルへ寄せてある。 */
    kind = kdev_open(name, (unsigned char)fd);
    if (kind < 0) { free_fd(fd); return -1; }
    if (kind > 0) { dev_kind[fd] = (unsigned char)kind; return fd; }

    __asm__("di");
    FRESULT r = f_open(&fd_table[fd].fil, name, mode);
    __asm__("ei");
    if (r != FR_OK) {
        free_fd(fd);
        return -1;
    }
    return fd;
}

int drv_write(int fd, const void *buf, unsigned int count)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    /* #32: 生デバイスはカーネルへ丸投げ。**eof/error フラグは立てない** ──
     * fd_table[fd] は 37B ストライドで、1 回触るたびに乗算が要る。DRIVER は
     * 4096B しかロードされない(dev.h の注記)ので、誰も呼んでいない
     * feof()/ferror() のためにその代金を払わない。戻り値で足りる。 */
    if (dev_kind[fd])
        return kdev_stream(dev_kind[fd], 1, (void *)buf, count, (unsigned char)fd);
    UINT bw = 0;
    __asm__("di");
    FRESULT r = f_write(&fd_table[fd].fil, buf, count, &bw);
    __asm__("ei");
    if (r != FR_OK) {
        fd_table[fd].error = 1;
        return -1;
    }
    return (int)bw;
}

int drv_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    if (dev_kind[fd]) { free_fd(fd); return 0; }   /* デバイスは閉じるものが無い */
    __asm__("di");
    FRESULT r = f_close(&fd_table[fd].fil);
    __asm__("ei");
    free_fd(fd);
    return (r == FR_OK) ? 0 : -1;
}

int drv_read(int fd, void *buf, unsigned int count)
{
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    if (dev_kind[fd])
        return kdev_stream(dev_kind[fd], 0, buf, count, (unsigned char)fd);
    UINT br = 0;
    __asm__("di");
    FRESULT r = f_read(&fd_table[fd].fil, buf, count, &br);
    __asm__("ei");
    if (r != FR_OK) {
        fd_table[fd].error = 1;
        return -1;
    }
    if (br < count) {
        fd_table[fd].eof = 1;
    }
    return (int)br;
}

/* ---- 高レベルファイルAPI（C標準ライク、libc非依存） ---- */
void *drv_fopen(const char *path, const char *mode) __sdcccall(0)
{
    unsigned char fmode = 0;
    if (my_strchr(mode, 'r')) fmode |= 0x01;        /* FA_READ */
    if (my_strchr(mode, 'w')) fmode |= 0x0B;        /* FA_WRITE | FA_CREATE_ALWAYS | FA_READ */
    if (my_strchr(mode, 'a')) fmode |= 0x13;        /* FA_WRITE | FA_OPEN_APPEND | FA_READ */
    if (my_strchr(mode, '+')) fmode |= (0x01 | 0x02);
    if (fmode == 0) return 0;

    int fd = drv_open(path, fmode);
    if (fd < 0) return 0;
    return (void*)(unsigned int)(fd + 1);
}

static unsigned int udiv(unsigned int num, unsigned int den)
{
    if (den == 0) return 0;
    unsigned int quot = 0;
    while (num >= den) {
        num -= den;
        quot++;
    }
    return quot;
}

static unsigned int umul(unsigned int a, unsigned int b)
{
    unsigned int res = 0;
    while (b > 0) {
        if (b & 1) res += a;
        a <<= 1;
        b >>= 1;
    }
    return res;
}

int drv_fwrite(const void *ptr, unsigned int size, unsigned int nmemb, void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used || size == 0) return 0;
    int total = umul(size, nmemb);
    int written = drv_write(fd, ptr, total);
    if (written < 0) return 0;
    return udiv((unsigned int)written, size);
}

int drv_fclose(void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    return drv_close(fd);
}

int drv_fread(void *ptr, unsigned int size, unsigned int nmemb, void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used || size == 0) return 0;
    int total = umul(size, nmemb);
    int read = drv_read(fd, ptr, total);
    if (read < 0) return 0;
    return udiv((unsigned int)read, size);
}

int drv_fputs(const char *s, void *fp) __sdcccall(0)
{
    unsigned int len = my_strlen(s);
    return (drv_fwrite(s, 1, len, fp) == len) ? 0 : -1;
}

char *drv_fgets(char *s, int size, void *fp) __sdcccall(0)
{
    int c;
    char *p = s;
    while (--size > 0 && (c = drv_getc_wrapper(fp)) >= 0) {
        *p++ = (char)c;
        if (c == '\n') break;
    }
    *p = '\0';
    return (p == s) ? 0 : s;
}

static int drv_getc_wrapper(void *fp)
{
    unsigned char ch;
    if (drv_fread(&ch, 1, 1, fp) != 1) return -1;
    return ch;
}

int drv_fgetc(void *fp) __sdcccall(0)
{
    return drv_getc_wrapper(fp);
}

int drv_fputc(int c, void *fp) __sdcccall(0)
{
    unsigned char ch = (unsigned char)c;
    if (drv_fwrite(&ch, 1, 1, fp) != 1) return -1;
    return ch;
}

int drv_fseek(void *fp, long offset, int whence) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    if (dev_kind[fd]) {
        /* #32: 生デバイスはセクタ粒度。**SEEK_SET の 512 倍数のみ**受ける
         * (dd の skip= / seek= がこれを使う)。32bit 換算はカーネル側。 */
        if (whence != 0) return -1;
        return kdev_seek(dev_kind[fd], offset, (unsigned char)fd);
    }
    long target = offset;
    if (whence == 1) { /* SEEK_CUR */
        target += (long)fd_table[fd].fil.fptr;
    } else if (whence == 2) { /* SEEK_END */
        target += (long)f_size(&fd_table[fd].fil);
    }
    if (target < 0) return -1;
    __asm__("di");
    FRESULT r = f_lseek(&fd_table[fd].fil, (FSIZE_t)target);
    __asm__("ei");
    if (r == FR_OK) {
        fd_table[fd].eof = 0;
        return 0;
    }
    fd_table[fd].error = 1;
    return -1;
}

long drv_ftell(void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1L;
    if (dev_kind[fd]) return kdev_tell((unsigned char)fd);   /* #32 */
    return (long)fd_table[fd].fil.fptr;
}

int drv_feof(void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return 1;
    return fd_table[fd].eof;
}

int drv_ferror(void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return 1;
    return fd_table[fd].error;
}

int drv_fflush(void *fp) __sdcccall(0)
{
    int fd = (int)(unsigned int)fp - 1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    if (dev_kind[fd]) return 0;         /* #32: 生デバイスに溜めるものは無い */
    __asm__("di");
    FRESULT r = f_sync(&fd_table[fd].fil);
    __asm__("ei");
    return (r == FR_OK) ? 0 : -1;
}

/* ---- 従来の I/O 関数 ---- */
int drv_putc(int c) __sdcccall(0) { kputchar(c); return c; }
int drv_getc(void) __sdcccall(0) { return kgetchar(); }

static void drv_puts_raw(const char *s) { while (*s) drv_putc(*s++); }

int drv_puts(const char *s) __sdcccall(0)
{
    drv_puts_raw(s);
    drv_putc('\n');
    return 0;
}

/* ---- drv_putn（10進変換、除算なし） ---- */
static unsigned int emit_digit(unsigned int v, unsigned int p,
                               unsigned char *started, unsigned char force)
{
    unsigned char digit = 0;
    while (v >= p) { v -= p; digit++; }
    if (digit || *started || force) {
        drv_putc('0' + digit);
        *started = 1;
    }
    return v;
}

static void drv_putn(unsigned int v)
{
    unsigned char started = 0;
    v = emit_digit(v, 10000, &started, 0);
    v = emit_digit(v,  1000, &started, 0);
    v = emit_digit(v,   100, &started, 0);
    v = emit_digit(v,    10, &started, 0);
    (void)emit_digit(v,     1, &started, 1);
}

/* ---- drv_putln（32bit 10進変換、除算なし。%ld / %lu 用） ----
 * 桁重みテーブルを回して各桁を減算ループで求める（ループ化でコード節約）。 */
static const unsigned long drv_pow10[10] = {
    1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
    10000UL,      1000UL,      100UL,      10UL,      1UL
};

static void drv_putln(unsigned long v)
{
    unsigned char started = 0;
    unsigned char i;
    for (i = 0; i < 10; i++) {
        unsigned long p = drv_pow10[i];
        unsigned char digit = 0;
        while (v >= p) { v -= p; digit++; }
        if (digit || started || i == 9) {
            drv_putc('0' + digit);
            started = 1;
        }
    }
}

/* ---- drv_printf ---- */
int drv_printf(const char *fmt, ...) __sdcccall(0)
{
    va_list ap;
    char c;
    int v;
    long lv;

    va_start(ap, fmt);
    while ((c = *fmt++) != 0) {
        if (c != '%') { drv_putc(c); continue; }
        switch (c = *fmt++) {
        case 's': drv_puts_raw(va_arg(ap, char *)); break;
        case 'd':
            v = va_arg(ap, int);
            if (v < 0) { drv_putc('-'); v = -v; }
            drv_putn((unsigned int)v);
            break;
        case 'u': drv_putn(va_arg(ap, unsigned int)); break;
        case 'l':
            c = *fmt++;
            if (c == 0) { drv_putc('%'); drv_putc('l'); fmt--; break; }
            if (c == 'd') {
                lv = va_arg(ap, long);
                if (lv < 0) { drv_putc('-'); lv = -lv; }
                drv_putln((unsigned long)lv);
            } else if (c == 'u') {
                drv_putln(va_arg(ap, unsigned long));
            } else {
                drv_putc('%'); drv_putc('l'); drv_putc(c);
            }
            break;
        case 'c': drv_putc((char)va_arg(ap, int)); break;
        case '%': drv_putc('%'); break;
        default:  drv_putc('%'); drv_putc(c); break;
        }
    }
    va_end(ap);
    return 0;
}

/* コンソールに文字が到着しているか（ノンブロッキング）。
 * #33: sh の con_break がカーネルの戻しバッファへ退避した 1 バイトも
 * 「到着している」として数える。ここを見ないと、sh に先取りされた文字が
 * RXRDY から消えて vi の ESC シーケンス判定が空振りする。 */
int drv_kbhit(void) __sdcccall(0)
{
    if (con_pending()) return 1;
    return CON_RXRDY();
}

/* 端末の生モード(drv_tbl[48]、tty の ISIG 無効に相当)。on=1 で呼んだプロセスの間
 * Ctrl+C を割り込みにしない(判定は src/io.c の con_break、戻すのは rx 自身と、前景ジョブ終了後の sh)。
 * 中身は src/io.c の con_setraw(m68k 用)と同じだが、z80board のカーネル ROM に
 * 21B の余地が無かったので z80 では DRIVER(RAM)に置く。 */
void drv_conraw(unsigned char on) __sdcccall(0)
{
    *(volatile unsigned char *)KW_CONRAW = on ? *(volatile unsigned char *)KW_CURRENT : 0;
}

/* タイムアウト付き getchar（ミリ秒単位の ticks = 100Hz） */
int drv_getc_timeout(int timeout_ticks) __sdcccall(0)
{
    unsigned int start = getticks();
    while ((getticks() - start) < (unsigned)timeout_ticks) {
        if (con_pending()) return kgetchar();
        if (CON_RXRDY()) return kgetchar();
    }
    return -1;
}

/* string.h / stdlib.h 互換関数群は user/string.c, user/stdlib.c へ移設した。 */
