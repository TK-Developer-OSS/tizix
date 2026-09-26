/* arch/x86-ia16/diskio.c  --  FatFs 下位ディスク層(BIOS INT 13h フロッピー)
 *   QEMU の 1.44MB フロッピー(ドライブ 0x00)を INT 13h AH=02/03 で read/write。
 *   単一ボリューム・パーティション無しなので FatFs の sector = 絶対 LBA。
 *   CF/ATA は M5。
 */
#include "ff.h"
#include "diskio.h"

#define FDRIVE 0x00

extern void     bios13_reset(unsigned drive);
extern unsigned bios13_rw(unsigned drive, unsigned lba, unsigned count,
                          void *buf, unsigned op);

static BYTE stat_bits = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv)
{
	if (pdrv != 0)
		return STA_NOINIT;
	bios13_reset(FDRIVE);
	stat_bits = 0;
	return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
	if (pdrv != 0)
		return STA_NOINIT;
	return stat_bits;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != 0 || (stat_bits & STA_NOINIT))
		return RES_NOTRDY;
	if (count == 0)
		return RES_OK;
	if (bios13_rw(FDRIVE, (unsigned)sector, (unsigned)count, buff, 2) != 0)
		return RES_ERROR;
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != 0 || (stat_bits & STA_NOINIT))
		return RES_NOTRDY;
	if (count == 0)
		return RES_OK;
	if (bios13_rw(FDRIVE, (unsigned)sector, (unsigned)count, (void *)buff, 3) != 0)
		return RES_ERROR;
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	(void)buff;
	if (pdrv != 0)
		return RES_PARERR;
	if (cmd == CTRL_SYNC)
		return RES_OK;
	return RES_PARERR;
}
