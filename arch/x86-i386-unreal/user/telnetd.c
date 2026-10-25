/* arch/x86-i386-unreal/user/telnetd.c -- telnet で入れるようにする(モデムの回線の getty と同じ考え)。
 *   telnetd &      /etc/rc から起動しておく。モデム(Pi の at_modem.py --listen)に着信が来たら ATA で出て、
 *                  つながっている間はコンソールを相手と共有する: コンソールの出力は COM1(モデム)にも出し(sercon のモード 3)、
 *                  相手が打ったものはコンソールの入力へ差し込む(syscall 65)。画面とキーボードもそのまま使える(同じシェル)。
 *                  相手が切ったら(NO CARRIER)コンソールを COM1 から外して、また着信を待つ。
 *   T105 のように COM が 1 つの機械では、先に `sercon off` で COM1 をコンソールから外しておくこと(rc.t105)。
 *   telnet の取り決めは最小限: こちらがエコーし(WILL ECHO)、1 文字ずつ送らせる(WILL SGA)。相手からの取り決めは読み捨てる。 */
#include "tzstdio.h"
#include "netcli.h"

#define IAC  0xFF
#define SB   0xFA
#define SE   0xF0

static unsigned char in[64], out[64];
static unsigned st;                       /* 0 = 普通 / 1 = IAC の後 / 2 = 選択肢の番号を捨てる / 3 = \r の後 / 4 = SB の中 / 5 = SB の中の IAC */

static void nap(unsigned ticks) { while (ticks) { unsigned t = ticks > 200 ? 200 : ticks; _syscall5_asm(51, t, 0, 0, 0, 0); ticks -= t; } }
static void sercon(unsigned mode) { _syscall5_asm(64, mode, 0, 0, 0, 0); }
static void inject(const unsigned char *b, unsigned n) { if (n) _syscall5_asm(65, 0, 0, n, 0, (unsigned)b); }

/* 相手からのバイト列を、telnet の制御を除いてコンソールの入力へ */
static void from_remote(const unsigned char *b, unsigned n)
{
    unsigned i, k = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = b[i];
        switch (st) {
        case 1:                                   /* IAC の後 */
            if (c == IAC) { out[k++] = IAC; st = 0; }
            else if (c >= 0xFB && c <= 0xFE) st = 2;      /* WILL / WONT / DO / DONT + 選択肢 */
            else if (c == SB) st = 4;
            else st = 0;
            continue;
        case 2: st = 0; continue;
        case 4: if (c == IAC) st = 5; continue;
        case 5: st = (c == SE) ? 0 : 4; continue;
        case 3:                                   /* \r の後の \n / \0 は捨てる(Enter は \r 1 つ) */
            st = 0;
            if (c == '\n' || c == 0) continue;
            break;
        }
        if (c == IAC) { st = 1; continue; }
        out[k++] = c;
        if (c == '\r') st = 3;
    }
    inject(out, k);
}

int main(int argc, char **argv)
{
    static const char hello[] = { (char)IAC, (char)0xFB, 1, (char)IAC, (char)0xFB, 3 };   /* WILL ECHO, WILL SGA */
    (void)argc; (void)argv;
    for (;;) {
        if (net_listen(23) != 0) {                /* モデムが居ない / 使えない: しばらくして試し直す */
            nap(500);
            continue;
        }
        while (net_state() == NET_LISTEN)
            ;
        if (net_state() != NET_CONNECTED) {
            net_close();
            continue;
        }
        st = 0;
        net_write(hello, sizeof hello);
        sercon(3);                                /* コンソールの出力を相手にも */
        inject((const unsigned char *)"\r", 1);   /* プロンプトを出させる */
        while (net_state() == NET_CONNECTED) {
            int n = net_read((char *)in, sizeof in);
            if (n > 0) from_remote(in, (unsigned)n);
        }
        sercon(0);                                /* 相手が切った: コンソールを COM1 から外す */
        net_close();
    }
    return 0;
}
