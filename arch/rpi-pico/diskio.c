#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include <string.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "../../src/ff.h"
#include "../../src/diskio.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SD_CS_LOW()   gpio_put(PIN_CS, 0)
#define SD_CS_HIGH()  gpio_put(PIN_CS, 1)

#define SD_CMD_TIMEOUT 100000

static bool sd_initialized = false;

/* ---- 内蔵 Flash の FAT(SD が無いときのドライブ 0)------------------------------
 *   flashfs.S がカーネルの像に抱かせた FAT イメージ(ビルドで /bin /etc /root を入れて
 *   mkfs.fat で作る)をそのまま読み、書きは 4KB の消去単位ごとに RAM へ取って直す
 *   (read-modify-write)。FatFs は 512B で書くが、Flash は 4KB 消してから 256B ずつしか
 *   書けないため。書き戻しは CTRL_SYNC(f_close / f_sync)と別の 4KB への移動のとき。
 *   消去・書き込み中は XIP が止まるので flash_safe_execute で両コアを止める(Core0 は
 *   kmain が flash_safe_execute_core_init 済み)。像は再書き込み(uf2)で初期化される。 */
extern const unsigned char flashfs_img[];
extern const unsigned char flashfs_end[];
#define FL_BLK 4096u
static bool     flash_ready = false;
static uint8_t  fl_cache[FL_BLK];
static int32_t  fl_cache_blk = -1;     /* fl_cache が写している 4KB 番号。-1 = 無し */
static bool     fl_dirty = false;

static uint32_t fl_bytes(void) { return (uint32_t)(flashfs_end - flashfs_img); }
static uint32_t fl_off(void)   { return (uint32_t)flashfs_img - XIP_BASE; }

struct fl_job { uint32_t off; const uint8_t *data; };
static void fl_do(void *p) {
    struct fl_job *j = (struct fl_job *)p;
    flash_range_erase(j->off, FL_BLK);
    flash_range_program(j->off, j->data, FL_BLK);
}

static bool fl_flush(void) {
    struct fl_job j;
    if (!fl_dirty || fl_cache_blk < 0) return true;
    j.off = fl_off() + (uint32_t)fl_cache_blk * FL_BLK;
    j.data = fl_cache;
    if (memcmp((const void *)(XIP_BASE + j.off), fl_cache, FL_BLK) != 0) {
        if (flash_safe_execute(fl_do, &j, 1000) != PICO_OK) return false;
    }
    fl_dirty = false;
    return true;
}

static bool fl_load(uint32_t blk) {
    if (fl_cache_blk == (int32_t)blk) return true;
    if (!fl_flush()) return false;
    memcpy(fl_cache, (const void *)(XIP_BASE + fl_off() + blk * FL_BLK), FL_BLK);
    fl_cache_blk = (int32_t)blk;
    return true;
}

static bool flashfs_ok(void) {
    uint32_t n = fl_bytes();
    return n >= 8 * FL_BLK && (n % FL_BLK) == 0 &&
           flashfs_img[510] == 0x55 && flashfs_img[511] == 0xAA;
}

static DRESULT fl_read(BYTE *buff, LBA_t sector, UINT count) {
    if ((uint32_t)(sector + count) * 512u > fl_bytes()) return RES_PARERR;
    for (UINT i = 0; i < count; i++, sector++) {
        uint32_t off = (uint32_t)sector * 512u;
        if ((int32_t)(off / FL_BLK) == fl_cache_blk)
            memcpy(buff + i * 512, fl_cache + (off % FL_BLK), 512);
        else
            memcpy(buff + i * 512, flashfs_img + off, 512);
    }
    return RES_OK;
}

static DRESULT fl_write(const BYTE *buff, LBA_t sector, UINT count) {
    if ((uint32_t)(sector + count) * 512u > fl_bytes()) return RES_PARERR;
    for (UINT i = 0; i < count; i++, sector++) {
        uint32_t off = (uint32_t)sector * 512u;
        if (!fl_load(off / FL_BLK)) return RES_ERROR;
        memcpy(fl_cache + (off % FL_BLK), buff + i * 512, 512);
        fl_dirty = true;
    }
    return RES_OK;
}

/* 1 = いまのドライブ 0 は SD / 0 = 内蔵 Flash(または未初期化) */
int disk_is_sd(void) { return sd_initialized; }

/* XMODEM の SD 書き込み用: Flash に落とさず、SD だけを初期化する。 */
bool disk_force_sd(void);

static void sd_spi_init(void) {
    spi_init(SPI_PORT, 100 * 1000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    SD_CS_HIGH();
    sleep_ms(10);
}

static void sd_spi_set_high_speed(void) {
    spi_set_baudrate(SPI_PORT, 25 * 1000 * 1000);
}

static uint8_t sd_spi_transfer(uint8_t tx) {
    uint8_t rx;
    spi_read_blocking(SPI_PORT, tx, &rx, 1);
    return rx;
}

static void sd_send_dummy_clocks(int count) {
    for (int i = 0; i < count; i++) {
        sd_spi_transfer(0xFF);
    }
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t response;
    uint8_t frame[6];
    
    frame[0] = cmd | 0x40;
    frame[1] = (arg >> 24) & 0xFF;
    frame[2] = (arg >> 16) & 0xFF;
    frame[3] = (arg >> 8) & 0xFF;
    frame[4] = arg & 0xFF;
    frame[5] = crc | 0x01;
    
    sd_spi_transfer(0xFF);
    for (int i = 0; i < 6; i++) {
        sd_spi_transfer(frame[i]);
    }
    
    for (int i = 0; i < 8; i++) {
        response = sd_spi_transfer(0xFF);
        if (response != 0xFF) break;
    }
    return response;
}

static uint32_t sd_send_cmd_get_r3_r7(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t response;
    uint8_t frame[6];
    
    frame[0] = cmd | 0x40;
    frame[1] = (arg >> 24) & 0xFF;
    frame[2] = (arg >> 16) & 0xFF;
    frame[3] = (arg >> 8) & 0xFF;
    frame[4] = arg & 0xFF;
    frame[5] = crc | 0x01;
    
    sd_spi_transfer(0xFF);
    for (int i = 0; i < 6; i++) {
        sd_spi_transfer(frame[i]);
    }
    
    for (int i = 0; i < 8; i++) {
        response = sd_spi_transfer(0xFF);
        if (response != 0xFF) break;
    }
    
    uint32_t result = 0;
    if (response != 0xFF) {
        result = sd_spi_transfer(0xFF) << 24;
        result |= sd_spi_transfer(0xFF) << 16;
        result |= sd_spi_transfer(0xFF) << 8;
        result |= sd_spi_transfer(0xFF);
    }
    return (result << 8) | response;
}

static bool sd_wait_ready(void) {
    uint32_t start = time_us_32();
    while (time_us_32() - start < SD_CMD_TIMEOUT) {
        if (sd_spi_transfer(0xFF) == 0xFF) return true;
    }
    return false;
}

static bool sd_init_card(void) {
    sd_send_dummy_clocks(80);
    
    SD_CS_LOW();
    uint8_t r1 = sd_send_cmd(0, 0, 0x95);
    SD_CS_HIGH();
    sd_spi_transfer(0xFF);
    
    if (r1 != 0x01) return false;
    
    SD_CS_LOW();
    uint32_t r7 = sd_send_cmd_get_r3_r7(8, 0x000001AA, 0x87);
    SD_CS_HIGH();
    sd_spi_transfer(0xFF);
    
    if ((r7 & 0xFF) != 0x01) return false;
    if (((r7 >> 8) & 0xFF) != 0xAA) return false;
    
    for (int retry = 0; retry < 1000; retry++) {
        SD_CS_LOW();
        sd_send_cmd(55, 0, 0xFF);
        SD_CS_HIGH();
        sd_spi_transfer(0xFF);
        
        SD_CS_LOW();
        r1 = sd_send_cmd(41, 0x40000000, 0xFF);
        SD_CS_HIGH();
        sd_spi_transfer(0xFF);
        
        if (r1 == 0x00) break;
        sleep_ms(10);
    }
    if (r1 != 0x00) return false;
    
    SD_CS_LOW();
    sd_send_cmd_get_r3_r7(58, 0, 0xFF);
    SD_CS_HIGH();
    sd_spi_transfer(0xFF);
    
    SD_CS_LOW();
    r1 = sd_send_cmd(16, 512, 0xFF);
    SD_CS_HIGH();
    sd_spi_transfer(0xFF);
    
    return r1 == 0x00;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    
    sd_spi_init();

    if (sd_init_card()) {
        sd_spi_set_high_speed();
        sd_initialized = true;
        return 0;
    }
    /* SD が無い / 応答しない → 内蔵 Flash の FAT */
    if (flashfs_ok()) {
        flash_ready = true;
        return 0;
    }
    return STA_NOINIT;
}

bool disk_force_sd(void) {
    sd_spi_init();
    if (!sd_init_card()) return false;
    sd_spi_set_high_speed();
    sd_initialized = true;
    return true;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return (sd_initialized || flash_ready) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_NOTRDY;
    if (!count) return RES_PARERR;
    if (!sd_initialized) return flash_ready ? fl_read(buff, sector, count) : RES_NOTRDY;
    
    for (UINT i = 0; i < count; i++) {
        uint8_t r1;
        
        SD_CS_LOW();
        r1 = sd_send_cmd(17, (uint32_t)sector * 512, 0xFF);
        if (r1 != 0x00) {
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
        }
        
        uint32_t start = time_us_32();
        uint8_t token;
        do {
            token = sd_spi_transfer(0xFF);
            if (token != 0xFF) break;
        } while (time_us_32() - start < 100000);
        
        if (token != 0xFE) {
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
        }
        
        for (int j = 0; j < 512; j++) {
            buff[i * 512 + j] = sd_spi_transfer(0xFF);
        }
        
        sd_spi_transfer(0xFF);
        sd_spi_transfer(0xFF);
        
        SD_CS_HIGH();
        sd_spi_transfer(0xFF);
        
        sector++;
    }
    
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_NOTRDY;
    if (!count) return RES_PARERR;
    if (!sd_initialized) return flash_ready ? fl_write(buff, sector, count) : RES_NOTRDY;
    
    for (UINT i = 0; i < count; i++) {
        SD_CS_LOW();
        uint8_t r1 = sd_send_cmd(24, (uint32_t)sector * 512, 0xFF);
        if (r1 != 0x00) {
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
        }
        
        sd_spi_transfer(0xFF);
        sd_spi_transfer(0xFE);
        
        for (int j = 0; j < 512; j++) {
            sd_spi_transfer(buff[i * 512 + j]);
        }
        
        sd_spi_transfer(0xFF);
        sd_spi_transfer(0xFF);
        
        uint8_t resp = sd_spi_transfer(0xFF);
        if ((resp & 0x1F) != 0x05) {
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
        }
        
        if (!sd_wait_ready()) {
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
        }
        
        SD_CS_HIGH();
        sd_spi_transfer(0xFF);
        sector++;
    }
    
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) return RES_NOTRDY;
    
    switch (cmd) {
        case CTRL_SYNC:
            if (!sd_initialized) {
                if (!flash_ready) return RES_NOTRDY;
                return fl_flush() ? RES_OK : RES_ERROR;
            }
            SD_CS_LOW();
            if (sd_wait_ready()) {
                SD_CS_HIGH();
                sd_spi_transfer(0xFF);
                return RES_OK;
            }
            SD_CS_HIGH();
            sd_spi_transfer(0xFF);
            return RES_ERROR;
            
        case GET_SECTOR_COUNT:
            if (!sd_initialized && flash_ready) {
                *(LBA_t*)buff = fl_bytes() / 512u;
                return RES_OK;
            }
            *(LBA_t*)buff = 0;
            return RES_PARERR;
            
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
            
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;
            
        default:
            return RES_PARERR;
    }
}
