/* kexec.c - 外部コマンドの fork/ロード (tizix scheduler 版)
 *
 *   検証済み資産(ktest.s / sched_kexit.s)をC に写したもの:
 *     - 再配置は hello の 1 箇所のみ: offset 0x29 の call _main に base 加算。
 *       _printf(0x0003)/_kexit(0x0006) は絶対=カーネル入口、触らない。
 *       データ参照は iy 実行時加算(iy_reg.py) なので触らない。
 *     - 偽コンテキスト seed: ブロック頂上-14B に 7 ワードを積み、restore が
 *       pop iy/ix/hl/de/bc/af → ei → reti でエントリ(base+0x20)へ入る。
 *       crt0cmd の _start が最初に SP=頂上へ張り直すので seed 領域は再利用。
 *
 *   注: ROM 埋め込みイメージ用の kexec(img,len) は cmdimg 廃止に伴い削除。
 *       コマンドは全て FAT 上にあり kexec_file() のみを使う。
 *
 *   PCB は crt0.s / kmem.h と一致(0=free):
 *     pid_tbl[block] @ 0x8400 (1B)   sp_tbl[block] @ 0x8408 (2B)  block 0..7
 */

#include "ff.h"
#include "kexec.h"
#include "kmem.h"
#include "io.h"        /* krun_wait: con_break / kputchar (#35) */

/* ps 用にコマンド名/引数を中央表(KW_CMDNAME/KW_CMDARGS)へ書く。
 *   本家 Unix の p_comm と同じ発想でプロセス自身ではなくカーネル側に持つ ──
 *   ps はスロット番号 n でここを直に覗くだけ。表の置き場所はアーキごとに
 *   kmem.h が決める(z80 は block0 の gap1、m68k-mega は kwork)。書き方は
 *   1 本(#78 で z80 専用だった処理を括り出した)。
 *   cmdname: fname の basename(拡張子除く)。15 文字超は切り詰め。
 *   args   : argpack の先頭 argc トークンをスペース区切りで詰める。
 *            7 文字超は切り詰め。どちらも NUL 終端。
 *   x86-ia16 は kwork に表を持っていないので対象外(#77 で当面リリース外)。
 *   #54: z80board の SD_DEBUG ビルドは ps 自体を落としている(builtin.c
 *   参照、プローブ用 ROM 容量確保のため)ので、使われないこの書き込みも
 *   一緒に外して容量を空ける。 */
#if !defined(ARCH_X86_IA16) && !defined(SD_DEBUG)
static void ps_note(unsigned char n, const char *fname,
                    const char *argpack, unsigned char argc)
{
    {
        unsigned char *cndst = (unsigned char *)KW_CMDNAME + (unsigned)n * KW_CMDNAME_LEN;
        const char *cp, *cbase;
        unsigned char ci = 0;

        cbase = fname;
        for (cp = fname; *cp; cp++)
            if (*cp == '/') cbase = cp + 1;
        while (cbase[ci] && cbase[ci] != '.' && ci < KW_CMDNAME_LEN - 1) {
            cndst[ci] = cbase[ci];
            ci++;
        }
        cndst[ci] = 0;
    }
    {
        unsigned char *ardst = (unsigned char *)KW_CMDARGS + (unsigned)n * KW_CMDARGS_LEN;
        unsigned char ai = 0, seen = 0, adi = 0;
        char c;

        /* argc>0 の呼び出しは常に argpack 非NULL(全呼び出し元で保証、
         * kexec.h の呼び出し規約通り)なので NULL チェックは不要。 */
        while (seen < argc && ai < KW_CMDARGS_LEN - 1) {
            c = argpack[adi++];
            if (c == 0) {
                seen++;
                if (seen >= argc) break;
                c = ' ';
            }
            ardst[ai++] = c;
        }
        ardst[ai] = 0;
    }
}
#endif

#if defined(ARCH_X86_IA16)
/* ---- x86-ia16: セグメント = プロセス基底(Z80 の block と同じ役回り)----
 *   slot n (1..7) ↔ セグメント (n+1)*0x1000。.BIN は pseg:0x0100 (.COM流儀)。
 *   偽コンテキスト(crt0.s _isr08 の復帰 pop 列と一致):
 *     [es][ds][bp][di][si][dx][cx][bx][ax][IP][CS][FLAGS]  = 12 word
 *   初回進入: 全 GP=0, DS=ES=CS=pseg, IP=0x0100, FLAGS=0x0202(IF=1)。
 */
extern void farcpy(unsigned dseg, unsigned doff, const void *src, unsigned n);

unsigned char kexec_file(const char *fname, const char *arg)
{
	/* 256B チャンク: FatFs の "整数セクタは user バッファへ直接 disk_read" 経路を
	 * 避ける(x86 の INT 13h 読みでは win 以外のバッファ宛が不安定だった)。
	 * 常に < 1 セクタ = win キャッシュ経由の memcpy になる。 */
	static char bounce[256];
	FIL fp;
	UINT br;
	unsigned n, pseg, off, i;
	unsigned ctx[12];
	unsigned csp;
	volatile unsigned char *pid = (volatile unsigned char *)KW_PIDTAB;
	volatile unsigned      *spt = (volatile unsigned *)KW_SPTBL;
	volatile unsigned      *sst = (volatile unsigned *)KW_SSTBL;

	if (f_open(&fp, fname, FA_READ) != FR_OK)
		return 0xFF;

	for (n = 1; n < 8; n++)
		if (pid[n] == 0)
			break;
	if (n == 8) { f_close(&fp); return 0; }
	pseg = (n + 1) << 12;

	off = 0x0100;
	for (;;) {
		if (f_read(&fp, bounce, sizeof bounce, &br) != FR_OK || br == 0)
			break;
		farcpy(pseg, off, bounce, br);
		off += br;
	}
	f_close(&fp);

	/* 引数文字列 -> pseg:0xFF00 */
	{
		char a[64];
		i = 0;
		if (arg)
			while (arg[i] && i < 63) { a[i] = arg[i]; i++; }
		a[i] = 0;
		farcpy(pseg, 0xFF00, a, i + 1);
	}

	/* 偽コンテキスト -> pseg:(0xFFF0 - 24) */
	ctx[0] = pseg;      /* es */
	ctx[1] = pseg;      /* ds */
	ctx[2] = 0; ctx[3] = 0; ctx[4] = 0; ctx[5] = 0;
	ctx[6] = 0; ctx[7] = 0; ctx[8] = 0;
	ctx[9]  = 0x0100;  /* IP  = entry */
	ctx[10] = pseg;    /* CS */
	ctx[11] = 0x0202;  /* FLAGS: IF=1 */
	csp = 0xFFF0 - sizeof ctx;
	farcpy(pseg, csp, ctx, sizeof ctx);

	pid[n] = (unsigned char)n;
	spt[n] = csp;
	sst[n] = pseg;          /* ss_tbl[n] = プロセスセグメント */
	return (unsigned char)n;
}

/* #48: src/sh.c は全アーキで kexec_argv を呼ぶ。x86 のユーザー側 crt0 は
 * 生文字列 1 本を argv_init.c で空白区切りに割り直す作りのままなので、ここで
 * トークンを空白で繋いで従来の kexec_file へ渡す(引用符内の空白は保たれない。
 * x86-ia16 は #77 で当面リリース対象外)。 */
unsigned char kexec_argv(const char *fname, const char *argpack, unsigned char argc)
{
	static char joined[128];
	unsigned o = 0;
	unsigned char k;

	for (k = 0; k < argc; k++) {
		if (k && o < sizeof(joined) - 1)
			joined[o++] = ' ';
		while (*argpack) {
			if (o < sizeof(joined) - 1)
				joined[o++] = *argpack;
			argpack++;
		}
		argpack++;                            /* NUL を越えて次のトークンへ */
	}
	joined[o] = 0;
	return kexec_file(fname, joined);
}

unsigned char kload_driver(void)
{
	return 0;      /* x86 に常駐 DRIVER は無い(コマンドは int 0x80 で直接) */
}
#elif defined(ARCH_M68K_MEGA)
/* m68k-mega: #47 で実装、#50 で複数スロット化、**#61 で PIC 化**。
 *
 * 以前は「固定アドレスに固定リンク、スロットごとにベースをずらす」再配置
 * ゼロの構成だった。そのため (a) コマンドごとにスロット数ぶんの .bin を
 * 再リンクする必要があり(ls1.bin/ls2.bin/…)、(b) Makefile の SLOTS と
 * ここの PROC_NSLOT を手で同期させる約束事が残り、(c) 使える番地が
 * リンク時に決まってしまっていた。
 *
 * いまはコマンドを `-mpcrel`(68000 の PC 相対)でコンパイルし、VMA=0 で
 * 1 本だけリンクする(arch/m68k-mega/user/cmd.ld)。**ロード時に像を
 * 書き換えない**ので [[loadtime-reloc-forbidden]] の地雷は踏まない ──
 * 68000 は PC 相対アドレッシングを持つので、z80 の iy_reg のような後処理を
 * せずにコンパイラの正規パスで位置独立コードが出る(#61 の調査で ls.c の
 * 絶対再配置 R_68K_32 x32 が R_68K_PC16 x29 になり、絶対参照が消えることを確認)。
 * crt0cmd.s も手書きなので PC 相対だけで書いてある。
 *
 * 残る制約は PC 相対変位が 16bit = ±32KB であること。像(コード+データ+BSS)が
 * 32KB 以内なら中のどこへでも届く。IMG_BUDGET は 24KB なので制約にならない。
 *
 * 偽コンテキストは crt0.s の irq6_handler/trap0_handler が使う保存形式と
 * 完全に一致させる必要がある(movem.l %d0-%d7/%a0-%a6 の並び + SR:PC)。
 * ここが崩れると起動直後に Address Error 等でハングするので、フィールド
 * 順序を変えたら crt0.s 側も必ず合わせて直すこと。 */
#include "kmem.h"

/* PROC_NSLOT: 同時に動かせる外部コマンド数。**#61 の PIC 化でビルドコストと
 * 無関係になった** ── 以前はスロットを増やすとその数だけ全コマンドを再リンク
 * する必要があったが、いまは .bin が 1 本なので、増やす代償は
 * src/kmem.h の KW_* テーブル(u8[NSLOT+1] / u32[NSLOT+1])と kwork の
 * 大きさだけ。現在は 62 枠 = テーブル 63 エントリ。
 *
 * メモリ側の上限: PROC_BASE(n) = 0x8000 + (n-1)*16KB なので、
 * 62 枠で 0x8000〜0x100000。**実機 SRAM 1MB をちょうど使い切る**。枠数を変える時は
 * src/kmem.h の M68K_NSLOT と KW_* オフセットを一緒に採り直すこと。 */
#define PROC_NSLOT   (M68K_NSLOT - 1)       /* slot 1..62(0 は kernel/shell)。定義は src/kmem.h */
#define PROC_SIZE    0x8000UL              /* 32KB/プロセス */
#define PROC_BASE(n) (0x8000UL + ((unsigned long)(n) - 1UL) * PROC_SIZE)
#define IMG_BUDGET   0x6000UL              /* 24KB。**像 + BSS** の上限(コードだけではない)。
                                           * arch/m68k-mega/user/cmd.ld の ASSERT と一致させること ──
                                           * 超えると crt0 の BSS クリアが kexec の置いた argv[] を
                                           * 消し、コマンドが「引数なし」で起動する(vi で実際に踏んだ)。 */
#define CTX_SIZE     0x42UL                /* D0-D7/A0-A6(60B)+SR(2B)+PC(4B) */

/* #61: カーネル(slot 0)のスタックは link-kernel.ld の __stack_top =
 * 0x100000 から下へ伸びる。最上位スロットの上端がそこへ食い込むと、
 * 症状が「たまに落ちる」形で出て追いにくいので、16KB を予約したうえで
 * 越えたらビルドを止める。 */
#define KSTACK_RESERVE  0x4000UL
typedef char proc_area_fits[(PROC_BASE(PROC_NSLOT) + PROC_SIZE
                             <= 0x100000UL - KSTACK_RESERVE) ? 1 : -1];
#define AV_MAX       32                    /* argv[] 枠数(末尾 NULL 込み) */
/* スタックは [AVPOOL_BASE+AVPOOL_SIZE, PROC_TOP) = 7168B。FatFs の呼び出し
 * 深度に対して十分な余裕を見た(実測: ls / 相当で数百B程度)。
 * #47 バグ修正の教訓: argv[]/pool は **イメージ直後の固定位置**に置き、
 * スタック(PROC_TOP から下へ伸びる)とは完全に分離すること(ブロック
 * 最上端に置くとスタックがそのまま踏み潰す)。偽コンテキスト(66B)だけは
 * PROC_TOP 直下に置く(スタックとして即座に再利用される前提の一時領域)。 */

unsigned char kexec_argv(const char *fname, const char *argpack, unsigned char argc)
{
    FIL fp;
    UINT br;
    unsigned char *pid = (unsigned char *)KW_PIDTAB;
    unsigned long *spt = (unsigned long *)KW_SPTBL;
    unsigned long base, top, avpool_base;
    unsigned char *dst;
    unsigned long *ctx;
    unsigned short *ctx_sr;
    unsigned long  *ctx_pc;
    char     *pool;
    unsigned long *av;
    unsigned char n;
    unsigned di, k, nv;

    for (n = 1; n <= PROC_NSLOT; n++)
        if (pid[n] == 0)
            break;
    if (n > PROC_NSLOT)
        return 0;                          /* 空きスロット無し */


    base = PROC_BASE(n);
    top  = base + PROC_SIZE;
    avpool_base = base + IMG_BUDGET;
    dst    = (unsigned char *)base;
    ctx    = (unsigned long *)(top - CTX_SIZE);
    ctx_sr = (unsigned short *)(top - CTX_SIZE + 60);
    ctx_pc = (unsigned long  *)(top - CTX_SIZE + 62);
    pool   = (char *)(avpool_base + AV_MAX * 4);
    av     = (unsigned long *)avpool_base;

    if (f_open(&fp, fname, FA_READ) != FR_OK)
        return 0xFF;
    if ((unsigned long)f_size(&fp) > IMG_BUDGET) {
        f_close(&fp);
        return 0;
    }
    f_read(&fp, dst, (UINT)IMG_BUDGET, &br);
    f_close(&fp);

    /* argv[] 配列 + トークン文字列プール(pool の残り容量は十分大きい:
     * IMG_BUDGET〜PROC_TOP の間 0x2000B から AV_MAX*4+CTX_SIZE を引いても
     * 700B超)。 */
    nv = argc;
    if (nv > AV_MAX - 1) nv = AV_MAX - 1;
    di = 0;
    for (k = 0; k < nv; k++) {
        av[k] = (unsigned long)(pool + di);
        if (argpack)
            while (argpack[di] && di < 0x2C0) { pool[di] = argpack[di]; di++; }
        pool[di++] = '\0';
    }
    av[nv] = 0;

    /* 偽コンテキスト: crt0.s の movem 復元順(D0..D7,A0..A6)と一致させる。 */
    for (k = 0; k < 15; k++)
        ctx[k] = 0;
    ctx[0] = nv;                            /* D0 = argc */
    ctx[8] = (unsigned long)av;             /* A0 = &argv[0] */
    *ctx_sr = 0x2000;                       /* S=1, 割込みマスク=0 */
    *ctx_pc = base;                         /* PC = crt0cmd _start */

    ps_note(n, fname, argpack, (unsigned char)nv);   /* #78: z80 と同じ ps 表示 */

    spt[n] = top - CTX_SIZE;
    PROC_CLEAR_BLOCKED(n);                  /* #72 */
    /* #82: 出力ルートを CONSOLE(0)に戻す(z80 の kexec と同じ)。前の占有者が
     * パイプの writer だった枠を再利用すると、出力がパイプへ流れてしまう。 */
    ((volatile unsigned char *)KW_OUTROUTE)[n] = 0;
    pid[n] = n;
    return n;
}

/* #48: src/sh.c は引数をトークンに割って kexec_argv を呼ぶ(上)。ここは
 * 旧 2 引数入口で、arg 全体を 1 トークンとして argv に載せる。 */
unsigned char kexec_file(const char *fname, const char *arg)
{
    return kexec_argv(fname, arg, (arg && arg[0]) ? 1 : 0);
}

unsigned char kload_driver(void)
{
    return 0;      /* m68k-mega に常駐 DRIVER は無い(x86 と同じ) */
}
#else

#define PIDTBL  ((volatile unsigned char *)0x8400)   /* pid_tbl[n]  0=free    */
#define SPTBL   ((volatile unsigned int  *)0x8408)   /* sp_tbl[n]   saved SP  */
#define OUTROUTE ((volatile unsigned char *)0x851A)  /* out_route[n] (kmem.h) */
#define BLKBASE(n) (0x8000u + (unsigned)(n) * 0x1000u)

#define PID_DRIVER   0xFE    /* block1 常駐ドライバ予約(kmem.h/crt0.s と一致) */
#define PID_CONT     0xFD    /* 4KB 超プロセスの継続ブロック(kmem.h/crt0.s と一致) */
/* ブロック割当の方針(kmem.h の PROC_BLK_* が唯一の定義場所)。
 *   **1 プロセスが取れるブロック数に政策上の上限は置かない。** 必要なだけ
 *   要求でき、連続した空きが無ければそれをもってエラーを返す。固定上限を
 *   置くと載るメモリが増えたときに追従できず、環境の伸縮性が失われる。
 *   以前ここには MAX_PROC_BLK=4 という定数があり、さらに要求超過を黙って
 *   切り詰めていた。要求より狭い枠で起動して隣のプロセスを踏むので、
 *   エラーを返すより悪い挙動だった。 */

#define CMD_ENTRY   0x20     /* crt0cmd _start = base + 0x20 */
#define RELOC_OFF   0x29     /* 旧 iy_reg bin のフォールバック単一 reloc    */
#define CTX_WORDS   7        /* iy,ix,hl,de,bc,af,PC                       */

/* z80board 実機プローブ [k2]〜[k6](2026-09-19 のブリングアップで使用)。
 * sh.bin のロード各段(サイズ・ブロック・読込バイト数・sp/pc・入口の実 RAM
 * 読み戻し)を出す。再度使う時は 1 に。ROM が 0x8000 ハード上限なので、
 * 足りなければ他の機能を一時的に外すこと。 */
#define KEXEC_PROBE 0
#define BLK_SIZE    0x1000u

/* kexec_argv: FAT 上の fname をロードして起動。argv[] を渡せる版。
 *   イメージが 1 ブロック(4KB)に収まらなければ、空いている連続ブロックを
 *   必要数だけ 1 プロセスに割り当てる(先頭ブロックの位置は固定ではなく、
 *   最初に見つかった連続空き run の先頭)。2 個目以降は pid_tbl[]=PID_CONT
 *   でマークし、先頭ブロックと同時に解放する。IY は先頭ブロック base のまま
 *   (iy_reg 変換は無改造。IY+offset はプロセス空間内で 64K 桁上がりしない)。
 *
 *   argpack = NUL 区切りトークン列 "tok0\0tok1\0…"(argc 個)。sh がパスの
 *   絶対化・クォート処理まで済ませて渡す。argc==0 なら argpack は無視。
 *
 *   プロセス最終ブロック(top = base + nblk*0x1000)への配置:
 *     [top-0x140, top-0x100)  argv[] 絶対ポインタ配列(32 枠、末尾 NULL)
 *     [top-0x100, top-0x12)   トークン文字列プール(argpack をそのままコピー)
 *     [top-0x0E,  top)        偽コンテキスト seed(7 word)
 *   crt0cmd _start は SP=top-0x140、HL=argc / DE=&argv[0] / BC=nblk を受け取る。
 *
 * 戻り: 2..7=先頭block, 0=連続空き無し/サイズ過大, 0xFF=ファイル無し */
unsigned char kexec_argv(const char *fname, const char *argpack, unsigned char argc)
{
    unsigned char n, k, nblk, span, nv, xblk;
    unsigned base, sp, top, imgtop;
    unsigned long fsz, lim;
    UINT br;
    FIL fp;
    volatile unsigned *ctx;
    unsigned char hdr[HDR_SIZE];

    if (f_open(&fp, fname, FA_READ) != FR_OK)
        return 0xFF;
#if KEXEC_PROBE && defined(ARCH_Z80BOARD)
    kprintf("[k2 sz=%u]", (unsigned)f_size(&fp));
#endif

    /* #38: 先頭 32B の予約ヘッダを読む(crt0 の `.ds 0x20`)。
     *   'T','Z' のマグマークがあれば hdr[HDR_XBLK] が「像とは別に欲しい
     *   ブロック数」。大きな作業領域(vi の本文バッファ等)を像に含めずに
     *   確保するための宣言 ── 像に持たせるとファイルが太り、ロードも遅く、
     *   何より 4KB 単位の切り上げで無駄が出る。
     *   マグ無し(= 従来の .BIN は全部ゼロ)なら xblk=0 で従来どおり。
     *   ここは **シークしない**: 読んだ 32B はそのまま base へ置き、続きを
     *   base+32 から読む(FDC は巻き戻しに弱い。[[cpmsim-di-fatfs-hang]])。 */
    br = 0;
    f_read(&fp, hdr, HDR_SIZE, &br);
    xblk = 0;
    if (br == HDR_SIZE && hdr[HDR_MAG0] == 'T' && hdr[HDR_MAG1] == 'Z') {
        xblk = hdr[HDR_XBLK];
        /* ★ここで切り詰めない。載らなければ下で連続空き探索が失敗し、
         * kexec_argv が 0 を返して sh がエラーを出す。 */
    }

    /* 必要ブロック数 = ceil((size + 予約) / 4KB)。
     *   予約 0x140 = 最終ブロック上端 [top-0x140, top) の argv[] 配列 + 文字列
     *   プール + 偽コンテキスト seed、および crt0 _start が張る初期 SP(=top-0x140)分。
     *   これを足さないとイメージ末尾(_DATA)がこの領域を踏み潰す。ちょうど
     *   3777..4096B 級のコマンド(iy_reg 由来で肥大しがちな tzcc 製など)で表面化し、
     *   argv 文字列やスタックが壊れてハング/Op-code trap になる。
     *
     *   ★以前は「上限固定の比較ラダー」で求めていたが、上限をコードに
     *     焼き込むと枠数が増えたときにここも直す必要があり、直し忘れると
     *     静かに頭打ちになる。4KB ずつ足す素直なループへ変えた ── z80 に
     *     除算命令が無いのでラダーにしていた事情はループでも満たせる
     *     (加算と比較だけ)。**枠数が変わってもこの関数は無修正**。 */
    fsz  = f_size(&fp) + 0x140UL;
    nblk = 1;
    lim  = (unsigned long)BLK_SIZE;
    while (lim < fsz && nblk < PROC_BLK_N) { lim += (unsigned long)BLK_SIZE; nblk++; }
    if (lim < fsz) { f_close(&fp); return 0; }   /* 物理枠(6 個)に載らない */

    /* 像が占めるブロック数を控えてから、追加ブロックを足す。
     * 追加分は **像の後ろ**に付く(base + imgtop から)。
     * xblk は切り詰めない。載らなければここでエラーにする。 */
    imgtop = nblk * BLK_SIZE;
    nblk = nblk + xblk;
    if (nblk > PROC_BLK_N) { f_close(&fp); return 0; }   /* 物理的に載らない */

    /* プロセス枠(block2 以降)を走査し、pid_tbl==0 が nblk 個連続する
     * 最初の場所を先頭ブロック n とする(1 個だけなら従来どおり先頭空き)。 */
    span = 0;
    n = PROC_BLK_HI;
    for (k = PROC_BLK_LO; k < PROC_BLK_HI; k++) {
        if (PIDTBL[k] == 0) {
            span++;
            if (span == nblk) { n = k + 1 - nblk; break; }
        } else {
            span = 0;
        }
    }
    if (n == PROC_BLK_HI) { f_close(&fp); return 0; }   /* nblk 連続の空きが無い */

    base = BLKBASE(n);
    top  = nblk * BLK_SIZE;                   /* プロセス空間サイズ(base+top=上端) */
#if KEXEC_PROBE && defined(ARCH_Z80BOARD)
    kprintf("[k3 blk=%u n=%u]", (unsigned)nblk, (unsigned)n);
#endif
    {
        unsigned char *dst = (unsigned char *)base;
        /* 先に読んだ 32B のヘッダを置き、続きをその後ろへ。シークしない。 */
        for (k = 0; k < HDR_SIZE; k++)
            dst[k] = hdr[k];
        f_read(&fp, dst + HDR_SIZE, imgtop - HDR_SIZE, &br);
#if KEXEC_PROBE && defined(ARCH_Z80BOARD)
        kprintf("[k4 br=%u]", (unsigned)br);   /* 実際に読めたバイト数 */
#endif
        /* #38: 追加ブロックは初期化しない(使う側が自分で埋める)。
         * ゼロクリアすると 4KB の memset がロード毎に乗るため。 */

        /* ロード時情報をヘッダの空き部へ書き戻す。プログラムはここを読んで
         * 自分の作業領域を知る(getxbase() = base + [HDR_IMGTOP])。
         * ヘッダ 0x00-0x1F は crt0 の `.ds 0x20` で誰も使っていない死に領域。 */
        dst[HDR_IMGTOP]     = (unsigned char)(imgtop & 0xFF);
        dst[HDR_IMGTOP + 1] = (unsigned char)(imgtop >> 8);
        dst[HDR_NBLK]       = nblk;
    }

    /* ps 用のコマンド名/引数(ps_note、ファイル先頭参照)。 */
#ifndef SD_DEBUG
    ps_note(n, fname, argpack, argc);
#endif
    f_close(&fp);

    /* アドレスパッチ(reloc)は完全廃止。IYレジスタをベースアドレスとして実行する */

    /* --- argv[] 配列 + トークン文字列プールを最終ブロックへ構築 ---
     *   pool と argpack は同一レイアウト(NUL 区切り)なので、単一 index di で
     *   両者を歩けば良い。pool は 0xEE B 上限で切り詰め(LINE_MAX=48 由来なら十分)。 */
    {
        char     *pool = (char *)(base + top - 0x100);
        unsigned *av   = (unsigned *)(base + top - 0x140);
        unsigned  di   = 0;

        nv = argc;
        if (nv > 31) nv = 31;                 /* argv[] は 32 枠(末尾 NULL 込み) */
        for (k = 0; k < nv; k++) {
            av[k] = (unsigned)(pool + di);
            if (argpack) {
                while (argpack[di] && di < 0xEE) {
                    pool[di] = argpack[di];
                    di++;
                }
            }
            pool[di++] = '\0';               /* このトークンを終端(argpack と同レイアウト) */
        }
        av[nv] = 0;                          /* argv[argc] = NULL */
    }

    /* 偽コンテキスト(最終ブロック上端 -14)。crt0cmd _start が
     * HL=argc / DE=&argv[0] / BC=nblk を受け、SP を最終ブロック側へ張り直す。 */
    sp  = base + top - CTX_WORDS * 2;
    ctx = (volatile unsigned *)sp;
    ctx[0] = base;                 /* iy = base */
    ctx[1] = 0;                    /* ix */
    ctx[2] = nv;                   /* hl = argc */
    ctx[3] = base + top - 0x140;   /* de = &argv[0](絶対) */
    /* #38: crt0 へ渡すのは **合計ブロック数**(従来どおり)。
     *   一度「追加ブロックを丸ごと空けたいから像のブロック数を渡す」形にしたが、
     *   それだと SP が像の直上に来て、像が上限ギリギリのとき **スタックが
     *   数十バイトしか残らない**(vi で実測 28B → 即破壊)。invariant は単純な
     *   方がよい: **SP と argv[] は常にプロセス最上端**。追加ブロックは
     *   「像とスタックの間に増えた空き」で、使う側は従来と同じ規則
     *   ([[kexec-block-headroom]])で上端に余白を残す。 */
    ctx[4] = nblk;                 /* bc = 合計ブロック数 */
    ctx[5] = 0;                    /* af */
    ctx[6] = base + CMD_ENTRY;     /* PC = base + 0x20 */
    SPTBL[n]  = sp;
    OUTROUTE[n] = 0;
#if KEXEC_PROBE && defined(ARCH_Z80BOARD)
    /* m= は入口(base+0x20)の先頭 2 バイトを実 RAM から読み戻した値。
     * crt0sh の _start 先頭と一致すれば像は正しく RAM に載っている
     * (push de / pop ix なら 213,221。S プローブ有効時は 62,83)。 */
    kprintf("[k5 sp=%u pc=%u m=%u,%u]", (unsigned)sp, (unsigned)ctx[6],
            (unsigned)*(volatile unsigned char *)(base + CMD_ENTRY),
            (unsigned)*(volatile unsigned char *)(base + CMD_ENTRY + 1));
#endif
    PROC_CLEAR_BLOCKED(n);                      /* #72: 前の占有者の park 状態を持ち越さない */
    PIDTBL[n] = n;                              /* 先頭ブロック = 実 PID */
    for (k = 1; k < nblk; k++)
        PIDTBL[n + k] = PID_CONT;               /* 継続ブロックを予約 */
#if KEXEC_PROBE && defined(ARCH_Z80BOARD)
    kprintf("[k6]");                            /* PCB 登録完了 = スケジューラ待ち */
#endif
    return n;
}

/* kexec_file: 旧 2 引数入口。arg 全体を 1 トークンとして argv 化する
 *   (init.c の sh 起動など、後方互換用)。 */
unsigned char kexec_file(const char *fname, const char *arg)
{
    return kexec_argv(fname, arg, (arg && arg[0]) ? 1 : 0);
}

/* ------------------------------------------------------------------
 * krun_wait: 子を起動して **終了まで待つ**(同期実行)。#35
 *
 *   これまで kexec_argv を呼べたのは sh だけで、普通のコマンドは子を持てなかった
 *   (doc の「機能分割 + exec」が未実装だった理由)。待ちループを各コマンドに
 *   書かせると PIDTBL の絶対番地と Ctrl+C 処理が全員にコピーされるので、
 *   カーネル側に 1 本だけ置く。sh の前景待ちと同じ作り。
 *
 *   tizix にメモリ保護は無い(意図的)。よって **子は親のバッファを絶対アドレスで
 *   直接書ける** ── 大きなプログラムを「本体 + コマンド」に割り、重い処理だけを
 *   子プロセスへ出して同じバッファを更新する構成が取れる。アドレスは argv に
 *   10 進文字列で渡す(argv は NUL 区切りの文字列列なので数値は文字列で運ぶ)。
 *
 *   注意: 子の .BIN は毎回 FAT から読み込まれる。1 打鍵ごとに呼ぶような
 *   使い方はしないこと(実機のフロッピでは数十 ms 掛かる)。
 *
 *   戻り: 1=正常に起動して終了した / 0=空きブロック不足 / 0xFF=ファイル無し
 * ------------------------------------------------------------------ */
unsigned char krun_wait(const char *fname, const char *argpack, unsigned char argc)
{
    unsigned char n;

    n = kexec_argv(fname, argpack, argc);
    if (n == 0 || n == 0xFF)
        return n;                       /* 空き無し / ファイル無し */

    while (PIDTBL[n] != 0) {            /* 前景待ち(sh の exec_external と同じ)*/
        if (con_break()) {              /* Ctrl+C → 子を刈る */
            PIDTBL[n] = 0;
            kputchar('\n'); kputchar('^'); kputchar('C'); kputchar('\n');
        }
    }
    return 1;
}

/* ------------------------------------------------------------------
 * kload_driver: DRIVER.BIN を block1(0x9000) へロードする。
 *
 *   常駐ドライバは外部コマンドと違い:
 *     - 再配置しない。0x9000 固定基点でリンクされる(reloc 不要)。
 *       DRIVER 先頭は 16B 間隔のジャンプベクタ表(BIOS チック):
 *         0x9000 jp <putc実処理>   ; ベクタ0
 *         0x9010 jp <getc実処理>   ; ベクタ1
 *         0x9020 ...               ; 将来
 *       hello 等ユーザーコードはこの固定番地を叩く。実処理の飛び先は
 *       DRIVER 内部の成り行き。根っこは kputchar(0x003E)/kgetchar(0x0041)。
 *     - スケジューラに載せない。呼ばれたら jp してくるだけの飛び地。
 *       PCB(block1)は crt0.s が PID_DRIVER で予約済み。ここでは触らない。
 *     - iy を使わない。コンテキストを持たないので seed も張らない。
 *
 *   シェル起動時(builtin_init)に一度だけ呼ぶ。
 *   戻り: 0=成功, 0xFF=DRIVER.BIN が無い/読めない。
 *   失敗時、呼び出し側(builtin_init)はエラー表示して停止する。
 *   フォールバック(層1 素通しで埋める)は持たない ── 起動時にドライバが
 *   無いのは構成不備なので、黙って素通しさせず明示エラーにする。
 * ------------------------------------------------------------------ */
#define DRIVER_BLOCK  1
#define DRIVER_BASE   0x9000u

unsigned char kload_driver(void)
{
    UINT br;
    FIL fp;
    FRESULT fr;
    unsigned char *dst = (unsigned char *)DRIVER_BASE;

    /* #92: DRIVER は fd_table(0x8600)を「fd_inited(0x86F0)== 0x5A なら初期化済み」で判定する。
     * 電源投入時の RAM は不定(cpmsim も乱数で埋める)なので、約 1/256 でゴミが 0x5A になり、
     * fd_table のゴミがそのまま使われて全 fd が「使用中」→ その起動ではどのファイルも開けなかった。
     * DRIVER を載せるたびに未初期化へ戻す。 */
    *(volatile unsigned char *)0x86F0 = 0;

    fr = f_open(&fp, "/bin/driver.bin", FA_READ);
    if (fr != FR_OK)
        return 0xFF;

    br = 0;
    f_read(&fp, dst, 0x1000, &br);   /* block1 全域(<=4KB)へ生ロード。reloc 無し */
    f_close(&fp);

    if (br == 0)                      /* 0 バイト = 実体が無い(壊れ) */
        return 0xFF;

    /* #57: ROM(カーネル)と SD(DRIVER.BIN)の版ずれ検出。
     *   DRIVER.BIN は **このカーネルの kernel.map の番地へ -g で直接リンク**
     *   される(user/Makefile FS_SYMS)。カーネルだけ焼き直して SD を古いまま
     *   にすると、DRIVER は新しいカーネルの別の関数の途中を呼び、「ls が動いた
     *   直後に sh: ls: not found」のように無言でおかしくなる(z80board 実機で実害)。
     *   drv_tbl の後半はカーネル関数の番地そのものなので、ロード直後に 1 つ
     *   突き合わせれば食い違いが分かる。比べるのは con_break(drv_tbl[44]):
     *   **最後にリンクされる io.lib の中**にあるので、それより前のどこかが
     *   1 バイトでもずれれば番地が変わる。ビルド ID を刻む仕組みは要らない。
     *   食い違っても止めない(止めると SD を直す手段も無くなる)。警告だけ出す。
     *   ※ sh.bin や tzcc のコマンドは固定ベクタと drv_tbl しか使わないので
     *     カーネルの番地には依存しない = DRIVER.BIN だけ見れば足りる。 */
    if (((unsigned *)DRIVER_BASE)[44] != (unsigned)con_break)
        kprintf("driver.bin != ROM\n");   /* 短いのは z80board の ROM 残量のため */

    return 0;                         /* 成功。PCB は crt0.s で PID_DRIVER 予約済み */
}

#endif /* ARCH_X86_IA16 */
