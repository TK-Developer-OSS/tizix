/* esp32-gpio(esp32d の中の処理、#112。コマンド esp32-gpio は cmd/client.c が依頼を出すだけ): GPIO の読み書き。
 *
 *   esp32-gpio                    使えるピンの一覧(向きと値)
 *   esp32-gpio PIN                値を読む(0 / 1)
 *   esp32-gpio PIN 0|1            出力にして書く
 *   esp32-gpio PIN toggle         出力を反転
 *   esp32-gpio PIN in [up|down]   入力にする(プルアップ / ダウンは RTC 系のピンでは効かない)
 */
#include "esp32d.h"
#include "../cmd/esp32io.h"

static int fail(const char *msg)
{
    printf("esp32-gpio: %s\n", msg);
    return 1;
}

int dev_gpio(int argc, char **argv)
{
    unsigned p;
    const char *why;

    if (argc == 0) {
        printf("pin  dir  val\n");
        for (p = 0; p < 40; p++) {
            if (pin_check(p, 0))
                continue;
            printf("%u%s%s  %u\n", p, p < 10 ? "    " : "   ", pin_is_output(p) ? "out" : "in ",
                   (unsigned)pin_get(p));
        }
        return 0;
    }
    if (!isnum(argv[0]))
        return fail("usage: esp32-gpio [PIN [0|1|toggle|in [up|down]]]");
    p = (unsigned)num(argv[0]);

    if (argc == 1) {                                     /* 読む */
        if ((why = pin_check(p, 0)) != 0) return fail(why);
        if (!pin_is_output(p)) pin_gpio(p);              /* 入力として IO_MUX を GPIO に */
        printf("%u\n", (unsigned)pin_get(p));
        return 0;
    }
    if (argv[1][0] == 'i') {                             /* in [up|down] */
        int pull = 0;
        if ((why = pin_check(p, 0)) != 0) return fail(why);
        if (argc >= 3) pull = argv[2][0] == 'u' ? 1 : argv[2][0] == 'd' ? 2 : 0;
        pin_output(p, 0);
        pin_pull(p, pull);
        if (pull && pin_is_rtc(p))
            printf("esp32-gpio: note: pull-up/down on RTC GPIO %u is not effective yet\n", p);
        return 0;
    }
    if ((why = pin_check(p, 1)) != 0) return fail(why);
    if (argv[1][0] == 't') {                             /* toggle */
        int v = pin_is_output(p) ? !((p < 32 ? REG(GPIO_OUT) >> p : REG(GPIO_OUT1) >> (p - 32)) & 1) : 1;
        pin_set(p, v);
        pin_output(p, 1);
        printf("%d\n", v);
        return 0;
    }
    if (argv[1][0] == '0' || argv[1][0] == '1') {
        pin_set(p, argv[1][0] == '1');                   /* 値を先に置いてから出力にする(ひげを出さない) */
        pin_output(p, 1);
        return 0;
    }
    return fail("usage: esp32-gpio [PIN [0|1|toggle|in [up|down]]]");
}
