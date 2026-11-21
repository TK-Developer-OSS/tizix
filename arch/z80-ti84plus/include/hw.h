#ifndef _HW_H
#define _HW_H

/* I/O ports of the TI-84 Plus (as emulated by z80e) */
#define TI_PORT_LINK_ASSIST_ENABLE 0x08
#define TI_PORT_LINK_ASSIST_STATUS 0x09	/* bit4 = rx_ready, bit5 = tx_ready */
#define TI_PORT_LINK_ASSIST_RX     0x0A
#define TI_PORT_LINK_ASSIST_TX     0x0D

/* flash layout: pages 0-1 = the kernel (32KB, XIP), pages 2.. = a read-only FAT image */
#define TI_DISK_FIRST_PAGE 2
#define TI_DISK_PAGES      60			/* the TI-84+ has 64 flash pages (1MB) */

#endif
