/* esp32-adc(esp32d の中の処理、#112。コマンド esp32-adc は cmd/client.c が依頼を出すだけ): ADC1 を 1 回読む(12 ビット)。
 *
 *   esp32-adc PIN [N]     PIN は 32〜39(ADC1)。N 回読んで平均(既定 1)。減衰は 11dB(〜約 3.3V)
 *   esp32-adc             ADC1 の 8 本を全部 1 回ずつ
 *
 *   ADC2(GPIO0/2/4/12-15/25-27)は WiFi が使うので扱わない。
 *   表示の mV は校正なしの目安(raw * 3300 / 4095)。
 *   レジスタは ESP-IDF の soc/esp32/register/soc/sens_reg.h と rtc_io_reg.h で確かめた値。
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

#define SENS_BASE           0x3FF48800UL
#define SAR_READ_CTRL       (SENS_BASE + 0x00)
#define SAR_MEAS_WAIT2      (SENS_BASE + 0x0C)
#define SAR_START_FORCE     (SENS_BASE + 0x2C)
#define SAR_ATTEN1          (SENS_BASE + 0x34)
#define SAR_MEAS_START1     (SENS_BASE + 0x54)
#define SAR_TOUCH_CTRL1     (SENS_BASE + 0x58)

#define SAR1_DATA_INV       (1UL << 28)
#define SAR1_DIG_FORCE      (1UL << 27)
#define SAR1_EN_PAD_FORCE   (1UL << 31)
#define MEAS1_START_FORCE   (1UL << 18)
#define MEAS1_START_SAR     (1UL << 17)
#define MEAS1_DONE_SAR      (1UL << 16)

#define RTCIO_BASE          0x3FF48400UL

/* ADC1 のチャネル番号(ピン 32〜39) */
static int adc1_ch(unsigned p)
{
    static const signed char ch[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };   /* 32,33,34,35,36,37,38,39 */
    return (p >= 32 && p <= 39) ? ch[p - 32] : -1;
}

/* パッドを RTC 側(アナログ)に切り替える: MUX_SEL = 1、FUN_SEL = 0、デジタル入力は切る */
static void pad_analog(unsigned p)
{
    unsigned long reg, mux, fs, ie;

    switch (p) {
    case 36: reg = 0x7C; mux = 1UL << 27; fs = 22; ie = 1UL << 19; break;   /* SENSE1 */
    case 37: reg = 0x7C; mux = 1UL << 26; fs = 17; ie = 1UL << 14; break;   /* SENSE2 */
    case 38: reg = 0x7C; mux = 1UL << 25; fs = 12; ie = 1UL << 9;  break;   /* SENSE3 */
    case 39: reg = 0x7C; mux = 1UL << 24; fs = 7;  ie = 1UL << 4;  break;   /* SENSE4 */
    case 34: reg = 0x80; mux = 1UL << 29; fs = 26; ie = 1UL << 23; break;   /* ADC1 pad */
    case 35: reg = 0x80; mux = 1UL << 28; fs = 21; ie = 1UL << 18; break;   /* ADC2 pad */
    case 32: reg = 0x8C; mux = 1UL << 17; fs = 9;  ie = 1UL << 5;  break;   /* X32P */
    case 33: reg = 0x8C; mux = 1UL << 18; fs = 15; ie = 1UL << 11; break;   /* X32N */
    default: return;
    }
    REG(RTCIO_BASE + reg) = (REG(RTCIO_BASE + reg) & ~(3UL << fs) & ~ie) | mux;
}

static void adc1_setup(void)
{
    REG(SAR_READ_CTRL) = (REG(SAR_READ_CTRL) & ~SAR1_DIG_FORCE & ~(3UL << 16))
                         | SAR1_DATA_INV | (3UL << 16);          /* RTC 側で読む、12 ビット */
    REG(SAR_START_FORCE) = (REG(SAR_START_FORCE) & ~3UL) | 3UL;  /* SAR1_BIT_WIDTH = 12 ビット */
    REG(SAR_MEAS_WAIT2) |= 3UL << 18;                            /* FORCE_XPD_SAR: 電源を入れる */
    REG(SAR_TOUCH_CTRL1) |= (1UL << 26) | (1UL << 27);           /* ホールセンサを外す */
    REG(SAR_MEAS_START1) |= MEAS1_START_FORCE | SAR1_EN_PAD_FORCE;
}

static unsigned adc1_read(int ch)
{
    unsigned long v;
    unsigned spin = 0;

    REG(SAR_ATTEN1) = (REG(SAR_ATTEN1) & ~(3UL << (2 * ch))) | (3UL << (2 * ch));   /* 11dB */
    v = REG(SAR_MEAS_START1) & ~(0xFFFUL << 19) & ~MEAS1_START_SAR;
    REG(SAR_MEAS_START1) = v | (1UL << (19 + ch));
    REG(SAR_MEAS_START1) = v | (1UL << (19 + ch)) | MEAS1_START_SAR;
    while (!(REG(SAR_MEAS_START1) & MEAS1_DONE_SAR))
        if (++spin > 1000000) return 0xFFFF;
    return (unsigned)(REG(SAR_MEAS_START1) & 0xFFFF);
}

static void show(unsigned p, unsigned n)
{
    unsigned long sum = 0;
    unsigned i, v;
    int ch = adc1_ch(p);

    pad_analog(p);
    for (i = 0; i < n; i++) {
        v = adc1_read(ch);
        if (v == 0xFFFF) { printf("GPIO%u: timeout\n", p); return; }
        sum += v;
    }
    v = (unsigned)(sum / n);
    printf("GPIO%u (ADC1_CH%d): %u  (~%u mV)\n", p, ch, v, (unsigned)((unsigned long)v * 3300 / 4095));
}

int dev_adc(int argc, char **argv)
{
    unsigned p, n = 1;

    adc1_setup();
    if (argc == 0) {
        for (p = 32; p <= 39; p++)
            show(p, 1);
        return 0;
    }
    if (!isnum(argv[0]) || adc1_ch((unsigned)num(argv[0])) < 0) {
        printf("esp32-adc: PIN must be 32..39 (ADC1)\n");
        return 1;
    }
    p = (unsigned)num(argv[0]);
    if (argc >= 2 && isnum(argv[1]))
        n = (unsigned)num(argv[1]);
    if (n == 0 || n > 1000) n = 1;
    show(p, n);
    return 0;
}
