/* diskio.c - FatFs disk I/O for the Amstrad CPC 6128: the uPD765 floppy controller, polled (interrupts off for each 512B
 * sector, non-DMA).  The controller code is in hi.s (F00F, in the page above the kernel); this is the FatFs side.
 * The disk is 40 tracks, one side, 9 sectors
 * of 512B (ids 0x41-0x49, "system format"); physical sector 0 is the boot sector, the FAT12 volume starts at physical sector 1, so FatFs sector s
 * is physical s + 1 (see mkdisk.py).  Drive A: only (/dev/fda and /dev/fdb both).  The motor is left on (port 0xFA7E).
 */
#include "ff.h"
#include "diskio.h"

#define SPT 9
#define D_TRK   (*(volatile unsigned char *)0xF3E0)
#define D_ID    (*(volatile unsigned char *)0xF3E1)
#define D_OP    (*(volatile unsigned char *)0xF3E2)
#define D_BUF   (*(unsigned char * volatile *)0xF3E3)
#define D_RES   (*(volatile unsigned char *)0xF3E6)

static void hi_disk(void) __naked { __asm jp 0xF00F __endasm; }

/* one 512B sector n (physical), op 0 = read, 1 = write.  Interrupts are off for the transfer.  0 = ok */
static unsigned char sec512(unsigned n, unsigned char *buf, unsigned char op) __critical
{
    unsigned char trk = 0;

    while (n >= SPT) { n -= SPT; trk++; }
    D_TRK = trk;
    D_ID = (unsigned char)n + 0x41;
    D_OP = op;
    D_BUF = buf;
    hi_disk();
    return D_RES;
}

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static DRESULT rw(BYTE *buff, unsigned lba, UINT count, unsigned char op)
{
    unsigned n = 1 + lba;

    while (count--) {
        if (sec512(n, buff, op)) return RES_ERROR;
        buff += 512;
        n++;
    }
    return RES_OK;
}

/* one 512B sector, drive number ignored (one disk); 0 = ok */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf, unsigned sect, unsigned char op)
{
    (void)drive;
    return rw(buf, sect, 1, op) == RES_OK ? 0 : 1;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw(buff, (unsigned)sector, count, 0);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw((BYTE *)buff, (unsigned)sector, count, 1);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
