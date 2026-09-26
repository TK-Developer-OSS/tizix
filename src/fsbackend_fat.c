/* src/fsbackend_fat.c  --  FS backend 界面(fsbackend.h)の FАТ 実装
 *
 *   fsb_* を FatFs(ff.c)の f_* へマップし、ファイル/ディレクトリ ハンドルを
 *   この中で所有する(FIL / DIR の実体は中核・コマンドから見えない)。
 *
 *   ★blk_* はまだ実装しない。ff.c は自前の porting 層で disk_* を直接呼ぶため、
 *     diskio.c(disk_read/write)を当面そのまま使う。blk_* への寄せは別タスク。
 *
 *   arch 非依存。x86-ia16 は FSBACKEND グループでリンク。z80pack は未結線
 *   (Z80 側は driver.c / fatcmd.c が f_* を直呼び中。寄せは [P1] 残タスク)。
 */
#include "ff.h"
#include "fsbackend.h"
#include "vfs.h"
#include "dev.h"

#define NFD   6

#define NDIR  2

static FIL           fdtab[NFD];
static unsigned char fdused[NFD];
static DIR           dirtab[NDIR];
static unsigned char dirused[NDIR];

/* FatFs FRESULT → FSB_* */
static int map_fr(FRESULT r)
{
	switch (r) {
	case FR_OK:            return FSB_OK;
	case FR_NO_FILE:
	case FR_NO_PATH:       return FSB_NOENT;
	case FR_DENIED:
	case FR_WRITE_PROTECTED:
	case FR_LOCKED:        return FSB_DENIED;
	case FR_NOT_ENOUGH_CORE:
	case FR_TOO_MANY_OPEN_FILES: return FSB_NOSPC;
	default:              return FSB_ERR;
	}
}

int fsb_mount(void)
{
	/* 実体は fatcmd.c fat_init() が握っている。ここでは何もしない
	 * (寄せ切ったら f_mount をこちらへ移す)。 */
	return FSB_OK;
}

int fsb_open(const char *abspath, unsigned mode)
{
	unsigned i;
	BYTE m;
	FRESULT r;
	int vfs_idx = vfs_resolve(abspath);
	if (vfs_idx >= 0) {
		/* デバイス */
		struct vnode *node = vfs_node(vfs_idx);
		const struct dev *dev = (const struct dev*)node->data;
		if (dev && dev->ops && dev->ops->open) {
			return dev->ops->open(abspath, mode);
		}
	}

	for (i = 0; i < NFD && fdused[i]; i++)
		;
	if (i == NFD)
		return FSB_NOSPC;

	if (mode & FSB_WRITE) {
		m = (BYTE)(FA_WRITE | ((mode & FSB_CREATE) ? FA_CREATE_ALWAYS : FA_OPEN_EXISTING));
		if (mode & FSB_READ)
			m |= FA_READ;           /* 読み書き(fopen "r+") */
	} else
		m = FA_READ;

	r = f_open(&fdtab[i], abspath, m);
	if (r != FR_OK)
		return map_fr(r);
	fdused[i] = 1;
	return (int)i;
}

int fsb_close(int fh)
{
	if (fh < 0 || fh >= NFD || !fdused[fh])
		return FSB_ERR;
	f_close(&fdtab[fh]);
	fdused[fh] = 0;
	return FSB_OK;
}

int fsb_read(int fh, void *buf, unsigned len)
{
	UINT br = 0;
	if (fh < 0 || fh >= NFD || !fdused[fh])
		return 0;
	if (f_read(&fdtab[fh], buf, len, &br) != FR_OK)
		return 0;
	return (int)br;
}

int fsb_write(int fh, const void *buf, unsigned len)
{
	UINT bw = 0;
	if (fh < 0 || fh >= NFD || !fdused[fh])
		return 0;
	if (f_write(&fdtab[fh], buf, len, &bw) != FR_OK)
		return 0;
	return (int)bw;
}

int fsb_seek(int fh, unsigned long pos)
{
	if (fh < 0 || fh >= NFD || !fdused[fh])
		return FSB_ERR;
	return map_fr(f_lseek(&fdtab[fh], (FSIZE_t)pos));
}

unsigned long fsb_size(int fh)
{
	if (fh < 0 || fh >= NFD || !fdused[fh])
		return 0;
	return (unsigned long)f_size(&fdtab[fh]);
}

int fsb_opendir(const char *abspath)
{
	unsigned i;
	for (i = 0; i < NDIR && dirused[i]; i++)
		;
	if (i == NDIR)
		return FSB_NOSPC;
	if (f_opendir(&dirtab[i], abspath) != FR_OK)
		return FSB_NOENT;
	dirused[i] = 1;
	return (int)i;
}

static unsigned long last_dir_size;

int fsb_readdir(int dh, char *name13)
{
	FILINFO fno;
	unsigned char k;

	last_dir_size = 0;
	if (dh < 0 || dh >= NDIR || !dirused[dh])
		return FSB_ERR;
	if (f_readdir(&dirtab[dh], &fno) != FR_OK || fno.fname[0] == 0)
		return 0;                       /* 終端 */
	for (k = 0; k < 12 && fno.fname[k]; k++)
		name13[k] = fno.fname[k];
	name13[k] = 0;
	if (!(fno.fattrib & AM_DIR))
		last_dir_size = (unsigned long)fno.fsize;
	return (fno.fattrib & AM_DIR) ? 2 : 1;
}

unsigned long fsb_readdir_size(void)
{
	return last_dir_size;
}

int fsb_closedir(int dh)
{
	if (dh < 0 || dh >= NDIR || !dirused[dh])
		return FSB_ERR;
	f_closedir(&dirtab[dh]);
	dirused[dh] = 0;
	return FSB_OK;
}

int fsb_unlink(const char *abspath)
{
	return map_fr(f_unlink(abspath));
}

int fsb_mkdir(const char *abspath)
{
	return map_fr(f_mkdir(abspath));
}

int fsb_rename(const char *oldabs, const char *newabs)
{
	return map_fr(f_rename(oldabs, newabs));
}
