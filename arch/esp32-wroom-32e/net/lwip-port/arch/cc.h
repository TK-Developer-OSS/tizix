/* lwIP の移植ヘッダ(arch/cc.h)。tizix の esp32(Xtensa、リトルエンディアン、newlib) */
#ifndef TIZIX_LWIP_CC_H
#define TIZIX_LWIP_CC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BYTE_ORDER  LITTLE_ENDIAN
typedef unsigned long sys_prot_t;   /* NO_SYS では arch/sys_arch.h が読まれないのでここで(保護 = 割込み禁止) */
#define LWIP_NO_INTTYPES_H 1
#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "u"

extern int ets_printf(const char *fmt, ...);
#define LWIP_PLATFORM_DIAG(x)    do { ets_printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x)  do { ets_printf("lwip assert: %s (%s:%d)\n", x, __FILE__, __LINE__); for (;;) ; } while (0)

#endif
