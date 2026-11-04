/* arch/m68k-x68000 diskio.c - FatFs disk layer for the X68000 2HD floppy
 *   (77 cylinders x 2 heads x 8 sectors x 1024 bytes), through the ROM's IOCS
 *   _B_READ ($46) / _B_WRITE ($45). A logical sector (LBA) becomes N C H R.
 *   The device code comes from the boot device (IOCS $8E): $9070 for a 2HD in FD0. */
#include "../../src/ff.h"
#include "../../src/diskio.h"
#include "plat.h"

extern long iocs_call(long no, long d1, long d2, long d3, void *a1);

#define SECTOR_SIZE 1024
#define IOCS_B_BOOTINF 0x8E
#define IOCS_B_READ    0x46
#define IOCS_B_WRITE   0x45

static unsigned short dev;

static unsigned long chrn(unsigned long lba)
{
    unsigned long trk = lba >> 3;
    return (3UL << 24) | ((trk >> 1) << 16) | ((trk & 1) << 8) | ((lba & 7) + 1);
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    dev = (unsigned short)((iocs_call(IOCS_B_BOOTINF, 0, 0, 0, 0) << 8) | 0x70);
    return 0;
}
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

extern volatile unsigned long plat_noswitch;   /* crt0.s: the timer interrupt does not switch tasks while set */

/* IOCS disk I/O waits for FDC / DMA interrupts, and the kernel calls it with IRQs
 * masked, so the mask is lowered for the call only.  The timer still ticks (time keeps
 * running), but plat_noswitch stops task switching so IOCS is never re-entered.
 * Only bit 30 of the result is an error; the other bits are status. */
static DRESULT xfer(long no, void *buff, LBA_t sector)
{
    unsigned short sr;
    long r;

    __asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2000,%%sr" : "=d"(sr) :: "memory");
    plat_noswitch++;
    r = iocs_call(no, dev, (long)chrn((unsigned long)sector), SECTOR_SIZE, buff);
    plat_noswitch--;
    __asm__ volatile ("move.w %0,%%sr" :: "d"(sr) : "memory");
    return (r & 0x40000000L) ? RES_ERROR : RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (; count; count--, buff += SECTOR_SIZE, sector++)
        if (xfer(IOCS_B_READ, buff, sector) != RES_OK) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (; count; count--, buff += SECTOR_SIZE, sector++)
        if (xfer(IOCS_B_WRITE, (void *)buff, sector) != RES_OK) return RES_ERROR;
    return RES_OK;
}

unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    (void)drive;
    return (op ? disk_write(0, buf, (LBA_t)sect, 1) : disk_read(0, buf, (LBA_t)sect, 1)) == RES_OK ? 0 : 1;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    if (cmd == GET_SECTOR_SIZE) {
        *(WORD *)buff = SECTOR_SIZE;
        return RES_OK;
    }
    if (cmd == GET_SECTOR_COUNT) {
        *(LBA_t *)buff = 1232;
        return RES_OK;
    }
    if (cmd == GET_BLOCK_SIZE) {
        *(DWORD *)buff = 1;
        return RES_OK;
    }
    return RES_OK;
}
