/* esp32-i2c(esp32d の中の処理、#112。コマンド esp32-i2c は cmd/client.c が依頼を出すだけ): I2C マスタ(GPIO のビット打ち、約 100kHz 以下)。
 *
 *   esp32-i2c scan                     繋がっている 7 ビットアドレスを探す
 *   esp32-i2c read ADDR REG [N]        REG から N バイト読む(既定 1)
 *   esp32-i2c write ADDR BYTE...       バイト列を書く(先頭が REG のことが多い)
 *   先頭に -p SDA SCL でピンを変えられる(既定 SDA=21 SCL=22)。数は 10 進か 0x..。
 *
 *   オープンドレイン: 0 を出すときだけ出力 0、1 は手を離す(入力 + 内蔵プルアップ。外付け推奨)。
 *   ハードの I2C コントローラは使わない(ピンの自由と、レジスタ手順の少なさを取った)。
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

static unsigned SDA = 21, SCL = 22;

#define T 5                                     /* 半周期 us */

static void lo(unsigned p) { pin_set(p, 0); pin_output(p, 1); }
static void hi(unsigned p) { pin_output(p, 0); }   /* 手を離す */

static void start(void) { hi(SDA); hi(SCL); udelay(T); lo(SDA); udelay(T); lo(SCL); udelay(T); }
static void stop(void)  { lo(SDA); udelay(T); hi(SCL); udelay(T); hi(SDA); udelay(T); }

/* 1 バイト送る。戻り: 1 = ACK */
static int wbyte(unsigned b)
{
    int i, ack;
    for (i = 7; i >= 0; i--) {
        if ((b >> i) & 1) hi(SDA); else lo(SDA);
        udelay(T); hi(SCL); udelay(T); lo(SCL);
    }
    hi(SDA); udelay(T); hi(SCL); udelay(T);
    ack = !pin_get(SDA);
    lo(SCL); udelay(T);
    return ack;
}

static unsigned rbyte(int ack)
{
    unsigned v = 0;
    int i;
    hi(SDA);
    for (i = 0; i < 8; i++) {
        udelay(T); hi(SCL); udelay(T);
        v = (v << 1) | (unsigned)pin_get(SDA);
        lo(SCL);
    }
    if (ack) lo(SDA); else hi(SDA);
    udelay(T); hi(SCL); udelay(T); lo(SCL); hi(SDA);
    return v;
}

static int fail(const char *msg)
{
    printf("esp32-i2c: %s\n", msg);
    return 1;
}

int dev_i2c(int argc, char **argv)
{
    int a = 0;
    unsigned addr, i, n;
    const char *why;

    SDA = 21; SCL = 22;                                     /* 既定(esp32d は常駐なので毎回戻す) */
    if (argc >= 3 && !strcmp(argv[0], "-p")) {
        SDA = (unsigned)num(argv[1]);
        SCL = (unsigned)num(argv[2]);
        a = 3;
    }
    if ((why = pin_check(SDA, 1)) != 0 || (why = pin_check(SCL, 1)) != 0)
        return fail(why);
    pin_gpio(SDA); pin_gpio(SCL);
    pin_pull(SDA, 1); pin_pull(SCL, 1);
    hi(SDA); hi(SCL);
    if (argc - a < 1)
        return fail("usage: esp32-i2c [-p SDA SCL] scan | read ADDR REG [N] | write ADDR BYTE...");

    if (argv[a][0] == 's') {                                /* scan */
        n = 0;
        for (addr = 0x08; addr < 0x78; addr++) {
            start();
            if (wbyte(addr << 1)) { printf("0x%x\n", addr); n++; }
            stop();
        }
        printf("%u device(s) on SDA=%u SCL=%u\n", n, SDA, SCL);
        return 0;
    }
    if (argc - a < 3)
        return fail("usage: esp32-i2c [-p SDA SCL] scan | read ADDR REG [N] | write ADDR BYTE...");
    addr = (unsigned)num(argv[a + 1]);

    if (argv[a][0] == 'r') {                                /* read ADDR REG [N] */
        n = (argc - a >= 4) ? (unsigned)num(argv[a + 3]) : 1;
        start();
        if (!wbyte(addr << 1)) { stop(); return fail("no ACK from device"); }
        wbyte((unsigned)num(argv[a + 2]));
        start();                                            /* repeated start */
        if (!wbyte((addr << 1) | 1)) { stop(); return fail("no ACK on read"); }
        for (i = 0; i < n; i++)
            printf("%s0x%x", i ? " " : "", rbyte(i + 1 < n));
        printf("\n");
        stop();
        return 0;
    }
    if (argv[a][0] == 'w') {                                /* write ADDR BYTE... */
        start();
        if (!wbyte(addr << 1)) { stop(); return fail("no ACK from device"); }
        for (i = (unsigned)a + 2; i < (unsigned)argc; i++)
            if (!wbyte((unsigned)num(argv[i]))) { stop(); return fail("no ACK on data"); }
        stop();
        return 0;
    }
    return fail("usage: esp32-i2c [-p SDA SCL] scan | read ADDR REG [N] | write ADDR BYTE...");
}
