#ifndef _PLAT_H
#define _PLAT_H

#define PLAT_FLAT32       1
#define PLAT_NSLOT        4
#define PLAT_SLOT_ADDR(n) (0x20020000UL)
#define PROC_SLOT_KB      32

#ifndef __ASSEMBLER__

#ifndef TIZIX_USERCMD   /* コマンド(user/)は Pico SDK 無しで組む。カーネル側だけが SDK を使う */
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

void pico_wifi_late_init(void);
#define PLAT_LATE_INIT() pico_wifi_late_init()

/* IRQ_ON/OFF using Pico SDK critical section API */
#define IRQ_OFF()  do { __save_irq = save_and_disable_interrupts(); } while (0)
#define IRQ_ON()   do { restore_interrupts(__save_irq); } while (0)
extern uint32_t __save_irq;

/* KYIELD - trigger a context switch by yielding via timer */
#define KYIELD()   do { hw_set_bits(&timer_hw->inte, 1u << 0); } while (0)
#endif /* !TIZIX_USERCMD */

/* ---- 共有 src/ を arm-none-eabi-gcc で通すための移植シム(esp32 / m68k-mega の
 * plat.h と同じ役目)。SDCC 専用の呼出規約属性は無意味 → 消す。 */
#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __critical

#endif /* !__ASSEMBLER__ */

#endif


