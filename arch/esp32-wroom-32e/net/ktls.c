/* arch/esp32-wroom-32e/net/ktls.c(#109): TLS(https 用)。net/knet.c の TCP 1 本の上に被せる。
 *
 *   ESP-IDF のビルド済み mbedtls(libmbedtls_2.a = TLS、libmbedx509 / libmbedcrypto、
 *   libmbedtls.a = 証明書バンドル esp_crt_bundle。Mozilla の CA 一式 69KB は DROM に乗る)を使う。
 *   証明書は既定で検証する(バンドルの CA に繋がること + ホスト名)。日時の検査があるので
 *   時計が要る(net/netif_wifi.c が SNTP で合わせる)。
 *   すべて slot0(knet_poll)から呼ばれる。下回りの送受信は knet.c の knet_raw_send / knet_raw_recv
 *   (lwIP の pcb と受信の pbuf を直接扱う)。どれも待たない(足りなければ WANT_READ / WANT_WRITE)。
 */
#include <stdint.h>
#include <string.h>
#include "osal.h"
#include "mbedtls/ssl.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"   /* MBEDTLS_ERR_NET_CONN_RESET */
#include "esp_crt_bundle.h"
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include "psa/crypto.h"
#endif

extern int kprintf(const char *fmt, ...);
extern void esp_fill_random(void *buf, size_t len);
extern int knet_raw_send(const uint8_t *buf, int len);   /* 0 = 今は送れない */
extern int knet_raw_recv(uint8_t *buf, int len);         /* 0 = まだ来ていない / -1 = 相手が閉じた */

/* mbedtls(x509 の名前照合)が IP アドレスの CN を読むのに呼ぶ。IPv4 だけ(AF_INET = 2) */
#include "lwip/ip4_addr.h"
int lwip_inet_pton(int af, const char *src, void *dst)
{
    ip4_addr_t a;
    if (af != 2 || !ip4addr_aton(src, &a))
        return 0;
    memcpy(dst, &a, 4);
    return 1;
}

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config  conf;
static int                 active, conf_ready;

static int rng(void *p, unsigned char *out, size_t len)
{
    (void)p;
    esp_fill_random(out, len);
    return 0;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n;
    (void)ctx;
    n = knet_raw_send(buf, (int)len);
    if (n < 0) return MBEDTLS_ERR_NET_CONN_RESET;
    return n ? n : MBEDTLS_ERR_SSL_WANT_WRITE;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int n;
    (void)ctx;
    n = knet_raw_recv(buf, (int)len);
    if (n < 0) return 0;            /* 相手が閉じた = EOF */
    return n ? n : MBEDTLS_ERR_SSL_WANT_READ;
}

/* verify: 1 = 証明書を検証する / 0 = しない(curl -k) */
int ktls_begin(const char *host, int verify)
{
    int r;

    if (active)
        return -1;
#if defined(MBEDTLS_PSA_CRYPTO_C)
    psa_crypto_init();
#endif
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    conf_ready = 1;
    if ((r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
        goto fail;
    mbedtls_ssl_conf_rng(&conf, rng, NULL);
    if (verify) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        if ((r = esp_crt_bundle_attach(&conf)) != 0)
            goto fail;
    } else
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    if ((r = mbedtls_ssl_setup(&ssl, &conf)) != 0)
        goto fail;
    if ((r = mbedtls_ssl_set_hostname(&ssl, host)) != 0)
        goto fail;
    mbedtls_ssl_set_bio(&ssl, NULL, bio_send, bio_recv, NULL);
    active = 1;
    return 0;
fail:
    kprintf("tls: setup failed (-%d)\n", -r);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    conf_ready = 0;
    return -1;
}

/* 0 = 終わった / 1 = 続く / -1 = 失敗 */
int ktls_handshake(void)
{
    int r = mbedtls_ssl_handshake(&ssl);

    if (r == 0)
        return 0;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 1;
    {
        uint32_t v = mbedtls_ssl_get_verify_result(&ssl);
        kprintf("tls: handshake failed (-%d)", -r);
        if (v && v != 0xFFFFFFFFu) {
            /* 証明書の検査で落ちた理由(mbedtls/x509.h の MBEDTLS_X509_BADCERT_*) */
            if (v & MBEDTLS_X509_BADCERT_EXPIRED)     kprintf(" certificate expired");
            if (v & MBEDTLS_X509_BADCERT_FUTURE)      kprintf(" certificate not yet valid");
            if (v & MBEDTLS_X509_BADCERT_CN_MISMATCH) kprintf(" name mismatch");
            if (v & MBEDTLS_X509_BADCERT_NOT_TRUSTED) kprintf(" untrusted CA");
            if (v & MBEDTLS_X509_BADCERT_REVOKED)     kprintf(" revoked");
            if (v & ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE |
                                MBEDTLS_X509_BADCERT_CN_MISMATCH | MBEDTLS_X509_BADCERT_NOT_TRUSTED |
                                MBEDTLS_X509_BADCERT_REVOKED))
                kprintf(" (verify flags %u)", (unsigned)v);
        } else if (r == MBEDTLS_ERR_X509_FATAL_ERROR)
            /* CA バンドル(esp_crt_bundle)の検査で落ちると、理由のフラグを立てずに FATAL_ERROR(-0x3000)が返る */
            kprintf(" certificate rejected (expired, self-signed, or CA not in the bundle)");
        kprintf("\n");
    }
    return -1;
}

/* 復号した平文。>0 = バイト数 / 0 = 今は無い / -1 = 相手が閉じた・エラー */
int ktls_read(uint8_t *buf, int len)
{
    int r = mbedtls_ssl_read(&ssl, buf, (size_t)len);

    if (r > 0)
        return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        return 0;
#endif
    return -1;                      /* 0(EOF)/ CLOSE_NOTIFY / エラー */
}

/* 平文を暗号化して送る。>=0 = 受け付けたバイト数 / -1 = エラー */
int ktls_write(const uint8_t *buf, int len)
{
    int r = mbedtls_ssl_write(&ssl, buf, (size_t)len);

    if (r >= 0)
        return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 0;
    return -1;
}

/* 復号済みでまだ読んでいない平文があるか */
int ktls_pending(void)
{
    return active && mbedtls_ssl_get_bytes_avail(&ssl) > 0;
}

void ktls_end(int notify)
{
    if (!active && !conf_ready)
        return;
    if (active && notify)
        mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    active = conf_ready = 0;
}
