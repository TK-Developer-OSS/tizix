/* WiFi 実験(#109): 初期化 → STA で起動 → スキャン(接続は次の段)。 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "osal.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"

extern int ets_printf(const char *fmt, ...);

extern esp_err_t esp_supplicant_init(void);   /* libwpa_supplicant.a(WPA の解析・鍵交換のコールバックを登録) */

static volatile int scan_done, sta_started, sta_result;   /* sta_result: 1 = 接続、2 = 切断 */
void wt_event_post(const char *base, int32_t id, void *data, size_t size)
{
    (void)size;
    ets_printf("event: %s %d\n", base ? base : "?", (int)id);
    if (base && !strcmp(base, "WIFI_EVENT")) {
        if (id == WIFI_EVENT_STA_START) sta_started = 1;
        if (id == WIFI_EVENT_SCAN_DONE) scan_done = 1;
        if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *c = data;
            ets_printf("  connected: ch %d authmode %d\n", c->channel, c->authmode);
            sta_result = 1;
        }
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *d = data;
            ets_printf("  disconnected: reason %d rssi %d\n", d->reason, d->rssi);
            sta_result = 2;
        }
    }
}

/* 接続先(実験用): flash 0x3F000 に "ssid=…\npsk=…\n" を置く(/etc/wifi と同じ書式の予定)。
 *   書き方は Windows 側の wtwifi.ps1。無ければ接続はしない。 */
extern int esp_rom_spiflash_read(uint32_t src, uint32_t *dst, uint32_t len);
static int load_creds(char *ssid, char *psk)
{
    static uint32_t buf[64];
    char *p, *e;
    if (esp_rom_spiflash_read(0x3F000, buf, sizeof buf) != 0) return 0;
    p = (char *)buf;
    if (strncmp(p, "ssid=", 5) != 0) return 0;
    p += 5; e = strchr(p, '\n'); if (!e || e - p > 32) return 0;
    memcpy(ssid, p, e - p); ssid[e - p] = 0;
    p = e + 1;
    if (strncmp(p, "psk=", 4) != 0) return 0;
    p += 4; e = strchr(p, '\n'); if (!e || e - p > 63) return 0;
    memcpy(psk, p, e - p); psk[e - p] = 0;
    return 1;
}

static int flag_set(void *p) { return *(volatile int *)p; }

void wifi_test(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t r;
    uint16_t n = 0;
    static wifi_ap_record_t recs[20];

    cfg.nvs_enable = 0;
    esp_wifi_internal_set_log_level(WIFI_LOG_INFO);
    r = esp_wifi_init_internal(&cfg);
    ets_printf("wifi: init_internal = 0x%x, heap free %u\n", r, (unsigned)os_heap_free());
    if (r) return;
    r = esp_supplicant_init();
    ets_printf("wifi: supplicant_init = 0x%x, heap free %u\n", r, (unsigned)os_heap_free());
    r = esp_wifi_set_mode(WIFI_MODE_STA);
    ets_printf("wifi: set_mode = 0x%x\n", r);
    r = esp_wifi_start();
    ets_printf("wifi: start = 0x%x\n", r);
    os_wait(flag_set, (void *)&sta_started, 5000);
    r = esp_wifi_scan_start(NULL, false);
    ets_printf("wifi: scan_start = 0x%x\n", r);
    os_wait(flag_set, (void *)&scan_done, 10000);
    n = 20;
    r = esp_wifi_scan_get_ap_records(&n, recs);
    ets_printf("wifi: %u APs (0x%x)\n", n, r);
    for (int i = 0; i < n; i++)
        ets_printf("  ch%2d %4d dBm  auth %d  %s\n", recs[i].primary, recs[i].rssi, recs[i].authmode, (char *)recs[i].ssid);

    {
        static wifi_config_t wc;
        char ssid[33], psk[64];
        if (!load_creds(ssid, psk)) {
            ets_printf("wifi: no credentials at flash 0x3F000 (skip connect)\n");
            return;
        }
        memset(&wc, 0, sizeof wc);
        strcpy((char *)wc.sta.ssid, ssid);
        strcpy((char *)wc.sta.password, psk);
        r = esp_wifi_set_config(WIFI_IF_STA, &wc);
        ets_printf("wifi: set_config(%s) = 0x%x\n", ssid, r);
        r = esp_wifi_connect();
        ets_printf("wifi: connect = 0x%x\n", r);
        os_wait(flag_set, (void *)&sta_result, 20000);
        ets_printf("wifi: result %s\n", sta_result == 1 ? "CONNECTED" : sta_result == 2 ? "DISCONNECTED" : "timeout");
    }
}
