/* user/telnet.c - net.bin(`net &`)常駐 + netcli.h 経由の簡易 telnet クライアント。
 *
 *   旧実装は NET_STAT/NET_DATA(port 50/51 = 0x32/0x33)を自前で直接叩き、
 *   ATD ダイヤルも自分でやっていた。net.c(`net &`)が同じポートの唯一の
 *   所有者になった今([[commands-access-via-syscall]] と同じ方針:
 *   ハードウェア入口は net.c 1本、他コマンドは共有リング経由の netcli.h だけ
 *   を使う)、telnet と net.c が同時に同じポートを取り合って壊れていた。
 *   atcli.c / tzftp.c と同じ土台(net_connect/net_read/net_write)に
 *   書き直した。`net &` を先に起動しておくこと。
 *
 *   使い方: telnet host port    (省略時は対話で host:port を尋ねる)
 */
#include "stdio.h"
#include "string.h"
#include "netcli.h"

/* 改行を足さない文字列出力。puts は末尾に '\n' を足すので、"connecting to " /
 * target / " ..." が 3 行に割れていた。メッセージ側が必要な改行を持っている。
 * (iy_reg: リテラルはポインタ変数へ代入せず引数で渡す ── 代入は +IY されない) */
static void outs(const char *s)
{
    while (*s)
        putchar(*s++);
}

int main(int argc, char **argv)
{
    char target[64];
    char *p;
    char c;

    target[0] = '\0';
    if (argc >= 1 && argv[0]) {
        strcpy(target, argv[0]);
        if (argc >= 2 && argv[1]) {
            strcat(target, ":");
            strcat(target, argv[1]);
        }
    } else {
        outs("Target (host:port): ");
        p = target;
        for (;;) {
            int ch = getchar();
            if (ch == EOF || ch == '\n' || ch == '\r') {
                *p = '\0';
                putchar('\n');
                break;
            }
            if (p < target + 63) {
                *p++ = (char)ch;
                putchar(ch);
            }
        }
    }

    if (target[0] == '\0') {
        outs("telnet: no target\n");
        return 1;
    }

    outs("telnet: connecting to ");
    outs(target);
    outs(" ...\n");

    if (net_connect(target) != 0) {
        outs("telnet: connect failed (net daemon down, or target unreachable)\n");
        return 1;
    }
    outs("telnet: connected (Ctrl+C to quit)\n");

    for (;;) {
        if (net_read(&c, 1) == 1)
            putchar(c);
        if (kbhit()) {
            c = (char)getchar();
            if (c == 0x03)
                break;
            /* ローカルエコー: 本物の TELNET はサーバ側がエコーするのが基本
             * だが、ここは IAC オプション交渉をしない生の TCP 中継なので、
             * 相手がエコーしないサーバ(HTTP等)だと打った内容が一切見えない。
             * 見えないまま Enter を押して不完全な行を送ってしまう事故を防ぐ。 */
            if (c == '\r' || c == '\n') {
                /* Enter: コンソールが '\r' と '\n' のどちらを返すかは端末
                 * 依存(sh_readline() も両方を Enter として受ける)。HTTP 等
                 * 行指向プロトコルは "\r\n" 必須なので、受けた1バイトを
                 * そのまま流さず常に CRLF を明示的に送る(NVT 準拠)。 */
                putchar('\n');
                net_write("\r\n", 2);
            } else {
                putchar(c);
                net_write(&c, 1);
            }
        }
    }

    net_close();
    outs("\ntelnet: closed\n");
    return 0;
}
