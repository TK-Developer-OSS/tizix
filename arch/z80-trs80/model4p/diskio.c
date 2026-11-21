/* diskio.c - FatFs disk I/O for the TRS-80 Model 4P: the WD1793 floppy controller, polled (interrupts off for each
 * 256B sector).  Ports: 0xF0 command/status, 0xF1 track, 0xF2 sector, 0xF3 data, 0xF4 drive select (0x81 = drive 0,
 * double density).  The disk is 256B sectors, 18 a track (ids 0-17); the FAT12 volume starts at disk sector 2, so a
 * FatFs (512B) sector s is disk sectors 2+2s and 3+2s (see mkdisk.py).  Drive 0 only (/dev/fda and /dev/fdb both).
 */
#include "ff.h"
#include "diskio.h"

__sfr __at 0xF0 FCMD;
__sfr __at 0xF1 FTRK;
__sfr __at 0xF2 FSEC;
__sfr __at 0xF3 FDAT;
__sfr __at 0xF4 FSEL;

#define SPT 18

static void fwait(void)
{
    unsigned char n = 40;
    while (n--)
        ;                              /* the 1793 sets BUSY within ~28us of a command */
    while (FCMD & 1)
        ;
}

/* one 256B sector, op 0 = read, 1 = write.  Interrupts are off for the transfer.  0 = ok */
static unsigned char sec256(unsigned n, unsigned char *buf, unsigned char op) __critical
{
    unsigned char trk = 0, id, s;
    unsigned cnt;
    unsigned t = n;
    unsigned char tries;

    while (t >= SPT) { t -= SPT; trk++; }
    id = (unsigned char)t;
    FSEL = 0x81;
    for (tries = 0; tries < 3; tries++) {
        if (FTRK != trk) {
            FDAT = trk;
            FCMD = 0x18;               /* seek, no verify */
            fwait();
        }
        FSEC = id;
        FCMD = op ? 0xA0 : 0x80;
        cnt = 0;
        do {
            s = FCMD;
            if (s & 2) {               /* DRQ */
                if (cnt < 256) { if (op) FDAT = buf[cnt]; else buf[cnt] = FDAT; }
                cnt++;
            } else if (!(s & 1))
                break;
        } while (1);
        fwait();
        s = FCMD;
        if (cnt == 256 && !(s & (op ? 0xFC : 0x9C))) return 0;
        FCMD = 0x03;                   /* restore, then retry */
        fwait();
    }
    return 1;
}

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static DRESULT rw(BYTE *buff, unsigned lba, UINT count, unsigned char op)
{
    unsigned n = 2 + 2 * lba;

    while (count--) {
        if (sec256(n, buff, op) || sec256(n + 1, buff + 256, op)) return RES_ERROR;
        buff += 512;
        n += 2;
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
