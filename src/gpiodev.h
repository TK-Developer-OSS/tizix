#ifndef _GPIODEV_H
#define _GPIODEV_H

/* ==================================================================
 * gpiodev.h - GPIO の共通の話し方(ハードに依らない部分)
 *
 *   esp32-wroom-32e の esp32-gpio(arch/esp32-wroom-32e/cmd/gpio.c)と同じ文法:
 *     (空)                   使えるピンの一覧(向きと値)
 *     PIN                    値を読む(0 / 1)
 *     PIN 0|1                出力にして書く
 *     PIN toggle             出力を反転
 *     PIN in [up|down]       入力にする
 *   builtin の pico-gpio と /dev/gpio に同じ文字列が通る。/dev/gpioN は PIN を
 *   パス側が持つので、書き込む語は "1" "toggle" "in up" だけ。
 *
 *   ハード側(arch/<arch>/gpio_hw.c)が下の hook を実装する。名前と意味は
 *   esp32io.h の pin_* と揃えてある(esp32 側が将来この core に乗り換えられる)。
 * ================================================================== */

#define GPIO_ALL  0xFFFF            /* gpio_exec / gpio_read の pin: 一覧 */

/* hook: ハード側が実装 */
unsigned    gpio_npins(void);                       /* ピン番号の上限 + 1 */
const char *pin_check(unsigned p, int out);         /* 0 = 使ってよい / それ以外 = 理由 */
void        pin_output(unsigned p, int on);         /* on=1 出力 / 0 入力 */
void        pin_set(unsigned p, int v);
int         pin_get(unsigned p);
int         pin_is_output(unsigned p);
void        pin_pull(unsigned p, int pull);         /* 0 なし / 1 上 / 2 下 */

/* line = "PIN [WORD [WORD]]"(pin >= 0 なら PIN は渡さず WORD だけ)を実行する。
 * 結果の文字列(あれば)を out に入れる。戻り: 0 = OK / 1 = エラー(out に理由)。 */
int gpio_exec(int pin, const char *line, char *out, unsigned max);

/* /dev/gpio[N] の読み。pin=GPIO_ALL で一覧。out に文字列。長さを返す。 */
int gpio_read(unsigned pin, char *out, unsigned max);

/* "gpio15" "gpio" などの名前(コンポーネント)を調べる。戻り: 0..=ピン / GPIO_ALL / -1 */
int gpio_name_to_pin(const char *comp);

#endif
