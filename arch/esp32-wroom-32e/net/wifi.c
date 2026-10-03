/* arch/esp32-wroom-32e/net/wifi.c(#109): ESP32 内蔵 WiFi を tizix から使う。
 *
 *   ・起動時(src/init.c の PLAT_LATE_INIT。FAT をマウントした後、sh を起こす前)に
 *     /etc/wifi を読み、書いてあれば STA で接続を始める。無ければ WiFi は起こさない。
 *       /etc/wifi の書式(1 行 1 項目):
 *         ssid=ネットワーク名
 *         psk=パスワード
 *         ip=192.168.x.y      (任意。書けば固定 IP。無いか ip=dhcp なら DHCP。net/netif_wifi.c)
 *         mask=255.255.255.0  (任意。固定 IP のとき。省略時 /24)
 *         gw=192.168.x.1      (任意。固定 IP のとき)
 *         dns=8.8.8.8         (任意。書けば DHCP の DNS より優先。固定 IP で省略時 8.8.8.8)
 *         ntp=pool.ntp.org    (任意。省略時 pool.ntp.org。繋がったら SNTP で時計を合わせる)
 *   ・WiFi ドライバのスレッド群は slot0(init)の中で協調して走る(net/osal.c)。
 *     slot0 の待ちループ(PLAT_IDLE)がそれらに順番を回してから、tizix の他のスロットへ譲る。
 *   ・接続・切断などの知らせは /var/log/message へ(net_log → klog_write)。コンソールには出さない
 *     (プロンプトの途中に割り込んでいた)。切れたら 5 秒おきにつなぎ直す。 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "osal.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "ff.h"
#include "esp_log.h"

extern void klog_write(const char *msg);
extern esp_err_t esp_supplicant_init(void);

/* ---- WiFi / ネットの知らせ → /var/log/message ----
 *   知らせは WiFi のスレッド(小さいスタック)や lwIP のコールバックの中で起きるので、その場では
 *   FatFs を叩かず、ここに貯めて slot0 の待ちループ(plat_idle → net_log_flush)で klog_write する。
 *   klog_write は割込みを止めて呼ぶ ── コマンドの syscall(FatFs を割込み禁止で使う)が
 *   slot0 の FatFs の途中に割り込まないように。あふれた分は捨てる。 */
#define NLOG_N   8
#define NLOG_LEN 96
static char nlog[NLOG_N][NLOG_LEN];
static volatile uint8_t nlog_h, nlog_t;

void net_log(const char *fmt, ...)
{
    va_list ap;
    uint32_t ps = irq_off();
    uint8_t nh = (uint8_t)((nlog_h + 1) % NLOG_N);

    if (nh != nlog_t) {
        va_start(ap, fmt);
        vsnprintf(nlog[nlog_h], NLOG_LEN, fmt, ap);
        va_end(ap);
        nlog_h = nh;
    }
    irq_restore(ps);
}

static void net_log_flush(void)
{
    while (nlog_t != nlog_h) {
        uint32_t ps = irq_off();
        klog_write(nlog[nlog_t]);
        nlog_t = (uint8_t)((nlog_t + 1) % NLOG_N);
        irq_restore(ps);
    }
}
extern void esp32_clock_pll(void);
extern void esp32_cache_map(void);

static volatile int sta_started, sta_connected;
static uint32_t retry_at;
static unsigned last_fail;          /* 直前の接続失敗の理由(同じ失敗はログに 1 回だけ) */
static int wifi_up;
static char ssid[33], psk[65];
static char ipaddr[16], netmask[16], gateway[16], dnssrv[16], ntpsrv[40];

extern int net_lwip_start(const char *ip, const char *mask, const char *gw, const char *dns, const char *ntp);
extern void net_link(int up);
extern void net_poll(void);

/* "key=値" の 1 行を buf へ写す(長すぎたら無視) */
static void conf_take(const char *p, size_t len, const char *key, char *buf, size_t size)
{
    size_t k = strlen(key);
    if (len > k && !strncmp(p, key, k) && len - k < size) { memcpy(buf, p + k, len - k); buf[len - k] = 0; }
}

void wt_event_post(const char *base, int32_t id, void *data, size_t size)
{
    (void)size;
    if (!base || strcmp(base, "WIFI_EVENT")) return;
    switch (id) {
    case WIFI_EVENT_STA_START:
        sta_started = 1;
        break;
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *c = data;
        net_log("wifi: connected to %s (ch %u)", ssid, (unsigned)c->channel);
        sta_connected = 1;
        last_fail = 0;
        net_link(1);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *d = data;
        if (sta_connected)
            net_log("wifi: disconnected (reason %u)", (unsigned)d->reason);
        else if (d->reason != last_fail)    /* 5 秒おきの再試行で同じ失敗を書き続けない */
            net_log("wifi: connect failed (reason %u), retrying every 5 s", (unsigned)d->reason);
        last_fail = d->reason;
        sta_connected = 0;
        net_link(0);
        retry_at = os_ticks() + 5000;
        break;
    }
    }
}

/* /etc/wifi を読む。1 = 両方そろった */
static int read_conf(void)
{
    static FIL f;
    char buf[128], *p, *e;
    UINT n = 0;

    if (f_open(&f, "/etc/wifi", FA_READ) != FR_OK) return 0;
    f_read(&f, buf, sizeof buf - 1, &n);
    f_close(&f);
    buf[n] = 0;
    ssid[0] = psk[0] = ipaddr[0] = netmask[0] = gateway[0] = dnssrv[0] = ntpsrv[0] = 0;
    for (p = buf; *p; p = e) {
        size_t len;
        e = strchr(p, '\n');
        if (!e) e = p + strlen(p);
        len = e - p;
        if (len && p[len - 1] == '\r') len--;
        conf_take(p, len, "ssid=", ssid, sizeof ssid);
        conf_take(p, len, "psk=", psk, sizeof psk);
        conf_take(p, len, "ip=", ipaddr, sizeof ipaddr);
        conf_take(p, len, "mask=", netmask, sizeof netmask);
        conf_take(p, len, "gw=", gateway, sizeof gateway);
        conf_take(p, len, "dns=", dnssrv, sizeof dnssrv);
        conf_take(p, len, "ntp=", ntpsrv, sizeof ntpsrv);
        if (*e) e++;
    }
    return ssid[0] != 0;
}

static int flag_set(void *p) { return *(volatile int *)p; }

void plat_late_init(void)
{
    static wifi_init_config_t cfg;
    static wifi_config_t wc;
    esp_err_t r;

    if (!read_conf()) return;               /* /etc/wifi が無ければ何もしない */
    esp32_cache_map();                      /* WiFi のコードは flash にある(kmain.c) */
    esp32_clock_pll();                      /* WiFi は CPU / APB 80MHz が前提(kmain.c) */
    /* 既定値の中の暗号の関数表は flash(DROM)にあるので、キャッシュを張った後で作る
     * (先に作っていたら中身が読めず「wpa crypto funcs expected size=44」で初期化が止まった) */
    {
        wifi_init_config_t d = WIFI_INIT_CONFIG_DEFAULT();
        cfg = d;
    }
    cfg.nvs_enable = 0;
    esp_wifi_internal_set_log_level(WIFI_LOG_ERROR);
    /* ESP-IDF の部品の ESP_LOG は newlib の stdout へ出る。バッファされたままだと、ずっと後の
     * 別の行に混ざって出てきた(起動時の phy_init の行が TLS の失敗のときに出た)→ バッファしない。
     * NVS が無いことの phy_init のエラーは毎回出るだけなので消す。証明書の失敗は net/ktls.c が理由を出す。 */
    setvbuf(stdout, NULL, _IONBF, 0);
    esp_log_level_set("phy_init", ESP_LOG_NONE);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_NONE);
    if ((r = esp_wifi_init_internal(&cfg)) != ESP_OK) { net_log("wifi: init failed (%d)", (int)r); return; }
    if ((r = esp_supplicant_init()) != ESP_OK) { net_log("wifi: supplicant failed (%d)", (int)r); return; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    if ((r = esp_wifi_start()) != ESP_OK) { net_log("wifi: start failed (%d)", (int)r); return; }
    os_wait(flag_set, (void *)&sta_started, 5000);
    esp_wifi_set_ps(WIFI_PS_NONE);          /* 省電力の居眠り無し(受信の遅れを避ける) */
    net_lwip_start(ipaddr, netmask, gateway, dnssrv, ntpsrv);   /* ip= が無いか ip=dhcp なら DHCP */
    memset(&wc, 0, sizeof wc);
    strcpy((char *)wc.sta.ssid, ssid);
    strcpy((char *)wc.sta.password, psk);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    net_log("wifi: connecting to %s", ssid);
    esp_wifi_connect();
    wifi_up = 1;
}

/* slot0 の待ちループから。WiFi のスレッドに順番を回し、切れていたらつなぎ直す */
void plat_idle(void)
{
    if (wifi_up) {
        os_yield();
        net_poll();
        if (!sta_connected && retry_at && (int32_t)(os_ticks() - retry_at) >= 0) {
            retry_at = 0;
            esp_wifi_connect();
        }
    }
    net_log_flush();                        /* 初期化の失敗も含め、溜まった知らせを /var/log/message へ */
    KYIELD();
}
