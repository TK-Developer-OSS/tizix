/* src/mbox.h -- 郵便受け(名前付きのプロセス間通信、task.md #112)。PLAT_FLAT32 のアーキ。
 *   コマンド側の約束は user/mbox.h(syscall 47..51、src/sysfile.c)。仕組みは src/mbox.c の冒頭。 */
#ifndef MBOX_H
#define MBOX_H

/* mb_call の引数(5 つあるので構造体で渡す。user/mbox.h の struct mb_call と同じ形) */
struct mbcall {
    const char *name;           /* 宛先の郵便受けの名前 */
    const void *req;            /* 依頼 */
    unsigned long reqlen;
    void *rep;                  /* 返事を受ける場所 */
    unsigned long repmax;
};

long          mb_bind(const char *name);
long          mb_call(const struct mbcall *m);
unsigned long mb_recv(unsigned port, void *buf, unsigned long max, unsigned long cs);
long          mb_reply(unsigned from, const void *buf, unsigned long len);
long          ksleep(unsigned long cs);
void          mbox_slot_reset(unsigned char n);

#endif
