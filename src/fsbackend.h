#ifndef _FSBACKEND_H
#define _FSBACKEND_H

/* ==================================================================
 * fsbackend.h - tizix ファイルシステム backend 界面(条件 B)
 *
 *   DEVELOP.md「==== 8/31 ディスカッション ====」の設計に沿った、
 *   「カーネル中核 ⇄ FS backend」の型定義。
 *
 *   ★現状: 型定義のみ。まだどこにも #include されておらず、実装も無い。
 *     既存コード(arch/x86-ia16/sysfile.c、src/fatcmd.c、user/driver.c)は
 *     いまも f_* / disk_* を直接呼ぶ。ここへ寄せる作業は [P1] 残タスク。
 *
 *   2 つの境界:
 *
 *     コマンド/中核 ──(この界面: fsb_*)──▶ FS backend(FAT / RAM ディスク / …)
 *                                              │
 *                                       (blk_* 界面)
 *                                              ▼
 *                                    block device = /dev/fda
 *                                    (arch の storage 下位層: FDC / SPI / テープ / INT13h)
 *
 *   設計原則:
 *     - 中核は FS 形式を知らない。ハンドルは backend が所有する小さい int。
 *       中核/コマンドは sizeof(FIL) 等を一切見ない(FatFs 依存を剥がす)。
 *     - パスは正規化済み絶対パス。vfs_resolve() が FAT と判定したものだけが
 *       ここへ来る(/dev 配下は DEVFS が処理済み)。
 *     - ポインタはすべて near(呼び出し側セグメント/ブロック内)。プロセス
 *       セグメント跨ぎの移送は arch の syscall 層(sysfile.c farcpy 等)の責務。
 *     - 戻り値は簡素な int。FRESULT を外へ出さない(0=OK / <0=エラー)。
 *     - backend は下位を blk_* だけで触る。diskio.c を直接参照しない。
 * ================================================================== */

/* ---- 結果コード(FRESULT を隠蔽) ---- */
#define FSB_OK        0
#define FSB_ERR      (-1)   /* 一般エラー(FR_DISK_ERR 等) */
#define FSB_NOENT    (-2)   /* 対象なし(FR_NO_FILE / FR_NO_PATH) */
#define FSB_DENIED   (-3)   /* 書込み禁止・読取専用 等 */
#define FSB_NOSPC    (-4)   /* 空きなし / ハンドル枯渇 */

/* ---- open モード ---- */
#define FSB_READ     0x01
#define FSB_WRITE    0x02
#define FSB_CREATE   0x04   /* 無ければ作る / あれば切詰め(FA_CREATE_ALWAYS 相当) */

/* ---- ファイル ---- */
/* 戻り: 0.. = backend ハンドル / <0 = FSB_* エラー */
int  fsb_open (const char *abspath, unsigned mode);
int  fsb_close(int fh);
/* read/write: 実転送バイト数(0 = EOF / エラー)。負値は返さない。 */
int  fsb_read (int fh, void *buf, unsigned len);
int  fsb_write(int fh, const void *buf, unsigned len);
int  fsb_seek (int fh, unsigned long pos);   /* 先頭からのバイト位置 */
unsigned long fsb_size(int fh);

/* ---- ディレクトリ反復(同時 1..NDIR) ---- */
int  fsb_opendir (const char *abspath);      /* 0.. = dir ハンドル / <0 */
/* name13 に短名(<=12+NUL)。戻り 0=終端 / 1=ファイル / 2=ディレクトリ / <0 */
int  fsb_readdir (int dh, char *name13);
int  fsb_closedir(int dh);
/* 直前の fsb_readdir が返したエントリのサイズ(ディレクトリ/終端後は 0)。
 * z80 の drv_tbl[38] readdir_size() と同じ「直前 readdir 限定」の約束。 */
unsigned long fsb_readdir_size(void);

/* ---- 名前空間操作 ---- */
int  fsb_unlink(const char *abspath);
int  fsb_mkdir (const char *abspath);
int  fsb_rename(const char *oldabs, const char *newabs);

/* ---- マウント(boot 時に 1 回) ---- */
int  fsb_mount(void);

/* ==================================================================
 * block device 界面(FS backend ⇄ arch storage 下位層)
 *   diskio.h の disk_* をリネームしたもの。/dev/fda の実体。
 *   単位はセクタ(512B 固定、FF_MIN_SS=FF_MAX_SS=512)。
 * ================================================================== */
#define BLK_OK     0
#define BLK_ERR  (-1)

int blk_init (void);
/* lba セクタから count セクタを buf へ / から。戻り BLK_OK / BLK_ERR。 */
int blk_read (void *buf, unsigned long lba, unsigned count);
int blk_write(const void *buf, unsigned long lba, unsigned count);
int blk_sync (void);
unsigned long blk_sectors(void);   /* 総セクタ数(0 = 不明) */

#endif /* _FSBACKEND_H */
