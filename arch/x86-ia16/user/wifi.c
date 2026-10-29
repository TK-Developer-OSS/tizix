/* arch/x86-ia16/user/wifi.c -- ESP-AT へ AT コマンドを 1 行送って応答を表示する(z80board の wifi の ia16 版。共有 user/wifi.c は z80board のリング前提なので Makefile の明示ルールで差し替え)
 *   使い方:
 *     wifi                      AT+CIFSR(IP アドレスの確認)
 *     wifi join SSID PASSWORD    AT+CWMODE=1 のあと AT+CWJAP="SSID","PASSWORD"(AP へ参加。ESP-AT はフラッシュに覚える)
 *     wifi AT+GMR               任意の AT コマンドをそのまま送る */
#include "tzstdio.h"

extern int net_atcmd(const char *cmd, unsigned ticks);

static void cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)) ; }

int main(int argc, char **argv)
{
    char cmd[100];
    if (argc >= 3 && !strcmp(argv[0], "join")) {
        net_atcmd("AT+CWMODE=1", 200);
        cmd[0] = 0;
        cat(cmd, "AT+CWJAP=\""); cat(cmd, argv[1]); cat(cmd, "\",\""); cat(cmd, argv[2]); cat(cmd, "\"");
        return net_atcmd(cmd, 2000) == 0 ? 0 : 1;
    }
    if (argc >= 1) return net_atcmd(argv[0], 1000) == 0 ? 0 : 1;
    return net_atcmd("AT+CIFSR", 200) == 0 ? 0 : 1;
}
