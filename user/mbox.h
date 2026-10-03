/* user/mbox.h - 郵便受け(名前付きのプロセス間通信、task.md #112)。stdio.h の後にインクルードする。
 *
 *   常駐する受け手(esp32d など)が名前で郵便受けを開き、コマンドはその名前へ依頼を出して
 *   返事を待つ。カーネルは出し手の依頼を受け手のバッファへ、受け手の返事を出し手の
 *   置き場へ直接写す(仕組みは src/mbox.c の冒頭)。時間の単位は 1/100 秒。
 *
 *     受け手:  port = mb_bind("name");
 *              for (;;) { r = mb_recv(port, buf, sizeof buf, MB_FOREVER);
 *                         … MB_FROM(r) から MB_LEN(r) バイト …
 *                         mb_reply(MB_FROM(r), ans, n); }
 *     出し手:  n = mb_call("name", req, len, ans, sizeof ans);   (-1 = 受け手がいない)
 *
 *   gcc 側(TZ_SYSCALL)だけ。z80 には無い。
 */
#ifndef MBOX_H
#define MBOX_H

#ifdef TZ_SYSCALL

struct mb_call {                /* src/mbox.h の struct mbcall と同じ形 */
        const char *name;
        const void *req;
        unsigned long reqlen;
        void *rep;
        unsigned long repmax;
};

#define MB_NOWAIT      0UL
#define MB_FOREVER     0xFFFFFFFFUL
#define MB_FROM(r)     ((unsigned)((r) >> 16))
#define MB_LEN(r)      ((unsigned)((r) & 0xFFFFUL))
#define MB_ERR         0xFFFFFFFFUL

/* 郵便受けを開く。戻り: 番号(0..)/ -1 = 同じ名前が使われている・空きが無い */
static int mb_bind(const char *name)
{
        return (int)syscall5(47, (unsigned long)name, 0, 0, 0);
}

/* 依頼を出して返事を待つ。戻り: 返事の長さ / -1 = 受け手がいない・途中で居なくなった */
static int mb_call(const char *name, const void *req, unsigned reqlen, void *rep, unsigned repmax)
{
        struct mb_call m;
        m.name = name; m.req = req; m.reqlen = reqlen; m.rep = rep; m.repmax = repmax;
        return (int)syscall5(48, (unsigned long)&m, 0, 0, 0);
}

/* 依頼を 1 通受け取る。cs = 待つ長さ(MB_NOWAIT / MB_FOREVER / 1/100 秒)。
 * 戻り: 0 = 時間切れ / MB_ERR / それ以外 = MB_FROM(r) が出し手、MB_LEN(r) が長さ */
static unsigned long mb_recv(int port, void *buf, unsigned max, unsigned long cs)
{
        return syscall5(49, (unsigned long)port, (unsigned long)buf, (unsigned long)max, cs);
}

/* 返事をする。戻り: 0 / -1 = その出し手はもう待っていない */
static int mb_reply(unsigned from, const void *buf, unsigned len)
{
        return (int)syscall5(50, (unsigned long)from, (unsigned long)buf, (unsigned long)len, 0);
}

/* 眠る(1/100 秒)。getticks を回して待つのと違い、その間このプロセスは CPU を使わない。 */
static void ksleep(unsigned long cs)
{
        syscall5(51, cs, 0, 0, 0);
}

#endif /* TZ_SYSCALL */

#endif
