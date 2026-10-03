/* user/mbtest.c - 郵便受け(user/mbox.h、#112)の試験用コマンド。gcc 側だけ。
 *
 *   mbtest s NAME        受け手: NAME を開き、届いた依頼を大文字にして返す。"quit" で終わる
 *   mbtest c NAME MSG    出し手: NAME へ MSG を出し、返事を表示する
 *   mbtest t NAME CS     受け手の時間切れ: CS(1/100 秒)待って誰も来なければ "timeout"
 *   mbtest z CS          眠る(ksleep)。眠っている間このプロセスは走らない
 *
 *   python/test_mbox.py が使う。
 */
#include "stdio.h"
#include "mbox.h"

static unsigned long num(const char *s)
{
    unsigned long n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10UL + (unsigned long)(*s++ - '0');
    return n;
}

static int serve(const char *name)
{
    static char buf[64];
    int port = mb_bind(name);
    unsigned long r;
    unsigned i, n;

    if (port < 0) {
        printf("mbtest: cannot bind %s\n", name);
        return 1;
    }
    printf("mbtest: serving %s\n", name);
    for (;;) {
        r = mb_recv(port, buf, sizeof buf - 1, MB_FOREVER);
        if (r == 0 || r == MB_ERR)
            continue;
        n = MB_LEN(r);
        buf[n] = 0;
        if (strcmp(buf, "quit") == 0) {
            mb_reply(MB_FROM(r), "bye", 3);
            return 0;
        }
        for (i = 0; i < n; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z')
                buf[i] = (char)(buf[i] - 'a' + 'A');
        mb_reply(MB_FROM(r), buf, n);
    }
}

int main(int argc, char **argv)
{
    static char ans[64];
    int n;

    if (argc >= 2 && strcmp(argv[0], "s") == 0)
        return serve(argv[1]);
    if (argc >= 3 && strcmp(argv[0], "c") == 0) {
        n = mb_call(argv[1], argv[2], (unsigned)strlen(argv[2]), ans, sizeof ans - 1);
        if (n < 0) {
            printf("mbtest: no mailbox %s\n", argv[1]);
            return 1;
        }
        ans[n] = 0;
        printf("reply: %s\n", ans);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[0], "t") == 0) {
        int port = mb_bind(argv[1]);
        unsigned t0 = getticks();
        unsigned long r = mb_recv(port, ans, sizeof ans, num(argv[2]));
        printf("%s %u\n", r == 0 ? "timeout" : "got", getticks() - t0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[0], "z") == 0) {
        unsigned t0 = getticks();
        ksleep(num(argv[1]));
        printf("slept %u\n", getticks() - t0);
        return 0;
    }
    puts("usage: mbtest s NAME | c NAME MSG | t NAME CS | z CS");
    return 1;
}
