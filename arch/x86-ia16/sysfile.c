/* arch/x86-ia16/sysfile.c  --  INT 80h システムコールの C ディスパッチャ
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

static int con_raw_flag;

static char pathbuf[PATHMAX];
static char pathbuf2[PATHMAX];
static char iobuf[IOCHUNK];
static char namebuf[13];

static void get_path(unsigned cseg, unsigned off)
{
	unsigned i;
	farcpy_in(pathbuf, cseg, off, PATHMAX);
	for (i = 0; i < PATHMAX; i++)
		if (pathbuf[i] == 0)
			return;
	pathbuf[PATHMAX - 1] = 0;
}

static void get_path2(unsigned cseg, unsigned off)
{
	unsigned i;
	farcpy_in(pathbuf2, cseg, off, PATHMAX);
	for (i = 0; i < PATHMAX; i++)
		if (pathbuf2[i] == 0)
			return;
	pathbuf2[PATHMAX - 1] = 0;
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
		return (unsigned)time_get();
	case 17:
		/* input_ready */
		return (unsigned)(con_rx_ready() ? 1 : 0);
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
		if (al == 10)
			return x86_scstk_headroom();  /* 全スロットの syscall スタックの最小余裕(バイト) */
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

	case 20:
		/* slot(n): pid 表(crt0.s の kwork[0..7])の n 番。0=空き、それ以外=占有。
		 * n が実行時スロット数以上なら 0xFFFF(ユーザ側の syscall5 が 0xFFFFFFFF に伸ばす。/bin/free の終端)。 */
		if (al >= x86_nslots())
			return 0xFFFF;
		return ((volatile unsigned char *)kwork)[al];
	}
	return 0xFFFF;
}
