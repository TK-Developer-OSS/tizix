/* esp32-rgb(esp32d の中の処理、#112。コマンド esp32-rgb は cmd/client.c が依頼を出すだけ): WS2812(NeoPixel)1 個に色を出す。
 *
 *   esp32-rgb R G B [PIN]       0〜255 で色を出す(既定 PIN 16)
 *   esp32-rgb red|green|blue|yellow|cyan|magenta|white [PIN]   控えめの明るさ(20/255)で
 *   esp32-rgb off [PIN]         消す
 *
 *   Freenove の ESP32-WROOM-32E ボードは GPIO16 に WS2812 が 1 個載っている(TK の Arduino スケッチ
 *   ESP32_WROOM_32E_CDC_WIFI の LEDS_PIN 16 / TYPE_GRB から)。色は G → R → B の順に 8 ビットずつ、MSB 先。
 *
 *   信号は 1 ビット 1.25us、0 は High 0.35us / 1 は High 0.7us、最後に Low を 300us 以上(ラッチ)。
 *   CPU のクロックは WiFi を起こすと 80MHz、起こさないと水晶の 40MHz なので、クロックの設定を
 *   読んでから CCOUNT の刻みを決める。送る間(約 30us)だけ割込みを止める。
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

static unsigned long ccount(void)
{
    unsigned long v;
    __asm__ volatile ("rsr.ccount %0" : "=r"(v));
    return v;
}

/* 1us あたりの CCOUNT = CPU の MHz。クロックの設定レジスタから読む(tick で測ると、その間に
 * 他のプロセスへ順番が回ったぶん狂う)。
 *   RTC_CNTL_CLK_CONF の SOC_CLK_SEL(bit 28-27): 0 = 水晶 / 1 = PLL
 *   PLL のとき DPORT_CPU_PER_CONF の CPUPERIOD_SEL(bit 1-0): 0 = 80 / 1 = 160 / 2 = 240MHz
 *   水晶のときは 40MHz(WROOM-32E。前置分周は ROM の既定の 0 のまま) */
#define RTC_CNTL_CLK_CONF   0x3FF48070UL
#define DPORT_CPU_PER_CONF  0x3FF0003CUL
static unsigned long cycles_per_us(void)
{
    static const unsigned char pll[4] = { 80, 160, 240, 240 };
    if (((REG(RTC_CNTL_CLK_CONF) >> 27) & 3) == 1)
        return pll[REG(DPORT_CPU_PER_CONF) & 3];
    return 40;
}

static void send(unsigned p, const unsigned char *grb, unsigned n, unsigned long mhz)
{
    unsigned long t0h = mhz * 35 / 100, t1h = mhz * 70 / 100, per = mhz * 125 / 100;
    unsigned long bit = p < 32 ? 1UL << p : 1UL << (p - 32);
    volatile unsigned long *set = (volatile unsigned long *)(p < 32 ? GPIO_OUT_W1TS : GPIO_OUT1_W1TS);
    volatile unsigned long *clr = (volatile unsigned long *)(p < 32 ? GPIO_OUT_W1TC : GPIO_OUT1_W1TC);
    unsigned long ps, t;
    unsigned i;
    int b;

    __asm__ volatile ("rsil %0, 15" : "=r"(ps) :: "memory");
    t = ccount();
    for (i = 0; i < n; i++)
        for (b = 7; b >= 0; b--) {
            unsigned long h = ((grb[i] >> b) & 1) ? t1h : t0h;
            while (ccount() - t < per)
                ;
            t = ccount();
            *set = bit;
            while (ccount() - t < h)
                ;
            *clr = bit;
        }
    __asm__ volatile ("wsr %0, ps; rsync" :: "r"(ps) : "memory");
    udelay(300);                                    /* ラッチ */
}

static const struct { const char *name; unsigned char r, g, b; } named[] = {
    { "red", 20, 0, 0 }, { "green", 0, 20, 0 }, { "blue", 0, 0, 20 },
    { "yellow", 20, 20, 0 }, { "cyan", 0, 20, 20 }, { "magenta", 20, 0, 20 },
    { "white", 20, 20, 20 }, { "off", 0, 0, 0 },
};

#define USAGE "usage: esp32-rgb R G B [PIN] | red|green|blue|yellow|cyan|magenta|white|off [PIN]"

int dev_rgb(int argc, char **argv)
{
    unsigned p = 16, k, r, g, b;
    unsigned char grb[3];
    unsigned long mhz;
    const char *why;
    int a;

    if (argc >= 3 && isnum(argv[0]) && isnum(argv[1]) && isnum(argv[2])) {
        r = (unsigned)num(argv[0]); g = (unsigned)num(argv[1]); b = (unsigned)num(argv[2]);
        a = 3;
    } else if (argc >= 1) {
        for (k = 0; k < sizeof named / sizeof named[0]; k++)
            if (strcmp(argv[0], named[k].name) == 0)
                break;
        if (k == sizeof named / sizeof named[0]) {
            printf("esp32-rgb: %s\n", USAGE);
            return 1;
        }
        r = named[k].r; g = named[k].g; b = named[k].b;
        a = 1;
    } else {
        printf("esp32-rgb: %s\n", USAGE);
        return 1;
    }
    if (r > 255 || g > 255 || b > 255) {
        printf("esp32-rgb: R G B must be 0..255\n");
        return 1;
    }
    if (argc > a)
        p = (unsigned)num(argv[a]);
    if ((why = pin_check(p, 1)) != 0) {
        printf("esp32-rgb: %s\n", why);
        return 1;
    }

    pin_set(p, 0);
    pin_output(p, 1);
    mhz = cycles_per_us();
    udelay(300);
    grb[0] = (unsigned char)g; grb[1] = (unsigned char)r; grb[2] = (unsigned char)b;
    send(p, grb, 3, mhz);
    return 0;
}
