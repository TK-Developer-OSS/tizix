/**
 * Dual-Core Wi-Fi & Tizix OS Boot Architecture for Raspberry Pi Pico W
 * Core 0: Hardware Control & Wi-Fi / lwIP Poll
 * Core 1: Tizix OS Kernel & Embedded Shell (Direct sh() Execution)
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/cyw43_arch.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/scb.h"
#include <stdio.h>
#include <stdbool.h>

// External declarations for Tizix builtin init, logger, and embedded shell
extern void builtin_init(void);
extern void klog_write(const char *msg);
extern void sh(void);
extern void check_and_flash_xmodem_sd(void);
extern void plt_interrupt(void);  // kernel.c

#include "../../src/kmem.h"  // KW_PIDTAB, PID_IDLE, PID_FREE, KW_SPTBL, KW_CURRENT

// IRQ save variable for IRQ_OFF/IRQ_ON macros
uint32_t __save_irq;

// Timer interrupt handler - called from hardware alarm IRQ
static void timer_irq_handler(void) {
    // Clear the alarm interrupt
    hw_clear_bits(&timer_hw->intr, 1u << 0);
    // Re-arm the alarm for next tick (TICK_HZ = 100 Hz = 10ms)
    timer_hw->alarm[0] = timer_hw->timerawl + (1000000 / TICK_HZ);
    // Call kernel tick handler
    plt_interrupt();
    // コンテキスト切替(PendSV)は未実装。loader.c の plat_load が未対応で、
    // 走るのは slot0(内蔵 sh)だけなので、ここでは tick を進めるだけ。
}

// Initialize hardware timer for kernel ticks (TICK_HZ Hz)
static void timer_init_kernel(void) {
    // Enable timer interrupt
    hw_set_bits(&timer_hw->inte, 1u << 0);
    // Set first alarm
    timer_hw->alarm[0] = timer_hw->timerawl + (1000000 / TICK_HZ);
    // Register IRQ handler
    irq_set_exclusive_handler(TIMER_IRQ_0, timer_irq_handler);
    irq_set_enabled(TIMER_IRQ_0, true);
}

// Stub for late init
void pico_wifi_late_init(void) {
    printf("[WiFi] Wi-Fi late init hook called.\n");
}

// 16-second boot countdown with XMODEM trigger check
// Runs AFTER stdio/UART initialization - matches z80board boot.s flow:
// UART init -> countdown with display -> main
// 起動時の待ち秒数。エミュレータの回帰用ビルドでは -DBOOT_COUNTDOWN_SEC=1 などで短くする
#ifndef BOOT_COUNTDOWN_SEC
#define BOOT_COUNTDOWN_SEC 16
#endif

static void boot_countdown_16s(void) {
    const uint LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    printf("\n========================================\n");
    printf("[Tizix Pico W] Boot countdown: %d seconds\n", BOOT_COUNTDOWN_SEC);
    printf("  Press 'x' to enter XMODEM SD flasher\n");
    printf("========================================\n");
    fflush(stdout);

    for (int sec = BOOT_COUNTDOWN_SEC; sec > 0; sec--) {
        // Print countdown every second
        printf("\r[Boot] %2d seconds remaining... ", sec);
        fflush(stdout);
        
        // Check for 'x' key during this 1-second interval (100ms x 10)
        bool xmodem_triggered = false;
        for (int i = 0; i < 10; i++) {
            int c = getchar_timeout_us(100000); // 100ms
            if (c == 'x' || c == 'X') {
                xmodem_triggered = true;
                break;
            }
            // Fast LED blink (50ms on, 50ms off)
            gpio_put(LED_PIN, 1);
            sleep_ms(50);
            gpio_put(LED_PIN, 0);
            sleep_ms(50);
        }
        
        if (xmodem_triggered) {
            printf("\n[XMODEM] 'x' detected. Entering SD flasher mode...\n\n");
            check_and_flash_xmodem_sd();
            // If check_and_flash_xmodem_sd returns, it means timeout or error
            // Re-print countdown header and continue
            printf("\n========================================\n");
            printf("[Tizix Pico W] Boot countdown: %d seconds\n", BOOT_COUNTDOWN_SEC);
            printf("  Press 'x' to enter XMODEM SD flasher\n");
            printf("========================================\n");
            fflush(stdout);
        }
    }
    printf("\n[Boot] Countdown complete. Starting system...\n\n");
    
    // Turn LED off after countdown
    gpio_put(LED_PIN, 0);
}

// Core 1 Entry: Runs Tizix OS Kernel and Embedded Shell
void core1_entry() {
    printf("\n[Core 1] Tizix OS booting on Core 1...\n");

    // 1. Initialize FAT mount, VFS, and drivers
    builtin_init();
    
    printf("tizix\n");
    klog_write("tizix boot");
    pico_wifi_late_init();
    
    printf("[Core 1] Launching embedded Tizix shell (sh) directly...\n");
    
    // 2. Launch embedded interactive shell directly
    sh();

    // Should not reach here
    while (true) {
        sleep_ms(1000);
    }
}

int main() {
    // Initialize onboard LED (GPIO 25) early
    const uint LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Initialize PID table (KW_PIDTAB) - required for PLAT_FLAT32
    // slot0 = kernel/shell (PID_IDLE = 1), slots 1..3 = free (PID_FREE = 0)
    volatile unsigned char *pid = (volatile unsigned char *)KW_PIDTAB;
    pid[0] = PID_IDLE;
    for (int i = 1; i < 4; i++) pid[i] = PID_FREE;

    // Initialize stdio (USB CDC) FIRST - like z80board UART init
    stdio_init_all();

    // Wait for USB CDC connection (up to 3 seconds) with fast LED blinking
    int wait_count = 0;
    while (!stdio_usb_connected() && wait_count < 30) {
        gpio_put(LED_PIN, 1);
        sleep_ms(50);
        gpio_put(LED_PIN, 0);
        sleep_ms(50);
        wait_count++;
    }

    printf("\n========================================\n");
    printf("[Tizix Pico W] Dual-Core Wi-Fi & OS Boot\n");
    printf("  - Core 0: Hardware & Wi-Fi / lwIP Poll\n");
    printf("  - Core 1: Tizix OS (Kernel & Embedded Shell)\n");
    printf("========================================\n");

    // 16-second boot countdown with XMODEM trigger check
    boot_countdown_16s();

    // Initialize kernel timer (hardware alarm) for ticks
    timer_init_kernel();

    // Initialize CYW43 Wi-Fi architecture on Core 0
    if (cyw43_arch_init()) {
        printf("[Core 0] ERROR: cyw43_arch_init failed!\n");
    } else {
        cyw43_arch_enable_sta_mode();
        printf("[Core 0] CYW43 Wi-Fi STA mode initialized successfully.\n");
    }

    // 内蔵 Flash の書き込み(diskio.c)は flash_safe_execute で Core0 を止める。
    // Core0 を「止められる側」にしておく(Core1 の起動前に)。
    flash_safe_execute_core_init();

    printf("[Core 0] Launching Tizix OS on Core 1...\n");
    multicore_launch_core1(core1_entry);

    // Core 0 loop: Wi-Fi / lwIP polling and hardware monitoring
    int count = 0;
    while (true) {
        cyw43_arch_poll();  // Keep Wi-Fi and lwIP stack active
        gpio_put(LED_PIN, (count / 20) & 1);  // Slow blink (2Hz)
        sleep_ms(50);
        count++;
    }

    return 0;
}
