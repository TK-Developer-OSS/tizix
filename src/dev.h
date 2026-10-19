#ifndef _DEV_H
#define _DEV_H

/* ==================================================================
 * dev.h - /dev 実体
 *
 *   2 つの流儀が同居している(理由は src/dev.c 冒頭):
 *     (A) struct dev_ops + dev_lookup … arch/x86-ia16/sysfile.c が使う。
 *     (B) kdev_*(関数ポインタ無し)  … #32 で z80pack 側が使う。
 *   Z80 側で関数ポインタを避けるのは掟(src/io.c。間接呼び出しがユーザー
 *   プロセスの iy=base を壊す恐れがある)。粒度は 512B セクタ固定。
 *
 *   (B) は DRIVER(user/driver.c)が FS_SYMS 経由で直接呼ぶ。コマンドが
 *   生パスや FDC ポートを触ることはない(掟: コマンドは syscall 経由)。
 * ================================================================== */

/* ---- (A) 旧 dev_ops 表。x86 / z80board が vnode->data 経由で使う ---- */

/* 各ドライバが実装すべき関数ポインタテーブル */
struct dev_ops {
    int (*open)(const char *path, int flags);
    int (*read)(int fd, void *buf, unsigned n);
    int (*write)(int fd, const void *buf, unsigned n);
    int (*close)(int fd);
    int (*ioctl)(int fd, unsigned cmd, unsigned arg);
};

/* デバイスインスタンス（テーブルに登録するもの） */
struct dev {
    const char *name;           /* デバイス名 ("/dev/null" の "null") */
    const struct dev_ops *ops;  /* 関数ポインタテーブル */
};

/* デバイス検索。見つからなければ 0。 */
const struct dev *dev_lookup(const char *name);

/* ---- (B) 生ブロックデバイス直接呼び (#32, z80pack) ---- */

/* ★DRIVER(block1)は 0x9000 から **ちょうど 4096 バイトしかロードされない**
 * (src/kexec.c kload_driver)。1 バイトでも超えると末尾の関数が載らず、
 * 呼んだ瞬間にゴミへ飛ぶ ── 実測で drv_printf が落ち、sh が無言でハングした。
 * よって /dev 対応のロジックは **できる限りカーネル側に置く**。とくに
 * 32bit 演算(セクタ⇔バイト変換)は SDCC のヘルパを引き込むので driver には
 * 書かない。位置(セクタ番号)も DRIVER ではなくカーネルが fd 番号で持つ。 */

/* kdev_open - 絶対パスが生デバイスかを判定し、その fd の位置を 0 に戻す。
 *   戻り: 0 = 通常の FAT ファイル(呼び出し側が f_open する)
 *         1..3 = デバイス種別(1=/dev/null 2=/dev/fda 3=/dev/fdb)
 *         -1 = 開けない(/dev 配下で該当無し / ディレクトリ等) */
int kdev_open(const char *abspath, unsigned char fd);

/* kdev_kind - vtree ノード種別(VT_DEV_*)→ デバイス種別(0=非デバイス 1=null 2=fda 3=fdb) */
unsigned char kdev_kind(unsigned char vtype);

/* kdev_stream - デバイスと count バイト転送する(512B セクタ粒度)。
 *   count が 512 の倍数でなければ -1。fd の位置は自動で進む。
 *   戻り: 転送バイト数(count 未満なら EOF に達した)/ -1=エラー */
int kdev_stream(unsigned char kind, unsigned char op, void *buf,
                unsigned count, unsigned char fd);

/* kdev_seek - バイトオフセット off へ移動(512 の倍数のみ)。0=OK / -1=不可 */
int kdev_seek(unsigned char kind, long off, unsigned char fd);

/* kdev_tell - fd の現在位置をバイトで返す。 */
long kdev_tell(unsigned char fd);

#endif
