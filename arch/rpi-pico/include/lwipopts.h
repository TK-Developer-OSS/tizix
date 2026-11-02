#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_DHCP 1
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define TCP_WND (4 * TCP_MSS)
#define TCP_MSS 1460
#define TCP_SND_BUF (4 * TCP_MSS)
#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000
#define MEMP_NUM_PBUF 15
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_SYS_TIMEOUT 10
#define LWIP_STATS 0
#define LWIP_PROVIDES_ERRNO 1

#endif
