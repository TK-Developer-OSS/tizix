/* fatcmd.c - FatFs backed builtins: mount + ls + cat (drive B / driveb.dsk)
 *
 *   read-only FAT12. f_mount is done once at shell start (fat_init).
 *   ls  : list root directory of the mounted FAT volume.
 *   cat : dump a file's bytes to the console.
 */
#include "ff.h"
#include "io.h"
#include "kmem.h"       /* KW_LSDIR(kdir 状態) */
#include "builtin.h"
#include "fatcmd.h"
#include "vfs.h"        /* VFS 一本化: パスは vfs_resolve で backend 振り分け */

static FATFS fs;                 /* the mounted volume (static: ~560B in TINY) */

/* kdir: backend 透過のディレクトリ反復状態(外部 ls 用、drv_tbl[21..23])。
 *   同時 1 個。定義は下の kdir_* 節も参照。全アーキ共通(#76)。
 *   置き場所だけアーキで違う: z80 は SDCC statics が FatFs win[512] と重なる
 *   回避で絶対番地 KW_LSDIR、x86-ia16 / m68k-mega は普通の static。 */
struct kdir {
    unsigned char inuse;
    unsigned char backend;      /* 0=FAT, 1=DEVFS */
    unsigned char devidx;       /* DEVFS: 親 vtree index */
    unsigned char devnext;      /* DEVFS: 次に見る vtree slot / FAT: ルートなら 1(末尾に dev を足す) */
    DIR fdir;                   /* FAT: FatFs DIR(~40B) */
};
#if !defined(ARCH_X86_IA16) && !defined(ARCH_M68K_MEGA)
#define KD ((struct kdir *)KW_LSDIR)
#else
static struct kdir kd_state;
#define KD (&kd_state)
#endif

/* ==================================================================
 * カレントディレクトリ(カーネル所有)と パス解決
 *
 *   従来は sh(user/sh.c)が cwd を持ち、「どの引数がパスか」を推測して
 *   起動前に絶対化していた: is_path_cmd の 16 個ハードコードリスト、
 *   直前トークンが "-n" なら次は絶対化しない、'=' を含むなら key=value
 *   だから絶対化しない、grep の第 1 引数は PATTERN だから素通し… という
 *   ヒューリスティクスの積み重ねで、`wc -l` が "cannot open -l" になったり、
 *   コマンドにオプションを足すたびに sh が壊れる原因になっていた。
 *
 *   cwd はカーネルが持ち、パスを受け取る入口が cwd 起点で解決する。
 *   コマンドは相対パスをそのまま渡してよく、sh は引数の意味を一切解釈しない。
 *   入口は 5 つ: kdir_open / kfs_mkdir / kfs_unlink / kfs_rename /
 *   redir_begin / in_begin と、DRIVER 側の drv_open(user/driver.c)。
 *   ([[commands-access-via-syscall]] の方針どおり、生パスの入口はここだけ)
 *
 *   単一 cwd で足りるのは cd を打つのが sh だけだから。背景ジョブ(&)は
 *   起動時点の cwd で解決されるので、従来(sh が起動時に絶対化)と同じ挙動。
 *
 *   x86-ia16 は builtin シェル(src/sh.c)が自前の cwd と resolve_arg を持ち、
 *   絶対パスで降りてくる。KW_CWD も未定義なので丸ごと除外する。
 * ================================================================== */
#if !defined(ARCH_X86_IA16)

#define KCWD  ((char *)KW_CWD)

/* base(絶対パス)を起点に arg を解決して out(KW_PATH_MAX)へ絶対パスを書く。
 *   ・arg が '/' 始まりなら base を無視してルートから
 *   ・"." は無視、".." は 1 階層戻る(root より上には行かない)
 *   ・余分な '/' は畳む
 *   戻り 0=ok / -1=out に収まらない。src/sh.c の同名関数からの移植。 */
static int path_norm(const char *base, const char *arg, char *out)
{
    unsigned char len;
    const char *p = arg;

    while (*p == ' ') p++;

    if (*p == '/') {                 /* 絶対指定: ルートから組み立て直す */
        out[0] = '/'; out[1] = 0;
        while (*p == '/') p++;
    } else {                         /* 相対指定: base をコピーして継ぎ足す */
        len = 0;
        while (base[len] && len < KW_PATH_MAX - 1) { out[len] = base[len]; len++; }
        out[len] = 0;
    }

    while (*p) {
        const char *s = p;
        unsigned char n = 0;
        while (*p && *p != '/') { p++; n++; }   /* [s, s+n) が 1 セグメント */

        if (n == 1 && s[0] == '.') {
            /* カレント: 何もしない */
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            len = 0; while (out[len]) len++;
            while (len > 0 && out[len - 1] != '/') len--;  /* 末尾セグメントを削る */
            if (len > 1) len--;                            /* 区切り '/' も(root '/' は残す) */
            out[len] = 0;
        } else if (n > 0) {
            len = 0; while (out[len]) len++;
            if ((unsigned int)len + 1 + n > KW_PATH_MAX - 1) return -1;
            if (len == 0 || out[len - 1] != '/') out[len++] = '/';
            { unsigned char k; for (k = 0; k < n; k++) out[len++] = s[k]; }
            out[len] = 0;
        }
        while (*p == '/') p++;
    }

    if (out[0] == 0) { out[0] = '/'; out[1] = 0; }
    return 0;
}

/* 実行中ブロック(プロセス)専用のパススクラッチを返す。slot は 0 か 1。
 *   共有 1 枚にすると「A が解決 → f_open を呼ぶ前にプリエンプトされる →
 *   B が同じバッファを上書き → A が B のファイルを開く」競合が起きる。
 *   パイプ(`cat a | tee b`)や `&` で実際に到達しうるので、スケジューラが
 *   持つ KW_CURRENT(実行中ブロック)で添字して分ける。 */
static char *kpbuf(unsigned char slot)
{
    unsigned char blk = *(volatile unsigned char *)KW_CURRENT;
    if (blk > PROC_BLOCK_MAX) blk = 0;               /* 想定外は block0 枠へ倒す */
    return (char *)(KW_PATHS + (unsigned)blk * KW_PATH_STRIDE
                             + (unsigned)slot * KW_PATH_MAX);
}

/* kpath : in を cwd 起点で解決し、実行中ブロック専用の slot 枠へ書いて返す。
 *   解決できない(長すぎる)場合は in をそのまま返す ── 呼び出し側は従来どおり
 *   FatFs のエラーで落ちる。パス入口の先頭で `p = kpath(p, 0);` と書く。
 *   slot 1 は kfs_rename のように 2 パスを同時に要る呼び出し専用。 */
const char *kpath(const char *in, unsigned char slot) __sdcccall(0)
{
    char *buf;
    if (in == 0) return in;
    buf = kpbuf(slot);
    if (path_norm(KCWD, in, buf) != 0) return in;
    return buf;
}

/* kchdir : cwd を変更する(drv_tbl[42])。実在するディレクトリのときだけ更新。
 *   戻り 0=ok / -1=そんなディレクトリは無い。無引数(空文字)はホーム /root。 */
int kchdir(const char *path) __sdcccall(0)
{
    char *tmp = kpbuf(0);
    unsigned char i;

    while (*path == ' ') path++;
    if (*path == 0) path = "/root";             /* 無引数 cd はホームへ */

    if (path_norm(KCWD, path, tmp) != 0)
        return -1;
    /* 開ければディレクトリ(FAT / DEVFS いずれも kdir_open が透過に見る)。
     * ファイル・存在しないパスはここで弾かれる。 */
    if (kdir_open_abs(tmp) != 0)
        return -1;
    kdir_close();

    for (i = 0; tmp[i] && i < KW_CWD_MAX - 1; i++)
        KCWD[i] = tmp[i];
    KCWD[i] = 0;
    return 0;
}

/* kgetcwd : cwd を out(KW_CWD_MAX 以上)へコピーする(drv_tbl[43])。 */
void kgetcwd(char *out) __sdcccall(0)
{
    unsigned char i;
    for (i = 0; KCWD[i] && i < KW_CWD_MAX - 1; i++)
        out[i] = KCWD[i];
    out[i] = 0;
}

/* 起動時に 1 回。BSS 非クリア環境なので明示初期化する(fat_init から呼ぶ)。 */
void kpath_init(void)
{
    KCWD[0] = '/'; KCWD[1] = 'r'; KCWD[2] = 'o';
    KCWD[3] = 'o'; KCWD[4] = 't'; KCWD[5] = 0;
}
#endif /* !ARCH_X86_IA16 */

int fat_init(void)
{
#if !defined(ARCH_X86_IA16)
    KD->inuse = 0;                          /* RAM ゴミ対策(builtin_init で 1 回)*/
    kpath_init();                           /* cwd = "/root" (BSS 非クリア対策) */
#endif
    return (int)f_mount(&fs, "", 1);        /* mount volume 0 (driveb) now。FR_OK=0 */
}

/* fat_isdir : path(絶対パス)がディレクトリなら 1、さもなくば 0。
 *   sh の cd がターゲットの存在検証に使う。"/" は常に有効。
 *   DIR をスタックに取る(fat_ls と同じ流儀。実績あり)。 */
int fat_isdir(const char *path)
{
    DIR dir;

    if (path[0] == '/' && path[1] == 0)   /* root は常に存在 */
        return 1;
    if (f_opendir(&dir, path) != FR_OK)
        return 0;
    f_closedir(&dir);
    return 1;
}

/* ==================================================================
 * kdir : backend 透過のディレクトリ反復。外部 ls(user/ls.c)が
 *   drv_tbl[21..23] 経由で叩く。同時 1 個(KW_LSDIR に単一状態、上で定義)。
 *   FAT パス   → FatFs f_opendir/f_readdir/f_closedir
 *   /dev(DEVFS) → vfs_dir_next で vtree の子を反復
 *   ルート(FAT)   → FAT の列挙が尽きたら合成ディレクトリ dev を 1 個返す
 *   #76: 全アーキ共通。x86-ia16 / m68k-mega は sysfile.c の readdir 系
 *   syscall から kdir_open_abs 以下を呼ぶ(パスは sh が絶対化済み)。
 *   以前は両アーキとも fsb_*dir を直に叩いていて、DEVFS が開けず、
 *   種別(dir)も捨てていた ── ls の表示がアーキでずれた原因。
 * ================================================================== */

/* kdir_read が最後に読んだ FAT エントリ。kdir_size がサイズを引くため file-scope。
 * カーネル BSS(FATFS fs の後ろ)。FatFs 共有窓 win[512] とは非重複。 */
static FILINFO kdir_fno;

#if !defined(ARCH_X86_IA16)
/* cwd 起点で解決してから開く(コマンドから来る通常の入口)。
 * cwd をカーネルが持つのは z80 だけ(kpath)。 */
int kdir_open(const char *path) __sdcccall(0)   /* 0=ok / -1=失敗 */
{
    return kdir_open_abs(kpath(path, 0));
}
#endif

/* 解決済みの絶対パスを開く。kchdir は自前で正規化した結果を渡すのでこちらを使う
 * (slot 0 を二重に使わないため)。 */
int kdir_open_abs(const char *path) __sdcccall(0)
{
    int v;

    /* 同時 1 個。前の反復が closedir されずに残っている場合(パイプの
     * writer/reader が kdir_read ループ中に kill された等)は、単一インスタンス
     * なので古い状態を回収してから続行する(inuse で弾くと以後 ls が全滅する)。 */
    if (KD->inuse) {
        if (KD->backend == 0)
            f_closedir(&KD->fdir);
        KD->inuse = 0;
    }
    v = vfs_resolve(path);
    if (v == VFS_ENOENT)
        return -1;
    if (v != VFS_FAT) {                          /* DEVFS ディレクトリのみ許可 */
        struct vnode *vn = vfs_node(v);
        if (!vn || vn->type != VT_DIR)
            return -1;
        KD->backend = 1;
        KD->devidx  = (unsigned char)v;
        KD->devnext = 0;
    } else {
        if (f_opendir(&KD->fdir, path) != FR_OK)
            return -1;
        KD->backend = 0;
        /* FAT が尽きたら vtree の root(idx 0)の子 = 合成ノード(/dev)へ続ける。
         * ルート以外は開始位置を VTREE の末尾にしておき、何も出さない。 */
        KD->devidx  = 0;
        KD->devnext = path[1] ? KW_VTREE_N : 0;
    }
    KD->inuse = 1;
    return 0;
}

int kdir_read(char *name) __sdcccall(0)          /* 0=終端 / 1=ファイル / 2=ディレクトリ */
{
    unsigned char i;

    if (!KD->inuse) { name[0] = 0; return 0; }

    if (KD->backend == 0) {
        if (f_readdir(&KD->fdir, &kdir_fno) == FR_OK && kdir_fno.fname[0] != 0) {
            for (i = 0; kdir_fno.fname[i] && i < 12; i++)
                name[i] = kdir_fno.fname[i];
            name[i] = 0;
            return (kdir_fno.fattrib & AM_DIR) ? 2 : 1;
        }
        /* FAT が尽きた。ルートの /dev は FAT 上に無い(vfs の合成)ので、
         * vtree の続きとして返す(#76。以前は ls が引数 "/" のときだけ
         * 足していて、`ls ..` 等で漏れた)。ルート以外は devnext が末尾で
         * vfs_dir_next がすぐ 0 を返す。FF_FS_LOCK=0 なので FAT の DIR は
         * 閉じずに捨ててよい。 */
        KD->backend = 1;
    }
    kdir_fno.fsize = 0;                          /* DEVFS はサイズ無し */
    kdir_fno.fattrib = 0;
    return vfs_dir_next(KD->devidx, &KD->devnext, name);
}

/* kdir_size: 直前の kdir_read が返したエントリのバイト数。
 *   FF_FS_TINY=1 で「ディレクトリ反復中に fopen」が窓破壊を起こすため、
 *   外部 ls -l はここからサイズを得る(ファイルを開かない)。
 *   ディレクトリ / DEVFS は 0。 */
unsigned long kdir_size(void) __sdcccall(0)
{
    if (kdir_fno.fattrib & AM_DIR)
        return 0;
    return (unsigned long)kdir_fno.fsize;
}

void kdir_close(void) __sdcccall(0)
{
    if (KD->inuse && KD->backend == 0)
        f_closedir(&KD->fdir);
    KD->inuse = 0;
}

/* cat は z80 では外部コマンド(CAT.BIN、user/cat.c)。VFS 一本化 Step 8 で
 * 旧 builtin fat_cat を撤去。cat FILE は fopen/fread、cat / cat < FILE は
 * getchar 経由(sh が in_begin で in_on を張ったまま CAT.BIN を前景実行)。 */

/* cp は z80 では外部コマンド(CP.BIN、user/cp.c)。旧 builtin fat_cp は
 * 未登録の dead code だったので削除(VFS 一本化 Step 6)。 */

/* ==================================================================
 * kfs_* : rm / mkdir / mv 用のカーネル入口(drv_tbl[24..26])。
 *   VFS 一本化 Step 9 で fat_rm / fat_mkdir / fat_mv を撤去し、コマンド
 *   (RM.BIN / MKDIR.BIN / MV.BIN)から drv_tbl 経由で叩く形にした。
 *   パスは必ず vfs_resolve を通す(/dev 配下 = 非 FAT は書込み拒否)。
 *   エラー表示はコマンド側の責務なので kprintf しない。
 *   戻り: 0 = OK / 0xFF = permission denied(非 FAT)/ 1..19 = FatFs FRESULT
 *   ※ 0xFF はコマンド側が符号付き比較(掟違反)を避けて denied を判別する
 *     ための番兵。FRESULT は 0..19 なので衝突しない。
 * ================================================================== */
#define KFS_DENY  0xFF

/* x86-ia16 は builtin シェルが絶対パスで降りてくるので解決は不要。
 * KW_PATH* も未定義なので素通しにする。 */
#if !defined(ARCH_X86_IA16)
#define KPATH1(p)  kpath((p), 0)
#define KPATH2(p)  kpath((p), 1)
#else
#define KPATH1(p)  (p)
#define KPATH2(p)  (p)
#endif

int kfs_mkdir(const char *path) __sdcccall(0)
{
    path = KPATH1(path);
    if (vfs_resolve(path) != VFS_FAT) return KFS_DENY;
    return (int)f_mkdir(path);
}

int kfs_unlink(const char *path) __sdcccall(0)
{
    path = KPATH1(path);
    if (vfs_resolve(path) != VFS_FAT) return KFS_DENY;
    return (int)f_unlink(path);
}

int kfs_rename(const char *src, const char *dst) __sdcccall(0)
{
    src = KPATH1(src);                  /* 2 引数なのでスクラッチを分ける */
    dst = KPATH2(dst);
    if (vfs_resolve(src) != VFS_FAT || vfs_resolve(dst) != VFS_FAT) return KFS_DENY;
    return (int)f_rename(src, dst);
}

/* ---- 出力リダイレクト (> file) ---- */
static FIL  redir_fp;
static unsigned char redir_active = 0;

int redir_sink(int c)              /* io.c から直接呼ばれる */
{
    UINT bw;
    char ch = (char)c;
    f_write(&redir_fp, &ch, 1, &bw);
    return c;
}

int redir_begin(const char *fname, unsigned char append)   /* 0=ok, -1=fail */
{
    FRESULT r;
    while (*fname == ' ') fname++;
    fname = KPATH1(fname);          /* cwd 起点で解決(sh は絶対化しない) */
    /* /dev/null は sh 側の discard 経路で捌かれここには来ない。
     * ここへ来る /dev 配下は書けないので弾く(FatFs の生エラーを見せない)。 */
    if (vfs_resolve(fname) != VFS_FAT) {
        kprintf("redir: %s: not a writable file\n", fname);
        return -1;
    }
    /* > : 常に truncate 新規。 >> : 無ければ作り末尾へ seek(FA_OPEN_APPEND)。 */
    r = f_open(&redir_fp, fname,
              append ? (FA_WRITE | FA_OPEN_APPEND) : (FA_WRITE | FA_CREATE_ALWAYS));
    if (r) { kprintf("redir: %s: %d\n", fname, r); return -1; }
    redir_active = 1;
    redir_enable(1);
    return 0;
}

void redir_end(void)
{
    if (redir_active) {
        redir_enable(0);          /* コンソールへ戻す(先に) */
        f_close(&redir_fp);
        redir_active = 0;
    }
}

/* ---- 入力リダイレクト (< file) ----
 * 出力側(redir_*)と完全対称。io.c の kgetchar が in_on フラグを見て
 * in_src() を「直接 call」する。関数ポインタ経由は iy 破壊のため禁止。 */
static FIL  in_fp;
static unsigned char in_active = 0;

int in_src(void)                   /* io.c から直接呼ばれる。-1 = EOF */
{
    UINT br;
    char ch;

    if (f_read(&in_fp, &ch, 1, &br) != FR_OK || br == 0)
        return -1;
    return (int)(unsigned char)ch;
}

int in_begin(const char *fname)    /* 0=ok, -1=fail */
{
    FRESULT r;
    while (*fname == ' ') fname++;
    fname = KPATH1(fname);          /* cwd 起点で解決(sh は絶対化しない) */
    if (vfs_resolve(fname) != VFS_FAT) {    /* < /dev/... は当面非対応 */
        kprintf("in: %s: not a readable file\n", fname);
        return -1;
    }
    r = f_open(&in_fp, fname, FA_READ);
    if (r) { kprintf("in: %s: %d\n", fname, r); return -1; }
    in_active = 1;
    in_enable(1);
    return 0;
}

void in_end(void)
{
    if (in_active) {
        in_enable(0);              /* コンソールへ戻す(先に) */
        f_close(&in_fp);
        in_active = 0;
    }
}

/* echo は z80 では外部コマンド(ECHO.BIN、user/echo.c)。VFS 一本化 Step 8 で
 * 旧 builtin fat_echo を撤去。echo TEXT > FILE は sh が redir_begin で redir_on
 * を張ったまま ECHO.BIN を前景実行し、子の putchar が redir_sink へ落ちる。 */

/* #61/rsyslog: 以前はここを x86-ia16 だけ除外していた。klog_write は
 * カーネル(ROM)側から呼べることに意味がある ── シェルが起動できない
 * 障害でもログが残るため ── ので、全 ARCH で有効にする。使うのは
 * FatFs(x86 も ff.o をリンクしている)と KW_EPOCH_SEC / KW_CURRENT だけで、
 * アーキ依存は無い。 */
/* ---- rsyslog: /var/log/message へ 1 行追記(drv_tbl[46]) ----
 *   "YYYY-MM-DD HH:MM:SS [pid] <msg>\n" を末尾追記(FA_OPEN_APPEND は
 *   無ければ作って末尾へ、既にあれば末尾へ seek)。1 行 256 文字上限。
 *
 *   epoch→日時変換は date.c(旧 builtin fat_date)からの移植。kmem.h の
 *   KW_EPOCH_SEC のコメントどおり、**除算・乗算は使わない**(減算ループの
 *   み)。32bit 除算ランタイムを引くと ROM が NSEC を超えて起動不能になった
 *   実績があるため([[z80-kernel-size-ceiling]])。
 *
 *   "YYYY-MM-DD HH:MM:SS [pid] " の固定部(25B)は 1 回の f_write にまとめる
 *   (1 バイトずつ f_write を呼ぶと呼び出し箇所ぶんコードが太り、0x8000 の
 *   壁を 18B 超えて起動不能になった実績あり)。25B のスタックバッファは
 *   FatFs の必要分(~250B)と合わせても 512B 枠に十分収まる。msg 本体は
 *   コピーせず、呼び出し側のポインタへ直接 f_write する(tizix にメモリ
 *   保護は無く、ここはカーネル文脈なのでそのまま読める)。 */
static const unsigned char klog_days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* 2 桁ゼロパディングでバッファへ書く(0..99)。除算は使わず減算ループのみ。 */
static void klog_p2(char *p, unsigned n)
{
    unsigned d = 0;
    while (n >= 10u) { n -= 10u; d++; }
    p[0] = (char)('0' + d);
    p[1] = (char)('0' + n);
}

#define KLOG_PATH      "/var/log/message"
#define KLOG_PFX_LEN   25
#define KLOG_LINE_MAX  256

void klog_write(const char *msg) __sdcccall(0)
{
    FIL fp;
    FRESULT r;
    UINT bw;
    unsigned long t;
    unsigned days, rem, y, m, d, hh, mm, ss, ydays, dim, yh;
    unsigned char leap, pid, len;
    char pfx[KLOG_PFX_LEN];

    r = f_open(&fp, KLOG_PATH, FA_WRITE | FA_OPEN_APPEND);
    if (r) return;                      /* /var/log 未整備 (mkfatdisk 側の掟) 等 */

    t = *(volatile unsigned long *)KW_EPOCH_SEC;
    days = 0;
    while (t >= 86400UL) { t -= 86400UL; days++; }
    rem = (unsigned)t;
    hh = 0; while (rem >= 3600u) { rem -= 3600u; hh++; }
    mm = 0; while (rem >= 60u)   { rem -= 60u;   mm++; }
    ss = rem;

    y = 1970;
    for (;;) {
        leap = (unsigned char)((y & 3u) == 0);
        ydays = leap ? 366u : 365u;
        if (days < ydays) break;
        days -= ydays; y++;
    }
    m = 1;
    leap = (unsigned char)((y & 3u) == 0);
    while (m <= 12u) {
        dim = klog_days_in_month[m - 1];
        if (m == 2u && leap) dim = 29u;
        if (days < dim) break;
        days -= dim; m++;
    }
    d = days + 1u;

    yh = 0; while (y >= 100u) { y -= 100u; yh++; }
    pid = *(unsigned char *)KW_CURRENT;

    klog_p2(pfx,      yh); klog_p2(pfx + 2,  y);
    pfx[4]  = '-';          klog_p2(pfx + 5,  m);
    pfx[7]  = '-';          klog_p2(pfx + 8,  d);
    pfx[10] = ' ';          klog_p2(pfx + 11, hh);
    pfx[13] = ':';          klog_p2(pfx + 14, mm);
    pfx[16] = ':';          klog_p2(pfx + 17, ss);
    pfx[19] = ' '; pfx[20] = '[';
    klog_p2(pfx + 21, pid);
    pfx[23] = ']'; pfx[24] = ' ';

    f_write(&fp, pfx, KLOG_PFX_LEN, &bw);

    len = 0;                            /* msg 本体は直接 f_write(コピーしない) */
    while (msg && msg[len] && (unsigned)(KLOG_PFX_LEN + len) < KLOG_LINE_MAX - 1)
        len++;
    if (len) f_write(&fp, msg, len, &bw);
    pfx[0] = '\n';
    f_write(&fp, pfx, 1, &bw);

    f_close(&fp);
}

/* df : マウント状態と容量を表示。ドライブ認識/空きの診断器。
 *   f_getfree の FRESULT でマウント健全性が分かる:
 *     no volume (3)  = FR_NOT_READY     … cpmsim に driveb 未接続
 *     no volume (13) = FR_NO_FILESYSTEM … 接続済みだが FAT12 でない
 *     no volume (1)  = FR_DISK_ERR      … I/O エラー(diskio/デブロック)
 *   容量が出れば = マウント成功。ls 無言はルートが空なだけ。
 *
 *   単位: FatFs セクタ=512B。KB = セクタ/2 で 16bit に収める
 *   (バイト表示は driveb 250KB でも 16bit を溢れるため不可)。
 *   n_fatent/csize は 32bit だが値は数千。kprintf(%u) 直前に 16bit へキャスト。
 */
/* kfs_df: 外部コマンド /bin/df(#56)へ容量を渡す。drv_tbl[47]。
 *   sel=0 → 総容量 KB / sel=1 → 空き KB。取れなければ 0xFFFFFFFF。
 *   long を「戻り値で」返す形にしてあるのは、tzcc が long の戻り値(DE:HL)は
 *   扱える(readdir_size で実績)が、long 配列への書き込み先を渡す形は
 *   確かめていないため。f_getfree は FSINFO / キャッシュが効くので 2 回呼んでも軽い。
 *   表示は外部コマンドの仕事なので、ここは数えるだけ(kprintf を持たない)。
 *   1 セクタ 512B なので KB は「セクタ数 / 2」= 1 ビット右シフト。 */
unsigned long kfs_df(unsigned sel) __sdcccall(0)
{
    FATFS *fp;
    DWORD  nclst;

    if (f_getfree("", &nclst, &fp) != FR_OK) return 0xFFFFFFFFUL;
    if (sel == 0) nclst = fp->n_fatent - 2;
    return (nclst * fp->csize) >> 1;
}

/* 旧ビルトイン df(表示までカーネルでやっていた版)。#56 で /bin/df + kfs_df に
 * 置き換えた。参考に残す。 */
#define FAT_DF 0
#if FAT_DF
int fat_df(const char *arg)
{
    FATFS *fp;
    DWORD  nclst;                 /* 空きクラスタ数 */
    DWORD  tot_sect, fre_sect;
    FRESULT r;
    (void)arg;

    r = f_getfree("", &nclst, &fp);
    if (r != FR_OK) {
        kprintf("df: no volume (%d)\n", r);
        return BUILTIN_OK;
    }

    /* 総クラスタ = n_fatent - 2(先頭2エントリは予約)。セクタ換算。 */
    tot_sect = (fp->n_fatent - 2) * fp->csize;
    fre_sect = nclst * fp->csize;

    kprintf("mounted fstype=%u\n", (unsigned int)fp->fs_type);
    kprintf("total %uKB free %uKB\n",
           (unsigned int)(tot_sect / 2), (unsigned int)(fre_sect / 2));
    kprintf("clusters %u csize %u sect\n",
           (unsigned int)(fp->n_fatent - 2), (unsigned int)fp->csize);
    return BUILTIN_OK;
}
#endif

#if defined(ARCH_X86_IA16)
/* ==================================================================
 * シェル行ヒストリ(z80 の user/sh.c #45 と同じ /root/history リング形式)。
 *   z80 は外部コマンドとして自前で FatFs を叩いていたが、こちらは
 *   カーネル内蔵シェル(src/sh.c)なのでカーネル側(ここ)に置く。
 *   src/io.c の readline() が矢印キー(ESC [ A / ESC [ B)で呼ぶ。
 *
 *   [0]              hhead: 次に書くスロット番号 (0..HIST_N-1)
 *   [1]              hcnt : 有効件数 (0..HIST_N)
 *   [2 + k*HIST_SLOT] スロット k: 行バッファ(NUL 終端、後ろはゴミで可)
 *   100 件を超えると最古のスロットを上書きする(ローテーション)。
 *   HIST_SLOT は src/io.c の LINE_MAX(48)と一致必須(hist_add が呼び出し元
 *   のバッファをそのまま HIST_SLOT バイト書き出すため)。
 * ================================================================== */
#define HIST_FILE  "/root/history"
#define HIST_N     100
#define HIST_HDR   2
#define HIST_SLOT  48

static unsigned char h_head = 0, h_cnt = 0;
static unsigned char h_ready = 0;

static FSIZE_t hist_off(unsigned char k)
{
    return (FSIZE_t)HIST_HDR + (FSIZE_t)k * HIST_SLOT;
}

void hist_init(void)
{
    FIL f;
    UINT br;

    h_head = 0; h_cnt = 0; h_ready = 1;
    if (f_open(&f, HIST_FILE, FA_READ) == FR_OK) {
        f_read(&f, &h_head, 1, &br);
        f_read(&f, &h_cnt, 1, &br);
        f_close(&f);
    }
    if (h_head >= HIST_N || h_cnt > HIST_N) { h_head = 0; h_cnt = 0; }
}

void hist_add(const char *line)
{
    FIL f;
    UINT bw;
    unsigned char k;

    if (!h_ready) hist_init();
    if (!line[0]) return;
    k = h_head;
    if (f_open(&f, HIST_FILE, FA_WRITE | FA_OPEN_ALWAYS) != FR_OK)
        return;
    if (f_size(&f) == 0) {               /* 無かった(初回 / rm された)→ 先頭から作り直す。
                                          * 以前は古い h_head のまま先へ書き、手前のスロットが
                                          * 未初期化(ゴミ)のファイルができていた。z80 の
                                          * user/sh.c hist_add と同じ扱い。 */
        k = 0;
        h_cnt = 0;
    }
    h_head = (unsigned char)(k + 1u == HIST_N ? 0 : k + 1u);
    if (h_cnt < HIST_N) h_cnt++;
    f_write(&f, &h_head, 1, &bw);
    f_write(&f, &h_cnt, 1, &bw);
    f_lseek(&f, hist_off(k));
    f_write(&f, line, HIST_SLOT, &bw);   /* line は呼び出し元で HIST_SLOT(=48)確保済み */
    f_close(&f);
}

/* back 個前(1=直前)を buf(HIST_SLOT バイト以上)へ読む。戻り: 文字数(0=無し)。 */
unsigned hist_get(unsigned char back, char *buf)
{
    FIL f;
    UINT br;
    unsigned char k = h_head;
    unsigned n;

    buf[0] = 0;
    if (!h_ready) hist_init();
    if (back == 0 || back > h_cnt) return 0;
    k = (unsigned char)(k < back ? k + HIST_N - back : k - back);
    if (f_open(&f, HIST_FILE, FA_READ) != FR_OK) return 0;
    f_lseek(&f, hist_off(k));
    f_read(&f, buf, HIST_SLOT, &br);
    f_close(&f);
    buf[HIST_SLOT - 1] = 0;
    for (n = 0; buf[n]; n++) ;
    return n;
}

unsigned char hist_count(void)
{
    if (!h_ready) hist_init();
    return h_cnt;
}
#endif /* ARCH_X86_IA16 */
