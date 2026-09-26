/* arch/x86-ia16/dev_fda.c  --  /dev/fda 生ブロックデバイス(BIOS INT 13h フロッピー)
 *   dev_ops 経由でセクタ単位の読み出しを提供する。書き込みは未対応。
 *   arch/z80pack/dev_fda.c と対の実装(disk_read は diskio.c 経由で INT 13h へ)。
 */
#include "ff.h"
#include "dev.h"
#include "diskio.h"

static unsigned long fda_pos = 0;

static int fda_open(const char *path, int flags)
{
	(void)path; (void)flags;
	fda_pos = 0;
	return 0;
}

static int fda_read(int fd, void *buf, unsigned n)
{
	static BYTE secbuf[512];
	unsigned long sector = fda_pos / 512;
	unsigned offset = (unsigned)(fda_pos % 512);
	unsigned read_len;
	unsigned char *dst = (unsigned char *)buf;
	unsigned i;

	(void)fd;
	if (disk_read(0, secbuf, (LBA_t)sector, 1) != RES_OK)
		return 0;
	read_len = (n > (unsigned)(512 - offset)) ? (unsigned)(512 - offset) : n;
	for (i = 0; i < read_len; i++)
		dst[i] = secbuf[offset + i];
	fda_pos += read_len;
	return (int)read_len;
}

static int fda_write(int fd, const void *buf, unsigned n)
{
	(void)fd; (void)buf; (void)n;
	return 0; /* 読み取り専用 */
}

static int fda_close(int fd) { (void)fd; return 0; }
static int fda_ioctl(int fd, unsigned cmd, unsigned arg) { (void)fd; (void)cmd; (void)arg; return 0; }

const struct dev_ops fda_ops = {
	fda_open, fda_read, fda_write, fda_close, fda_ioctl
};
