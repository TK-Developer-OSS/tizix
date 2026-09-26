/* diskio.c - FatFs disk I/O for z80board 実機(SD カード, bit-bang SPI)
 *
 *   z80pack の diskio.c(cpmsim FDC 版)と役割は同じだが、実機は物理ドライブが
 *   SD カード 1 枚だけ(A/B の区別が無い)。FatFs の pdrv は常に 0 として無視する。
 *
 *   実体は sdcard.s(bit-bang SPI + SD SPI モード)。sd_init/sd_data_read/
 *   sd_data_write は Z80 生レジスタ渡し(HL=buf, DE=sector)の手書きルーチンなので、
 *   C から直接呼べない。_disk_raw_rw(sdcard.s 末尾)が --sdcccall 0 の C ABI で
 *   包んでいる。
 *
 *   #32 の z80pack 方式(disk_raw_rw を src/dev.c から直接 extern する生ブロック
 *   デバイス経路)に合わせてある。/dev/fda 相当は実機では SD カードそのもの。
 *
 *   2026-09-19 実機(2GB SDSC)で読み書き確認済み(#54)。z80boardsim の仮想 SD は
 *   SDHC 相当・書き込みビジー無しなので、SDSC のアドレス変換やビジー待ちは
 *   エミュレータでは確かめられない。
 */
#include "ff.h"
#include "diskio.h"
#include "io.h"         /* kprintf */

extern unsigned char kcon_direct;   /* src/io.c: 1 の間は物理コンソールへ直接出す */

/* 読み書きの結果を表示する。エラーは常に、SD_DEBUG なら成功も出す。
 * r は disk_raw_rw32 の生の戻り値: 0=成功、1〜0x7F/0xFF=R1 応答(0xFF=無応答)、
 * 0xFD=データトークン待ちタイムアウト、1=書き込み応答/ビジー明けタイムアウト。
 * ★必ず物理コンソールへ直接出す。リダイレクト中にこの表示がファイル書き込み
 *   へ流れると、ディスク I/O の中から f_write を呼ぶことになり FatFs が再入して
 *   壊れる。rsyslog へ書かないのも同じ理由(#54)。 */
static void sdlog(unsigned char wr, unsigned lba, unsigned char r)
{
    kcon_direct = 1;
    kprintf(r ? "SD %s ERR lba=%u r=%u\n" : "%s lba=%u r=%u\n",
            wr ? "WR" : "RD", lba, (unsigned)r);
    kcon_direct = 0;
}

/* dev.c(#32 /dev/fda 相当、16bit セクタ = 先頭32MBのみ)が使う経路。ここでは
 * 使わない(下の disk_read/disk_write は 32bit 版 disk_raw_rw32 を使う)。 */
extern unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                                 unsigned sect, unsigned char op);
/* FatFs 本体用。LBA_t(32bit)をそのまま SD のブロック番号として渡せる
 * (ACMD41 を HCS=1 で送っているので SDHC/SDXC のブロックアドレッシング前提。
 * sdcard.s 側コメント参照)。 */
extern unsigned char disk_raw_rw32(unsigned char *buf, unsigned long sect,
                                   unsigned char op);
extern int sd_init(void);

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return sd_init() == 0 ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    unsigned long lba = (unsigned long)sector;
    unsigned char r;

    (void)pdrv;
    for (; count; count--, lba++, buff += 512) {
        r = disk_raw_rw32(buff, lba, 0);
#ifdef SD_DEBUG
        sdlog(0, (unsigned)lba, r);
#else
        if (r) sdlog(0, (unsigned)lba, r);
#endif
        if (r != 0)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    unsigned long lba = (unsigned long)sector;
    unsigned char *p = (unsigned char *)buff;
    unsigned char r;

    (void)pdrv;
    for (; count; count--, lba++, p += 512) {
        r = disk_raw_rw32(p, lba, 1);
#ifdef SD_DEBUG
        sdlog(1, (unsigned)lba, r);
#else
        if (r) sdlog(1, (unsigned)lba, r);
#endif
        if (r != 0)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv; (void)cmd; (void)buff;
    return RES_OK;
}
