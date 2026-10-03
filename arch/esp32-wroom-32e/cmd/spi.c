/* esp32-spi(esp32d の中の処理、#112。コマンド esp32-spi は cmd/client.c が依頼を出すだけ): SPI マスタ(GPIO のビット打ち、モード 0、MSB 先)。
 *
 *   esp32-spi BYTE...                  CS を下げて BYTE を送り、同時に受けたバイトを出す
 *   先頭に -p SCK MOSI MISO CS でピンを変えられる(既定 18 23 19 5)。数は 10 進か 0x..。
 *
 *   ハードの SPI コントローラは使わない(SPI0/1 は flash が使っている。ピンの自由を取った)。
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

static unsigned SCK = 18, MOSI = 23, MISO = 19, CS = 5;

#define T 2                                     /* 半周期 us */

static unsigned xfer(unsigned b)
{
    unsigned v = 0;
    int i;
    for (i = 7; i >= 0; i--) {
        pin_set(MOSI, (b >> i) & 1);
        udelay(T);
        pin_set(SCK, 1);
        v = (v << 1) | (unsigned)pin_get(MISO);
        udelay(T);
        pin_set(SCK, 0);
    }
    return v;
}

int dev_spi(int argc, char **argv)
{
    int a = 0, i;
    const char *why;

    SCK = 18; MOSI = 23; MISO = 19; CS = 5;                 /* 既定(esp32d は常駐なので毎回戻す) */
    if (argc >= 5 && !strcmp(argv[0], "-p")) {
        SCK = (unsigned)num(argv[1]);
        MOSI = (unsigned)num(argv[2]);
        MISO = (unsigned)num(argv[3]);
        CS = (unsigned)num(argv[4]);
        a = 5;
    }
    if ((why = pin_check(SCK, 1)) || (why = pin_check(MOSI, 1)) || (why = pin_check(MISO, 0)) ||
        (why = pin_check(CS, 1))) {
        printf("esp32-spi: %s\n", why);
        return 1;
    }
    if (argc - a < 1) {
        printf("usage: esp32-spi [-p SCK MOSI MISO CS] BYTE...\n");
        return 1;
    }
    pin_set(CS, 1);  pin_output(CS, 1);
    pin_set(SCK, 0); pin_output(SCK, 1);
    pin_set(MOSI, 0); pin_output(MOSI, 1);
    pin_gpio(MISO);  pin_output(MISO, 0);

    pin_set(CS, 0);
    udelay(T);
    for (i = a; i < argc; i++)
        printf("%s0x%x", i > a ? " " : "", xfer((unsigned)num(argv[i])));
    printf("\n");
    udelay(T);
    pin_set(CS, 1);
    return 0;
}
