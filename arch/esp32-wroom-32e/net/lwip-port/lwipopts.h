/* arch/esp32-wroom-32e/net/lwip-port/lwipopts.h(#109): tizix の WiFi に載せる lwIP の設定。
 *   OS 無し(NO_SYS = 1、raw API)。lwIP は slot0 の中だけで動く(WiFi の受信コールバックと
 *   init の待ちループが同じ協調スレッド群の中なので、lwIP の中で取り合いは起きない)。
 *   メモリは newlib の malloc(net/osal.c の _sbrk = 上の DRAM)から取る。 */
#ifndef TIZIX_LWIPOPTS_H
#define TIZIX_LWIPOPTS_H

#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        1      /* sys_arch_protect = 割込み禁止(受信は WiFi のスレッドから来るが念のため) */
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1      /* /etc/wifi に ip= が無い(か ip=dhcp)なら DHCP(#112 の前段) */
#define LWIP_NETIF_STATUS_CALLBACK  1      /* アドレスが付いた / 変わったのを知る(ログに出す) */
#define LWIP_DNS                    1      /* net/knet.c: net_connect("名前:port") */
#define LWIP_IGMP                   0
#define LWIP_AUTOIP                 0
#define LWIP_STATS                  0
#define LWIP_NETIF_HOSTNAME         0

#define MEM_LIBC_MALLOC             1
#define MEMP_MEM_MALLOC             1
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)

#define TCP_MSS                     1436
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
/* 待ち受け(knet_listen)を閉じた直後に同じポートで張り直せるように(サーバ側が先に閉じると
 * その接続が TIME_WAIT でポートを握り、tcp_bind が ERR_USE になる。#110 の mcpd で踏んだ) */
#define SO_REUSE                    1
#define PBUF_POOL_SIZE              16

/* SNTP(apps/sntp): リンクが上がったら時計を合わせる(TLS の証明書の有効期限を見るのに要る)。
 * サーバは /etc/wifi の ntp=(省略時 pool.ntp.org)。合ったら net_sntp_set → カーネルの time_set。 */
extern void net_sntp_set(unsigned long sec);
#define SNTP_SERVER_DNS             1
#define SNTP_STARTUP_DELAY          0
#define SNTP_UPDATE_DELAY           3600000
#define SNTP_MAX_SERVERS            2      /* ntp= と time.google.com を交互に */
#define SNTP_RETRY_TIMEOUT          3000   /* 既定 15 秒では起動直後の https が時計待ちで時間切れになった */
#define SNTP_RETRY_TIMEOUT_MAX      30000
#define SNTP_RETRY_TIMEOUT_EXP      1
#define SNTP_SET_SYSTEM_TIME(sec)   net_sntp_set((unsigned long)(sec))

#define LWIP_CHECKSUM_ON_COPY       0
#define LWIP_TCP_KEEPALIVE          1

extern unsigned long esp_random(void);
#define LWIP_RAND()                 ((u32_t)esp_random())

#endif
