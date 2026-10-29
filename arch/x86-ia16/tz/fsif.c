/* tizix x86-ia16 : glue between the tzcc kernel and the shared FatFs stack (src/ff.c, fsbackend_fat.c, fatcmd.c, vfs.c,
 * dev.c), compiled by ia16-gcc like the rest of that stack. The functions here run on the file system stack of
 * glue.S (fs_tramp: SS = DS = kernel, no preemption), so gcc code may take the address of its locals.
 *   - tzcc has no 32-bit type: the 32-bit lseek is taken and returned as two words.
 *   - the tz sysfile.c wants the device kinds 2 (/dev/null) and 3 (/dev/fda) from the resolve call.
 */
#include "ff.h"
#include "vfs.h"
#include "dev.h"
#include "fsbackend.h"

extern const struct dev_ops fda_ops;

/* vfs_resolve for the tz sysfile.c: VFS_FAT / VFS_ENOENT, or the device kind (2 = /dev/null, 3 = /dev/fda). */
int fsif_resolve(const char *p)
{
	int r = vfs_resolve(p);

	if (r < 0)
		return r;
	switch (vfs_node_type(r)) {
	case VT_DEV_NULL:
		return 2;
	case VT_DEV_FDA:
		return 3;
	default:
		return VFS_ENOENT;
	}
}

void fsif_dev_open(int kind)
{
	if (kind == 3)
		fda_ops.open("/dev/fda", 0);
}

int fsif_dev_read(int kind, char *buf, unsigned n)
{
	if (kind == 3)
		return fda_ops.read(0, buf, n);
	return 0;                               /* /dev/null reads end at once */
}

int fsif_dev_write(int kind, char *buf, unsigned n)
{
	if (kind == 2)
		return (int)n;                      /* /dev/null swallows everything */
	return 0;                               /* /dev/fda is read only */
}

/* lseek(fh, whence 0 = SET / 2 = END, offset hi:lo) -> new position low word; fsif_seek_hi() is the high word
 * (0xFFFF after an error, with 0xFFFF returned) */
static unsigned seek_hi;

unsigned fsif_lseek(int fh, unsigned whence, unsigned lo, unsigned hi)
{
	unsigned long off = ((unsigned long)hi << 16) | lo;
	unsigned long np;

	if (fh < 0 || (whence != 0 && whence != 2)) {
		seek_hi = 0xFFFF;
		return 0xFFFF;
	}
	np = (whence == 2) ? fsb_size(fh) + off : off;
	if (fsb_seek(fh, np) != FSB_OK) {
		seek_hi = 0xFFFF;
		return 0xFFFF;
	}
	seek_hi = (unsigned)(np >> 16);
	return (unsigned)np;
}

unsigned fsif_seek_hi(void)
{
	return seek_hi;
}

/* mv (syscall 15): the result the user commands expect -- 0 = done, 1 = failed, 0xFF = denied. FatFs does not
 * replace an existing target: when the plain rename fails and the target is a file, remove it and rename again
 * (the same steps as ../sysfile.c). */
unsigned fsif_rename(const char *src, const char *dst)
{
	int fh, dh, r;

	fh = fsb_open(src, FSB_READ);
	if (fh < 0) {
		dh = fsb_opendir(src);
		if (dh < 0)
			return 1;
		fsb_closedir(dh);
	} else {
		fsb_close(fh);
	}
	r = fsb_rename(src, dst);
	if (r == FSB_OK)
		return 0;
	fh = fsb_open(dst, FSB_READ);
	if (fh >= 0) {
		fsb_close(fh);
		dh = fsb_opendir(dst);
		if (dh < 0) {
			fsb_unlink(dst);
			r = fsb_rename(src, dst);
			if (r == FSB_OK)
				return 0;
		} else {
			fsb_closedir(dh);
		}
	}
	return (r == FSB_DENIED) ? 0xFF : 1;
}
