#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <stdbool.h>

#include "../../src/ff.h"
#include "../../src/diskio.h"

extern bool disk_force_sd(void);   /* diskio.c: SD だけを初期化(Flash に落とさない) */

#define SOH  0x01
#define STX  0x02
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x18
#define CRC_MODE 'C'

// CRC-16 (XMODEM) calculation
static uint16_t crc16_xmodem(const uint8_t *data, int len) {
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

static int rx_getc_timeout(uint32_t timeout_ms) {
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    while (absolute_time_diff_us(get_absolute_time(), timeout) > 0) {
        int c = getchar_timeout_us(10000);
        if (c >= 0) return c;
    }
    return -1;
}

void check_and_flash_xmodem_sd(void) {
    printf("\n========================================\n");
    printf("[Pico W SD Flasher] XMODEM mode activated\n");
    printf("Initializing SD card for raw flashing...\n");
    printf("========================================\n");

    if (!disk_force_sd()) {
        printf("[XMODEM] ERROR: SD card initialization failed!\n");
        return;
    }

    printf("[XMODEM] Ready to receive image via XMODEM...\n");
    printf("[XMODEM] Please start XMODEM transfer now...\n");

    uint8_t sector_buf[512];
    int sector_buf_idx = 0;
    LBA_t current_lba = 0;
    unsigned char seq = 1;
    bool started = false;
    int errors = 0;
    int crc_mode = 1;
    int init_tries = 0;
    unsigned long total_bytes = 0;
    unsigned long last_progress = 0;

    while (getchar_timeout_us(0) >= 0);

    for (;;) {
        if (!started) {
            putchar(crc_mode ? CRC_MODE : NAK);
        }

        int c = rx_getc_timeout(started ? 5000 : 3000);
        if (c < 0) {
            if (!started) {
                init_tries++;
                if (crc_mode && init_tries >= 10) {
                    crc_mode = 0;
                    init_tries = 0;
                } else if (!crc_mode && init_tries >= 10) {
                    printf("\n[XMODEM] Handshake timeout.\n");
                    return;
                }
                continue;
            } else {
                putchar(NAK);
                errors++;
                if (errors >= 20) {
                    printf("\n[XMODEM] Transfer timeout/errors exceeded.\n");
                    return;
                }
                continue;
            }
        }

        if (c == EOT) {
            putchar(ACK);
            if (sector_buf_idx > 0) {
                for (int i = sector_buf_idx; i < 512; i++) sector_buf[i] = 0;
                if (disk_write(0, sector_buf, current_lba, 1) != RES_OK) {
                    printf("\n[XMODEM] ERROR: disk_write failed at LBA %lu!\n", (unsigned long)current_lba);
                    return;
                }
                total_bytes += sector_buf_idx;
            }
            printf("\n[XMODEM] SUCCESS! Flashed %lu bytes to SD card.\n", total_bytes);
            printf("[XMODEM] Rebooting in 3 seconds...\n");
            sleep_ms(3000);
            watchdog_reboot(0, 0, 0);
            while (true);
        }

        if (c == SOH || c == STX) {
            started = true;
            int packet_size = (c == SOH) ? 128 : 1024;
            
            int blk_seq = rx_getc_timeout(5000);
            int blk_seq_comp = rx_getc_timeout(5000);
            if (blk_seq < 0 || blk_seq_comp < 0) {
                putchar(NAK);
                errors++;
                continue;
            }

            uint8_t packet[1024];
            int checksum_ok = 1;
            for (int i = 0; i < packet_size; i++) {
                int b = rx_getc_timeout(5000);
                if (b < 0) {
                    checksum_ok = 0;
                    break;
                }
                packet[i] = (uint8_t)b;
            }

            int ck1 = rx_getc_timeout(5000);
            int ck2 = crc_mode ? rx_getc_timeout(5000) : 0;
            if (ck1 < 0 || (crc_mode && ck2 < 0)) {
                putchar(NAK);
                errors++;
                continue;
            }

            if (!checksum_ok || blk_seq != (unsigned char)(255 - blk_seq_comp)) {
                putchar(NAK);
                errors++;
                continue;
            }

            if (blk_seq == (unsigned char)(seq - 1)) {
                putchar(ACK);
                continue;
            }

            if (blk_seq != seq) {
                putchar(NAK);
                errors++;
                continue;
            }

            if (crc_mode) {
                uint16_t calc_crc = crc16_xmodem(packet, packet_size);
                uint16_t recv_crc = ((uint16_t)ck1 << 8) | (uint16_t)ck2;
                if (calc_crc != recv_crc) {
                    putchar(NAK);
                    errors++;
                    continue;
                }
            } else {
                uint8_t checksum = 0;
                for (int i = 0; i < packet_size; i++) checksum += packet[i];
                if (checksum != (uint8_t)ck1) {
                    putchar(NAK);
                    errors++;
                    continue;
                }
            }

            for (int i = 0; i < packet_size; i++) {
                sector_buf[sector_buf_idx++] = packet[i];
                total_bytes++;
                if (sector_buf_idx >= 512) {
                    if (disk_write(0, sector_buf, current_lba, 1) != RES_OK) {
                        printf("\n[XMODEM] ERROR: disk_write failed at LBA %lu!\n", (unsigned long)current_lba);
                        return;
                    }
                    current_lba++;
                    sector_buf_idx = 0;
                    
                    if (total_bytes - last_progress >= 65536) {
                        printf("\r[XMODEM] Progress: %lu KB written...", total_bytes / 1024);
                        fflush(stdout);
                        last_progress = total_bytes;
                    }
                }
            }

            seq++;
            putchar(ACK);
            errors = 0;
        }
    }
}
