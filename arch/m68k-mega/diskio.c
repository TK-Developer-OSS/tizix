/* FatFs disk I/O for arch/m68k-mega SD-over-Mega (tizix)。
 *
 *   Mega は CPU の SRAM を直接書けない(/DTACK のみ駆動)ので、UART と同じ
 *   1バイトずつポーリングする方式で SD カードの生セクタを読み書きする
 *   (plat.h SD_* 参照)。FAT の解釈は一切せず、FatFs(ff.c)がこの上で動く
 *   ── z80pack/z80board と同じ役割分担。
 *
 *   実機・m68ksim(ソフト先行検証)の両方でこのファイルがそのまま使える
 *   (m68ksim.c 側が同じレジスタ規約をエミュレートする)。
 */
#include "../../src/ff.h"
#include "../../src/diskio.h"
#include "plat.h"

#define REG_LBA_HI  (*(volatile unsigned char *)SD_LBA_HI)
#define REG_LBA_MID (*(volatile unsigned char *)SD_LBA_MID)
#define REG_LBA_LO  (*(volatile unsigned char *)SD_LBA_LO)
#define REG_CMD     (*(volatile unsigned char *)SD_CMD)
#define REG_STATUS  (*(volatile unsigned char *)SD_STATUS)
#define REG_DATA    (*(volatile unsigned char *)SD_DATA)

#define SECTOR_SIZE 512

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static DRESULT disk_xfer(BYTE *buff, LBA_t sector, UINT count, unsigned char op)
{
    unsigned long lba;
    unsigned i;

    for (; count; count--, buff += SECTOR_SIZE) {
        lba = (unsigned long)sector++;
        REG_LBA_HI  = (unsigned char)(lba >> 16);
        REG_LBA_MID = (unsigned char)(lba >> 8);
        REG_LBA_LO  = (unsigned char)lba;
        REG_CMD = op;                       /* 1=read / 2=write */

        for (i = 0; i < SECTOR_SIZE; i++) {
            while (!(REG_STATUS & SD_DATA_RDY)) {}
            if (op == 1)
                buff[i] = REG_DATA;
            else
                REG_DATA = buff[i];
        }
        if (REG_STATUS & SD_ERROR) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return disk_xfer(buff, sector, count, 1);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return disk_xfer((BYTE *)buff, sector, count, 2);
}

/* /dev/fda・/dev/fdb の生セクタ(src/dev.c の kdev_* が呼ぶ)。SD は 1 枚
 * なので drive は無視 ── z80board と同じく fda も fdb も同じ媒体を指す。
 * op: 0=read 1=write。戻り 0=OK / 非0=エラー。 */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    (void)drive;
    return disk_xfer(buf, (LBA_t)sect, 1, op ? 2 : 1) == RES_OK ? 0 : 1;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
