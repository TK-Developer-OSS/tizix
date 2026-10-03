/* arch/esp32-wroom-32e/cmd/esp32io.h(#112): 常駐 esp32d の中の周辺機器の処理が共有する GPIO の下回り。
 *
 *   周辺機器はカーネルのドライバではなく、常駐の esp32d(ユーザープロセス)がレジスタを叩き、
 *   esp32-* コマンドは郵便受けで依頼するだけ(cmd/esp32d.h)。カーネルが持つのは郵便受けだけ
 *   (8/31 の「中核最小化」と net.bin の前例に沿う)。
 *   番地と値は ESP32 Technical Reference Manual(GPIO / IO_MUX / LEDC)から。
 *
 *   守るピン: 6〜11(SPI flash。触ると即死)と 1 / 3(UART0 = コンソール)は使わせない。
 *   34〜39 は入力専用(出力・プルアップ不可)。20 / 24 / 28〜31 は存在しない。
 */
#ifndef ESP32IO_H
#define ESP32IO_H

#define REG(a)              (*(volatile unsigned long *)(a))

#define GPIO_BASE           0x3FF44000UL
#define GPIO_OUT_W1TS       (GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC       (GPIO_BASE + 0x0C)
#define GPIO_OUT1_W1TS      (GPIO_BASE + 0x14)
#define GPIO_OUT1_W1TC      (GPIO_BASE + 0x18)
#define GPIO_ENABLE_W1TS    (GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC    (GPIO_BASE + 0x28)
#define GPIO_ENABLE1_W1TS   (GPIO_BASE + 0x30)
#define GPIO_ENABLE1_W1TC   (GPIO_BASE + 0x34)
#define GPIO_ENABLE         (GPIO_BASE + 0x20)
#define GPIO_ENABLE1        (GPIO_BASE + 0x2C)
#define GPIO_OUT            (GPIO_BASE + 0x04)
#define GPIO_OUT1           (GPIO_BASE + 0x10)
#define GPIO_IN             (GPIO_BASE + 0x3C)
#define GPIO_IN1            (GPIO_BASE + 0x40)
#define GPIO_OUT_SEL(n)     (GPIO_BASE + 0x530 + 4 * (n))   /* GPIO_FUNCn_OUT_SEL_CFG */
#define SIG_GPIO_OUT        256                              /* 「GPIO_OUT の値をそのまま出す」 */

#define IO_MUX_BASE         0x3FF49000UL
#define MUX_WPD             (1UL << 7)
#define MUX_WPU             (1UL << 8)
#define MUX_IE              (1UL << 9)
#define MUX_SEL_SHIFT       12
#define MUX_SEL_MASK        (7UL << 12)
#define MUX_FUNC_GPIO       2                                /* どのピンも機能 2 が GPIO */

/* IO_MUX の各ピンのレジスタ(番地の並びは不規則。0 = そのピンは無い) */
static const unsigned short iomux_off[40] = {
    0x44, 0x88, 0x40, 0x84, 0x48, 0x6C, 0x60, 0x64, 0x68, 0x54,   /* 0-9 */
    0x58, 0x5C, 0x34, 0x38, 0x30, 0x3C, 0x4C, 0x50, 0x70, 0x74,   /* 10-19 */
    0x00, 0x7C, 0x80, 0x8C, 0x00, 0x24, 0x28, 0x2C, 0x00, 0x00,   /* 20-29 */
    0x00, 0x00, 0x1C, 0x20, 0x14, 0x18, 0x04, 0x08, 0x0C, 0x10,   /* 30-39 */
};

/* RTC 系のピン(IO_MUX のプルアップ / ダウンが効かない。ADC / タッチの道もこちら) */
static int pin_is_rtc(unsigned p)
{
    return p == 0 || p == 2 || p == 4 || (p >= 12 && p <= 15) || (p >= 25 && p <= 27) || p >= 32;
}

/* 0 = 使ってよい / それ以外 = 理由 */
static const char *pin_check(unsigned p, int out)
{
    if (p > 39 || !iomux_off[p])            return "no such GPIO";
    if (p >= 6 && p <= 11)                  return "GPIO6-11 are the SPI flash (refused)";
    if (p == 1 || p == 3)                   return "GPIO1/3 are the console UART (refused)";
    if (out && p >= 34)                     return "GPIO34-39 are input only";
    return 0;
}

/* ピンを GPIO 機能にする(入力は常に有効) */
static void pin_gpio(unsigned p)
{
    unsigned long a = IO_MUX_BASE + iomux_off[p];
    REG(a) = (REG(a) & ~MUX_SEL_MASK) | ((unsigned long)MUX_FUNC_GPIO << MUX_SEL_SHIFT) | MUX_IE;
}

/* pull: 0 = なし / 1 = プルアップ / 2 = プルダウン */
static void pin_pull(unsigned p, int pull)
{
    unsigned long a = IO_MUX_BASE + iomux_off[p];
    unsigned long v = REG(a) & ~(MUX_WPU | MUX_WPD);
    if (pull == 1) v |= MUX_WPU;
    if (pull == 2) v |= MUX_WPD;
    REG(a) = v;
}

static void pin_output(unsigned p, int on)
{
    pin_gpio(p);
    REG(GPIO_OUT_SEL(p)) = SIG_GPIO_OUT;
    if (on) {
        if (p < 32) REG(GPIO_ENABLE_W1TS) = 1UL << p; else REG(GPIO_ENABLE1_W1TS) = 1UL << (p - 32);
    } else {
        if (p < 32) REG(GPIO_ENABLE_W1TC) = 1UL << p; else REG(GPIO_ENABLE1_W1TC) = 1UL << (p - 32);
    }
}

static void pin_set(unsigned p, int v)
{
    if (p < 32) REG(v ? GPIO_OUT_W1TS : GPIO_OUT_W1TC) = 1UL << p;
    else        REG(v ? GPIO_OUT1_W1TS : GPIO_OUT1_W1TC) = 1UL << (p - 32);
}

static int pin_get(unsigned p)
{
    return p < 32 ? (int)((REG(GPIO_IN) >> p) & 1) : (int)((REG(GPIO_IN1) >> (p - 32)) & 1);
}

static int pin_is_output(unsigned p)
{
    return p < 32 ? (int)((REG(GPIO_ENABLE) >> p) & 1) : (int)((REG(GPIO_ENABLE1) >> (p - 32)) & 1);
}

/* 引数の数字(10 進。0x で 16 進) */
static unsigned long num(const char *s)
{
    unsigned long v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (s += 2; *s; s++) {
            int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10
                  : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
            if (d < 0) break;
            v = v * 16 + (unsigned long)d;
        }
        return v;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (unsigned long)(*s++ - '0');
    return v;
}

static int isnum(const char *s)
{
    return s && *s >= '0' && *s <= '9';
}

/* だいたい us 単位の空回し(CPU 80MHz。ビット打ち I2C / SPI の間合い用) */
static void udelay(unsigned us)
{
    unsigned long t0, t;
    __asm__ volatile ("rsr.ccount %0" : "=r"(t0));
    do { __asm__ volatile ("rsr.ccount %0" : "=r"(t)); } while (t - t0 < (unsigned long)us * 80);
}

#endif
