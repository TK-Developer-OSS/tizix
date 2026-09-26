/* user/nettest.c - net.bin/netcli の実証用コマンド。
 * 127.0.0.1:8080 へ繋いで1行送り、数秒間 受信バイトをそのまま表示する。
 * 2026-09-16: 送信は実証済み(nc側に正しく届く)。受信側で iosim.c 由来と
 * 思われるバグ(peer切断後に無関係なバイト列を読み続ける)を発見した
 * ため、当面は削除せずこのバグの再現手段として残す(DEVELOP.md 5.6)。 */
#include "stdio.h"
#include "netcli.h"

extern unsigned int getticks(void);

int main(int argc, char **argv)
{
    char buf[64];
    int n;
    unsigned int t0;
    (void)argc;
    (void)argv;

    puts("nettest: connecting...\n");
    if (net_connect("127.0.0.1:8080") != 0) {
        puts("nettest: connect failed\n");
        return 1;
    }
    puts("nettest: connected (optimistic)\n");

    net_write("hello from tizix\n", 17);

    t0 = getticks();
    while ((unsigned)(getticks() - t0) < 300) {   /* ~3秒 */
        n = net_read(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            puts("nettest: RX [");
            puts(buf);
            puts("]\n");
        }
    }
    net_close();
    puts("nettest: done\n");
    return 0;
}
