/* WiFi 実験(#109 W1): ROM ローダから起動 → flash 直接実行 → クロック(PLL)→ ... */
#include <stdint.h>
#include "soc/rtc.h"
#include "esp_private/esp_clk.h"

#define REG(a) (*(volatile uint32_t *)(a))

extern int ets_printf(const char *fmt, ...);
extern void Cache_Read_Disable_rom(int cpu);
extern void Cache_Read_Enable_rom(int cpu);
extern void Cache_Flush_rom(int cpu);
extern void mmu_init(int cpu);
extern int cache_flash_mmu_set_rom(int cpu, int pid, uint32_t vaddr, uint32_t paddr, int psize, int num);
extern void uart_div_modify(uint8_t uart_no, uint32_t div);
extern void uart_tx_wait_idle(uint8_t uart_no);
extern void ets_update_cpu_frequency_rom(uint32_t mhz);

#define DPORT_PRO_CACHE_CTRL1  0x3FF00044
/* バスのマスク。0x400C0000-0x403FFFFF(flash の命令。0x400D0000 もここ)は「IRAM0」の
 * バス、0x3F400000- は DROM0(ESP-IDF esp32 cache_ll_l1_get_bus と同じ対応)。
 * IROM0 のビットは 0x40800000- の話で、最初これを外して命令が bad00bad になった。 */
#define MASK_IBUS0  (1u << 0)
#define MASK_DROM0  (1u << 4)

#define EARLY __attribute__((section(".dram1")))
#define IRAM  __attribute__((section(".iram1")))
static EARLY const char m0[] = "\nwt: start (RAM)\n";

/* ウォッチドッグ(ROM が flash 起動時に RTC WDT と TIMG0 の MWDT を仕掛けたまま来る) */
IRAM static void wdt_off(void)
{
    REG(0x3FF5F064) = 0x50D83AA1; REG(0x3FF5F048) = 0; REG(0x3FF5F064) = 0;   /* TIMG0 */
    REG(0x3FF60064) = 0x50D83AA1; REG(0x3FF60048) = 0; REG(0x3FF60064) = 0;   /* TIMG1 */
    REG(0x3FF480A4) = 0x50D83AA1; REG(0x3FF4808C) = 0; REG(0x3FF480A4) = 0;   /* RTC */
}

IRAM void cache_map(void)
{
    Cache_Read_Disable_rom(0);
    Cache_Flush_rom(0);
    mmu_init(0);
    cache_flash_mmu_set_rom(0, 0, 0x3F400000, 0x040000, 64, 3);
    cache_flash_mmu_set_rom(0, 0, 0x400D0000, 0x070000, 64, 9);
    REG(DPORT_PRO_CACHE_CTRL1) &= ~(MASK_IBUS0 | MASK_DROM0);
    Cache_Read_Enable_rom(0);
}

/* CPU 80MHz / APB 80MHz(PLL)。WiFi はこれが前提。UART の分周も合わせ直す。 */
IRAM static void clock_init(void)
{
    rtc_clk_config_t cfg = RTC_CLK_CONFIG_DEFAULT();

    cfg.xtal_freq = SOC_XTAL_FREQ_40M;
    cfg.cpu_freq_mhz = 80;
    uart_tx_wait_idle(0);
    rtc_clk_init(cfg);
    ets_update_cpu_frequency_rom(80);
    uart_div_modify(0, (80000000u << 4) / 115200);
}

#include "osal.h"
static volatile int tcount;
static void tfn(void *arg)
{
    int i;
    for (i = 0; i < 3; i++) {
        ets_printf("  thread %s: %d at %u ms\n", (const char *)arg, i, (unsigned)os_ticks());
        os_delay(100);
    }
    tcount++;
}
static void tick_cb(void *arg) { (*(volatile int *)arg)++; }
static void thread_test(void)
{
    static uint32_t tm[5];
    static volatile int fired;
    os_thread_create(tfn, "A", 4096, "A", NULL);
    os_thread_create(tfn, "B", 4096, "B", NULL);
    os_timer_setfn(tm, tick_cb, (void *)&fired);
    os_timer_arm_ms(tm, 50, 1);
    while (tcount < 2)
        os_yield();
    os_timer_disarm(tm);
    ets_printf("wt: threads done, timer fired %d times, heap free %u\n", fired, (unsigned)os_heap_free());
}

static uint32_t ccount(void) { uint32_t v; __asm__ volatile ("rsr.ccount %0" : "=r"(v)); return v; }

IRAM int main(void)
{
    wdt_off();
    ets_printf(m0);
    cache_map();
    ets_printf("wt: cache mapped\n");
    clock_init();
    ets_printf("wt: clock: cpu %u MHz, apb %u Hz\n", (unsigned)(esp_clk_cpu_freq() / 1000000), (unsigned)esp_clk_apb_freq());
    {
        uint32_t t0 = ccount();
        extern void ets_delay_us(uint32_t);
        ets_delay_us(100000);
        ets_printf("wt: 100ms = %u cycles\n", (unsigned)(ccount() - t0));
    }
    thread_test();
    {
        extern void wifi_test(void);
        wifi_test();
    }
    ets_printf("wt: end\n");
    for (;;)
        os_yield();
}
