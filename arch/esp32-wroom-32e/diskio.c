/* FatFs disk I/O for arch/esp32-wroom-32e(tizix)。
 *
 *   ディスクはモジュール内蔵の SPI flash の一部(plat.h DISK_FLASH_OFF から
 *   DISK_FLASH_SIZE)。make が mkfs.fat + mtools で作った FAT 像を flash.bin の
 *   その位置へ焼き込む。QEMU でも実機でも同じ。SD カードは後の段(task.md #108)。
 *
 *   flash の読み書きは ROM の関数(esp_rom_spiflash_*、番地は link-kernel.ld)。
 *   ROM は flash 起動の時点で SPI を設定済みなので、そのまま呼べる。
 *   制約: 転送バッファは 4 バイト境界、書き込み前に 4KB 単位の消去が要る。
 *   FatFs は 512B セクタで書くので、4KB を読んで差し替え・消去・書き戻す。
 *   (書き込み回数の寿命は当面考えない。)
 */
#include "../../src/ff.h"
#include "../../src/diskio.h"
#include "plat.h"

#define SECTOR_SIZE   512
#define ERASE_SIZE    4096
#define NSECT         (DISK_FLASH_SIZE / SECTOR_SIZE)

/* ROM の関数(番地は link-kernel.ld)。ROM もカーネルも windowed なので、そのまま呼べる
 * (#109 までカーネルが call0 で、crt0.S の rom_call が窓を作って呼んでいた名残の形)。
 * 戻り値 0 = OK。 */
extern char tizix_rom_spiflash_read[];          /* (src, unsigned long *dst, len) */
extern char tizix_rom_spiflash_write[];         /* (dst, const unsigned long *src, len) */
extern char tizix_rom_spiflash_erase_sector[];  /* (sector) */
#define esp_rom_spiflash_read         tizix_rom_spiflash_read
#define esp_rom_spiflash_write        tizix_rom_spiflash_write
#define esp_rom_spiflash_erase_sector tizix_rom_spiflash_erase_sector
static unsigned long rom_call(void *fn, unsigned long p0, unsigned long p1, unsigned long p2)
{
    return ((unsigned long (*)(unsigned long, unsigned long, unsigned long))fn)(p0, p1, p2);
}
/* esp_rom_spiflash_unlock(書き込み保護の解除)は呼ばない。ESP32 の ROM には
 * 使える実装が無く、ESP-IDF 自身も ld に番地を持たず(0x400????? のまま)パッチ側で
 * 自前実装している。QEMU では保護が掛かっていないので不要。実機で保護ビットが
 * 立っていた場合は status register を自前で書く(task.md #108 未解決)。 */

static unsigned long ebuf[ERASE_SIZE / 4];   /* 4KB 消去単位の作業域(4B 境界) */

/* flash の処理は割込みを止めて行う(下の DISK_LOCK)。その間も UART には文字が届くので、
 * 処理を細かく区切り、切れ目ごとに受信 FIFO(128B = 115200bps で約 11ms)をソフトの
 * リングへ移す(console.c)。
 *   読み出し・書き込みは ROM 関数を CHUNK ずつ呼ぶ(以前は 4KB を 1 回で読み書きしていて、
 *   その間に FIFO があふれた。実機の rx が毎回 NAK になった原因)。
 *   消去(4KB)は ROM 関数 1 回のまま。実機(2026-10-02、COM4 の個体)では、これで rx の
 *   テストが通った。消去の時間は測っていないので、遅いフラッシュでは消去中にあふれうる。
 *   (SPI1 に SE を自分で出して待つ間に FIFO を移す版も試したが、書き込みが効かなく
 *   なったので戻した。原因は未特定。) */
#define CHUNK 256
extern void con_poll_rx(void);                /* console.c */

static unsigned char flash_erase4k(unsigned long addr)
{
    unsigned char r = rom_call(esp_rom_spiflash_erase_sector, addr / ERASE_SIZE, 0, 0) != 0;

    con_poll_rx();
    return r;
}

static unsigned char flash_read(unsigned long addr, unsigned long *dst, unsigned long len)
{
    unsigned long done;

    for (done = 0; done < len; done += CHUNK) {
        if (rom_call(esp_rom_spiflash_read, addr + done, (unsigned long)dst + done, CHUNK) != 0)
            return 1;
        con_poll_rx();
    }
    return 0;
}

static unsigned char flash_write(unsigned long addr, const unsigned long *src, unsigned long len)
{
    unsigned long done;

    for (done = 0; done < len; done += CHUNK) {
        if (rom_call(esp_rom_spiflash_write, addr + done, (unsigned long)src + done, CHUNK) != 0)
            return 1;
        con_poll_rx();
    }
    return 0;
}

/* ebuf は 1 個しか無いので、1 セクタぶんの処理の間は割込みを止める(syscall の途中で
 * IRQ_ON 済みのプロセスが切り替えられ、別のプロセスのディスク操作に ebuf を
 * 書き換えられるのを防ぐ)。入ったときの PS を控えて、出るときにそのまま戻す。 */
#ifdef PLAT_WIFI
/* WiFi 入りでは flash からも命令を読む(キャッシュ)。SPI1 で flash を読み書きしている間に
 * キャッシュが同じ flash を読みに行かないよう、その間はキャッシュを止める(ESP-IDF と同じ)。
 * 止めている間に走るのは IRAM のもの(このファイル・console.c・ROM)だけ。 */
extern void Cache_Read_Disable_rom(int cpu);
extern void Cache_Read_Enable_rom(int cpu);
extern int esp32_cache_mapped;                  /* kmain.c: WiFi を起こしてキャッシュを張ったら 1 */
#define CACHE_OFF()  do { if (esp32_cache_mapped) Cache_Read_Disable_rom(0); } while (0)
#define CACHE_ON()   do { if (esp32_cache_mapped) Cache_Read_Enable_rom(0); } while (0)
#else
#define CACHE_OFF()
#define CACHE_ON()
#endif
#define DISK_LOCK(ps)    do { __asm__ volatile ("rsil %0, 15" : "=r"(ps) :: "memory"); CACHE_OFF(); } while (0)
#define DISK_UNLOCK(ps)  do { CACHE_ON(); __asm__ volatile ("wsr.ps %0\n\trsync" :: "r"(ps) : "memory"); } while (0)

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

static unsigned char sect_read(unsigned char *buf, unsigned long lba)
{
    unsigned long ps;
    unsigned char r = 1;
    unsigned i;

    if (lba >= NSECT) return 1;
    DISK_LOCK(ps);
    if (flash_read(DISK_FLASH_OFF + lba * SECTOR_SIZE, ebuf, SECTOR_SIZE) == 0) {
        for (i = 0; i < SECTOR_SIZE; i++)
            buf[i] = ((unsigned char *)ebuf)[i];
        r = 0;
    }
    DISK_UNLOCK(ps);
    return r;
}

static unsigned char sect_write(const unsigned char *buf, unsigned long lba)
{
    unsigned long addr, base, ps;
    unsigned char *p = (unsigned char *)ebuf;
    unsigned char r = 1;
    unsigned i;

    if (lba >= NSECT) return 1;
    addr = DISK_FLASH_OFF + lba * SECTOR_SIZE;
    base = addr & ~(unsigned long)(ERASE_SIZE - 1);
    DISK_LOCK(ps);
    if (flash_read(base, ebuf, ERASE_SIZE) == 0) {
        for (i = 0; i < SECTOR_SIZE; i++)
            p[(addr - base) + i] = buf[i];
        if (flash_erase4k(base) == 0 && flash_write(base, ebuf, ERASE_SIZE) == 0)
            r = 0;
    }
    DISK_UNLOCK(ps);
    return r;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (; count; count--, buff += SECTOR_SIZE)
        if (sect_read(buff, (unsigned long)sector++)) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (; count; count--, buff += SECTOR_SIZE)
        if (sect_write(buff, (unsigned long)sector++)) return RES_ERROR;
    return RES_OK;
}

/* /dev/fda・/dev/fdb の生セクタ(src/dev.c の kdev_* が呼ぶ)。媒体は 1 つなので
 * drive は無視(z80board / m68k-mega と同じ)。op: 0=read 1=write。0=OK。 */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    (void)drive;
    return op ? sect_write(buf, sect) : sect_read(buf, sect);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
