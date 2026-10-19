/* ==================================================================
 * vfs.c - tizix VFS 名前空間 (パス解決の統合ハブ)
 *
 *   block0 内の絶対番地固定配列 KW_VTREE(0x8419) にツリーを持つ。
 *   固定ノード 3 個 + 動的スロット(fd 端点/将来枝)。
 *
 *   固定レイアウト(vfs_init が焼く):
 *     idx 0 : "/"         VT_FAT_ROOT  parent=NOPARENT   ← FAT が見える
 *     idx 1 : /dev        VT_DIR       parent=0          ← 合成(ディスク上に無い)
 *     idx 2 : /dev/null   VT_DEV_NULL  parent=1
 *     idx 3..N-1 : VT_FREE (動的 fd 端点)
 *
 *   2026-08-30〜 VFS 一本化:
 *     ・オーバーレイは /dev のみ。/proc /bin /root /var /var/log /var/run の
 *       空 stub は削除(/proc は将来復活の可能性あり)。
 *     ・ファイルパスを取る操作は vfs_resolve() で backend(FAT / DEVFS)へ振り分け。
 *     ・詳細は DEVELOP.md「==== VFS 一本化 ====」。
 * ================================================================== */
#include "kmem.h"
#include "vfs.h"
#include "dev.h"
#include "io.h"      /* kprintf(vfs_ls_children / vfs_dump) */


/* KW_VTREE(0x8419) を struct vnode 配列として見る。KW_VTREE_N 個。 */
#define VTREE   ((struct vnode *)KW_VTREE)
#define VTREE_N KW_VTREE_N
#if defined(PLAT_FLAT32)
/* kwork の中の枠は 1 個 20B で取ってある(src/kmem.h の KW_VTREE_SIZE)。ポインタが
 * 太るアーキで struct vnode が枠を越えたら、隣の項目を踏む前にビルドを止める。 */
typedef char kw_vnode_fits[(sizeof(struct vnode) * KW_VTREE_N <= KW_VTREE_SIZE) ? 1 : -1];
#endif

/* fd 採番カウンタ。KW_NEXTFD(0x8519) に 1B 固定配置。
 * 8bit 単調増加。一周でハング許容(方針)。 */
#define NEXTFD  (*(volatile unsigned char *)KW_NEXTFD)

/* /dev の index(固定)。fd ノードはこの直下に生やす。 */
#define DEV_IDX 1

/* 固定ノード数。idx 0..VFIXED-1 は vfs_init が焼く。VFIXED.. が動的 fd 端点。
 * 0="/" 1=/dev 2=/dev/null 3=/dev/fda 4=/dev/fdb */
#define VFIXED  5

/* ---- 小物: 名前コピー(<=11 文字 + NUL)。string.h を増やさない。 ---- */
static void vname_set(struct vnode *n, const char *s)
{
    unsigned char i = 0;
    while (s[i] && i < 11) { n->name[i] = s[i]; i++; }
    n->name[i] = 0;
}

/* ---- 小物: 名前比較。等しければ 1。 ---- */
static unsigned char vname_eq(const struct vnode *n, const char *s)
{
    unsigned char i = 0;
    while (s[i] && s[i] != '/') {
        if (n->name[i] != s[i]) return 0;
        i++;
    }
    return (n->name[i] == 0);      /* ノード名も同じ長さで終端か */
}

void vfs_init(void)
{
    unsigned char i;

    /* 全スロットを空きに */
    for (i = 0; i < VTREE_N; i++) {
        VTREE[i].type    = VT_FREE;
        VTREE[i].parent  = VT_NOPARENT;
        VTREE[i].name[0] = 0;
        VTREE[i].data    = 0;
    }

    /* 固定ノードを焼く(VFIXED 個)。オーバーレイは /dev のみ。 */
    VTREE[0].type = VT_FAT_ROOT; VTREE[0].parent = VT_NOPARENT; vname_set(&VTREE[0], "");
    VTREE[1].type = VT_DIR;      VTREE[1].parent = 0;           vname_set(&VTREE[1], "dev");
    VTREE[2].type = VT_DEV_NULL; VTREE[2].parent = DEV_IDX;     vname_set(&VTREE[2], "null");
    VTREE[2].data = (void*)dev_lookup("null");
    /* #32: 生ブロックデバイス。cpmsim の FDC ドライブ番号に 1:1 で対応する。
     *   /dev/fda = drive A(boot + kernel が載っている floppy)
     *   /dev/fdb = drive B(FatFs がマウントしている FAT12 ボリューム)
     * VBR を取りたいなら FAT が載っているのは B 側なので /dev/fdb。
     * z80pack はノード type から kdev_rw が switch で振り分ける(関数ポインタを
     * 使わない掟。src/io.c 参照)。data は x86 の sysfile.c が dev_ops 経由で
     * 使うので、そちらのために dev_lookup の結果も入れておく。 */
    VTREE[3].type = VT_DEV_FDA;  VTREE[3].parent = DEV_IDX; vname_set(&VTREE[3], "fda");
    VTREE[3].data = (void*)dev_lookup("fda");
    VTREE[4].type = VT_DEV_FDB;  VTREE[4].parent = DEV_IDX; vname_set(&VTREE[4], "fdb");
    VTREE[4].data = (void*)dev_lookup("fdb");

    NEXTFD = 0;
}

/* parent の子で name(先頭コンポーネント)に一致するノード index。無ければ -1。 */
static int find_child(unsigned char parent, const char *comp)
{
    unsigned char i;
    for (i = 0; i < VTREE_N; i++) {
        if (VTREE[i].type == VT_FREE) continue;
        if (VTREE[i].parent != parent) continue;
        if (vname_eq(&VTREE[i], comp)) return i;
    }
    return -1;
}

int vfs_lookup(const char *path)
{
    const char *p = path;
    int cur = 0;                    /* root */

    if (*p != '/') return -1;       /* 絶対パスのみ */
    p++;                            /* 先頭 '/' を飛ばす */

    while (*p) {
        int nxt;
        /* 空コンポーネント("//" や末尾 '/')はスキップ */
        if (*p == '/') { p++; continue; }
        nxt = find_child((unsigned char)cur, p);
        if (nxt < 0) return -1;
        cur = nxt;
        /* 次の '/' か終端まで進める */
        while (*p && *p != '/') p++;
    }
    return cur;
}

struct vnode *vfs_node(int idx)
{
    if (idx < 0 || idx >= VTREE_N) return 0;
    if (VTREE[idx].type == VT_FREE) return 0;
    return &VTREE[idx];
}

/* #32: ノード種別だけを返す。DRIVER は struct vnode のレイアウトを知らずに
 * 済ませたい(driver.c は別リンク単位で、構造体を共有すると版ズレが怖い)。
 * 戻り: VT_* / 範囲外・未使用は VT_FREE。 */
unsigned char vfs_node_type(int idx)
{
    if (idx < 0 || idx >= VTREE_N) return VT_FREE;
    return VTREE[idx].type;
}

/* vfs_resolve - 正規化済み絶対パスを backend へ振り分ける。詳細は vfs.h。
 *   >=0: DEVFS(vtree index) / -1(VFS_FAT): FAT / -2(VFS_ENOENT): /dev 配下で無し
 *   まだ誰も呼んでいない(Step 2 で fat_* / do_cd から結線)。 */
int vfs_resolve(const char *p)
{
    /* "/dev" ちょうど、または "/dev/..." か? (末尾が NUL か '/' で境界確認) */
    if (p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' &&
        (p[4] == 0 || p[4] == '/')) {
        int idx = vfs_lookup(p);          /* vtree(/dev サブツリー)を走査 */
        return (idx < 0) ? VFS_ENOENT : idx;
    }
    return VFS_FAT;                       /* /dev 以外は全部 FAT */
}

/* DEVFS ディレクトリ反復。parent の子のうち slot 以降で最初のものを name へ。
 *   *slot は次回開始位置に更新。戻り 0=終端 / 1=非ディレクトリ / 2=ディレクトリ。
 *   fatcmd.c kdir_read から(kdir_read の戻り規約に合わせる)。 */
int vfs_dir_next(unsigned char parent, unsigned char *slot, char *name)
{
    unsigned char i, k;

    for (i = *slot; i < VTREE_N; i++) {
        if (VTREE[i].type == VT_FREE) continue;
        if (VTREE[i].parent != parent) continue;
        for (k = 0; VTREE[i].name[k] && k < 11; k++)
            name[k] = VTREE[i].name[k];
        name[k] = 0;
        *slot = (unsigned char)(i + 1);
        return (VTREE[i].type == VT_DIR) ? 2 : 1;
    }
    *slot = VTREE_N;
    name[0] = 0;
    return 0;
}

/* ---- socket 段フック(構造のみ。結線側は次段で実装) ---- */

int vfs_alloc_fd(void *bufptr)
{
    int slot = -1;
    unsigned char i, n, j;
    char nm[12];

    IRQ_OFF();                          /* 採番 critical section 開始 */

    for (i = VFIXED; i < VTREE_N; i++) {   /* 0..VFIXED-1 は固定、VFIXED.. が動的 */
        if (VTREE[i].type == VT_FREE) { slot = i; break; }
    }
    if (slot < 0) {
        IRQ_ON();
        return -1;
    }

    /* 名前 "fd" + 10進(NEXTFD) を組む */
    nm[0] = 'f'; nm[1] = 'd';
    n = NEXTFD;
    j = 2;
    if (n >= 100) { nm[j++] = '0' + n / 100;      n %= 100; }
    if (n >= 10  || j > 2) { nm[j++] = '0' + n / 10; n %= 10; }
    nm[j++] = '0' + n;
    nm[j]   = 0;

    VTREE[slot].type   = VT_FD;
    VTREE[slot].parent = DEV_IDX;          /* /dev 直下 */
    VTREE[slot].data   = bufptr;
    vname_set(&VTREE[slot], nm);

    NEXTFD++;                              /* 8bit。一周でハング許容 */

    IRQ_ON();                          /* critical section 終了 */
    return slot;
}

void vfs_free_fd(int idx)
{
    if (idx < VFIXED || idx >= VTREE_N) return; /* 固定枠(0..VFIXED-1)は解放しない */
    VTREE[idx].type   = VT_FREE;
    VTREE[idx].parent = VT_NOPARENT;
    VTREE[idx].data   = 0;
    VTREE[idx].name[0] = 0;
}

/* ---- 検証用ダンプ。tree コマンド(Step 9 で外部スケルトン化予定)---- */
/* builtin の tree は外部コマンド化の方針で無効化中(task.md #56)。呼び手が
 * 無くてもモジュール単位で ROM に残るので、実体ごと無効化する(コードは残す)。 */
#define VFS_DUMP 0
#if VFS_DUMP

/* idx のフルパスを出力(親チェーンを辿って逆順表示)。 */
static void print_path(int idx)
{
    unsigned char stack[8];        /* 深さ上限 8(FF_PATH_DEPTH 相当で十分) */
    unsigned char sp = 0;
    int cur = idx;

    /* root(idx 0, name="") は "/" として特別扱い */
    if (cur == 0) { kprintf("/"); return; }

    while (cur > 0 && sp < 8) {
        stack[sp++] = (unsigned char)cur;
        cur = VTREE[cur].parent;
        if (cur == VT_NOPARENT) break;
    }
    while (sp) {
        kprintf("/%s", VTREE[stack[--sp]].name);
    }
}

void vfs_dump(void)
{
    unsigned char i;
    for (i = 0; i < VTREE_N; i++) {
        if (VTREE[i].type == VT_FREE) continue;
        kprintf("[%u] ", (unsigned)i);
        print_path(i);
        kprintf(" (t=%u", (unsigned)VTREE[i].type);
        if (VTREE[i].type == VT_FD)
            kprintf(" buf=%u", (unsigned)(unsigned int)VTREE[i].data);
        kprintf(")\n");
    }
}
#endif /* VFS_DUMP */
