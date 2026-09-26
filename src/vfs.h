#ifndef _VFS_H
#define _VFS_H

/* ==================================================================
 * vfs.h - tizix VFS 名前空間 (パス解決の統合ハブ)
 *
 *   前セッションで撤去した VFS(vfs[]/resolve/ppath) とは別物。
 *   あれは「実 FAT と無関係の飾り」だったので消した。今回のは
 *   endpoint 名前空間の器 ── /dev/null / 将来 /dev/fdN(パイプ端点) /
 *   /dev/uart 等を、番号や実体に対応づける土台。
 *
 *   2026-08-30〜 VFS 一本化(DEVELOP.md「==== VFS 一本化 ====」):
 *   ファイルパスを取る操作は必ず vfs_resolve() を通す。オーバーレイは
 *   "/dev" 固定 1 エントリ、それ以外は全部 FAT。/proc /bin /var 等の
 *   空 stub は削除(/proc は将来復活の可能性あり)。fstab/mount は作らない。
 *
 *   設計判断(このセッションで確定):
 *     - ノードは block0 内の *絶対番地固定* 配列(KW_VTREE=0x8419)。
 *       C static にすると SDCC が DATA_LOC=0x8000 から上へ積み、既存
 *       statics(~0x830A) の直後 = PCB(0x8400) に衝突するため。PCB と
 *       同じ流儀で絶対番地に置く。
 *     - 配列でツリー(parent index)。ポインタ地獄を避け Z80 で軽い。
 *     - 名前は RAM 保持(name[12])。将来 ROM 化で更に削れるが今はしない。
 *     - lookup は文字列パス走査。重くて可(最速は目指さない方針)。
 *     - fd 採番はグローバル単調増加(8bit)。一周したらハング許容
 *       (モニタ延長の割り切り。68000 移植時に真面目に作る)。
 *     - 採番の read-inc-write は di/ei で囲む(ISR 二重採番防止)。
 *
 *   iy 注記: vfs.c は block0(カーネル/シェル文脈)専用。iy=base 相対の
 *            制約は掛からない。通常 C でよい(builtin.c と同じ位置づけ)。
 *
 *   ★未検証: 本ヘッダ/実装は sdcc ビルド・実機検証を一切通していない。
 *            ホスト GCC 構文チェックとロジック確認のみ。
 * ================================================================== */

/* ノード種別 */
#define VT_FREE      0   /* 空きスロット                          */
#define VT_FAT_ROOT  1   /* "/"  = FAT マウント(組込、mount 省略)  */
#define VT_DIR       2   /* ディレクトリ(/dev /proc /bin ...)      */
#define VT_DEV_NULL  3   /* /dev/null   (将来)                    */
#define VT_DEV_UART  4   /* /dev/uart   (将来)                    */
#define VT_FD        5   /* /dev/fdN    パイプ端点(socket 段で使用) */
#define VT_DEV_FDA   6   /* /dev/fda  生ブロックデバイス(cpmsim drive A = boot/kernel) */
#define VT_DEV_FDB   7   /* /dev/fdb  生ブロックデバイス(cpmsim drive B = FAT ボリューム) */

#define VT_NOPARENT  0xFF   /* 親なし(root)                       */

/* ノード。sizeof = 12+1+1+2 = 16B (SDCC z80, パディング無し)。 */
struct vnode {
    char          name[12];   /* 短名。"" = FAT ルート("/")         */
    unsigned char type;       /* VT_*                              */
    unsigned char parent;     /* 親ノードの index。VT_NOPARENT=root */
    void         *data;       /* VT_FD: バッファポインタ / 他: 用途別 */
};

/* 起動時に呼ぶ。KW_VTREE 域を初期化し、固定ディレクトリを焼く。
 * crt0(gsinit) は絶対番地配列を初期化しないので明示的に呼ぶこと
 * (builtin_init と同じ理由)。 */
void vfs_init(void);

/* 絶対パス(先頭 '/')を index に解決。見つからなければ -1。
 * "/" 単独は root(index 0) を返す。相対パスは未対応(RPATH 相当は無し)。
 * vtree(/dev サブツリー)内の走査専用。一般のパス振り分けは vfs_resolve()。 */
int  vfs_lookup(const char *path);

/* vfs_resolve - 正規化済み *絶対* パスを backend へ振り分ける(パス解決の統合入口)。
 *   sh の path_norm で "."/".." 解決済みの絶対パスを渡すこと。
 *   戻り値:
 *     >= 0 : DEVFS が担当。戻り値 = vtree index(vfs_node で参照)
 *     -1   : FAT が担当。呼び出し側は絶対パスをそのまま f_* へ渡す
 *     -2   : "/dev" 配下だが該当ノード無し(ENOENT)
 *   ルール(オーバーレイ固定): パスが "/dev" ちょうど or "/dev/..." → DEVFS、
 *   それ以外(/ 直下含む)→ FAT。 */
int  vfs_resolve(const char *abspath);
#define VFS_FAT     (-1)
#define VFS_ENOENT  (-2)

/* DEVFS ディレクトリ反復。parent の子で *slot 以降の最初を name(>=12B)へ。
 * *slot を次回開始位置に更新。戻り 1=あり / 0=終端。 */
int vfs_dir_next(unsigned char parent, unsigned char *slot, char *name);

/* index のノードへのポインタ。範囲外/未使用は 0。 */
struct vnode *vfs_node(int idx);

/* index のノード種別(VT_*)だけを返す。範囲外/未使用は VT_FREE。
 * DRIVER(別リンク単位)が struct vnode のレイアウトを知らずに済むように。 */
unsigned char vfs_node_type(int idx);

/* ---- socket 段のフック(構造だけ用意。今は結線側から未使用) ----
 * 空きスロットに VT_FD ノードを 1 個割り当て、/dev 直下に "fdN" として
 * 生やす。data にバッファポインタを格納。index を返す。空き無しは -1。
 * 採番は di/ei で保護。名前は "fd" + 10進(next_fd)。 */
int  vfs_alloc_fd(void *bufptr);

/* パイプ終了時にカーネルが呼ぶ想定(socket 段)。当該 fd ノードを
 * VT_FREE に戻す(スロット再利用可)。番号自体は捨てる(先番号)。 */
void vfs_free_fd(int idx);

/* 検証用: vtree の全ノードをフルパスで出力する。
 * 実機で配置(0x8419〜)と初期化を目視確認するための一時コマンド。
 * builtin "tree" から呼ぶ。確認が済んだら builtin ごと外してよい。 */
void vfs_dump(void);

#endif
