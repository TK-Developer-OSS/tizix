/* dev.c - /dev 実体。
 *
 *   2 つの流儀が同居している。理由も含めて明記しておく:
 *
 *   (A) 旧: struct dev_ops(関数ポインタ表)+ dev_lookup(name)。
 *       vfs_init が vnode->data に dev_lookup の結果を貼り、
 *       **arch/x86-ia16/sysfile.c がそれを経由して open/read/write する**。
 *       x86 側は現にこれで動いているので残す。
 *
 *   (B) 新(#32、z80pack): 関数ポインタを使わない kdev_* 直接呼び。
 *       Z80 側で関数ポインタを避けるのは本プロジェクトの掟(src/io.c 参照。
 *       間接呼び出しがユーザープロセスの iy=base を壊す恐れがある)。
 *       種別は vtree のノード type(VT_DEV_*)で持ち、switch で振り分ける。
 *       粒度は 512B セクタ固定 ── 生ブロックデバイスにバイト単位の窓を
 *       付けるとカーネルに 512B のバウンスバッファが要る。読み手は dd
 *       だけなので bs=512 を前提にした方が安い。
 *
 *   z80pack / z80board では (A) の fda エントリは撤去した。旧 arch/z80pack/
 *   dev_fda.c は dev_ops 前提で、しかも pdrv を無視して常に drive B を
 *   読んでいた ── 名前は fda なのに実体は B、かつ誰からも呼ばれていなかった。
 *   z80board の旧 arch/z80board/dev_fda.c も同じ理由で削除済み(#32 の
 *   z80board キャッチアップ)。
 */
#include "dev.h"
#include "vfs.h"
#include <string.h>

/* ---- (A) 旧 dev_ops 表(x86 / z80board が使う) ---- */

static int null_open(const char *path, int flags) { (void)path; (void)flags; return 0; }
static int null_read(int fd, void *buf, unsigned n) { (void)fd; (void)buf; (void)n; return 0; } /* EOF */
static int null_write(int fd, const void *buf, unsigned n) { (void)fd; (void)buf; return (int)n; }
static int null_close(int fd) { (void)fd; return 0; }
static int null_ioctl(int fd, unsigned cmd, unsigned arg) { (void)fd; (void)cmd; (void)arg; return 0; }

static const struct dev_ops null_ops = {
    null_open, null_read, null_write, null_close, null_ioctl
};

#if defined(ARCH_Z80PACK) || defined(ARCH_Z80BOARD) || defined(ARCH_M68K_MEGA)
/* #32/z80board キャッチアップ: どちらも (B) へ移行。dev_ops 版 fda は持たない。
 * m68k-mega はまだ (A)(B) どちらの /dev/fda も実装していないので、
 * ひとまず z80 と同じ「null だけの表」に合流させる(fda_ops 未定義でも
 * リンクできるようにするため)。 */
static const struct dev dev_tbl[] = {
    { "null", &null_ops },
    { 0, 0 }
};
#else
extern const struct dev_ops fda_ops;      /* arch/<arch>/dev_fda.c */
static const struct dev dev_tbl[] = {
    { "null", &null_ops },
    { "fda", &fda_ops },
    { 0, 0 }
};
#endif

const struct dev *dev_lookup(const char *name)
{
    const struct dev *d = dev_tbl;
    while (d->name) {
        if (strcmp(d->name, name) == 0) return d;
        d++;
    }
    return 0;
}

/* ---- (B) 生ブロックデバイス直接呼び (#32 z80pack / z80board キャッチアップ) ---- */
#if defined(ARCH_Z80PACK) || defined(ARCH_Z80BOARD) || defined(ARCH_M68K_MEGA)

/* arch 提供: drive を明示して 512B セクタ 1 本を転送。
 *   z80pack: cpmsim FDC の drive(0=A/1=B)。
 *   z80board / m68k-mega: SD カード 1 枚のみなので drive は無視(arch/z80board/sdcard.s,
 *   arch/m68k-mega/diskio.c
 *   _disk_raw_rw 参照)。op: 0=read 1=write。戻り 0=OK / 非0=エラー。
 *   ※ sect は 16bit 固定(この生ブロック経路の届く範囲は先頭 32MB)。
 *   z80board で SD カード全域に届く読み書きが要るのは FatFs 側
 *   (diskio.c の disk_read/disk_write)で、そちらは 32bit 版
 *   disk_raw_rw32 を直接使っており、ここは経由しない。 */
extern unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                                 unsigned sect, unsigned char op);

#if defined(ARCH_Z80PACK)
/* cpmsim のフロッピは 77 トラック × 26 セクタ × 128B = 256256B。
 * 512B セクタに直すと 500.5 本 → 端数を切って 500 本を上限にする。 */
#define FD_NSECT  500
#else
/* z80board: disk_raw_rw の sect が 16bit なので届く上限がそのまま上限
 * (65536 セクタ = 32MB)。SD カード自体はもっと大きいが、この生ブロック
 * 経路(/dev/fda 相当)は当面この範囲で十分。 */
#define FD_NSECT  65535
#endif

#define KIND_NONE 0
#define KIND_NULL 1
#define KIND_FDA  2
#define KIND_FDB  3

#define DEV_NFD   8                    /* DRIVER の MAX_FD(6)より広めに取る */

/* デバイス fd の現在位置(512B セクタ番号)。**DRIVER ではなくカーネルが持つ**
 * ── driver は 4096B の枠が厳しく、32bit 演算を 1 つ足すだけで溢れるため
 * (dev.h の注記)。fd 番号は DRIVER の fd_table の添字。 */
static unsigned dev_pos[DEV_NFD];

unsigned char kdev_kind(unsigned char vtype)
{
    if (vtype == VT_DEV_NULL) return KIND_NULL;
    if (vtype == VT_DEV_FDA)  return KIND_FDA;
    if (vtype == VT_DEV_FDB)  return KIND_FDB;
    return KIND_NONE;
}

/* 512B セクタ 1 本の転送。512=OK / 0=範囲外(EOF)/ -1=エラー */
static int dev_sector(unsigned char kind, unsigned char op, void *buf, unsigned sect)
{
    unsigned char drive;

    if (kind == KIND_NULL)
        return op ? 512 : 0;           /* write=捨てて成功 / read=即 EOF */

    if (kind == KIND_FDA)      drive = 0;
    else if (kind == KIND_FDB) drive = 1;
    else                       return -1;

    if (sect >= FD_NSECT) return 0;    /* 範囲外 = EOF */

    if (disk_raw_rw(drive, (unsigned char *)buf, sect, op) != 0)
        return -1;
    return 512;
}

int kdev_open(const char *abspath, unsigned char fd)
{
    int v;
    unsigned char k;

    if (fd < DEV_NFD) dev_pos[fd] = 0;

    v = vfs_resolve(abspath);
    if (v == VFS_FAT) return 0;              /* 通常の FAT ファイル */
    if (v < 0)        return -1;             /* VFS_ENOENT */

    k = kdev_kind(vfs_node_type(v));
    if (k == KIND_NONE) return -1;           /* /dev 自身や VT_FD 等は開けない */
    return (int)k;
}

int kdev_stream(unsigned char kind, unsigned char op, void *buf,
                unsigned count, unsigned char fd)
{
    unsigned done = 0;
    int rc;

    if (fd >= DEV_NFD) return -1;
    if (count == 0) return 0;
    if (count & 511u) return -1;             /* セクタ粒度のみ */

    while (done < count) {
        rc = dev_sector(kind, op, (unsigned char *)buf + done, dev_pos[fd]);
        if (rc < 0) return -1;
        if (rc == 0) break;                  /* 範囲外 = EOF */
        dev_pos[fd]++;
        done += 512;
    }
    return (int)done;
}

int kdev_seek(unsigned char kind, long off, unsigned char fd)
{
    unsigned long u;

    if (kind == KIND_NULL) return -1;        /* /dev/null に位置は無い */
    if (fd >= DEV_NFD) return -1;
    if (off < 0) return -1;
    u = (unsigned long)off;
    if ((unsigned)(u & 511UL)) return -1;    /* セクタ境界のみ */
    dev_pos[fd] = (unsigned)(u >> 9);
    return 0;
}

long kdev_tell(unsigned char fd)
{
    unsigned long u;

    if (fd >= DEV_NFD) return -1L;
    u = (unsigned long)dev_pos[fd];
    return (long)(u << 9);
}

#endif /* ARCH_Z80PACK || ARCH_Z80BOARD || ARCH_M68K_MEGA */
