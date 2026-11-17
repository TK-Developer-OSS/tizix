/* FatFs disk I/O for arch/rv32-fpga-papiliolx6: a disk image in RAM (DISK_BASE; placed with QEMU's -device loader).
 *   Writes only affect RAM (they vanish when QEMU stops). */
#include "../../src/ff.h"
#include "../../src/diskio.h"
#include "plat.h"

#define SECTOR_SIZE 512UL

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static DRESULT disk_xfer(BYTE *buff, LBA_t sector, UINT count, int wr)
{
	unsigned long off = (unsigned long)sector * SECTOR_SIZE;
	unsigned long n = (unsigned long)count * SECTOR_SIZE, i;
	volatile unsigned char *d = (volatile unsigned char *)(DISK_BASE + off);

	if (off + n > DISK_BYTES)
		return RES_PARERR;
	if (wr)
		for (i = 0; i < n; i++) d[i] = buff[i];
	else
		for (i = 0; i < n; i++) buff[i] = d[i];
	return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{ (void)pdrv; return disk_xfer(buff, sector, count, 0); }

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{ (void)pdrv; return disk_xfer((BYTE *)buff, sector, count, 1); }

/* /dev/fda and /dev/fdb (kdev_* in src/dev.c). There is one medium. op: 0=read 1=write. Returns 0=OK */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf, unsigned sect, unsigned char op)
{
	(void)drive;
	return disk_xfer(buf, (LBA_t)sect, 1, op ? 1 : 0) == RES_OK ? 0 : 1;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
