/* arch/esp32-wroom-32e/cmd/esp32d.h(#112): 常駐 esp32d と、その中の周辺機器の処理が共有する約束。
 *
 *   esp32d(/bin/esp32d.bin)が周辺機器(GPIO / PWM / ADC / I2C / SPI)を持ち、郵便受け "esp32d"
 *   (user/mbox.h)で依頼を受ける。esp32-gpio などのコマンドは依頼を出して返事を表示するだけ
 *   (cmd/client.c)。続く処理(PWM の出しっぱなし、長いサンプリング)は esp32d の側にあるので、
 *   コマンドは返事を受けたらすぐ終わる。
 *
 *   依頼:  "装置名\0引数1\0引数2\0…"(各語を NUL で区切る。長さは郵便で渡る)
 *   返事:  [終了コード 1 バイト][表示する文字列 …]
 *
 *   各装置の処理(gpio.c / pwm.c / adc.c / i2c.c / spi.c)は int dev_xxx(argc, argv) で、
 *   argv[0] は 1 個目の引数(tizix のコマンドと同じ並び)。printf は返事の文字列へ積む
 *   (下の define。実体は esp32d.c の rprintf)。
 */
#ifndef ESP32D_H
#define ESP32D_H

#include "stdio.h"
#include "string.h"

#define ESP32D_NAME   "esp32d"
#define ESP32D_REQMAX 256
#define ESP32D_REPMAX 1024

int rprintf(const char *fmt, ...);
#define printf rprintf

int dev_gpio(int argc, char **argv);
int dev_pwm(int argc, char **argv);
int dev_adc(int argc, char **argv);
int dev_i2c(int argc, char **argv);
int dev_spi(int argc, char **argv);
int dev_rgb(int argc, char **argv);

#endif
