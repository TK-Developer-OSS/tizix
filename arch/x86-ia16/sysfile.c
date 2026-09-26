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
#include "vfs.h"
#include "fatcmd.h"   /* klog_write(case 18) */        /* vfs_resolve : パス系入口の backend 振り分け */
#include "dev.h"        /* DEVFS ノードの dev_ops 経由ディスパッチ(/dev/null,/dev/fda 共通) */

extern void farcpy(unsigned dseg, unsigned doff, const void *src, unsigned n);
extern void farcpy_in(void *dst, unsigned sseg, unsigned soff, unsigned n);

#define NUFD    6
#define PATHMAX 64
#define IOCHUNK 128

/* ユーザー fd 表。kind: 0=空き / 1=FAT(bh=backend ハンドル) / 2=DEVFS(ops 経由) */
static unsigned char ufd_kind[NUFD];
static int           ufd_bh[NUFD];
static const struct dev_ops *ufd_ops[NUFD];

static char pathbuf[PATHMAX];
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

unsigned sys_call(unsigned ax, unsigned bx, unsigned cx,
                  unsigned si, unsigned di, unsigned cseg)
{
	unsigned char fn = (unsigned char)(ax >> 8);
	unsigned al = ax & 0xFF;

	switch (fn) {
	case 1:
		kputchar((int)(unsigned char)al);
		return al;
	case 2:
		return (unsigned)(unsigned char)kgetchar();
	case 3:
		return getticks();

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

	case 18:
		/* klog(SI=msg): /var/log/message へ 1 行追記。実体は
		 * src/fatcmd.c の klog_write(全 ARCH 共有)。番号は
		 * m68k-mega(arch/m68k-mega/sysfile.c case 18)と揃えてある。
		 * x86 はセグメントがあるので、まず get_path() で pathbuf へ
		 * 移送してから渡す(他の文字列引数と同じ手口)。 */
		get_path(cseg, si);
		klog_write(pathbuf);
		return 0;
	}
	return 0xFFFF;
}
