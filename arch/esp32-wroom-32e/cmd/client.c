/* esp32-gpio / esp32-pwm / esp32-adc / esp32-i2c / esp32-spi / esp32-rgb(/bin/esp32-*.bin、#112)
 *
 *   どれもこの 1 本から作る(Makefile が -DDEV="gpio" などを渡す)。引数を装置名と一緒に
 *   常駐の esp32d へ郵便で出し、返事の文字列を表示して、返事の終了コードで終わる。
 *   使い方と中身は各装置の処理(cmd/gpio.c など)の冒頭。
 *
 *   esp32d は /etc/rc が起こす(要らなければ rc の行をコメントに)。居なければそう言って終わる
 *   (勝手には起こさない)。
 */
#include "stdio.h"
#include "mbox.h"
#include "esp32d.h"
#undef printf

static char req[ESP32D_REQMAX];
static char rep[ESP32D_REPMAX];

int main(int argc, char **argv)
{
    unsigned len = 0, k;
    int i, n;
    const char *s;

    for (i = -1; i < argc; i++) {
        s = i < 0 ? DEV : argv[i];
        k = (unsigned)strlen(s) + 1;
        if (len + k > sizeof req) {
            printf("esp32-%s: arguments too long\n", DEV);
            return 1;
        }
        while (k--)
            req[len++] = *s++;
    }

    n = mb_call(ESP32D_NAME, req, len, rep, sizeof rep);
    if (n < 1) {
        printf("esp32-%s: esp32d is not running (/etc/rc, or esp32d &)\n", DEV);
        return 1;
    }
    for (i = 1; i < n; i++)
        putchar((unsigned char)rep[i]);
    return rep[0];
}
