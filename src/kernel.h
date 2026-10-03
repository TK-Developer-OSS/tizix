#ifndef _KERNEL_H
#define _KERNEL_H

/* 1 秒あたりの plt_interrupt() 呼び出し回数。date/sleep の秒換算に直結。
 * z80pack は cpmsim の仮想 100Hz デバイス(port 27)で正確に 100。
 * z80board は PIC(PIC_RESET_IC.X)が Z80 /INT を外部駆動しており実測未校正
 * ── 確定するまで暫定で 100 のまま(arch.mk の PLAT_DEF で -DTICK_HZ=<実測値>
 * を渡せば上書きできる)。 */
#ifndef TICK_HZ
#define TICK_HZ 100
#endif

extern volatile unsigned int ticks;

void         kernel_init(void);
void         plt_interrupt(void);
unsigned int uptime_sec(void);
unsigned int getticks(void) __sdcccall(0);

/* Unix 秒(32bit)。100Hz 積算。カーネルは刻んで渡すだけ(除算・乗算なし)。
 * 外部 DATE.BIN がベクタ経由で叩く。di/ei 保護済み。 */
unsigned long time_get(void) __sdcccall(0);
void time_set(unsigned long sec) __sdcccall(0);

/* プロセス wait/wake (5a)。ベクタ経由(DRIVER drv_tbl[19..20])で
 * コマンドからも叩くため __sdcccall(0)。パイプ/SD 段の block/wake 土台。 */
void proc_block(void) __sdcccall(0);
void proc_wake(unsigned char block) __sdcccall(0);
#if defined(PLAT_FLAT32)
/* 期限つきの眠り(#112)。wakeat = 起きる時刻(tick、0 = 期限なし)。
 * 戻り 1 = 起こされた / 0 = 期限で起きた。欄はプロセスの見出し(src/phdr.h)。 */
int proc_block_until(unsigned long wakeat);
/* スロット n のプロセスを解放(続きの PID_CONT も。#113)。exit / kill が使う */
void proc_release(unsigned char n);
#endif

#endif