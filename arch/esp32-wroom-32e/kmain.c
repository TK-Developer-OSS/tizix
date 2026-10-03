/* arch/esp32-wroom-32e kmain.c
 * crt0.S から来る。ウォッチドッグを止め、tick のタイマを仕掛け、slot0 を runnable にして
 * 共有 src/ の init()(kernel_init → FAT マウント → /bin/sh.bin 起動)へ進む
 * (m68k-mega と同じ流れ)。例外入口(crt0.S)から呼ばれる C もここに置く。
 */
#include "kmem.h"
#include "io.h"
#include "kernel.h"

#define REG(a) (*(volatile unsigned long *)(a))

/* CPU の特殊レジスタ(CCOUNT / CCOMPARE0 / INTERRUPT / INTENABLE) */
#define RSR(r)     ({ unsigned long _v; __asm__ volatile ("rsr." #r " %0" : "=r"(_v)); _v; })
#define WSR(r, v)  __asm__ volatile ("wsr." #r " %0\n\trsync" :: "r"((unsigned long)(v)))

#define TIMER_BIT     (1UL << TIMER_INTNO)
static unsigned long tick_cycles = CPU_HZ / TICK_HZ;     /* PLL を起こしたら作り直す */
#define TICK_CYCLES   tick_cycles

/* タイマ以外の割込み(WiFi など)。net/osi.c が登録する。番号 = CPU の割込み番号 */
static struct { void (*f)(void *); void *arg; } isr_tab[32];
void esp32_set_isr(int n, void (*f)(void *), void *arg)
{
    if (n >= 0 && n < 32 && n != TIMER_INTNO) { isr_tab[n].f = f; isr_tab[n].arg = arg; }
}
void esp32_ints_on(unsigned long mask)
{
    unsigned long ps;
    __asm__ volatile ("rsil %0, 15" : "=r"(ps) :: "memory");
    WSR(intenable, RSR(intenable) | mask);
    __asm__ volatile ("wsr.ps %0\n\trsync" :: "r"(ps) : "memory");
}
void esp32_ints_off(unsigned long mask)
{
    unsigned long ps;
    __asm__ volatile ("rsil %0, 15" : "=r"(ps) :: "memory");
    WSR(intenable, RSR(intenable) & ~mask);
    __asm__ volatile ("wsr.ps %0\n\trsync" :: "r"(ps) : "memory");
}

#ifdef PLAT_WIFI
/* WiFi のバイナリは flash から直接走らせる(IRAM に入らない)。ROM ローダは RAM のセグメントしか
 * 載せないので、キャッシュ MMU を自分で張る(Makefile が drom.bin / irom.bin をこの位置へ書く):
 *   0x3F400000(DROM)← flash DROM_FLASH_OFF、0x400D0000(IROM)← flash IROM_FLASH_OFF。
 * 0x400D0000 の命令のバスのマスクは DPORT_PRO_CACHE_CTRL1 の bit0(IRAM0)。bit2(IROM0)は
 * 0x40800000- の話で、取り違えると命令が bad00bad で読める(wifi/ の実験で踏んだ)。 */
extern void Cache_Read_Disable_rom(int cpu);
extern void Cache_Read_Enable_rom(int cpu);
extern void Cache_Flush_rom(int cpu);
extern void mmu_init(int cpu);
extern int cache_flash_mmu_set_rom(int cpu, int pid, unsigned long vaddr, unsigned long paddr, int psize, int num);
/* WiFi を起こすとき(net/wifi.c)だけ張る。張ったら diskio.c は flash の読み書きの間キャッシュを止める。
 * 起動時に常に張っていた版は、/etc/wifi が無くても QEMU の回帰が毎回いくつか落ちた
 * (WiFi 無しのビルドは同じ時間帯に 2 周とも全部通った。QEMU の中で何が遅くなるのかは調べていない)。 */
int esp32_cache_mapped;
void esp32_cache_map(void)
{
    esp32_cache_mapped = 1;
    Cache_Read_Disable_rom(0);
    Cache_Flush_rom(0);
    mmu_init(0);
    cache_flash_mmu_set_rom(0, 0, 0x3F400000UL, DROM_FLASH_OFF, 64, DROM_FLASH_SIZE / 0x10000);
    cache_flash_mmu_set_rom(0, 0, 0x400D0000UL, IROM_FLASH_OFF, 64, IROM_FLASH_SIZE / 0x10000);
    REG(0x3FF00044) &= ~((1UL << 0) | (1UL << 4));     /* DPORT_PRO_CACHE_CTRL1: IRAM0 / DROM0 のバスを開ける */
    Cache_Read_Enable_rom(0);
}

/* CPU / APB 80MHz(PLL)。WiFi はこれが前提。ESP-IDF の rtc_clk_init(ビルド済みの
 * libesp_hw_support.a)を借りる。UART の分周と tick の周期も合わせ直す。
 * WiFi を起こすとき(net/wifi.c、/etc/wifi があるとき)だけ呼ぶ ── QEMU はこの切り替えで
 * 止まる(調べていない)ので、/etc/wifi の無い回帰は今までどおり 40MHz で回る。 */
#include "soc/rtc.h"
extern void uart_div_modify(unsigned char uart_no, unsigned long div);
extern void uart_tx_wait_idle(unsigned char uart_no);
extern void ets_update_cpu_frequency_rom(unsigned long mhz);
void esp32_clock_pll(void)
{
    rtc_clk_config_t cfg = RTC_CLK_CONFIG_DEFAULT();

    cfg.xtal_freq = SOC_XTAL_FREQ_40M;
    cfg.cpu_freq_mhz = 80;
    uart_tx_wait_idle(0);
    rtc_clk_init(cfg);
    ets_update_cpu_frequency_rom(80);
    uart_div_modify(0, (80000000UL << 4) / 115200);
    tick_cycles = 80000000UL / TICK_HZ;
    /* 切り替えの後、タイマ(CCOMPARE0)が過去を指したまま止まっていた(実機)。仕掛け直す。 */
    WSR(ccompare0, RSR(ccount) + tick_cycles);
}
#endif

extern void init(void);        /* src/init.c */
extern void con_putc(char c);  /* console.c */
extern void con_poll_rx(void); /* console.c */

static void wdt_off(void)
{
    REG(TIMG0_WDTWPROTECT) = WDT_WKEY;
    REG(TIMG0_WDTCONFIG0)  = 0;
    REG(TIMG0_WDTWPROTECT) = 0;
    REG(TIMG1_WDTWPROTECT) = WDT_WKEY;
    REG(TIMG1_WDTCONFIG0)  = 0;
    REG(TIMG1_WDTWPROTECT) = 0;
    REG(RTC_WDTWPROTECT)   = WDT_WKEY;
    REG(RTC_WDTCONFIG0)    = 0;
    REG(RTC_WDTWPROTECT)   = 0;
}

/* レベル 1 割込み(crt0.S exc_irq から。割込みは止まっている)。
 *   タイマ: CCOMPARE0 を 1 周期先へ進める(書き込みが割込み要求を下ろす)。
 *   flash の消去(rom_call)のように長く割込みを止めたあとは何周期も遅れているので、
 *   CCOUNT に追いつくまで回して tick を数え直す ── 進めた先がもう過去だと、
 *   次に一致するのは CCOUNT が一周したあと(40MHz で 107 秒後)になってしまう。 */
void esp32_irq(void)
{
    unsigned long pend = RSR(interrupt) & RSR(intenable);

    if (pend & TIMER_BIT) {
        unsigned long cmp = RSR(ccompare0);
        do {
            cmp += TICK_CYCLES;
            WSR(ccompare0, cmp);
            plt_interrupt();
        } while ((long)(RSR(ccount) - cmp) >= 0);
        pend &= ~TIMER_BIT;
        con_poll_rx();      /* UART の受信 FIFO をリングへ(誰も読まない間にあふれさせない) */
    }
    while (pend) {
        int n = __builtin_ctz(pend);
        pend &= pend - 1;
        if (isr_tab[n].f)
            isr_tab[n].f(isr_tab[n].arg);
        else
            WSR(intenable, RSR(intenable) & ~(1UL << n));   /* 身に覚えのない割込みは止める(鳴りっぱなし防止) */
    }
}

/* 障害の報告。UART へ直接
 *     *** EXC cause=nn PC=xxxxxxxx VADDR=xxxxxxxx slot=n ***
 *   cause は Xtensa の EXCCAUSE(0=不正命令 / 2,3=命令・データのバスエラー /
 *   6=0 除算 / 9=境界違反の 32bit アクセス / 20,28,29=番地違反)。 */
static void rputs(const char *s)
{
    /* kputchar は通さない(出力がパイプやリダイレクトへ向いていても端末に出す) */
    for (; *s; s++) {
        if (*s == '\n') con_putc('\r');
        con_putc(*s);
    }
}

static void rhex(unsigned long v, int digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4)) & 0xF]);
}

static void report(unsigned long what, unsigned long pc, unsigned long vaddr)
{
    rputs("\n*** EXC cause=");  rhex(what, 2);
    rputs(" PC=");              rhex(pc, 8);
    rputs(" VADDR=");           rhex(vaddr, 8);
    rputs(" slot=");            rhex(*(volatile unsigned char *)KW_CURRENT, 1);
    rputs(" ***\n");
}

/* crt0.S exc_entry から(割込み・syscall 以外の例外)。外部コマンドの中なら
 * 戻る(呼び出し側がその枠を畳む)。カーネル(slot0)なら続けようが無いので止まる。 */
void esp32_fault(unsigned long cause, unsigned long *frame, unsigned long vaddr)
{
    report(cause, frame[CTX_PC / 4], vaddr);
    if (*(volatile unsigned char *)KW_CURRENT != 0)
        return;
    rputs("kernel halted\n");
    for (;;)
        ;
}

/* crt0.S vec_fatal から(来るはずのないベクタ。what = 0x1n 窓 / 0x2n 高位割込み・Double) */
void esp32_panic(unsigned long what, unsigned long pc, unsigned long vaddr, unsigned long cause)
{
    report(what, pc, vaddr);
    rputs("bad vector, exccause=");  rhex(cause, 2);
    rputs(". halted\n");
    for (;;)
        ;
}

void kmain(void)
{
    wdt_off();
    {
        extern void con_init(void);
        con_init();                     /* UART の受信割込み(割込みはまだ止まっている) */
    }
    *(volatile unsigned char *)KW_PIDTAB = PID_IDLE;   /* slot0 = kernel/init */

    /* tick。割込みはまだ止まっている(解くのは src/kernel.c kernel_init の IRQ_ON)。 */
    WSR(ccompare0, RSR(ccount) + TICK_CYCLES);
    WSR(intenable, RSR(intenable) | TIMER_BIT);   /* con_init の UART の分は残す */

    init();

    for (;;)
        ;
}
