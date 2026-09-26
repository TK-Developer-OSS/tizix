/* user/uptime.c - 外部コマンド uptime(#56。旧ビルトインの外部化)
 *
 *   uptime : 起動からの秒数を表示する("up N sec")。
 *
 *   カーネルベクタ getticks()(0x0044、TZVEC で全コマンドに束縛済み)の
 *   生 tick を TICK_HZ(100)で秒に畳む。旧ビルトインの uptime_sec() と同じ
 *   計算で、tick は 16bit なので **655 秒で一周する**(旧ビルトインと同じ制約。
 *   長時間の稼働時間は date の Unix 秒の差で見る)。
 *   z80board の TICK_HZ は未較正(src/kernel.h。#59 の TK 判断で後回し)。
 *
 *   掟(tzcc): 除算を書かない(引き算のループで畳む)/ printf 不使用。
 */
#include "stdio.h"

#define TICK_HZ 100

extern unsigned int getticks(void) __sdcccall(0);

int main(int argc, char **argv)
{
    unsigned up_t;
    unsigned up_s;

    (void)argc;
    (void)argv;
    up_t = getticks();
    up_s = 0;
    while (up_t >= TICK_HZ) {
        up_t = up_t - TICK_HZ;
        up_s++;
    }
    prs("up ");
    prnum(up_s);
    puts(" sec");
    return 0;
}
