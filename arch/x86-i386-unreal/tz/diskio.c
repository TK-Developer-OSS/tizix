/* tizix x86-ia16 : FatFs lower disk layer (BIOS INT 13h floppy) -- tzcc version of ../diskio.c.
 * Single volume, no partitions: a FatFs sector is the absolute LBA.
 * Argument layout matches the gcc callers: BYTE is a pushed word (take only the low byte, so char),
 * LBA_t is a 32-bit value pushed as two words (sec_lo, sec_hi). Only the low word is used (floppy). */
unsigned tz_boot_drive(void);
void bios13_reset(unsigned drive);
unsigned bios13_rw(unsigned drive, unsigned lba, unsigned count, char *buf, unsigned op);

unsigned fdrive;          /* boot drive number (stage2 leaves it at 0000:0510) */
unsigned initialized;     /* tzcc drops initializers of scalar globals, so "not initialized" is the zero state */

char disk_initialize(char pdrv)
{
    if (pdrv != 0)
        return 1;                       /* STA_NOINIT */
    fdrive = tz_boot_drive();
    bios13_reset(fdrive);
    initialized = 1;
    return 0;
}

char disk_status(char pdrv)
{
    if (pdrv != 0)
        return 1;
    if (initialized)
        return 0;
    return 1;
}

int disk_read(char pdrv, char *buff, unsigned sec_lo, unsigned sec_hi, unsigned count)
{
    if (pdrv != 0 || !initialized)
        return 3;                       /* RES_NOTRDY */
    if (count == 0)
        return 0;
    if (bios13_rw(fdrive, sec_lo, count, buff, 2) != 0)
        return 1;                       /* RES_ERROR */
    return 0;
}

int disk_write(char pdrv, char *buff, unsigned sec_lo, unsigned sec_hi, unsigned count)
{
    if (pdrv != 0 || !initialized)
        return 3;
    if (count == 0)
        return 0;
    if (bios13_rw(fdrive, sec_lo, count, buff, 3) != 0)
        return 1;
    return 0;
}

int disk_ioctl(char pdrv, char cmd, char *buff)
{
    if (pdrv != 0)
        return 4;                       /* RES_PARERR */
    if (cmd == 0)
        return 0;                       /* CTRL_SYNC */
    return 4;
}
