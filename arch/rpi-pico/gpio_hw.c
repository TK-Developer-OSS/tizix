/* arch/rpi-pico/gpio_hw.c - GPIO の下回り(src/gpiodev.h の hook)。
 *   名前と意味は esp32-wroom-32e/cmd/esp32io.h の pin_* に揃えてある。 */
#include "hardware/gpio.h"
#include "gpiodev.h"

unsigned gpio_npins(void) { return 30; }

/* 守るピン: 16〜19 = SD カードの SPI0(diskio.c)。23 / 24 / 25 / 29 = Pico W の CYW43
 * (Wi-Fi チップ。25 は SPI の CS)。触ると SD か Wi-Fi が即死する。 */
const char *pin_check(unsigned p, int out)
{
    (void)out;
    if (p > 29)                      return "no such GPIO";
    if (p >= 16 && p <= 19)          return "GPIO16-19 are the SD card SPI (refused)";
    if (p == 23 || p == 24 || p == 25 || p == 29)
                                     return "GPIO23/24/25/29 are the Wi-Fi chip (refused)";
    return 0;
}

void pin_output(unsigned p, int on)
{
    gpio_set_function(p, GPIO_FUNC_SIO);   /* gpio_init() は出力値を 0 に戻すので使わない */
    gpio_set_dir(p, on ? GPIO_OUT : GPIO_IN);
}

void pin_set(unsigned p, int v)
{
    gpio_put(p, v);
}

/* 出力にしているピンは出している値を返す(パッドを読むと、外から引かれた時や
 * エミュレータで食い違う。toggle も出力値を基準にしたい)。入力はパッドの値。 */
int pin_get(unsigned p)
{
    if (gpio_get_dir(p) == GPIO_OUT)
        return gpio_get_out_level(p);
    return gpio_get(p);
}

int pin_is_output(unsigned p)
{
    return gpio_get_dir(p) == GPIO_OUT;
}

void pin_pull(unsigned p, int pull)
{
    gpio_set_pulls(p, pull == 1, pull == 2);
}
