/* arch/x86-i386-unreal/user/ntpdate.c -- ESP-AT の SNTP で現在時刻を取り、システム時計を合わせる(ia16 版 ntpdate)
 *   共有の user/ntpdate.c(Makefile の明示ルールでこちらに差し替え)は z80 の at_modem.py 経由の中継(ntpdate_srv.py)前提なので、ia16 は ESP-AT の
 *   SNTP(AT+CIPSNTPTIME?)を直接使う。tizix は表示にそのまま使える時刻(JST 加算済み)を保持する約束。
 *   使い方: ntpdate     取れたら time_set して表示 */
#include "tzstdio.h"

extern unsigned long net_sntp_time(void);

int main(int argc, char **argv)
{
    unsigned long t;
    (void)argc; (void)argv;
    t = net_sntp_time();
    if (!t) { puts("ntpdate: no time (ESP not connected / SNTP not synced)"); return 1; }
    time_set(t);
    prs("ntpdate: clock set, epoch "); prnuml(t); putchar('\n');
    return 0;
}
