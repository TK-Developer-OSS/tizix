/* diskio.c - FatFs disk I/O for the TI-84 Plus.
 *   The FAT image lives in flash pages 2.. (TI_DISK_FIRST_PAGE), sector n at page 2 + n/32, offset (n%32)*512.
 *   Flash cannot be rewritten sector by sector, so writes go to a copy-on-write overlay kept in RAM pages 2..7
 *   (64KB, pages 2..5, 127 sectors); the table of the LBA that each slot holds (0xFFFF = free) is in the kernel RAM. A sector that is in the overlay is read from there, otherwise from flash. The overlay is
 *   gone at power off (RAM) - persisting it to flash is not done yet.
 *   _ti_xfer (crt0.s, in page 0) maps a page onto 0x4000 for the 512-byte copy. */
#include "ff.h"
#include "diskio.h"
#include "include/hw.h"

extern void ti_xfer(unsigned char *buf, unsigned bank, unsigned off, unsigned dir);

#define OV_SLOTS   128			/* 4 RAM pages (2..5) x 32 sectors; slot 0 is not used for data */
#define OV_BANK(s) (0x80 + 2 + ((s) >> 5))
#define OV_OFF(s)  (((s) & 31) << 9)

/* The table (256 bytes) sits in the free area 0x8C68-0x8D67 of the kernel RAM (src/kmem.h): C statics must stay
 * below 0x8400, where the scheduler's control blocks are fixed. */
#define ovtab ((unsigned short *)0x8C68)
static unsigned char ovused;

DSTATUS disk_initialize(BYTE pdrv)
{
    unsigned char i;
    (void)pdrv;
    for (i = 0; i < OV_SLOTS; i++)
        ovtab[i] = 0xFFFF;
    ovused = 1;				/* slot 0 is not used for data, so that a slot number 0 never means "none" */
    return 0;
}

DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }

/* the overlay slot that holds lba, or 0 */
static unsigned char ov_find(unsigned lba)
{
    unsigned char i;
    for (i = 1; i < ovused; i++)
        if (ovtab[i] == lba)
            return i;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    unsigned long lba = (unsigned long)sector;

    (void)pdrv;
    for (; count; count--, lba++, buff += 512) {
        unsigned char s = ov_find((unsigned)lba);
        if (s) {
            ti_xfer(buff, OV_BANK(s), OV_OFF(s), 0);
        } else {
            unsigned page = TI_DISK_FIRST_PAGE + (unsigned)(lba >> 5);
            if (page >= TI_DISK_FIRST_PAGE + TI_DISK_PAGES)
                return RES_ERROR;
            ti_xfer(buff, page, (unsigned)(lba & 31) << 9, 0);
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    unsigned long lba = (unsigned long)sector;

    (void)pdrv;
    for (; count; count--, lba++, buff += 512) {
        unsigned char s = ov_find((unsigned)lba);
        if (!s) {
            if (ovused >= OV_SLOTS)
                return RES_ERROR;	/* the overlay is full */
            s = ovused++;
            ovtab[s] = (unsigned)lba;
        }
        ti_xfer((unsigned char *)buff, OV_BANK(s), OV_OFF(s), 1);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv; (void)cmd; (void)buff;
    return RES_OK;
}

/* raw block access for /dev/fda (src/dev.c): op 0 = read, 1 = write. 0 = success */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf, unsigned sect, unsigned char op)
{
    (void)drive;
    if (sect >= (unsigned)TI_DISK_PAGES * 32)
        return 1;
    return (op ? disk_write(0, buf, sect, 1) : disk_read(0, buf, sect, 1)) == RES_OK ? 0 : 1;
}
