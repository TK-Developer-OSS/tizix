/* WiFi 実験(#109): ESP-IDF のビルド済みライブラリと WiFi ドライバが求める OS の機能。
 *   1 コア・協調スレッド: スレッドは待つとき自分から譲る(os_yield)。割込みは条件を
 *   変える(セマフォを上げる等)だけで、切り替えはしない。待っている側は譲りながら
 *   条件を見直す(busy な待ち。省電力は考えない)。tick = 1ms(CONFIG_FREERTOS_HZ=1000)。 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "osal.h"

extern int ets_printf(const char *fmt, ...);

/* ---- 割込み禁止のネスト(FreeRTOS の critical section の代わり) ---- */
static volatile uint32_t crit_nest, crit_ps;
static volatile int in_isr;

int xPortEnterCriticalTimeout(void *mux, int timeout)
{
    uint32_t ps = irq_off();
    (void)mux; (void)timeout;
    if (crit_nest++ == 0)
        crit_ps = ps;
    return 1;
}
void vPortEnterCritical(void *mux) { xPortEnterCriticalTimeout(mux, -1); }
void vPortExitCritical(void *mux)
{
    (void)mux;
    if (crit_nest && --crit_nest == 0)
        irq_restore(crit_ps);
}
int xPortInIsrContext(void) { return in_isr; }
int os_in_isr(void) { return in_isr; }
void os_isr_enter(void) { in_isr++; }
void os_isr_leave(void) { in_isr--; }

/* ---- 時刻(CCOUNT から。CPU 80MHz 前提) ---- */
static inline uint32_t ccount(void) { uint32_t v; __asm__ volatile ("rsr.ccount %0" : "=r"(v)); return v; }
static uint64_t us_base;
static uint32_t cc_last;
uint64_t os_time_us(void)
{
    uint32_t ps = irq_off();
    uint32_t d = ccount() - cc_last;
    us_base += d / 80;
    cc_last += (d / 80) * 80;
    uint64_t r = us_base;
    irq_restore(ps);
    return r;
}
uint32_t os_ticks(void) { return (uint32_t)(os_time_us() / 1000); }
uint32_t xTaskGetTickCount(void) { return os_ticks(); }
uint32_t xTaskGetTickCountFromISR(void) { return os_ticks(); }
int xTaskGetSchedulerState(void) { return 2; }      /* taskSCHEDULER_RUNNING */

/* ---- ヒープ(newlib malloc の下回り) ---- */
extern char _heap_start[], _heap_end[];
static char *brk_p;
void *_sbrk(ptrdiff_t inc)
{
    char *p;
    if (!brk_p) brk_p = _heap_start;
    if (brk_p + inc > _heap_end) { errno = ENOMEM; return (void *)-1; }
    p = brk_p; brk_p += inc;
    return p;
}
size_t os_heap_free(void) { return (size_t)(_heap_end - (brk_p ? brk_p : _heap_start)); }

/* ---- 協調スレッド ---- */
#define MAX_THREADS 10
struct thread {
    uint32_t ctx[2];            /* ctx_switch の save/load(SP と a0) */
    const char *name;
    void *stack;
    int alive;
};
extern void ctx_switch(uint32_t *save, const uint32_t *load);
extern void thread_trampoline(void);

static struct thread threads[MAX_THREADS] = { { {0, 0}, "main", NULL, 1 } };
static int cur;
static int in_timers;

void *os_current(void) { return &threads[cur]; }

static void run_timers(void);

void os_yield(void)
{
    int i, n;

    if (in_isr) return;
    if (!in_timers) { in_timers = 1; run_timers(); in_timers = 0; }
    for (i = 1; i <= MAX_THREADS; i++) {
        n = (cur + i) % MAX_THREADS;
        if (threads[n].alive && n != cur) {
            int me = cur;
            cur = n;
            ctx_switch(threads[me].ctx, threads[n].ctx);
            return;
        }
    }
}

void thread_exit(void)
{
    threads[cur].alive = 0;
    for (;;)
        os_yield();
}

int os_thread_create(void (*fn)(void *), const char *name, uint32_t stack_size, void *arg, void **handle)
{
    int i;
    uint32_t *top;

    for (i = 1; i < MAX_THREADS; i++)
        if (!threads[i].alive && !threads[i].stack)
            break;
    if (i == MAX_THREADS) return 0;
    if (stack_size < 2048) stack_size = 2048;
    threads[i].stack = malloc(stack_size);
    if (!threads[i].stack) return 0;
    top = (uint32_t *)(((uint32_t)threads[i].stack + stack_size) & ~15u);
    /* ctx_switch の retw が「呼び出し元」として読み戻す 16 バイト(SP の直下):
     *   a0 = 0(トランポリンは戻らない)、a1 = トランポリンの SP、a2 = 関数、a3 = 引数 */
    top[-8] = 0; top[-7] = (uint32_t)top; top[-6] = (uint32_t)fn; top[-5] = (uint32_t)arg;
    threads[i].ctx[0] = (uint32_t)(top - 4);                         /* ctx_switch の SP 相当 */
    threads[i].ctx[1] = (1u << 30) | ((uint32_t)thread_trampoline & 0x3FFFFFFFu);   /* call4 から戻る形 */
    threads[i].name = name;
    threads[i].alive = 1;
    if (handle) *handle = &threads[i];
    ets_printf("os: thread %d '%s' stack %u\n", i, name ? name : "?", (unsigned)stack_size);
    return 1;
}

void os_thread_delete(void *h)
{
    struct thread *t = h ? (struct thread *)h : &threads[cur];
    t->alive = 0;
    if (t == &threads[cur])
        thread_exit();
}

/* 期限つきの待ち。cond が真になるまで譲る。ticks = OS_WAIT_FOREVER なら無期限 */
int os_wait(int (*cond)(void *), void *arg, uint32_t ticks)
{
    uint32_t t0 = os_ticks();
    for (;;) {
        if (cond(arg)) return 1;
        if (ticks != OS_WAIT_FOREVER && (uint32_t)(os_ticks() - t0) >= ticks) return 0;
        os_yield();
    }
}

void os_delay(uint32_t ticks)
{
    uint32_t t0 = os_ticks();
    while ((uint32_t)(os_ticks() - t0) < ticks)
        os_yield();
}

/* ---- ETS 形式のタイマ(WiFi ドライバが自分で確保して渡してくる構造体) ---- */
struct ets_timer {
    struct ets_timer *next;
    uint32_t expire;            /* ms */
    uint32_t period;            /* ms。0 なら 1 回 */
    void (*func)(void *);
    void *arg;
};
static struct ets_timer *timer_list;

static void timer_unlink(struct ets_timer *t)
{
    struct ets_timer **pp;
    for (pp = &timer_list; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; break; }
    t->next = NULL;
}

void os_timer_setfn(void *pt, void *fn, void *arg)
{
    struct ets_timer *t = pt;
    uint32_t ps = irq_off();
    timer_unlink(t);
    t->func = fn; t->arg = arg; t->period = 0; t->expire = 0;
    irq_restore(ps);
}

void os_timer_arm_ms(void *pt, uint32_t ms, int repeat)
{
    struct ets_timer *t = pt;
    uint32_t ps = irq_off();
    timer_unlink(t);
    t->expire = os_ticks() + ms;
    t->period = repeat ? ms : 0;
    t->next = timer_list;
    timer_list = t;
    irq_restore(ps);
}

void os_timer_disarm(void *pt)
{
    uint32_t ps = irq_off();
    timer_unlink(pt);
    irq_restore(ps);
}

static void run_timers(void)
{
    struct ets_timer *t;
    uint32_t now = os_ticks();
again:
    for (t = timer_list; t; t = t->next) {
        if ((int32_t)(now - t->expire) >= 0) {
            void (*f)(void *) = t->func;
            void *a = t->arg;
            if (t->period) t->expire += t->period;
            else timer_unlink(t);
            if (f) f(a);
            goto again;          /* コールバックがリストを触っていてもよいように、頭からやり直す */
        }
    }
}
