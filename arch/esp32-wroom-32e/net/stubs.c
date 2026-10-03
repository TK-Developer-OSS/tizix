/* arch/esp32-wroom-32e/net/stubs.c(#109): ESP-IDF のビルド済みライブラリと WiFi のバイナリが
 *   参照する雑多な関数と、newlib(WiFi 側だけが使う)の下回り。
 *   NVS は無し(PHY の校正値も毎回取り直す)。ログは UART へ。 */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "osal.h"

extern int ets_printf(const char *fmt, ...);

/* newlib のシステムコール(ファイルは無い。abort / スタック破壊の検出は止まるだけ) */
void _exit(int rc) { ets_printf("net: _exit(%d)\n", rc); for (;;) ; }
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int _lseek(int fd, int off, int wh) { (void)fd; (void)off; (void)wh; return -1; }
int _read(int fd, void *p, int n) { (void)fd; (void)p; (void)n; return -1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }
/* 秒はカーネルの時計(time_get。SNTP / date で合う)。mbedtls が証明書の有効期限を見るのに使う */
extern unsigned long time_get(void);
int _gettimeofday(struct timeval *tv, void *tz)
{
    uint64_t us = os_time_us();
    (void)tz;
    if (tv) { tv->tv_sec = time_get(); tv->tv_usec = us % 1000000; }
    return 0;
}

/* mbedtls の mbedtls_ms_time(TLS のタイマ)。どの時計も起動からの単調時間で答える */
#include <time.h>
int clock_gettime(clockid_t id, struct timespec *ts)
{
    uint64_t us = os_time_us();
    (void)id;
    ts->tv_sec = (time_t)(us / 1000000);
    ts->tv_nsec = (long)(us % 1000000) * 1000;
    return 0;
}

void _esp_error_check_failed(int rc, const char *file, int line, const char *function, const char *expression)
{
    ets_printf("ESP_ERROR_CHECK failed: 0x%x at %s:%d %s (%s)\n", rc, file, line, function, expression);
    for (;;) ;
}

/* ---- FreeRTOS の queue 系(ESP-IDF のビルド済みライブラリが直接呼ぶ分) ----
 *   ミューテックスもキューも同じ関数(xQueueGenericSend 等)で操作されるので、先頭に種別を持たせる。 */
extern void *queue_create(uint32_t len, uint32_t size);
extern void queue_delete(void *q);
extern int32_t queue_send(void *q, void *item, uint32_t ticks);
extern int32_t queue_recv(void *q, void *item, uint32_t ticks);
extern uint32_t queue_msg_waiting(void *q);
enum { FR_QUEUE = 1, FR_MUTEX = 2 };
struct frobj { int kind; void *q; void *owner; int depth; };
static int fm_try(void *p)
{
    struct frobj *m = p;
    void *me = os_current();
    uint32_t ps = irq_off();
    int ok = !m->owner || m->owner == me;
    if (ok) { m->owner = me; m->depth++; }
    irq_restore(ps);
    return ok;
}
#define TICKS(t) ((t) == 0xFFFFFFFFu ? OS_WAIT_FOREVER : (t))
void *xQueueCreateMutex(uint8_t type) { struct frobj *o = calloc(1, sizeof *o); (void)type; if (o) o->kind = FR_MUTEX; return o; }
void *xQueueGenericCreate(uint32_t len, uint32_t size, uint8_t type)
{
    struct frobj *o = calloc(1, sizeof *o);
    (void)type;
    if (!o) return NULL;
    o->kind = FR_QUEUE;
    o->q = queue_create(len, size ? size : 1);
    return o;
}
void vQueueDelete(void *h) { struct frobj *o = h; if (o && o->q) queue_delete(o->q); free(o); }
int xQueueSemaphoreTake(void *h, uint32_t ticks)
{
    struct frobj *o = h;
    if (o->kind == FR_MUTEX) return os_wait(fm_try, o, TICKS(ticks));
    { uint8_t dummy[4]; return queue_recv(o->q, dummy, TICKS(ticks)); }
}
int xQueueGenericSend(void *h, const void *item, uint32_t ticks, int pos)
{
    struct frobj *o = h;
    (void)pos;
    if (o->kind == FR_MUTEX) {
        uint32_t ps = irq_off();
        if (o->depth > 0 && --o->depth == 0) o->owner = NULL;
        irq_restore(ps);
        return 1;
    }
    { uint8_t dummy[4] = {0}; return queue_send(o->q, item ? (void *)item : dummy, TICKS(ticks)); }
}
int xQueueReceive(void *h, void *item, uint32_t ticks) { struct frobj *o = h; return queue_recv(o->q, item, TICKS(ticks)); }
uint32_t uxQueueMessagesWaiting(void *h) { struct frobj *o = h; return o->kind == FR_QUEUE ? queue_msg_waiting(o->q) : (o->owner ? 0 : 1); }
void vTaskDelay(uint32_t ticks) { os_delay(ticks); }
int usleep(unsigned us) { os_delay((us + 999) / 1000); return 0; }
unsigned sleep(unsigned s) { os_delay(s * 1000); return 0; }

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void *heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
void heap_caps_free(void *p) { free(p); }

/* esp_event: WiFi の glue / supplicant が投げるイベントは wifi.c へ */
extern void wt_event_post(const char *base, int32_t id, void *data, size_t size);
int esp_event_post(const char *base, int32_t id, const void *data, size_t size, uint32_t ticks)
{
    (void)ticks;
    wt_event_post(base, id, (void *)data, size);
    return 0;
}

/* mbedtls の多倍長演算(RSA アクセラレータ)の排他とクロック */
#include "soc/periph_defs.h"
extern void periph_module_enable(int);
extern void periph_module_disable(int);
void esp_crypto_mpi_lock_acquire(void) { }
void esp_crypto_mpi_lock_release(void) { }
void esp_crypto_mpi_enable_periph_clk(int enable)
{
    if (enable) periph_module_enable(PERIPH_RSA_MODULE);
    else periph_module_disable(PERIPH_RSA_MODULE);
}

/* ---- NVS: 無い ---- */
#define ESP_ERR_NVS_NOT_INITIALIZED 0x1101
#define ESP_ERR_NVS_NOT_FOUND       0x1102
int nvs_open(const char *n, int mode, uint32_t *h) { (void)n; (void)mode; (void)h; return ESP_ERR_NVS_NOT_INITIALIZED; }
void nvs_close(uint32_t h) { (void)h; }
int nvs_commit(uint32_t h) { (void)h; return ESP_ERR_NVS_NOT_INITIALIZED; }
int nvs_get_blob(uint32_t h, const char *k, void *v, size_t *l) { (void)h; (void)k; (void)v; (void)l; return ESP_ERR_NVS_NOT_FOUND; }
int nvs_set_blob(uint32_t h, const char *k, const void *v, size_t l) { (void)h; (void)k; (void)v; (void)l; return ESP_ERR_NVS_NOT_INITIALIZED; }
int nvs_get_u32(uint32_t h, const char *k, uint32_t *v) { (void)h; (void)k; (void)v; return ESP_ERR_NVS_NOT_FOUND; }
int nvs_set_u32(uint32_t h, const char *k, uint32_t v) { (void)h; (void)k; (void)v; return ESP_ERR_NVS_NOT_INITIALIZED; }

int64_t esp_timer_get_time(void) { return (int64_t)os_time_us(); }
int esp_reset_reason(void) { return 1; }            /* ESP_RST_POWERON */

/* newlib の排他(協調スレッドなので要らない) */
#include <sys/lock.h>
void _lock_acquire(_lock_t *l) { (void)l; }
void _lock_release(_lock_t *l) { (void)l; }
void _lock_acquire_recursive(_lock_t *l) { (void)l; }
void _lock_release_recursive(_lock_t *l) { (void)l; }

/* WiFi の制御(wifi_init.c の esp32 用)。MAC の時刻の更新通知は使わない */

uint8_t g_espnow_user_oui[3] = { 0x18, 0xfe, 0x34 };


/* バイナリのログ(lib_printf.c と同じ名前)。既定では出さない(多すぎる)。 */
int wifi_lib_log = 0;
static int lib_vprintf(const char *tag, const char *fmt, va_list ap)
{
    char buf[160];
    if (!wifi_lib_log) return 0;
    vsnprintf(buf, sizeof buf, fmt, ap);
    ets_printf("(%s) %s", tag, buf);
    return 0;
}
#define LIBPRINTF(name) int name(const char *fmt, ...) { va_list ap; va_start(ap, fmt); lib_vprintf(#name, fmt, ap); va_end(ap); return 0; }
LIBPRINTF(net80211_printf)
LIBPRINTF(pp_printf)
LIBPRINTF(phy_printf)
LIBPRINTF(coexist_printf)
LIBPRINTF(rtc_printf)
LIBPRINTF(core_printf)
LIBPRINTF(wpa_printf_dummy)

/* memcpy / memset は語単位で(-Wl,--wrap=memcpy,--wrap=memset で全員がここを通る)。
 *   WiFi のバイナリは MAC の鍵レジスタ(0x3FF74408〜)へ memcpy で書く(hal_crypto_set_key_entry)。
 *   周辺レジスタは 32 ビットでしか書けず、src/libc の 1 バイトずつの memcpy だと同じバイトが
 *   4 つ並んだ語になって鍵が壊れる(暗号化・復号が全滅し、接続はできるのにデータが 1 つも通らなかった)。
 *   ESP-IDF の memcpy は番地と長さがそろっていれば語で写すので、それに合わせる。 */
extern void *__real_memcpy(void *d, const void *s, size_t n);
extern void *__real_memset(void *d, int c, size_t n);
void *__wrap_memcpy(void *d, const void *s, size_t n)
{
    if ((((uintptr_t)d | (uintptr_t)s | n) & 3) == 0) {
        volatile uint32_t *dw = d;              /* volatile: ループを memcpy 呼び出しに畳ませない */
        const uint32_t *sw = s;
        size_t i;
        for (i = 0; i < n / 4; i++) dw[i] = sw[i];
        return d;
    }
    return __real_memcpy(d, s, n);
}
void *__wrap_memset(void *d, int c, size_t n)
{
    if ((((uintptr_t)d | n) & 3) == 0) {
        volatile uint32_t *dw = d;
        uint32_t v = (uint8_t)c * 0x01010101u;
        size_t i;
        for (i = 0; i < n / 4; i++) dw[i] = v;
        return d;
    }
    return __real_memset(d, c, n);
}

/* newlib の再入構造体(スレッドごとに持たず 1 つで済ませる。協調スレッドなので) */
#include <reent.h>
struct _reent *__getreent(void) { return _impure_ptr; }

/* newlib の出力(printf を使った場合)→ UART */
extern void uart_tx_one_char(uint8_t c);
int _write(int fd, const char *p, int n)
{
    int i;
    (void)fd;
    for (i = 0; i < n; i++) { if (p[i] == '\n') uart_tx_one_char('\r'); uart_tx_one_char(p[i]); }
    return n;
}
