/* arch/x86-ia16/fat12min.c -- 最小の FAT12(8.3 名・1 ボリューム・スーパーフロッピー)。FatFs(ff.c、14.6KB)の代わり。
 *   カーネルの 64KB セグメントが窮屈なので(z80 流に)必要なぶんだけ持つ。API は FatFs の部分集合(fat12min.h)。
 *   作業域は 512B の窓(win)1 枚だけ(FatFs の FF_FS_TINY と同じ考え)。FAT の更新は FAT1 を書き、窓を吐き出すときに
 *   FAT2 以降へ写す。長いファイル名(LFN)のエントリは読み飛ばす(作らない)。日時は固定値。 */
#include "ff.h"
#include "diskio.h"

#define SS 512
#define EOC 0xFF8UL                        /* FAT12: これ以上 = チェーンの終わり */
#define FIXED_DATE 0x5A77                  /* 2025-03-23 相当の固定値(RTC は使わない) */

static FATFS *Fs;
static BYTE  win[SS];
static DWORD winsect = 0xFFFFFFFFUL;
static BYTE  windirty;
static BYTE  tmpent[32];

/* ---------------- 窓 ---------------- */
static FRESULT flush(void)
{
	if (windirty) {
		BYTE k;
		if (disk_write(0, win, winsect, 1) != RES_OK)
			return FR_DISK_ERR;
		if (Fs && winsect >= Fs->fatbase && winsect < Fs->fatbase + Fs->fatsz)      /* FAT1 → FAT2 以降へ写す */
			for (k = 1; k < Fs->nfats; k++)
				if (disk_write(0, win, winsect + (DWORD)k * Fs->fatsz, 1) != RES_OK)
					return FR_DISK_ERR;
		windirty = 0;
	}
	return FR_OK;
}

static FRESULT move_window(DWORD sect)
{
	if (sect == winsect)
		return FR_OK;
	if (flush() != FR_OK)
		return FR_DISK_ERR;
	if (disk_read(0, win, sect, 1) != RES_OK) {
		winsect = 0xFFFFFFFFUL;
		return FR_DISK_ERR;
	}
	winsect = sect;
	return FR_OK;
}

static FRESULT window_noread(DWORD sect)     /* 全部上書きするセクタ用(読まない) */
{
	if (sect == winsect)
		return FR_OK;
	if (flush() != FR_OK)
		return FR_DISK_ERR;
	winsect = sect;
	return FR_OK;
}

/* ---------------- FAT12 ---------------- */
static int fat_byte(DWORD off)
{
	if (move_window(Fs->fatbase + (off >> 9)) != FR_OK)
		return -1;
	return win[off & (SS - 1)];
}

static FRESULT fat_set(DWORD off, BYTE mask, BYTE val)
{
	if (move_window(Fs->fatbase + (off >> 9)) != FR_OK)
		return FR_DISK_ERR;
	win[off & (SS - 1)] = (BYTE)((win[off & (SS - 1)] & ~mask) | (val & mask));
	windirty = 1;
	return FR_OK;
}

static DWORD get_fat(DWORD c)               /* 0xFFFFFFFF = 読めない */
{
	DWORD off = c + (c >> 1);
	int b0 = fat_byte(off), b1;
	if (b0 < 0 || (b1 = fat_byte(off + 1)) < 0)
		return 0xFFFFFFFFUL;
	return (c & 1) ? (DWORD)(((unsigned)b0 >> 4) | ((unsigned)b1 << 4)) : (DWORD)((unsigned)b0 | (((unsigned)b1 & 0x0F) << 8));
}

static FRESULT put_fat(DWORD c, DWORD v)
{
	DWORD off = c + (c >> 1);
	if (c & 1) {
		if (fat_set(off, 0xF0, (BYTE)(v << 4)) != FR_OK) return FR_DISK_ERR;
		return fat_set(off + 1, 0xFF, (BYTE)(v >> 4));
	}
	if (fat_set(off, 0xFF, (BYTE)v) != FR_OK) return FR_DISK_ERR;
	return fat_set(off + 1, 0x0F, (BYTE)(v >> 8));
}

static DWORD clust2sect(DWORD c) { return Fs->database + ((c - 2) << Fs->cshift); }

static DWORD alloc_cluster(DWORD prev)       /* 0 = 空き無し/エラー */
{
	DWORD c, n = Fs->n_fatent - 2;
	DWORD start = Fs->hint < 2 || Fs->hint >= Fs->n_fatent ? 2 : Fs->hint;
	c = start;
	while (n--) {
		DWORD v = get_fat(c);
		if (v == 0xFFFFFFFFUL) return 0;
		if (v == 0) {
			if (put_fat(c, 0xFFF) != FR_OK) return 0;
			if (prev && put_fat(prev, c) != FR_OK) return 0;
			Fs->hint = c + 1;
			return c;
		}
		if (++c >= Fs->n_fatent) c = 2;
	}
	return 0;
}

static void remove_chain(DWORD c)
{
	while (c >= 2 && c < EOC) {
		DWORD nx = get_fat(c);
		if (nx == 0xFFFFFFFFUL) return;
		put_fat(c, 0);
		if (c < Fs->hint) Fs->hint = c;
		c = nx;
	}
}

static FRESULT zero_cluster(DWORD c)
{
	DWORD s = clust2sect(c);
	BYTE k;
	UINT i;
	for (k = 0; k < Fs->csize; k++) {
		if (window_noread(s + k) != FR_OK) return FR_DISK_ERR;
		for (i = 0; i < SS; i++) win[i] = 0;
		windirty = 1;
	}
	return FR_OK;
}

/* ---------------- ディレクトリ ---------------- */
/* dcl = ディレクトリの先頭クラスタ(0 = ルート)、idx = エントリ番号。窓の中の 32 バイトを指す。 */
static BYTE *dir_ptr(DWORD dcl, WORD idx)
{
	DWORD sect;
	if (dcl == 0) {
		if (idx >= Fs->n_rootdir) return 0;
		sect = Fs->dirbase + (idx >> 4);
	} else {
		UINT epc = (UINT)Fs->csize << 4;           /* 1 クラスタのエントリ数 */
		UINT ci = idx / epc;
		DWORD c = dcl;
		while (ci--) {
			c = get_fat(c);
			if (c < 2 || c >= EOC) return 0;
		}
		sect = clust2sect(c) + ((idx % epc) >> 4);
	}
	if (move_window(sect) != FR_OK) return 0;
	return win + ((idx & 15) << 5);
}

static WORD ld16(const BYTE *p) { return (WORD)(p[0] | ((WORD)p[1] << 8)); }
static DWORD ld32(const BYTE *p) { return (DWORD)ld16(p) | ((DWORD)ld16(p + 2) << 16); }
static void st16(BYTE *p, WORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void st32(BYTE *p, DWORD v) { st16(p, (WORD)v); st16(p + 2, (WORD)(v >> 16)); }

/* パスの次の 1 要素を 8.3(11 バイト、大文字、空白詰め)にする。戻り: 1 = 取れた(*pp は次へ)/ 0 = 終わり / -1 = 不正 */
static int next_name(const TCHAR **pp, BYTE *nm)
{
	const TCHAR *p = *pp;
	UINT i = 0, ext = 0;
	BYTE c;
	while (*p == '/') p++;
	if (!*p) { *pp = p; return 0; }
	for (c = 0; c < 11; c++) nm[c] = ' ';
	while (*p && *p != '/') {
		c = (BYTE)*p++;
		if (c == '.') {
			if (ext || i == 0) return -1;          /* "." と ".." と 先頭のドットは受けない(絶対パスで来る) */
			ext = 1; i = 8; continue;
		}
		if (c < 0x21 || c == '"' || c == '*' || c == '+' || c == ',' || c == ':' || c == ';' || c == '<' || c == '=' ||
		    c == '>' || c == '?' || c == '[' || c == '\\' || c == ']' || c == '|' || c == 0x7F)
			return -1;
		if (c >= 'a' && c <= 'z') c = (BYTE)(c - 32);
		if (i >= (ext ? 11u : 8u)) return -1;
		nm[i++] = c;
	}
	*pp = p;
	return 1;
}

static BYTE name_eq(const BYTE *e, const BYTE *nm)
{
	UINT i;
	for (i = 0; i < 11; i++) if (e[i] != nm[i]) return 0;
	return 1;
}

/* ディレクトリ dcl で nm を探す。見つかれば *pidx に番号、戻り 1。無ければ 0(エラーは -1)。 */
static int dir_find(DWORD dcl, const BYTE *nm, WORD *pidx)
{
	WORD idx = 0;
	for (;; idx++) {
		BYTE *e = dir_ptr(dcl, idx);
		if (!e) return 0;
		if (e[0] == 0) return 0;
		if (e[0] == 0xE5 || (e[11] & 0x08) || e[11] == 0x0F) continue;     /* 削除済み・ラベル・LFN */
		if (name_eq(e, nm)) { *pidx = idx; return 1; }
	}
}

/* パスを「親ディレクトリ + 最後の名前」に分ける。*isroot = パスが "/" のみ。 */
static FRESULT walk(const TCHAR *path, DWORD *pdcl, BYTE *nm, BYTE *isroot)
{
	const TCHAR *p = path;
	DWORD dcl = 0;
	int r;
	BYTE nxt[11];
	WORD idx;
	*isroot = 0;
	r = next_name(&p, nm);
	if (r < 0) return FR_INVALID_NAME;
	if (r == 0) { *isroot = 1; *pdcl = 0; return FR_OK; }
	for (;;) {
		const TCHAR *q = p;
		r = next_name(&q, nxt);
		if (r < 0) return FR_INVALID_NAME;
		if (r == 0) break;                           /* nm が最後 */
		{   /* nm は途中のディレクトリ */
			BYTE *e;
			int f = dir_find(dcl, nm, &idx);
			if (f <= 0) return FR_NO_PATH;
			e = dir_ptr(dcl, idx);
			if (!e || !(e[11] & AM_DIR)) return FR_NO_PATH;
			dcl = ld16(e + 26);
		}
		{ UINT k; for (k = 0; k < 11; k++) nm[k] = nxt[k]; }
		p = q;
	}
	*pdcl = dcl;
	return FR_OK;
}

/* 空きエントリ(0x00 か 0xE5)を探す。サブディレクトリが一杯なら 1 クラスタ増やす。 */
static FRESULT dir_alloc(DWORD dcl, WORD *pidx)
{
	WORD idx = 0;
	for (;; idx++) {
		BYTE *e = dir_ptr(dcl, idx);
		if (!e) {
			DWORD c, last;
			if (dcl == 0) return FR_DENIED;            /* ルートは固定長 */
			last = dcl;
			for (;;) { c = get_fat(last); if (c < 2 || c >= EOC) break; last = c; }
			c = alloc_cluster(last);
			if (!c) return FR_DENIED;
			if (zero_cluster(c) != FR_OK) return FR_DISK_ERR;
			e = dir_ptr(dcl, idx);
			if (!e) return FR_INT_ERR;
		}
		if (e[0] == 0 || e[0] == 0xE5) { *pidx = idx; return FR_OK; }
	}
}

static void fill_entry(BYTE *e, const BYTE *nm, BYTE attr, DWORD clust, DWORD size)
{
	UINT i;
	for (i = 0; i < 32; i++) e[i] = 0;
	for (i = 0; i < 11; i++) e[i] = nm[i];
	e[11] = attr;
	st16(e + 24, FIXED_DATE);
	st16(e + 22, 0);
	st16(e + 26, (WORD)clust);
	st32(e + 28, size);
	windirty = 1;
}

/* ---------------- マウント ---------------- */
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt)
{
	DWORD tot, rootsec;
	WORD rsv, nroot, fatsz16;
	BYTE spc, nf;
	(void)path; (void)opt;
	Fs = fs;
	winsect = 0xFFFFFFFFUL; windirty = 0;
	if (disk_initialize(0) & STA_NOINIT) return FR_NOT_READY;
	if (move_window(0) != FR_OK) return FR_DISK_ERR;
	if (win[510] != 0x55 || win[511] != 0xAA || ld16(win + 11) != SS) return FR_NO_FILESYSTEM;
	spc = win[13]; rsv = ld16(win + 14); nf = win[16]; nroot = ld16(win + 17);
	tot = ld16(win + 19); if (!tot) tot = ld32(win + 32);
	fatsz16 = ld16(win + 22);
	if (!spc || (spc & (spc - 1)) || !nf || !fatsz16) return FR_NO_FILESYSTEM;
	fs->csize = spc; fs->cshift = 0; while ((1u << fs->cshift) < spc) fs->cshift++;
	fs->nfats = nf; fs->n_rootdir = nroot; fs->fatsz = fatsz16;
	fs->fatbase = rsv;
	fs->dirbase = rsv + (DWORD)nf * fatsz16;
	rootsec = ((DWORD)nroot * 32 + SS - 1) / SS;
	fs->database = fs->dirbase + rootsec;
	fs->n_fatent = ((tot - fs->database) >> fs->cshift) + 2;
	if (fs->n_fatent > 4085) return FR_NO_FILESYSTEM;   /* FAT12 のみ */
	fs->hint = 2;
	return FR_OK;
}

/* ---------------- ファイル ---------------- */
static FRESULT sync_entry(FIL *fp)
{
	BYTE *e;
	if (!(fp->flag & 0x40)) return FR_OK;
	e = dir_ptr(fp->dcl, fp->didx);
	if (!e) return FR_INT_ERR;
	e[11] |= AM_ARC;
	st16(e + 26, (WORD)fp->sclust);
	st32(e + 28, fp->obj.objsize);
	st16(e + 24, FIXED_DATE); st16(e + 22, 0);
	windirty = 1;
	fp->flag &= (BYTE)~0x40;
	return FR_OK;
}

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
	DWORD pdcl;
	BYTE nm[11], isroot;
	WORD idx;
	BYTE *e;
	FRESULT fr;
	int f;
	fr = walk(path, &pdcl, nm, &isroot);
	if (fr != FR_OK) return fr;
	if (isroot) return FR_INVALID_NAME;
	f = dir_find(pdcl, nm, &idx);
	if (f < 0) return FR_DISK_ERR;
	if (f) {
		e = dir_ptr(pdcl, idx);
		if (!e) return FR_DISK_ERR;
		if (e[11] & AM_DIR) return FR_NO_FILE;
		if ((mode & FA_CREATE_NEW)) return FR_EXIST;
		if ((mode & FA_WRITE) && (e[11] & AM_RDO)) return FR_DENIED;
		if (mode & FA_CREATE_ALWAYS) {               /* 中身を捨てる */
			DWORD c = ld16(e + 26);
			st16(e + 26, 0); st32(e + 28, 0); e[11] |= AM_ARC;
			windirty = 1;
			remove_chain(c);
		}
	} else {
		if (!(mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS))) return FR_NO_FILE;
		fr = dir_alloc(pdcl, &idx);
		if (fr != FR_OK) return fr;
		e = dir_ptr(pdcl, idx);
		if (!e) return FR_DISK_ERR;
		fill_entry(e, nm, AM_ARC, 0, 0);
	}
	fp->sclust = ld16(e + 26);
	fp->obj.objsize = ld32(e + 28);
	fp->fptr = 0; fp->clust = fp->sclust; fp->cidx = 0;
	fp->dcl = pdcl; fp->didx = idx;
	fp->flag = (BYTE)(mode & (FA_READ | FA_WRITE)); fp->err = 0;
	if ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) return f_lseek(fp, fp->obj.objsize);
	return FR_OK;
}

/* fp->clust を fptr を含むクラスタに合わせる。make != 0 なら足りないクラスタを足す。 */
static FRESULT seek_clust(FIL *fp, BYTE make)
{
	DWORD ci = (fp->fptr >> 9) >> Fs->cshift;
	if (fp->sclust == 0) {
		DWORD c;
		if (!make) return FR_INT_ERR;
		c = alloc_cluster(0);
		if (!c) return FR_DENIED;
		fp->sclust = c; fp->clust = c; fp->cidx = 0; fp->flag |= 0x40;
	}
	if (ci < fp->cidx) { fp->clust = fp->sclust; fp->cidx = 0; }
	while (fp->cidx < ci) {
		DWORD nx = get_fat(fp->clust);
		if (nx == 0xFFFFFFFFUL) return FR_DISK_ERR;
		if (nx < 2 || nx >= EOC) {
			if (!make) return FR_INT_ERR;
			nx = alloc_cluster(fp->clust);
			if (!nx) return FR_DENIED;
		}
		fp->clust = nx; fp->cidx++;
	}
	return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
	BYTE *d = (BYTE *)buff;
	*br = 0;
	if (!(fp->flag & FA_READ)) return FR_DENIED;
	if (fp->fptr >= fp->obj.objsize) return FR_OK;
	if ((DWORD)btr > fp->obj.objsize - fp->fptr) btr = (UINT)(fp->obj.objsize - fp->fptr);
	while (btr) {
		UINT off = (UINT)(fp->fptr & (SS - 1)), n = SS - off, i;
		DWORD sect;
		FRESULT fr = seek_clust(fp, 0);
		if (fr != FR_OK) { fp->err = (BYTE)fr; return fr; }
		sect = clust2sect(fp->clust) + (((fp->fptr >> 9)) & (DWORD)(Fs->csize - 1));
		if (move_window(sect) != FR_OK) return FR_DISK_ERR;
		if (n > btr) n = btr;
		for (i = 0; i < n; i++) d[i] = win[off + i];
		d += n; btr -= n; *br += n; fp->fptr += n;
	}
	return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
	const BYTE *s = (const BYTE *)buff;
	*bw = 0;
	if (!(fp->flag & FA_WRITE)) return FR_DENIED;
	while (btw) {
		UINT off = (UINT)(fp->fptr & (SS - 1)), n = SS - off, i;
		DWORD sect;
		FRESULT fr = seek_clust(fp, 1);
		if (fr != FR_OK) { fp->err = (BYTE)fr; return fr; }
		sect = clust2sect(fp->clust) + (((fp->fptr >> 9)) & (DWORD)(Fs->csize - 1));
		if (n > btw) n = btw;
		fr = (off == 0 && n == SS) ? window_noread(sect) : move_window(sect);
		if (fr != FR_OK) return FR_DISK_ERR;
		for (i = 0; i < n; i++) win[off + i] = s[i];
		windirty = 1;
		s += n; btw -= n; *bw += n; fp->fptr += n;
		if (fp->fptr > fp->obj.objsize) fp->obj.objsize = fp->fptr;
		fp->flag |= 0x40;
	}
	return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
	if (ofs > fp->obj.objsize) {
		if (!(fp->flag & FA_WRITE)) ofs = fp->obj.objsize;
		else {                                        /* 書き込みモードでは伸ばす(クラスタを確保) */
			DWORD save = fp->fptr;
			if (ofs) {
				fp->fptr = ofs - 1;
				if (seek_clust(fp, 1) != FR_OK) { fp->fptr = save; return FR_DENIED; }
			}
			fp->obj.objsize = ofs; fp->flag |= 0x40;
		}
	}
	fp->fptr = ofs;
	if (fp->sclust) {                                 /* clust / cidx を fptr に合わせておく(次の read/write が seek_clust で直す) */
		fp->clust = fp->sclust; fp->cidx = 0;
	}
	return FR_OK;
}

FRESULT f_sync(FIL *fp)
{
	FRESULT fr = sync_entry(fp);
	if (fr != FR_OK) return fr;
	return flush();
}

FRESULT f_close(FIL *fp)
{
	return f_sync(fp);
}

/* ---------------- ディレクトリ操作 ---------------- */
FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
	DWORD pdcl;
	BYTE nm[11], isroot;
	WORD idx;
	FRESULT fr = walk(path, &pdcl, nm, &isroot);
	if (fr != FR_OK) return fr;
	dp->idx = 0;
	if (isroot) { dp->sclust = 0; return FR_OK; }
	if (dir_find(pdcl, nm, &idx) <= 0) return FR_NO_PATH;
	{
		BYTE *e = dir_ptr(pdcl, idx);
		if (!e || !(e[11] & AM_DIR)) return FR_NO_PATH;
		dp->sclust = ld16(e + 26);
	}
	return FR_OK;
}

FRESULT f_closedir(DIR *dp) { (void)dp; return FR_OK; }

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
	if (!fno) { dp->idx = 0; return FR_OK; }
	for (;;) {
		BYTE *e = dir_ptr(dp->sclust, dp->idx);
		UINT i, k = 0;
		if (!e || e[0] == 0) { fno->fname[0] = 0; return FR_OK; }
		dp->idx++;
		if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 0x08) || e[11] == 0x0F) continue;
		for (i = 0; i < 8 && e[i] != ' '; i++) fno->fname[k++] = (TCHAR)e[i];
		if (e[8] != ' ') {
			fno->fname[k++] = '.';
			for (i = 8; i < 11 && e[i] != ' '; i++) fno->fname[k++] = (TCHAR)e[i];
		}
		fno->fname[k] = 0;
		fno->fattrib = e[11];
		fno->fsize = ld32(e + 28);
		fno->fdate = ld16(e + 24); fno->ftime = ld16(e + 22);
		return FR_OK;
	}
}

FRESULT f_unlink(const TCHAR *path)
{
	DWORD pdcl, c;
	BYTE nm[11], isroot, *e;
	WORD idx;
	FRESULT fr = walk(path, &pdcl, nm, &isroot);
	if (fr != FR_OK) return fr;
	if (isroot) return FR_INVALID_NAME;
	if (dir_find(pdcl, nm, &idx) <= 0) return FR_NO_FILE;
	e = dir_ptr(pdcl, idx);
	if (!e) return FR_DISK_ERR;
	if (e[11] & AM_RDO) return FR_DENIED;
	c = ld16(e + 26);
	if (e[11] & AM_DIR) {                             /* 空のディレクトリだけ消せる */
		WORD i;
		for (i = 2;; i++) {
			BYTE *d = dir_ptr(c, i);
			if (!d || d[0] == 0) break;
			if (d[0] != 0xE5 && d[11] != 0x0F) return FR_DENIED;
		}
		e = dir_ptr(pdcl, idx);
		if (!e) return FR_DISK_ERR;
	}
	e[0] = 0xE5; windirty = 1;
	remove_chain(c);
	return flush();
}

FRESULT f_mkdir(const TCHAR *path)
{
	DWORD pdcl, c;
	BYTE nm[11], isroot, *e, dot[11];
	WORD idx;
	UINT i;
	FRESULT fr = walk(path, &pdcl, nm, &isroot);
	if (fr != FR_OK) return fr;
	if (isroot) return FR_INVALID_NAME;
	if (dir_find(pdcl, nm, &idx) > 0) return FR_EXIST;
	c = alloc_cluster(0);
	if (!c) return FR_DENIED;
	if (zero_cluster(c) != FR_OK) return FR_DISK_ERR;
	e = dir_ptr(c, 0);                                /* "." */
	if (!e) return FR_DISK_ERR;
	for (i = 0; i < 11; i++) dot[i] = ' ';
	dot[0] = '.';
	fill_entry(e, dot, AM_DIR, c, 0);
	e = dir_ptr(c, 1);                                /* ".." */
	if (!e) return FR_DISK_ERR;
	dot[1] = '.';
	fill_entry(e, dot, AM_DIR, pdcl, 0);
	fr = dir_alloc(pdcl, &idx);
	if (fr != FR_OK) { remove_chain(c); return fr; }
	e = dir_ptr(pdcl, idx);
	if (!e) return FR_DISK_ERR;
	fill_entry(e, nm, AM_DIR, c, 0);
	return flush();
}

FRESULT f_rename(const TCHAR *path_old, const TCHAR *path_new)
{
	DWORD odcl, ndcl;
	BYTE onm[11], nnm[11], isroot, *e;
	WORD oidx, nidx;
	UINT i;
	FRESULT fr = walk(path_old, &odcl, onm, &isroot);
	if (fr != FR_OK) return fr;
	if (isroot) return FR_INVALID_NAME;
	if (dir_find(odcl, onm, &oidx) <= 0) return FR_NO_FILE;
	e = dir_ptr(odcl, oidx);
	if (!e) return FR_DISK_ERR;
	for (i = 0; i < 32; i++) tmpent[i] = e[i];
	fr = walk(path_new, &ndcl, nnm, &isroot);
	if (fr != FR_OK) return fr;
	if (isroot) return FR_INVALID_NAME;
	if (dir_find(ndcl, nnm, &nidx) > 0) return FR_EXIST;
	fr = dir_alloc(ndcl, &nidx);
	if (fr != FR_OK) return fr;
	e = dir_ptr(ndcl, nidx);
	if (!e) return FR_DISK_ERR;
	for (i = 0; i < 32; i++) e[i] = tmpent[i];
	for (i = 0; i < 11; i++) e[i] = nnm[i];
	windirty = 1;
	e = dir_ptr(odcl, oidx);                          /* 古い方を消す(新しい方は別の番号) */
	if (!e) return FR_DISK_ERR;
	e[0] = 0xE5; windirty = 1;
	if ((tmpent[11] & AM_DIR) && odcl != ndcl) {      /* ディレクトリの移動: ".." を付け替える */
		e = dir_ptr(ld16(tmpent + 26), 1);
		if (e) { st16(e + 26, (WORD)ndcl); windirty = 1; }
	}
	return flush();
}

FRESULT f_getfree(const TCHAR *path, DWORD *nclst, FATFS **fatfs)
{
	DWORD c, n = 0;
	(void)path;
	for (c = 2; c < Fs->n_fatent; c++) {
		DWORD v = get_fat(c);
		if (v == 0xFFFFFFFFUL) return FR_DISK_ERR;
		if (v == 0) n++;
	}
	*nclst = n;
	*fatfs = Fs;
	return FR_OK;
}
