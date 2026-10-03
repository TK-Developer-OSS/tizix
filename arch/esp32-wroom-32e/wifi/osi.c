/* WiFi 実験(#109): WiFi ドライバ(Espressif のバイナリ)に渡す OS 層 wifi_osi_funcs_t。
 *   ESP-IDF の components/esp_wifi/esp32/esp_adapter.c と同じ表を、FreeRTOS ではなく
 *   osal.c(協調スレッド)の上に作る。 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "osal.h"
#include "esp_private/wifi_os_adapter.h"

extern int ets_printf(const char *fmt, ...);
extern void intr_matrix_set(int cpu_no, uint32_t model_num, uint32_t intr_num);
extern void *_xtos_set_interrupt_handler_arg(int n, void (*f)(void *), void *arg);
extern uint32_t _xtos_ints_on(uint32_t mask);
extern uint32_t _xtos_ints_off(uint32_t mask);
extern uint32_t esp_random(void);
extern void esp_fill_random(void *buf, size_t len);
extern int esp_read_mac(uint8_t *mac, int type);
extern void wifi_module_enable(void);
extern void wifi_module_disable(void);
extern void periph_module_reset(int periph);
extern void esp_phy_enable(int modem);
extern void esp_phy_disable(int modem);
extern void phy_wifi_enable_set(uint8_t enable);
extern void esp_phy_common_clock_enable(void);
extern void esp_phy_common_clock_disable(void);
extern int esp_phy_update_country_info(const char *country);
extern void wt_event_post(const char *base, int32_t id, void *data, size_t size);   /* main.c */

#include "soc/periph_defs.h"
#define PERIPH_WIFI_MODULE_ID  PERIPH_WIFI_MODULE
#define PHY_MODEM_WIFI 1

/* ---- 割込み ---- */
static struct { void (*f)(void *); void *arg; } isr_tab[32];
static void isr_wrap(void *p)
{
    int n = (int)p;
    os_isr_enter();
    if (isr_tab[n].f) isr_tab[n].f(isr_tab[n].arg);
    os_isr_leave();
}
static void set_intr(int32_t cpu, uint32_t src, uint32_t num, int32_t prio)
{
    (void)prio;
    intr_matrix_set(cpu, src, num);
}
static void clear_intr(uint32_t src, uint32_t num) { (void)src; (void)num; }
static void set_isr(int32_t n, void *f, void *arg)
{
    isr_tab[n].f = f; isr_tab[n].arg = arg;
    _xtos_set_interrupt_handler_arg(n, isr_wrap, (void *)n);
}
static void ints_on(uint32_t mask) { _xtos_ints_on(mask); }
static void ints_off(uint32_t mask) { _xtos_ints_off(mask); }
static bool is_from_isr(void) { return os_in_isr(); }

static void *spin_lock_create(void) { return calloc(1, 8); }
static void spin_lock_delete(void *l) { free(l); }
static uint32_t wifi_int_disable(void *mux) { (void)mux; return irq_off(); }
static void wifi_int_restore(void *mux, uint32_t ps) { (void)mux; irq_restore(ps); }
static void task_yield_from_isr(void) { }

/* ---- セマフォ ---- */
struct sem { volatile int count, max; };
void *semphr_create(uint32_t max, uint32_t init)
{
    struct sem *s = malloc(sizeof *s);
    if (s) { s->count = init; s->max = max; }
    return s;
}
static void semphr_delete(void *s) { free(s); }
static int sem_try(void *p)
{
    struct sem *s = p;
    uint32_t ps = irq_off();
    int ok = s->count > 0;
    if (ok) s->count--;
    irq_restore(ps);
    return ok;
}
int32_t semphr_take(void *s, uint32_t ticks) { return os_wait(sem_try, s, ticks); }
int32_t semphr_give(void *p)
{
    struct sem *s = p;
    uint32_t ps = irq_off();
    int ok = s->count < s->max;
    if (ok) s->count++;
    irq_restore(ps);
    return ok;
}
static void *wifi_thread_semphr_get(void)
{
    /* スレッドごとの数え上げセマフォ。スレッドの数は少ないので表で持つ */
    static struct { void *th; void *sem; } tab[16];
    void *me = os_current();
    int i;
    for (i = 0; i < 16; i++) if (tab[i].th == me) return tab[i].sem;
    for (i = 0; i < 16; i++) if (!tab[i].th) { tab[i].th = me; tab[i].sem = semphr_create(1, 0); return tab[i].sem; }
    return NULL;
}

/* ---- 再帰ミューテックス ---- */
struct mtx { void *owner; int depth; };
static void *mutex_create(void) { return calloc(1, sizeof(struct mtx)); }
static void mutex_delete(void *m) { free(m); }
static int mtx_try(void *p)
{
    struct mtx *m = p;
    void *me = os_current();
    uint32_t ps = irq_off();
    int ok = (m->owner == NULL || m->owner == me);
    if (ok) { m->owner = me; m->depth++; }
    irq_restore(ps);
    return ok;
}
static int32_t mutex_lock(void *m) { return os_wait(mtx_try, m, OS_WAIT_FOREVER); }
static int32_t mutex_unlock(void *p)
{
    struct mtx *m = p;
    uint32_t ps = irq_off();
    if (m->owner == os_current() && m->depth > 0 && --m->depth == 0) m->owner = NULL;
    irq_restore(ps);
    return 1;
}

/* ---- キュー ---- */
struct queue { uint32_t len, size, head, count; uint8_t *buf; };
void *queue_create(uint32_t len, uint32_t size)
{
    struct queue *q = calloc(1, sizeof *q + len * size);
    if (!q) return NULL;
    q->len = len; q->size = size; q->buf = (uint8_t *)(q + 1);
    return q;
}
void queue_delete(void *q) { free(q); }
struct qop { struct queue *q; void *item; int front; };
static int q_put(void *p)
{
    struct qop *o = p;
    struct queue *q = o->q;
    uint32_t ps = irq_off();
    int ok = q->count < q->len;
    if (ok) {
        uint32_t slot;
        if (o->front) { q->head = (q->head + q->len - 1) % q->len; slot = q->head; }
        else slot = (q->head + q->count) % q->len;
        memcpy(q->buf + slot * q->size, o->item, q->size);
        q->count++;
    }
    irq_restore(ps);
    return ok;
}
static int q_get(void *p)
{
    struct qop *o = p;
    struct queue *q = o->q;
    uint32_t ps = irq_off();
    int ok = q->count > 0;
    if (ok) {
        memcpy(o->item, q->buf + q->head * q->size, q->size);
        q->head = (q->head + 1) % q->len;
        q->count--;
    }
    irq_restore(ps);
    return ok;
}
int32_t queue_send(void *q, void *item, uint32_t ticks) { struct qop o = { q, item, 0 }; return os_wait(q_put, &o, ticks); }
static int32_t queue_send_from_isr(void *q, void *item, void *hptw) { struct qop o = { q, item, 0 }; if (hptw) *(int *)hptw = 0; return q_put(&o); }
static int32_t queue_send_to_back(void *q, void *item, uint32_t ticks) { return queue_send(q, item, ticks); }
static int32_t queue_send_to_front(void *q, void *item, uint32_t ticks) { struct qop o = { q, item, 1 }; return os_wait(q_put, &o, ticks); }
int32_t queue_recv(void *q, void *item, uint32_t ticks) { struct qop o = { q, item, 0 }; return os_wait(q_get, &o, ticks); }
uint32_t queue_msg_waiting(void *q) { return ((struct queue *)q)->count; }

/* WiFi ドライバの「静的キュー」は先頭に handle を持つ構造体(wifi_static_queue_t) */
struct wifi_static_queue { void *handle; void *storage; };
static void *wifi_create_queue(int len, int size)
{
    struct wifi_static_queue *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->handle = queue_create(len, size);
    return s;
}
static void wifi_delete_queue(void *p)
{
    struct wifi_static_queue *s = p;
    if (s) { queue_delete(s->handle); free(s); }
}

/* ---- イベントグループ ---- */
struct evg { volatile uint32_t bits; };
static void *event_group_create(void) { return calloc(1, sizeof(struct evg)); }
static void event_group_delete(void *e) { free(e); }
static uint32_t event_group_set_bits(void *e, uint32_t b) { uint32_t ps = irq_off(); ((struct evg *)e)->bits |= b; irq_restore(ps); return ((struct evg *)e)->bits; }
static uint32_t event_group_clear_bits(void *e, uint32_t b) { uint32_t ps = irq_off(); uint32_t old = ((struct evg *)e)->bits; ((struct evg *)e)->bits &= ~b; irq_restore(ps); return old; }
struct evw { struct evg *e; uint32_t bits; int all; };
static int evg_ok(void *p)
{
    struct evw *w = p;
    uint32_t v = w->e->bits & w->bits;
    return w->all ? (v == w->bits) : (v != 0);
}
static uint32_t event_group_wait_bits(void *e, uint32_t bits, int clear, int all, uint32_t ticks)
{
    struct evw w = { e, bits, all };
    uint32_t r;
    os_wait(evg_ok, &w, ticks);
    r = ((struct evg *)e)->bits;
    if (clear && evg_ok(&w)) event_group_clear_bits(e, bits);
    return r;
}

/* ---- タスク ---- */
static int32_t task_create_pinned_to_core(void *fn, const char *name, uint32_t stack, void *param, uint32_t prio, void *handle, uint32_t core)
{
    (void)prio; (void)core;
    return os_thread_create(fn, name, stack, param, handle);
}
static int32_t task_create(void *fn, const char *name, uint32_t stack, void *param, uint32_t prio, void *handle)
{
    return task_create_pinned_to_core(fn, name, stack, param, prio, handle, 0);
}
static void task_delete(void *h) { os_thread_delete(h); }
static void task_delay(uint32_t ticks) { os_delay(ticks); }
static int32_t task_ms_to_tick(uint32_t ms) { return ms; }
static void *task_get_current_task(void) { return os_current(); }
static int32_t task_get_max_priority(void) { return 25; }

/* ---- メモリ ---- */
static void *zalloc(size_t n) { return calloc(1, n); }
static uint32_t get_free_heap_size(void) { return os_heap_free(); }

/* ---- 雑多 ---- */
static int32_t event_post(const char *base, int32_t id, void *data, size_t size, uint32_t ticks)
{
    (void)ticks;
    wt_event_post(base, id, data, size);
    return 0;
}
static void empty(void) { }
static void phy_enable(void) { esp_phy_enable(PHY_MODEM_WIFI); phy_wifi_enable_set(1); }
static void phy_disable(void) { phy_wifi_enable_set(0); esp_phy_disable(PHY_MODEM_WIFI); }
static int read_mac(uint8_t *mac, unsigned int type) { return esp_read_mac(mac, type); }
static void timer_arm(void *t, uint32_t ms, bool repeat) { os_timer_arm_ms(t, ms, repeat); }
static void timer_arm_us(void *t, uint32_t us, bool repeat) { os_timer_arm_ms(t, (us + 999) / 1000, repeat); }
static void timer_done(void *t) { os_timer_disarm(t); }
static void wifi_reset_mac(void) { periph_module_reset(PERIPH_WIFI_MODULE_ID); }
static int64_t timer_get_time(void) { return (int64_t)os_time_us(); }
static int nvs_fail(void) { return 0x1102; }      /* ESP_ERR_NVS_NOT_FOUND */
static void nvs_close_(uint32_t h) { (void)h; }
static int get_random(uint8_t *buf, size_t len) { esp_fill_random(buf, len); return 0; }
static int get_time(void *t) { uint64_t us = os_time_us(); ((uint32_t *)t)[0] = us / 1000000; ((uint32_t *)t)[1] = us % 1000000; return 0; }
static unsigned long os_random_(void) { return esp_random(); }

static int log_level = 3;   /* 0 なし 1 E 2 W 3 I 4 D 5 V */
static void log_writev(unsigned int level, const char *tag, const char *fmt, va_list ap)
{
    char buf[200];
    if ((int)level > log_level) return;
    vsnprintf(buf, sizeof buf, fmt, ap);
    ets_printf("[%s] %s", tag ? tag : "", buf);
}
static void log_write(unsigned int level, const char *tag, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); log_writev(level, tag, fmt, ap); va_end(ap);
}
static uint32_t log_timestamp(void) { return os_ticks(); }

static int ret0(void) { return 0; }
static uint32_t ret0u(void) { return 0; }
static void *retnull(void) { return NULL; }
static uint8_t ret1u8(void) { return 1; }

wifi_osi_funcs_t g_wifi_osi_funcs = {
    ._version = ESP_WIFI_OS_ADAPTER_VERSION,
    ._env_is_chip = (bool (*)(void))ret1u8,
    ._set_intr = set_intr,
    ._clear_intr = clear_intr,
    ._set_isr = set_isr,
    ._ints_on = ints_on,
    ._ints_off = ints_off,
    ._is_from_isr = is_from_isr,
    ._spin_lock_create = spin_lock_create,
    ._spin_lock_delete = spin_lock_delete,
    ._wifi_int_disable = wifi_int_disable,
    ._wifi_int_restore = wifi_int_restore,
    ._task_yield_from_isr = task_yield_from_isr,
    ._semphr_create = semphr_create,
    ._semphr_delete = semphr_delete,
    ._semphr_take = semphr_take,
    ._semphr_give = semphr_give,
    ._wifi_thread_semphr_get = wifi_thread_semphr_get,
    ._mutex_create = mutex_create,
    ._recursive_mutex_create = mutex_create,
    ._mutex_delete = mutex_delete,
    ._mutex_lock = mutex_lock,
    ._mutex_unlock = mutex_unlock,
    ._queue_create = queue_create,
    ._queue_delete = queue_delete,
    ._queue_send = queue_send,
    ._queue_send_from_isr = queue_send_from_isr,
    ._queue_send_to_back = queue_send_to_back,
    ._queue_send_to_front = queue_send_to_front,
    ._queue_recv = queue_recv,
    ._queue_msg_waiting = queue_msg_waiting,
    ._event_group_create = event_group_create,
    ._event_group_delete = event_group_delete,
    ._event_group_set_bits = event_group_set_bits,
    ._event_group_clear_bits = event_group_clear_bits,
    ._event_group_wait_bits = event_group_wait_bits,
    ._task_create_pinned_to_core = task_create_pinned_to_core,
    ._task_create = task_create,
    ._task_delete = task_delete,
    ._task_delay = task_delay,
    ._task_ms_to_tick = task_ms_to_tick,
    ._task_get_current_task = task_get_current_task,
    ._task_get_max_priority = task_get_max_priority,
    ._malloc = malloc,
    ._free = free,
    ._event_post = event_post,
    ._get_free_heap_size = get_free_heap_size,
    ._rand = esp_random,
    ._dport_access_stall_other_cpu_start_wrap = empty,
    ._dport_access_stall_other_cpu_end_wrap = empty,
    ._wifi_apb80m_request = empty,
    ._wifi_apb80m_release = empty,
    ._phy_disable = phy_disable,
    ._phy_enable = phy_enable,
    ._phy_common_clock_enable = esp_phy_common_clock_enable,
    ._phy_common_clock_disable = esp_phy_common_clock_disable,
    ._phy_update_country_info = esp_phy_update_country_info,
    ._read_mac = read_mac,
    ._timer_arm = timer_arm,
    ._timer_disarm = os_timer_disarm,
    ._timer_done = timer_done,
    ._timer_setfn = os_timer_setfn,
    ._timer_arm_us = timer_arm_us,
    ._wifi_reset_mac = wifi_reset_mac,
    ._wifi_clock_enable = wifi_module_enable,
    ._wifi_clock_disable = wifi_module_disable,
    ._wifi_rtc_enable_iso = empty,
    ._wifi_rtc_disable_iso = empty,
    ._esp_timer_get_time = timer_get_time,
    ._nvs_set_i8 = (void *)nvs_fail,
    ._nvs_get_i8 = (void *)nvs_fail,
    ._nvs_set_u8 = (void *)nvs_fail,
    ._nvs_get_u8 = (void *)nvs_fail,
    ._nvs_set_u16 = (void *)nvs_fail,
    ._nvs_get_u16 = (void *)nvs_fail,
    ._nvs_open = (void *)nvs_fail,
    ._nvs_close = nvs_close_,
    ._nvs_commit = (void *)nvs_fail,
    ._nvs_set_blob = (void *)nvs_fail,
    ._nvs_get_blob = (void *)nvs_fail,
    ._nvs_erase_key = (void *)nvs_fail,
    ._get_random = get_random,
    ._get_time = get_time,
    ._random = os_random_,
    ._log_write = log_write,
    ._log_writev = log_writev,
    ._log_timestamp = log_timestamp,
    ._malloc_internal = malloc,
    ._realloc_internal = realloc,
    ._calloc_internal = calloc,
    ._zalloc_internal = zalloc,
    ._wifi_malloc = malloc,
    ._wifi_realloc = realloc,
    ._wifi_calloc = calloc,
    ._wifi_zalloc = zalloc,
    ._wifi_create_queue = wifi_create_queue,
    ._wifi_delete_queue = wifi_delete_queue,
    ._coex_init = ret0,
    ._coex_deinit = (void *)empty,
    ._coex_enable = ret0,
    ._coex_disable = empty,
    ._coex_status_get = ret0u,
    ._coex_condition_set = (void *)empty,
    ._coex_wifi_request = (void *)ret0,
    ._coex_wifi_release = (void *)ret0,
    ._coex_wifi_channel_set = (void *)ret0,
    ._coex_event_duration_get = (void *)ret0,
    ._coex_pti_get = (void *)ret0,
    ._coex_schm_status_bit_clear = (void *)empty,
    ._coex_schm_status_bit_set = (void *)empty,
    ._coex_schm_interval_set = (void *)ret0,
    ._coex_schm_interval_get = ret0u,
    ._coex_schm_curr_period_get = (void *)ret0,
    ._coex_schm_curr_phase_get = retnull,
    ._coex_schm_process_restart = ret0,
    ._coex_schm_register_cb = (void *)ret0,
    ._coex_register_start_cb = (void *)ret0,
    ._coex_schm_flexible_period_set = (void *)ret0,
    ._coex_schm_flexible_period_get = ret1u8,
    ._coex_schm_get_phase_by_idx = (void *)retnull,
    ._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};
