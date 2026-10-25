/* arch/x86-i386-unreal/sysfile.c  --  INT 80h システムコールの C ディスパッチャ
 *   crt0.s の _isr80 から sys_call(ax, bx, cx, si, di, cseg) が呼ばれる。
 *     ax = AH<<8 | AL(番号 + 小引数)
 *     bx/cx/si/di = 呼び出し時のレジスタ
 *     cseg        = 呼び出し元プロセスのセグメント
 *   ポインタ引数(path/buf)は cseg 側にあるので farcpy/farcpy_in で移送する。
 *
 *   FS 操作は fsbackend.h(fsb_*)経由。この層の責務はセグメント跨ぎの移送と
 *   ユーザー fd ↔ backend ハンドル / DEVFS 擬似 fd の対応付けだけ。
 *
 *   番号:
 *     1 putchar(AL)              -> AL
 *     2 getchar                  -> AL
 *     3 getticks                 -> AX
 *     4 open(SI=path, AL=flags)  -> fd(0..) / 0xFFFF   flags: bit0=1 で write(新規), 0 で read
 *     5 close(AL=fd)             -> 0
 *     6 read(BX=fd, DI=buf, CX=n)   -> 実読みバイト数
 *     7 write(BX=fd, DI=buf, CX=n)  -> 実書きバイト数
 *     8 opendir(SI=path)         -> dh / 0xFFFF
 *     9 readdir(BX=dh, DI=name13)-> 1(エントリあり) / 0(終端)
 *    10 closedir(AL=dh)          -> 0
 *    11 unlink(SI=path)          -> 0 / 0xFFFF
 *    12 readdir_size()           -> 直前 readdir エントリのサイズ下位16bit
 *                                    (tizix の全コマンドは 64KB 未満ファイル前提。
 *                                    z80 の readdir_size() drv_tbl[38] と同じ約束)
 */
#include "fsbackend.h"  /* fsb_* : FS backend 界面 */
#include "io.h"         /* kputchar / kgetchar */
#include "kernel.h"     /* getticks */
#include "kmem.h"       /* KW_TICKS */
#include "pipe.h"       /* pipe_tail(case 21) */
#include "vfs.h"
#include "builtin.h"   /* builtin_try / builtin_is(mcpd の run) */
#include "fatcmd.h"   /* klog_write(case 18) */        /* vfs_resolve : パス系入口の backend 振り分け */
#include "dev.h"        /* DEVFS ノードの dev_ops 経由ディスパッチ(/dev/null,/dev/fda 共通) */

extern void klog_write(const char *s);

extern void farcpy(unsigned dseg, unsigned doff, const void *src, unsigned n);
extern void farcpy_in(void *dst, unsigned sseg, unsigned soff, unsigned n);

#define NUFD    6
#define PATHMAX 64
#define IOCHUNK 128

/* ユーザー fd 表。kind: 0=空き / 1=FAT(bh=backend ハンドル) / 2=DEVFS(ops 経由) */
static unsigned char ufd_kind[NUFD];
static int           ufd_bh[NUFD];
static const struct dev_ops *ufd_ops[NUFD];
static unsigned seek_hi = 0;

extern int con_rx_ready(void);   /* console.c(同アーキ内)。物理層の RX-ready */
extern int con_pending(void);    /* src/io.c: 戻しバッファに 1 バイトあるか */

static int con_raw_flag;

static char pathbuf[PATHMAX];
static char pathbuf2[PATHMAX];
static char iobuf[IOCHUNK];
static char namebuf[13];
static void x86_proc_release(unsigned char n)   /* src/kernel.c の proc_release は PLAT_FLAT32 専用なので、ここに同じものを持つ */
{
	extern unsigned char kwork[];
	extern unsigned x86_nslots(void);
	volatile unsigned char *pid = (volatile unsigned char *)kwork;
	unsigned ns = x86_nslots();
	if (n == 0 || n >= ns)
		return;
	pid[n] = 0;
	while (++n < ns && pid[n] == PID_CONT)
		pid[n] = 0;
}
static char pkbuf[160];          /* syscall 22 / 29 の引数パック(NUL 区切りの語 + 終端 NUL。sh の argpack は 144B まで) */
static char pkbuf2[104];         /* syscall 29(krun_pipe)の左辺の引数パック(sh の argpackL は 96B まで) */
extern unsigned char x86_exitcode[];   /* crt0.s: スロットごとの終了コード */
extern unsigned char kexec_argv(const char *fname, const char *argpack, unsigned char argc);
extern int builtin_is(const char *cmd);
extern unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                               const char *rn, const char *rp, unsigned char rc);
extern int con_break(void);
extern void kgetcwd(char *out);
extern int kchdir(const char *path);

#ifdef X86_EXTSH
/* 外部シェル(/bin/sh.bin)は引数を解釈せずに渡すので、相対パスはカーネルが cwd 起点で絶対化する(src/fatcmd.c の kpath。
 *   他の gcc 系ポートと同じ)。結果は実行中スロット専用の枠に入るので、pathbuf / pathbuf2 へ写し直す。 */
extern const char *kpath(const char *in, unsigned char slot);
static void abs_into(char *buf, unsigned char slot)
{
	const char *r = kpath(buf, slot);
	unsigned i;
	if (r != buf) {
		for (i = 0; i < PATHMAX - 1 && r[i]; i++)
			buf[i] = r[i];
		buf[i] = 0;
	}
}
#endif

static void get_path(unsigned cseg, unsigned off)
{
	unsigned i;
	farcpy_in(pathbuf, cseg, off, PATHMAX);
	for (i = 0; i < PATHMAX; i++)
		if (pathbuf[i] == 0)
			break;
	if (i == PATHMAX)
		pathbuf[PATHMAX - 1] = 0;
#ifdef X86_EXTSH
	abs_into(pathbuf, 0);
#endif
}

static void get_path2(unsigned cseg, unsigned off)
{
	unsigned i;
	farcpy_in(pathbuf2, cseg, off, PATHMAX);
	for (i = 0; i < PATHMAX; i++)
		if (pathbuf2[i] == 0)
			break;
	if (i == PATHMAX)
		pathbuf2[PATHMAX - 1] = 0;
#ifdef X86_EXTSH
	abs_into(pathbuf2, 1);
#endif
}

extern unsigned char kwork[];   /* crt0.s: pid_tbl[0..7] を先頭に持つ */

/* 0000:off の 1 語を読む(stage2 が置いたメモリ情報)。ES は保存して戻す。 */
unsigned x86_lowword(unsigned off)
{
	unsigned v;
	__asm__ volatile (
		"push %%es\n\t"
		"xor %%ax, %%ax\n\t"
		"mov %%ax, %%es\n\t"
		"mov %%es:(%%bx), %%ax\n\t"
		"pop %%es"
		: "=a"(v) : "b"(off) : "memory", "cc");
	return v;
}

/* 使うプロセススロット数(slot0 = カーネル/シェル を含む)。メモリ(4KB ブロック)の量から決める:
 * 2 + ブロック数/4、最大 X86_MAXSLOT。ブロックの少ない(メモリの小さい)機械ではスロットも減る。 */
extern unsigned x86_blkinfo(unsigned sel);
extern unsigned x86_pool_blocks(void);
extern unsigned x86_scstk_headroom(void);
extern unsigned x86_kstack_headroom(void);
extern unsigned char x86_xmem_ok;
/* 割り込み受信のシリアルドライバ(uart.s)。n = 0: COM1 / 1: COM2 */
extern unsigned uart_hw_open(unsigned n, unsigned div, unsigned mcr);
extern unsigned uart_avail(unsigned n);
extern int uart_getb(unsigned n);
extern void uart_putb(unsigned n, unsigned c);
extern unsigned uart_msr(unsigned n);
extern int con_serial_state(void);   /* console.c: コンソールが COM1 を使っているか */

unsigned x86_nslots(void)
{
	static unsigned n;
	if (!n) {
		unsigned t = x86_pool_blocks() / 4u + 2u;   /* blkinfo(0) だと blk_used → x86_nslots で再帰する */
		n = t > X86_MAXSLOT ? X86_MAXSLOT : t;
	}
	return n;
}

unsigned sys_call(unsigned ax, unsigned bx, unsigned cx,
                  unsigned si, unsigned di, unsigned cseg)
{
	unsigned char fn = (unsigned char)(ax >> 8);
	unsigned al = ax & 0xFF;

	switch (fn) {
	case 1:
		kputchar((int)(unsigned char)al);
		return al;
	case 2: {
		/* EOF(パイプの終端 / < FILE の終端)は -1。以前は unsigned char に切り詰めて 0xFF にしており、
		 * cat がパイプ後段で EOF を認識できず 0xFF を垂れ流した。0xFFFF で返す。 */
		int gc = kgetchar();
		return gc < 0 ? 0xFFFFu : (unsigned)(unsigned char)gc;
	}
	case 3:
		/* sti は入れない: syscall は単一の sc_stack で IF=0 のまま走る。ここで sti すると
		 * タイマ ISR が syscall 途中で別プロセスへ切り替え、そのプロセスの syscall が
		 * sc_stack を上書きして元の syscall フレームが壊れる(a & / b & が B しか出ない症状)。
		 * ticks はユーザコード走行中(IF=1)に進むので、sti 無しでも delay ループは進む。 */
		/* 共有の getticks() は中で KYIELD(x86 では sti;hlt;cli)するので syscall 内では使えない
		 * (IF=1 になって別プロセスへ切り替わり、単一の sc_stack が衝突する)。tick を直接読む。 */
		return *(volatile unsigned int *)KW_TICKS;
	case 16:
		return (unsigned)time_get();          /* 下位 16 ビット。上位は 55 */
	case 55:
		return (unsigned)(time_get() >> 16);  /* time_get の上位 16 ビット(syscall の戻りは 16 ビット) */
	case 54:
		/* time_set(sec): 下位 = bx、上位 = cx。ntpdate / sntp が使う */
		time_set(((unsigned long)cx << 16) | (unsigned long)bx);
		return 0;
	case 17:
		/* input_ready */
		/* con_break(外部シェルの前景待ち)が Ctrl+C の確認で取り出した 1 文字は io.c の戻しバッファ(con_ung)に
		 * 入っているので、そちらも見る(見ないと kbhit が偽になり、入力が次の 1 文字まで遅れる)。 */
		return (unsigned)((con_rx_ready() || con_pending()) ? 1 : 0);
	case 35:
		/* con_raw(on) */
		con_raw_flag = (int)al;
		return 0;


	case 4: {
		unsigned u;
		int r, bh;
		get_path(cseg, si);
		/* VFS 一本化: パスは必ず vfs_resolve を通す。 */
		r = vfs_resolve(pathbuf);
		if (r == VFS_ENOENT)
			return 0xFFFF;
		for (u = 0; u < NUFD && ufd_kind[u]; u++)
			;
		if (u == NUFD)
			return 0xFFFF;
		if (r >= 0) {
			/* DEVFS。ノードの data(dev_lookup 結果)が持つ dev_ops 経由で
			 * open/read/write/close を振り分ける(/dev/null 専用の決め打ちを廃止)。 */
			struct vnode *vn = vfs_node(r);
			const struct dev *dv = vn ? (const struct dev *)vn->data : 0;
			if (!dv || !dv->ops)
				return 0xFFFF;
			if (dv->ops->open && dv->ops->open(pathbuf, (int)al) < 0)
				return 0xFFFF;
			ufd_kind[u] = 2;
			ufd_ops[u]  = dv->ops;
			return u;
		}
		/* r == VFS_FAT */
		bh = fsb_open(pathbuf, (al & 1) ? (FSB_WRITE | FSB_CREATE) : FSB_READ);
		if (bh < 0)
			return 0xFFFF;
		ufd_kind[u] = 1;
		ufd_bh[u]   = bh;
		return u;
	}
	case 5: {
		unsigned u = al;
		if (u < NUFD && ufd_kind[u]) {
			if (ufd_kind[u] == 1)
				fsb_close(ufd_bh[u]);
			else if (ufd_kind[u] == 2 && ufd_ops[u] && ufd_ops[u]->close)
				ufd_ops[u]->close(0);
			ufd_kind[u] = 0;
		}
		return 0;
	}
	case 6: {
		unsigned u = bx, n = cx, done = 0;
		if (u >= NUFD || !ufd_kind[u])
			return 0;
		if (ufd_kind[u] == 2) {
			if (!ufd_ops[u] || !ufd_ops[u]->read)
				return 0;
			while (n) {
				unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
				int got = ufd_ops[u]->read(0, iobuf, chunk);
				if (got <= 0)
					break;
				farcpy(cseg, di + done, iobuf, (unsigned)got);
				done += (unsigned)got;
				n    -= (unsigned)got;
				if ((unsigned)got < chunk)
					break;
			}
			return done;
		}
		if (ufd_kind[u] != 1)
			return 0;
		while (n) {
			unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
			int got = fsb_read(ufd_bh[u], iobuf, chunk);
			if (got <= 0)
				break;
			farcpy(cseg, di + done, iobuf, (unsigned)got);
			done += (unsigned)got;
			n    -= (unsigned)got;
			if ((unsigned)got < chunk)
				break;
		}
		return done;
	}
	case 7: {
		unsigned u = bx, n = cx, done = 0;
		if (u >= NUFD || !ufd_kind[u])
			return 0;
		if (ufd_kind[u] == 2) {
			if (!ufd_ops[u] || !ufd_ops[u]->write)
				return 0;
			while (n) {
				unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
				int wr;
				farcpy_in(iobuf, cseg, di + done, chunk);
				wr = ufd_ops[u]->write(0, iobuf, chunk);
				if (wr <= 0)
					break;
				done += (unsigned)wr;
				n    -= (unsigned)wr;
				if ((unsigned)wr < chunk)
					break;
			}
			return done;
		}
		if (ufd_kind[u] != 1)
			return 0;
		while (n) {
			unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
			int wr;
			farcpy_in(iobuf, cseg, di + done, chunk);
			wr = fsb_write(ufd_bh[u], iobuf, chunk);
			if (wr <= 0)
				break;
			done += (unsigned)wr;
			n    -= (unsigned)wr;
			if ((unsigned)wr < chunk)
				break;
		}
		return done;
	}

	/* 8/9/10/12: ディレクトリ反復は z80 / m68k-mega と同じ kdir_*
	 * (src/fatcmd.c)を通す(#76)。/dev も開け、readdir は種別を返す。
	 * kdir は同時 1 個なので dh は常に 0。 */
	case 8: {
		const char *p;
		get_path(cseg, si);
		/* cwd が "/" のとき sh は引数を絶対化しないので "." が来る(m68k と同じ)。 */
		p = (pathbuf[0] && !(pathbuf[0] == '.' && !pathbuf[1])) ? pathbuf : "/";
		if (kdir_open_abs(p) != 0)
			return 0xFFFF;
		return 0;
	}
	case 9: {
		int t = kdir_read(namebuf);
		(void)bx;
		if (t <= 0)
			return 0;
		farcpy(cseg, di, namebuf, 13);
		return (unsigned)t;                 /* 1=ファイル / 2=ディレクトリ */
	}
	case 10:
		(void)al;
		kdir_close();
		return 0;

	case 11:
		get_path(cseg, si);
		if (vfs_resolve(pathbuf) != VFS_FAT)   /* /dev 配下は削除不可 */
			return 0xFFFF;
		return (fsb_unlink(pathbuf) == FSB_OK) ? 0 : 0xFFFF;

	case 12:
		return (unsigned)kdir_size();

	case 13: {
		unsigned u = bx;
		unsigned lo = cx;
		unsigned hi = si;
		unsigned long offset = ((unsigned long)hi << 16) | (unsigned long)lo;
		unsigned long newpos;

		if (al == 3) {
			return seek_hi;
		}

		if (u >= NUFD || ufd_kind[u] != 1) {
			seek_hi = 0xFFFF;
			return 0xFFFF;
		}

		if (al == 0) {
			newpos = offset;
		} else if (al == 2) {
			newpos = fsb_size(ufd_bh[u]) + offset;
		} else {
			seek_hi = 0xFFFF;
			return 0xFFFF;
		}

		if (fsb_seek(ufd_bh[u], newpos) != FSB_OK) {
			seek_hi = 0xFFFF;
			return 0xFFFF;
		}

		seek_hi = (unsigned)(newpos >> 16);
		return (unsigned)(newpos & 0xFFFF);
	}

	case 21:
		/* pipe_tail(want): パイプ後段の tail。writer 完了まで待つ(proc_block)ので、待っている間は
		 * プロセスごとの syscall スタックで他プロセスが走る。 */
		return pipe_tail(al);

	case 53:
		/* meminfo(sel): stage2 が BIOS から集めた値。0=従来メモリ KB 1=拡張 KB(INT15h/88h) 2=E801 の 1〜16MB を KB 3=E801 の 16MB 超を 64KB 単位 4=実行時スロット数 */
		if (al == 4)
			return x86_nslots();
		if (al >= 6 && al <= 9)
			return x86_blkinfo(al - 6);   /* 6=ブロック総数 7=使用中 8=空き 9=最大連続空き(4KB 単位) */
		if (al == 11)
			return x86_kstack_headroom();     /* カーネル本体スタックの最小余裕(バイト。0 = 底まで使った) */
		if (al == 10)
			return x86_scstk_headroom();  /* 全スロットの syscall スタックの最小余裕(バイト) */
		if (al == 5)
			return x86_xmem_ok;           /* アンリアルモードで拡張メモリを読み書きできたか */
		return al < 4 ? x86_lowword(0x512 + 2 * al) : 0xFFFF;

	case 14:
		get_path(cseg, si);
		if (vfs_resolve(pathbuf) != VFS_FAT)   /* /dev 配下は作成不可 */
			return 0xFF;
		{
			int r = fsb_mkdir(pathbuf);
			if (r == FSB_OK)
				return 0;
			return (r == FSB_DENIED) ? 0xFF : 1;
		}

	case 15:
		get_path(cseg, si);
		get_path2(cseg, di);
		if (vfs_resolve(pathbuf) != VFS_FAT || vfs_resolve(pathbuf2) != VFS_FAT)
			return 0xFF;
		{
			int src_fh, dst_fh, dst_dh, r;
			src_fh = fsb_open(pathbuf, FSB_READ);
			if (src_fh < 0) {
				dst_dh = fsb_opendir(pathbuf);
				if (dst_dh < 0)
					return 1;
				fsb_closedir(dst_dh);
			} else {
				fsb_close(src_fh);
			}

			r = fsb_rename(pathbuf, pathbuf2);
			if (r == FSB_OK)
				return 0;

			dst_fh = fsb_open(pathbuf2, FSB_READ);
			if (dst_fh >= 0) {
				fsb_close(dst_fh);
				dst_dh = fsb_opendir(pathbuf2);
				if (dst_dh < 0) {
					fsb_unlink(pathbuf2);
					r = fsb_rename(pathbuf, pathbuf2);
					if (r == FSB_OK)
						return 0;
				} else {
					fsb_closedir(dst_dh);
				}
			}

			return (r == FSB_DENIED) ? 0xFF : 1;
		}

	case 18:
		/* klog(SI=msg): /var/log/message へ 1 行追記。実体は
		 * src/fatcmd.c の klog_write(全 ARCH 共有)。番号は
		 * m68k-mega(arch/m68k-mega/sysfile.c case 18)と揃えてある。
		 * x86 はセグメントがあるので、まず get_path() で pathbuf へ
		 * 移送してから渡す(他の文字列引数と同じ手口)。 */
		get_path(cseg, si);
		klog_write(pathbuf);
		return 0;

	case 19:
		return (unsigned)kfs_df(al);

	/* ---- 22〜28 / 33 / 51 / 52: mcpd(MCP サーバ)が使う、外部コマンドの起動・出力の取り込み・終了待ち。
		 *   番号と意味は src/sysfile.c(gcc 系)と同じ。引数は x86 の流儀(al = 小さい数、si/di = ユーザーのポインタ)。---- */
	case 22:
		/* kexec_argv(fn, pack, argc): al = argc、si = ファイル名、di = 引数パック。戻り: スロット(0xFF = 無い / 0 = 空き無し) */
		get_path(cseg, si);
		farcpy_in(pkbuf, cseg, di, sizeof pkbuf);
		pkbuf[sizeof pkbuf - 1] = 0;
		return kexec_argv(pathbuf, pkbuf, (unsigned char)al);
	case 23:
		/* builtin_try(cmd, arg): si = コマンド名、di = 引数文字列(64 文字まで) */
		get_path(cseg, si);
		get_path2(cseg, di);
		return (unsigned)builtin_try(pathbuf, pathbuf2);
	case 24:
		get_path(cseg, si);
		return (unsigned)builtin_is(pathbuf);
	case 25:
		/* redir_begin(fname, append): al = append、si = ファイル名 */
		get_path(cseg, si);
		return (unsigned)redir_begin(pathbuf, (unsigned char)al);
	case 26:
		redir_end();
		return 0;
	case 27:
		get_path(cseg, si);
		return (unsigned)in_begin(pathbuf);
	case 28:
		in_end();
		return 0;
	case 29: {
		/* krun_pipe(ln, lp, lc, rn, rp, rc): si = ユーザー側の 6 語の配列 { ln, lp, lc, rn, rp, rc }(ポインタは near)。
		 *   A | B の両側が終わるまでここで待つ(スロット別 syscall スタックなので KYIELD できる)。 */
		unsigned a[6];
		farcpy_in(a, cseg, si, sizeof a);
		farcpy_in(pathbuf, cseg, a[0], PATHMAX);  pathbuf[PATHMAX - 1] = 0;
		farcpy_in(pathbuf2, cseg, a[3], PATHMAX); pathbuf2[PATHMAX - 1] = 0;
		farcpy_in(pkbuf2, cseg, a[1], sizeof pkbuf2); pkbuf2[sizeof pkbuf2 - 1] = 0;
		farcpy_in(pkbuf, cseg, a[4], sizeof pkbuf);   pkbuf[sizeof pkbuf - 1] = 0;
		return krun_pipe(pathbuf, pkbuf2, (unsigned char)a[2], pathbuf2, pkbuf, (unsigned char)a[5]);
	}
#ifdef X86_EXTSH	/* cd / pwd は外部シェルのときだけ(内蔵シェルは自前の cwd を持ち、kchdir / kgetcwd はカーネルに無い) */
	case 30:
		get_path(cseg, si);
		return (unsigned)kchdir(pathbuf);
	case 31: {
		/* kgetcwd(out): di = ユーザー側の受け取り(49 バイト) */
		char cw[56];
		kgetcwd(cw);
		cw[sizeof cw - 1] = 0;
		farcpy(cseg, di, cw, 49);
		return 0;
	}
#endif
	case 32:
		return (unsigned)(con_break() ? 1 : 0);
	case 34:
		/* route(n, r): 出力ルート表(n 番のスロットの出力先)。al = n、bx = r */
		if (al < x86_nslots())
			((volatile unsigned char *)KW_OUTROUTE)[al] = (unsigned char)bx;
		return 0;
	case 33:
		/* kill(n): 前景の Ctrl+C 相当。終了コードは 130 */
		if (al >= 1 && al < x86_nslots() && ((volatile unsigned char *)kwork)[al] != PID_CONT) {
			x86_exitcode[al] = 130;
			x86_proc_release((unsigned char)al);
		}
		return 0;
	case 51: {
		/* ksleep(ticks): al tick(1/100 秒)眠る。スロット別 syscall スタックなので KYIELD できる */
		unsigned t0 = *(volatile unsigned int *)KW_TICKS;
		while ((unsigned)(*(volatile unsigned int *)KW_TICKS - t0) < al)
			KYIELD();
		return 0;
	}
	case 52:
		/* exitcode(n): スロット n で最後に終わったプロセスの終了コード(main の戻り値。kill は 130) */
		if (al >= x86_nslots())
			return 0xFFFF;
		return x86_exitcode[al];
	/* ---- 56〜60: シリアル(割り込み受信のドライバ uart.s 経由。ネットの下回りがポートを直接叩かないための口)---- */
	case 56:
		/* uart_open(n = al, 分周比 = bx, DTR/RTS = cx) */
		if (al > 1)
			return 0xFFFF;
		if (al == 0 && con_serial_state())
			return 0xFFFF;                      /* COM1 はコンソール: モデムにすると応答がシェルに流れ込む */
		return uart_hw_open(al, bx, cx) ? 0 : 0xFFFF;   /* 0xFFFF = 16550 が無い */
	case 57: {
		/* uart_read(n = bx, buf = di, max = cx): 溜まっている分だけ(待たない) */
		unsigned k = 0;
		int c;
		if (bx > 1)
			return 0;
		if (cx > IOCHUNK)
			cx = IOCHUNK;
		while (k < cx && (c = uart_getb(bx)) >= 0)
			iobuf[k++] = (char)c;
		if (k)
			farcpy(cseg, di, iobuf, k);
		return k;
	}
	case 58: {
		/* uart_write(n = bx, buf = di, len = cx) */
		unsigned done = 0, chunk, i;
		if (bx > 1)
			return 0;
		while (done < cx) {
			chunk = cx - done > IOCHUNK ? IOCHUNK : cx - done;
			farcpy_in(iobuf, cseg, di + done, chunk);
			for (i = 0; i < chunk; i++)
				uart_putb(bx, (unsigned char)iobuf[i]);
			done += chunk;
		}
		return done;
	}
	case 59: {
		/* uart_wait(n = al, ticks = bx): データが来るか bx tick 経つまで待つ。待つ間は譲る(全員が待ちなら hlt し、
		 * 受信の割り込みか次の tick で起きる)。戻り = 溜まっているバイト数(0 = 時間切れ) */
		unsigned t0 = *(volatile unsigned int *)KW_TICKS, a;
		if (al > 1)
			return 0;
		for (;;) {
			if ((a = uart_avail(al)) != 0)
				return a;
			if ((unsigned)(*(volatile unsigned int *)KW_TICKS - t0) >= bx)
				return 0;
			KYIELD();
		}
	}
	case 60:
		/* uart_msr(n = al): bit7 = DCD */
		return al > 1 ? 0 : uart_msr(al);
	case 20:
		/* slot(n): pid 表(crt0.s の kwork[0..7])の n 番。0=空き、それ以外=占有。
		 * n が実行時スロット数以上なら 0xFFFF(ユーザ側の syscall5 が 0xFFFFFFFF に伸ばす。/bin/free の終端)。 */
		if (al >= x86_nslots())
			return 0xFFFF;
		return ((volatile unsigned char *)kwork)[al];
	}
	return 0xFFFF;
}
