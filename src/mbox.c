/* src/mbox.c -- 郵便受け(名前付きのプロセス間通信、task.md #112)。PLAT_FLAT32 のアーキが共有する。
 *
 *   常駐するプロセス(esp32d など)が名前で郵便受けを開き、ほかのプロセスはその名前へ
 *   依頼を出して返事を待つ。形は「送る → 受け取る → 返事する」の同期型:
 *
 *     受け手(常駐)                         出し手(コマンド)
 *     port = mb_bind("esp32d")
 *     n = mb_recv(port, buf, max, 待ち) ←── mb_call("esp32d", 依頼, 返事の置き場)
 *     … 処理 …                                 (返事が来るまで眠る)
 *     mb_reply(from, 返事, len)          ──→  戻り = 返事の長さ
 *
 *   カーネルはバッファを持たない。保護が無いので、出し手が眠っている間に、出し手の
 *   依頼をそのまま受け手のバッファへ写し(mb_recv)、受け手の返事をそのまま出し手の
 *   置き場へ写す(mb_reply)。カーネルが覚えるのは「誰がどの郵便受けへ何を出したか」
 *   の表だけ(郵便受け MB_PORTS 個 + スロットごとに 1 行)。
 *   眠り / 起こすは見出しの欄(src/phdr.h、proc_block_until / proc_wake)で、
 *   入れてから起こす(起こしそこねは見出しの PH_WAKEPEND が拾う)。
 *
 *   プロセスが死んだとき: 郵便受けの持ち主が死ねば、待っている出し手は 1 秒ごとの
 *   見直しで気づいて -1 で戻る。出し手が死んでいれば返事は捨てる。スロットが次の
 *   プロセスに渡るときは kexec が mbox_slot_reset で表の行を消す(前の占有者の
 *   郵便受け・依頼を持ち越さない)。
 */
#include "kmem.h"
#include "kernel.h"
#include "mbox.h"

#define MB_PORTS    4
#define MB_NAMELEN  16

#define CL_IDLE     0
#define CL_SENT     1           /* 出した。受け手はまだ受け取っていない */
#define CL_TAKEN    2           /* 受け手が受け取った。返事待ち */
#define CL_DONE     3           /* 返事が届いた */

struct mbport {
    unsigned char slot;         /* 持ち主のスロット。0 = 空き */
    char name[MB_NAMELEN];
};

struct mbcl {
    unsigned char state;        /* CL_* */
    unsigned char port;
    const struct mbcall *m;     /* 出し手のデータ枠の中(眠っている間はそのまま残る) */
    unsigned long rlen;         /* 届いた返事の長さ */
};

static struct mbport ports[MB_PORTS];
static struct mbcl   cl[KW_NSLOT];
static unsigned char rr;        /* mb_recv の見回りの出発点(公平に回す) */

#define PIDTAB  ((volatile unsigned char *)KW_PIDTAB)
#define CUR     (*(volatile unsigned char *)KW_CURRENT)
#define NOW     ((unsigned long)*(volatile unsigned int *)KW_TICKS)

static int alive(unsigned char s)
{
    unsigned char p = PIDTAB[s];
    return s != 0 && p != 0 && p != PID_PIPEBUF;
}

static int name_eq(const char *a, const char *b)
{
    unsigned i;
    for (i = 0; i < MB_NAMELEN; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static void copy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
}

/* 1/100 秒 → tick(期限の時刻)。0 にならないようにする(0 は「期限なし」の印) */
static unsigned long deadline(unsigned long cs)
{
    unsigned long t = NOW + (cs * TICK_HZ + 99UL) / 100UL;
    return t ? t : 1;
}

static int find(const char *name)
{
    unsigned i;
    for (i = 0; i < MB_PORTS; i++)
        if (ports[i].slot && alive(ports[i].slot) && name_eq(ports[i].name, name))
            return (int)i;
    return -1;
}

/* 郵便受けを開く。戻り: 番号(0..)/ -1 = 同じ名前が使われている・空きが無い */
long mb_bind(const char *name)
{
    unsigned i;
    int r = -1;

    if (!name || !name[0])
        return -1;
    IRQ_OFF();
    if (find(name) < 0)
        for (i = 0; i < MB_PORTS; i++)
            if (!ports[i].slot || !alive(ports[i].slot)) {
                unsigned k;
                for (k = 0; k < MB_NAMELEN - 1 && name[k]; k++)
                    ports[i].name[k] = name[k];
                ports[i].name[k] = 0;
                ports[i].slot = CUR;
                r = (int)i;
                break;
            }
    IRQ_ON();
    return r;
}

/* 依頼を出して返事を待つ。戻り: 返事の長さ(置き場に入らなかった分は切る)/
 * -1 = その名前の郵便受けが無い・持ち主が途中で死んだ */
long mb_call(const struct mbcall *m)
{
    unsigned char me = CUR;
    int p;

    IRQ_OFF();
    p = find(m->name);
    if (p < 0 || ports[p].slot == me) {
        IRQ_ON();
        return -1;
    }
    cl[me].m     = m;
    cl[me].port  = (unsigned char)p;
    cl[me].rlen  = 0;
    cl[me].state = CL_SENT;
    IRQ_ON();
    proc_wake(ports[p].slot);           /* 入れてから起こす */

    while (cl[me].state != CL_DONE) {
        if (!ports[p].slot || !alive(ports[p].slot)) {
            cl[me].state = CL_IDLE;
            return -1;
        }
        (void)proc_block_until(deadline(100));   /* 1 秒ごとに持ち主の生死を見直す */
    }
    cl[me].state = CL_IDLE;
    return (long)cl[me].rlen;
}

/* 依頼を 1 通受け取る。cs = 待つ長さ(1/100 秒)。0 = 待たない / 0xFFFFFFFF = 来るまで。
 * 戻り: (出し手のスロット << 16) | 長さ / 0 = 時間切れ / 0xFFFFFFFF = 自分の郵便受けでない */
unsigned long mb_recv(unsigned port, void *buf, unsigned long max, unsigned long cs)
{
    unsigned char me = CUR;
    unsigned long until = (cs == 0 || cs == 0xFFFFFFFFUL) ? 0 : deadline(cs);

    if (port >= MB_PORTS || ports[port].slot != me)
        return 0xFFFFFFFFUL;
    for (;;) {
        unsigned n;
        unsigned char c = rr;
        IRQ_OFF();
        for (n = 0; n < KW_NSLOT; n++) {
            if (++c >= KW_NSLOT) c = 0;
            if (cl[c].state == CL_SENT && cl[c].port == port && alive(c)) {
                unsigned long len = cl[c].m->reqlen;
                if (len > max) len = max;
                if (len > 0xFFFFUL) len = 0xFFFFUL;
                copy(buf, cl[c].m->req, len);
                cl[c].state = CL_TAKEN;
                rr = c;
                IRQ_ON();
                return ((unsigned long)c << 16) | len;
            }
        }
        IRQ_ON();
        if (cs == 0)
            return 0;
        if (until && (long)(NOW - until) >= 0)
            return 0;
        (void)proc_block_until(until);
    }
}

/* 受け取った依頼に返事をする。戻り: 0 / -1 = その出し手はもう返事を待っていない */
long mb_reply(unsigned from, const void *buf, unsigned long len)
{
    unsigned char me = CUR;

    if (from >= KW_NSLOT)
        return -1;
    IRQ_OFF();
    if (cl[from].state != CL_TAKEN || ports[cl[from].port].slot != me || !alive((unsigned char)from)) {
        IRQ_ON();
        return -1;
    }
    if (len > cl[from].m->repmax)
        len = cl[from].m->repmax;
    copy(cl[from].m->rep, buf, len);
    cl[from].rlen  = len;
    cl[from].state = CL_DONE;
    IRQ_ON();
    proc_wake((unsigned char)from);
    return 0;
}

/* 眠る(1/100 秒)。getticks を回す待ちと違って、その間このスロットは走らない。 */
long ksleep(unsigned long cs)
{
    unsigned long until = deadline(cs);
    while ((long)(NOW - until) < 0)
        (void)proc_block_until(until);
    return 0;
}

/* スロット n を新しいプロセスへ渡す前に、前の占有者の郵便受けと依頼を消す(kexec が呼ぶ) */
void mbox_slot_reset(unsigned char n)
{
    unsigned i;
    IRQ_OFF();
    for (i = 0; i < MB_PORTS; i++)
        if (ports[i].slot == n)
            ports[i].slot = 0;
    if (n < KW_NSLOT)
        cl[n].state = CL_IDLE;
    IRQ_ON();
}
