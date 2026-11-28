/* diskio.c - FatFs disk I/O for the RC2014: the Compact Flash module (ports 0x10-0x17), 8-bit transfers, LBA, polled.
 *   The FAT12 volume starts at the card's sector 0, so FatFs sector = card sector.  One disk: /dev/fda and /dev/fdb are
 *   both the card.  boot.s turned 8-bit mode on (and RomWBW before it).
 *   Registers: 0x10 data, 0x11 error / features, 0x12 count, 0x13-0x16 LBA (0x16: 0xE0 | LBA 24-27), 0x17 status /
 *   command (status 0x80 busy, 0x08 data request, 0x01 error). */
#include "ff.h"
#include "diskio.h"

__sfr __at 0x12 CF_CNT;
__sfr __at 0x13 CF_LBA0;
__sfr __at 0x14 CF_LBA1;
__sfr __at 0x15 CF_LBA2;
__sfr __at 0x16 CF_LBA3;
__sfr __at 0x17 CF_STAT;

/* 512 bytes from / to the data register (inir / otir twice) */
static void cf_in(unsigned char *buf) __naked __sdcccall(1)
{
    (void)buf;
    __asm
        ld      bc, #0x0010
        inir
        inir
        ret
    __endasm;
}

static void cf_out(const unsigned char *buf) __naked __sdcccall(1)
{
    (void)buf;
    __asm
        ld      bc, #0x0010
        otir
        otir
        ret
    __endasm;
}

/* wait for not busy; then for data request (or an error) */
static unsigned char cf_drq(void)
{
    unsigned char s;

    while ((s = CF_STAT) & 0x80)
        ;
    while (!((s = CF_STAT) & 0x09))
        ;
    return s & 0x01;
}

/* one 512B sector, op 0 = read, 1 = write; 0 = ok */
static unsigned char cf_rw(unsigned char *buf, unsigned long lba, unsigned char op)
{
    while (CF_STAT & 0x80)
        ;
    CF_CNT = 1;
    CF_LBA0 = (unsigned char)lba;
    CF_LBA1 = (unsigned char)(lba >> 8);
    CF_LBA2 = (unsigned char)(lba >> 16);
    CF_LBA3 = 0xE0 | ((unsigned char)(lba >> 24) & 0x0F);
    CF_STAT = op ? 0x30 : 0x20;             /* write / read sectors */
    if (cf_drq()) return 1;
    if (op) cf_out(buf); else cf_in(buf);
    while (CF_STAT & 0x80)
        ;
    return CF_STAT & 0x01;
}

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static DRESULT rw(BYTE *buff, LBA_t lba, UINT count, unsigned char op)
{
    for (; count; count--, lba++, buff += 512)
        if (cf_rw(buff, lba, op)) return RES_ERROR;
    return RES_OK;
}

/* one 512B sector, drive number ignored (one disk); 0 = ok */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf, unsigned sect, unsigned char op)
{
    (void)drive;
    return cf_rw(buf, sect, op);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw(buff, sector, count, 0);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw((BYTE *)buff, sector, count, 1);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
