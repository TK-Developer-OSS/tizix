/* diskio.c - FatFs disk I/O for z80pack cpmsim FDC (tizix)
 *
 *   FatFs sector = 512B。cpmsim FDC は 128B/sector なので 4 本束ねる。
 *   floppy 線形セクタ lin = LBA*4 + i  ->  track = lin/26, sector = lin%26+1
 *   FAT ボリュームは drive B (FDCD=1)。drivea は boot/カーネル。
 *
 *   ROM 節約: read/write はレジスタ操作が完全対称なので disk_rw() に一本化し、
 *   FDCOP に渡す op(0=read/1=write)だけを変えている。
 *   get_fattime() は ffconf.h の FF_FS_NORTC=1 化で不要になったため削除。
 */
#include "ff.h"
#include "diskio.h"

__sfr __at 10 FDCD;    /* drive select */
__sfr __at 11 FDCT;    /* track */
__sfr __at 12 FDCS;    /* sector (1-based) */
__sfr __at 13 FDCOP;   /* 0=read 1=write -> triggers DMA */
__sfr __at 14 FDCST;   /* status: 0=OK */
__sfr __at 15 DMAL;    /* DMA addr low */
__sfr __at 16 DMAH;    /* DMA addr high */

#define FAT_FDC_DRIVE 1        /* cpmsim drive B */
#define SPT           26       /* sectors per track (128B) */

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

/* LBA_t は DWORD(32bit)。cpmsim フロッピは 2002 セクタなので 16bit で足りる。
 * ループ変数を unsigned に落として 32bit インクリメントを消してある。 */
/* 512B セクタ 1 本(=128B×4)の転送。**ドライブ番号を明示で取る**のがミソ。
 * #32: /dev/fda(drive A)/ /dev/fdb(drive B)の生アクセスがここを使う。
 * FatFs 経路(disk_rw)は従来どおり FAT_FDC_DRIVE 固定で呼ぶ。
 * 戻り 0=OK / 1=エラー(FatFs 型に依存しないので src/dev.c から extern できる)。 */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    unsigned lin, addr;
    unsigned char i, track, sec;

    for (i = 0; i < 4; i++) {                 /* 4x128B = 512B */
        lin   = sect * 4 + i;
        track = (unsigned char)(lin / SPT);
        sec   = (unsigned char)(lin % SPT) + 1;
        addr  = (unsigned)buf + (unsigned)i * 128;
        FDCD  = drive;
        FDCT  = track;
        FDCS  = sec;
        DMAL  = (unsigned char)(addr & 0xFF);
        DMAH  = (unsigned char)(addr >> 8);
        FDCOP = op;                            /* 0=read 1=write (DMA) */
        if (FDCST) return 1;
    }
    return 0;
}

static DRESULT disk_rw(BYTE *buff, LBA_t sector, UINT count, unsigned char op)
{
    unsigned lba = (unsigned)sector;

    for (; count; count--, lba++) {
        if (disk_raw_rw(FAT_FDC_DRIVE, buff, lba, op) != 0)
            return RES_ERROR;
        buff += 512;
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return disk_rw(buff, sector, count, 0);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return disk_rw((BYTE *)buff, sector, count, 1);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
