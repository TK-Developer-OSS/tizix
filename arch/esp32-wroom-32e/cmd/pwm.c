/* esp32-pwm(esp32d の中の処理、#112。コマンド esp32-pwm は cmd/client.c が依頼を出すだけ): LEDC(低速側)で PWM を出す。
 *
 *   esp32-pwm PIN FREQ DUTY     FREQ Hz、DUTY は 0〜100(%)。出しっぱなし(コマンドが終わっても続く)
 *   esp32-pwm PIN off           止めて出力 0
 *   esp32-pwm                   使っているチャネルの一覧
 *
 *   チャネルは 4 つまで(LEDC の低速チャネル 0〜3 を、それぞれ専用のタイマ 0〜3 で)。
 *   同じピンにもう一度出せば同じチャネルを使い直す。分解能は周波数から決める(最大 13 ビット)。
 *   クロックは APB 80MHz(WiFi を起こした後の値。起こしていないと周波数がずれる)。
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

#define DPORT_PERIP_CLK_EN  0x3FF000C0UL
#define DPORT_PERIP_RST_EN  0x3FF000C4UL
#define LEDC_BIT            (1UL << 11)

#define LEDC_BASE           0x3FF59000UL
#define LSCH_CONF0(c)       (LEDC_BASE + 0xA0 + 0x14 * (c))
#define LSCH_HPOINT(c)      (LEDC_BASE + 0xA4 + 0x14 * (c))
#define LSCH_DUTY(c)        (LEDC_BASE + 0xA8 + 0x14 * (c))
#define LSCH_CONF1(c)       (LEDC_BASE + 0xAC + 0x14 * (c))
#define LSTIMER_CONF(t)     (LEDC_BASE + 0x160 + 8 * (t))
#define LEDC_CONF           (LEDC_BASE + 0x190)

#define CONF0_SIG_OUT_EN    (1UL << 2)
#define CONF0_PARA_UP       (1UL << 4)
#define CONF1_DUTY_START    (1UL << 31)
#define CONF1_DUTY_INC      (1UL << 30)
#define TIMER_RST           (1UL << 24)
#define TIMER_TICK_SEL      (1UL << 25)     /* 1 = SLOW_CLK(LEDC_CONF の APB_CLK_SEL で APB) */
#define TIMER_PARA_UP       (1UL << 26)
#define LEDC_LS_SIG(c)      (79 + (c))      /* GPIO マトリクスの出力信号番号 */
#define NCH                 4

static int fail(const char *msg)
{
    printf("esp32-pwm: %s\n", msg);
    return 1;
}

/* このピンに繋いであるチャネル / 空いているチャネル。無ければ -1 */
static int find_ch(unsigned p)
{
    int c;
    unsigned long sel = REG(GPIO_OUT_SEL(p)) & 0x1FF;
    for (c = 0; c < NCH; c++)
        if (sel == (unsigned long)LEDC_LS_SIG(c) && (REG(LSCH_CONF0(c)) & CONF0_SIG_OUT_EN))
            return c;
    for (c = 0; c < NCH; c++)
        if (!(REG(LSCH_CONF0(c)) & CONF0_SIG_OUT_EN))
            return c;
    return -1;
}

int dev_pwm(int argc, char **argv)
{
    unsigned p, c, bits;
    unsigned long freq, duty, div = 0, maxd;
    const char *why;

    if (!(REG(DPORT_PERIP_CLK_EN) & LEDC_BIT)) {     /* LEDC のクロックを入れてリセットを外す */
        REG(DPORT_PERIP_CLK_EN) |= LEDC_BIT;
        REG(DPORT_PERIP_RST_EN) &= ~LEDC_BIT;
        REG(LEDC_CONF) = 1;                          /* APB_CLK_SEL: 低速側も APB 80MHz */
    }

    if (argc == 0) {
        printf("ch  pin  freq-div  duty\n");
        for (c = 0; c < NCH; c++) {
            if (!(REG(LSCH_CONF0(c)) & CONF0_SIG_OUT_EN))
                continue;
            for (p = 0; p < 40; p++)
                if (iomux_off[p] && (REG(GPIO_OUT_SEL(p)) & 0x1FF) == (unsigned long)LEDC_LS_SIG(c))
                    printf("%u   %u    %u  %u/%u\n", c, p, (unsigned)((REG(LSTIMER_CONF(c)) >> 5) & 0x3FFFF),
                           (unsigned)(REG(LSCH_DUTY(c)) >> 4),
                           (unsigned)(1UL << (REG(LSTIMER_CONF(c)) & 0x1F)));
        }
        return 0;
    }
    if (!isnum(argv[0]))
        return fail("usage: esp32-pwm [PIN FREQ DUTY% | PIN off]");
    p = (unsigned)num(argv[0]);
    if ((why = pin_check(p, 1)) != 0)
        return fail(why);

    if (argc >= 2 && argv[1][0] == 'o') {            /* off */
        for (c = 0; c < NCH; c++)
            if ((REG(GPIO_OUT_SEL(p)) & 0x1FF) == (unsigned long)LEDC_LS_SIG(c)) {
                REG(LSCH_CONF0(c)) &= ~CONF0_SIG_OUT_EN;
                REG(LSCH_CONF0(c)) |= CONF0_PARA_UP;
            }
        pin_set(p, 0);
        pin_output(p, 1);                            /* 普通の GPIO 出力 0 に戻す */
        return 0;
    }
    if (argc < 3 || !isnum(argv[1]) || !isnum(argv[2]))
        return fail("usage: esp32-pwm [PIN FREQ DUTY% | PIN off]");
    freq = num(argv[1]);
    duty = num(argv[2]);
    if (!freq || duty > 100)
        return fail("FREQ must be > 0 and DUTY 0..100");
    if ((int)(c = (unsigned)find_ch(p)) < 0 || c >= NCH)
        return fail("all 4 channels are in use (esp32-pwm PIN off to free one)");

    /* 分解能: 80MHz / (freq * 2^bits) の分周(8 ビット小数)が 1.0 以上・18 ビットに収まる一番細かいもの */
    /* (64 ビットの割り算は libgcc が要るので使わない。q = 80MHz / freq を桁ずらしで分周値にする) */
    for (bits = 13; bits >= 1; bits--) {
        unsigned long q = 80000000UL / freq, d;
        if (bits >= 8)
            d = q >> (bits - 8);
        else if (q >= (1UL << (10 + bits)))
            continue;
        else
            d = q << (8 - bits);
        if (d >= 256 && d < (1UL << 18)) { div = d; break; }
    }
    if (!div)
        return fail("frequency out of range (about 10Hz .. 40MHz)");
    maxd = 1UL << bits;

    REG(LSTIMER_CONF(c)) = TIMER_RST;
    REG(LSTIMER_CONF(c)) = bits | (div << 5) | TIMER_TICK_SEL;
    REG(LSTIMER_CONF(c)) |= TIMER_PARA_UP;

    REG(LSCH_HPOINT(c)) = 0;
    REG(LSCH_DUTY(c)) = (maxd * duty / 100) << 4;
    REG(LSCH_CONF0(c)) = c | CONF0_SIG_OUT_EN;       /* タイマ c */
    REG(LSCH_CONF1(c)) = CONF1_DUTY_START | CONF1_DUTY_INC | (1UL << 20) | (1UL << 10);
    REG(LSCH_CONF0(c)) |= CONF0_PARA_UP;

    pin_gpio(p);
    REG(GPIO_OUT_SEL(p)) = LEDC_LS_SIG(c);           /* GPIO マトリクスで LEDC の信号をピンへ */
    if (p < 32) REG(GPIO_ENABLE_W1TS) = 1UL << p; else REG(GPIO_ENABLE1_W1TS) = 1UL << (p - 32);
    printf("esp32-pwm: GPIO%u ch%u %uHz %u%% (%u-bit)\n", p, c, (unsigned)freq, (unsigned)duty, bits);
    return 0;
}
