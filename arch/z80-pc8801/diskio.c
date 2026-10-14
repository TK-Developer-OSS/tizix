/* diskio.c - FatFs disk I/O for PC-8801(サブ CPU 経由のフロッピー)
 *
 *   PC-8801 のディスクは、メイン CPU から ports 0xFC-0xFF(8255)の握手で
 *   サブ CPU(FDD 側 Z80)にコマンドを送って読み書きする。手順は IPL(ipl.s)と
 *   同じで、cisc 氏の擬似 BIOS(bios.asm の PHYDIO、disk.asm の READDISK)
 *   から読み取ったもの。実機 ROM では未確認。
 *
 *     read  : cmd 2 + {枚数, ドライブ, トラック, セクタ} → cmd 3 で 256B x 枚数
 *     write : cmd 1 + {枚数, ドライブ, トラック, セクタ} → 続けて 256B x 枚数
 *
 *   フロッピーのセクタは 256B、FatFs は 512B なので 2 枚で 1 セクタ。
 *   D88 の通しセクタ番号 lin = LBA*2 + i、トラック通し番号 = lin/16
 *   (1 面 16 セクタ。トラック番号は「シリンダ*2+面」)、セクタは 1 起点。
 *   LBA*2 は偶数なので 2 枚は必ず同じトラックに収まり、1 コマンドで済む。
 *   FAT ボリュームは drive B(サブ CPU のドライブ 1)。drive A は IPL とカーネル。
 *   エラー応答(サブ CPU のステータス)は見ていない。
 */
#include "ff.h"
#include "diskio.h"

__sfr __at 0xFC SUBIN;     /* サブ CPU からのデータ */
__sfr __at 0xFD SUBOUT;    /* サブ CPU へのデータ */
__sfr __at 0xFE SUBST;     /* bit0 DAV / bit1 RFD / bit2 DAC */
__sfr __at 0xFF SUBCTL;    /* 握手線の制御 */

#define FAT_FDC_DRIVE 1

static void subsend(unsigned char v, unsigned char atn)
{
    if (atn)
        SUBCTL = 0x0F;                 /* ATN=1(コマンド) */
    while (!(SUBST & 0x02))
        ;                              /* RFD=1 待ち */
    SUBCTL = 0x0E;                     /* ATN=0 */
    SUBOUT = v;
    SUBCTL = 0x09;                     /* DAV=1 */
    while (!(SUBST & 0x04))
        ;                              /* DAC=1 待ち */
    SUBCTL = 0x08;                     /* DAV=0 */
    while (SUBST & 0x04)
        ;                              /* DAC=0 待ち */
}

static unsigned char subrecv(void)
{
    unsigned char v;

    SUBCTL = 0x0B;                     /* RFD=1 */
    while (!(SUBST & 0x01))
        ;                              /* DAV=1 待ち */
    SUBCTL = 0x0A;                     /* RFD=0 */
    v = SUBIN;
    SUBCTL = 0x0D;                     /* DAC=1 */
    while (SUBST & 0x01)
        ;                              /* DAV=0 待ち */
    SUBCTL = 0x0C;                     /* DAC=0 */
    return v;
}

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

/* 512B セクタ 1 本の転送。ドライブ番号を明示で取る(src/dev.c の /dev/fda,fdb が使う)。
 * 戻り 0=OK / 1=エラー(今はエラーを検出しない)。 */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    unsigned n;

    subsend(op ? 1 : 2, 1);
    subsend(2, 0);                                        /* 枚数 */
    subsend(drive, 0);
    subsend((unsigned char)(sect >> 3), 0);               /* トラック通し番号 */
    subsend((unsigned char)((sect & 7) * 2 + 1), 0);      /* セクタ(1 起点) */
    if (op) {
        for (n = 0; n < 512; n++)
            subsend(buf[n], 0);
    } else {
        subsend(3, 1);
        for (n = 0; n < 512; n++)
            buf[n] = subrecv();
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
