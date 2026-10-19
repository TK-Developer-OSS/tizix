/* user/rsyslog.c - 外部コマンド rsyslog
 *   rsyslog MESSAGE : カーネルの klog(drv_tbl[46])を叩き、/var/log/message へ
 *   "YYYY-MM-DD HH:MM:SS [pid] MESSAGE" を 1 行追記する(256 文字上限)。
 *
 *   複数単語のメッセージは sh のクォート対応トークナイズに従い、
 *   `rsyslog "boot ok"` のように引用符で 1 トークンにまとめること
 *   (echo と違い argv を連結しない ── ローカル配列を作って klog() へ渡すのは
 *   tzcc の「配列アドレスを変数へ代入すると +IY されない」既知の穴を踏む
 *   おそれがあるため避け、argv[0] をそのまま渡す)。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    if (argc < 1 || argv[0] == 0 || argv[0][0] == 0) {
        puts("rsyslog: usage: rsyslog MESSAGE");
        return 1;
    }
    klog(argv[0]);
    return 0;
}
