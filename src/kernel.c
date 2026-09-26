#include "io.h"
#include "kernel.h"
#include "kmem.h"

#if defined(ARCH_X86_IA16)
/* 絶対番地ワークの実体(kmem.h が KW_* をこの配列オフセットに再定義)。 */
unsigned char kwork[0x160];
#elif defined(ARCH_M68K_MEGA)
/* m68k は int=32bit・ポインタ=4B で z80/x86(いずれも 16bit)より各フィールドが
 * 太る(struct vnode も 16B→20B)ため、専用サイズで確保する(kmem.h 参照)。 */
unsigned char kwork[KWORK_SIZE];
/* #78: ps 用の名前表を足したので、使用末端が KWORK_SIZE を越えたら止める。 */
typedef char kw_used_fits[(KW_M68K_USED <= KWORK_SIZE) ? 1 : -1];
/* #61: kmem.h の KW_* オフセットは 63 エントリで手計算してある。M68K_NSLOT を
 * それ以上へ増やすとテーブル同士が静かに重なるので、ここでビルドを止める
 * (配列長が負になりコンパイルエラー)。増やす時は kmem.h のオフセットを
 * 採り直してから、この 63 も一緒に上げること。 */
typedef char kw_slot_table_fits[(M68K_NSLOT <= 63) ? 1 : -1];
#elif defined(ARCH_Z80PACK)
__sfr __at 27 TIMER;   /* cpmsim 仮想デバイス。out 1 で 100Hz tick が回り出す */
#endif
/* z80board: TIMER デバイスは無い。tick は PIC(PIC_RESET_IC.X)が Z80 /INT を
 * 外部駆動して供給するので、Z80 側は im 1 / ei するだけでよい。 */

/* 時刻/tick はすべて絶対番地に置く。
 *   C グローバルにすると _DATA 先頭(0x8000)へ配置され、FatFs の共有セクタ
 *   バッファ win[512] と重なって FAT アクセスのたびに破壊される。
 *   (旧 `volatile unsigned int ticks;` がこれに当たり、getticks() が
 *    0x8001 等のゴミを返していた) */
#define EPOCH   (*(volatile unsigned long *)KW_EPOCH_SEC)
#define SUBTICK (*(volatile unsigned char *)KW_SUB_TICK)
#define TICKS   (*(volatile unsigned int  *)KW_TICKS)

void plt_interrupt(void)
{
    TICKS++;                      /* delay 用の生 tick(100Hz) */

    /* TICK_HZ を秒に畳む。ISR 内なので割り込みは閉じている(競合なし)。
     * ++ と比較のみ、除算しない。 */
    if (++SUBTICK >= TICK_HZ) {
        SUBTICK = 0;
        EPOCH++;                  /* 32bit ++。__divul 等は呼ばない */
    }
}

void kernel_init(void)
{
    TICKS = 0;
    EPOCH = 0;                    /* 1970/1/1 00:00:00 から。date <秒> で設定可 */
    SUBTICK = 0;

#if defined(ARCH_X86_IA16)
    /* PIT 100Hz / INT 08h ベクタ / PIC アンマスクは crt0.s で設定済み。
     * ここで割り込みを解禁する(Z80 の ei 相当)。 */
    IRQ_ON();
#elif defined(ARCH_M68K_MEGA)
    /* レベル6ベクタ(crt0.s irq6_handler)は周期 tick を受ける。周期は
     * arch/m68k-mega/Makefile の TICK_HZ(既定 100、m68ksim も同じ値。実機の
     * Mega Timer5 が 1Hz なら TICK_HZ=1 でビルド)。#83。
     * ここでは SR の割込みマスクを解くだけでよい。 */
    IRQ_ON();
#else
    __asm
        im 1
    __endasm;
#if defined(ARCH_Z80PACK)
    TIMER = 1;                    /* out (27),1 : cpmsim 仮想 100Hz tick を起動 */
#endif
    /* z80board(#59): ei 解禁。Z80_INT は FT245 ~RXF と PIC GP5(タイマ、
     * 2026-09-19 配線)のダイオード OR。crt0.s の isr が SYS_STAT_PORT の
     * bit6(~RXF)で振り分ける ── ~RXF なら 1 バイトドレインして戻るだけ、
     * それ以外は GP5 起因として tick++ + save/pick/restore(isr_timer)。
     * GP5 は 500us Low が続くレベル信号なので、その間 isr が連続で
     * 再入し tick がまとまって進む見込み(TICK_HZ の精密較正はせず、
     * 将来 ntpdate 等の定期補正に任せる方針。ユーザー判断)。 */
    __asm
        ei
    __endasm;
#endif
}

/* ==================================================================
 * プロセス wait/wake (5a)
 *   proc_block: 呼び出しブロックを runnable から外し、proc_wake まで park。
 *               z80: crt0.s sched_pick が KW_BLOCKED[block]!=0 を飛ばす。
 *   proc_wake : 対象ブロックを runnable へ戻す。ISR/他プロセスから可。
 *   取りこぼし対策: proc_block の前に proc_wake が来たら wakepend に記録し、
 *   proc_block がそれを消費して即戻る(lost wakeup 防止)。フラグ更新は
 *   di/ei で囲む(他プロセスの time slice 内で走る proc_wake との排他)。
 *   パイプ / SD ドライバ段の本物の block/wake 土台。
 * ================================================================== */
#define KBLOCKED  ((volatile unsigned char *)KW_BLOCKED)
#define KWAKEPEND ((volatile unsigned char *)KW_WAKEPEND)
#define KCUR      (*(volatile unsigned char *)KW_CURRENT)

void proc_block(void) __sdcccall(0)
{
    unsigned char me = KCUR;

    IRQ_OFF();
    if (KWAKEPEND[me]) {              /* 先に wake が来ていた → 消費して即戻る */
        KWAKEPEND[me] = 0;
        IRQ_ON();
        return;
    }
    KBLOCKED[me] = 1;
    IRQ_ON();

    while (KBLOCKED[me]) {            /* park。ISR が退避 → sched_pick が飛ばす */
#if defined(ARCH_X86_IA16)
        __asm__ volatile ("sti; hlt");
#elif defined(ARCH_M68K_MEGA)
        /* ★#47: m68ksim(rocket68)の STOP 実装が呼出1回あたり数秒単位で
         * ホストへ制御を返さない(内部で割込み到着を長々とポーリングし、
         * CYCLES_PER_SLICE を守らない)ことが判明した(kmain.c の同種の
         * 修正コメント参照)。proc_block は今のところどこからも呼ばれて
         * いないが、将来使われた時に同じ「数秒ブロック」を踏まないよう
         * STOP は使わない。#94: NOP スピンをやめて KYIELD(TRAP #1)で譲る
         * ── park 中のスロットは sched_tick_sp が飛ばすので、起こされるまで回らない。 */
        KYIELD();
#elif defined(ARCH_Z80BOARD)
        KYIELD();       /* タイマ未結線: ei;halt だと二度と起きない(#54) */
#else
        __asm
            ei
            halt
        __endasm;
#endif
    }
}

void proc_wake(unsigned char block) __sdcccall(0)
{
    if (block > PROC_BLOCK_MAX)      /* #82: m68k はスロットが 8 より多い */
        return;
    IRQ_OFF();
    if (KBLOCKED[block])
        KBLOCKED[block] = 0;         /* park 中 → 起こす */
    else
        KWAKEPEND[block] = 1;        /* まだ park してない → 取りこぼし防止 */
    IRQ_ON();
}

#if defined(ARCH_M68K_MEGA)
/* ==================================================================
 * m68k-mega スケジューラ(2 スロット: 0=kernel/shell, 1=外部コマンド)。
 *   crt0.s の irq6_handler(タイマ)/trap0_handler(syscall)から呼ばれる。
 *   レジスタ退避(movem)と SP の切替は asm 側、「誰に切り替えるか」だけ
 *   ここで計算する(x86 の _isr08 のロジックを C に写したもの)。
 * ================================================================== */
/* M68K_NSLOT は src/kmem.h が唯一の定義場所(#61)。ここで再定義しない ──
 * 以前ここが 8 のままで、スロット 8 以降が永久にスケジュールされなかった。 */
#define M68K_PIDTAB ((volatile unsigned char *)KW_PIDTAB)
#define M68K_SPTBL  ((volatile unsigned long  *)KW_SPTBL)
#define M68K_CUR    (*(volatile unsigned char *)KW_CURRENT)

/* 走らせてよいスロットか。z80 の crt0.s sched_pick と同じ規則(#82):
 *   pid==0(free)と PID_PIPEBUF(カーネルパイプのバッファ枠。プロセスではない
 *   ので「切り替える」と暴走する)は飛ばす。slot0(kernel/shell)以外は
 *   proc_block 中(KW_BLOCKED)も飛ばす。 */
static unsigned char m68k_runnable(unsigned char c)
{
    unsigned char p = M68K_PIDTAB[c];

    if (p == 0 || p == PID_PIPEBUF)
        return 0;
    if (c != 0 && KBLOCKED[c])
        return 0;
    return 1;
}

/* タイマ割込み: 現在のスロットの SP を保存し、次に走らせるスロットの
 * SP を返す。走れないスロット(m68k_runnable)は飛ばす。1 個も無ければ
 * 現在のスロットを維持する(slot0 は常時 runnable なので実際には起きない)。 */
unsigned long sched_tick_sp(unsigned long cur_sp)
{
    unsigned char c = M68K_CUR;
    unsigned char n;

    M68K_SPTBL[c] = cur_sp;
    for (n = 0; n < M68K_NSLOT; n++) {
        c++;
        if (c >= M68K_NSLOT) c = 0;
        if (m68k_runnable(c)) {
            M68K_CUR = c;
            return M68K_SPTBL[c];
        }
    }
    return cur_sp;   /* 走行可能スロットが無い(異常系)。今のまま続行 */
}

/* exit syscall: 現在のスロットを解放してから次を探す(現コンテキストは
 * 破棄するので保存しない)。 */
unsigned long sched_exit_sp(void)
{
    unsigned char c = M68K_CUR;
    unsigned char n;

    M68K_PIDTAB[c] = 0;
    for (n = 0; n < M68K_NSLOT; n++) {
        c++;
        if (c >= M68K_NSLOT) c = 0;
        if (m68k_runnable(c)) {
            M68K_CUR = c;
            return M68K_SPTBL[c];
        }
    }
    /* slot0(kernel/shell)は pid_tbl[0]!=0 で常時 runnable のはずなので
     * ここに来るのは PCB 初期化忘れ等の異常系。slot0 へ強制的に戻す。 */
    M68K_CUR = 0;
    return M68K_SPTBL[0];
}
#endif

unsigned int uptime_sec(void)
{
    unsigned int t = TICKS;
    unsigned int s = 0;
    while (t >= TICK_HZ) { t -= TICK_HZ; s++; }   /* 除算ヘルパを使わない */
    return s;
}

unsigned int getticks(void) __sdcccall(0)
{
    /* z80board: 時間待ちループ(getc_timeout / sleep)が getticks を回す間に
     * 他のプロセスへ譲る。**KYIELD は tick を進めない**(#71 で修正。以前は
     * タイマ未結線時代の名残で 1 回ごとに tick++ しており、待ちが実時間に
     * ならなかった)。z80pack 等では KYIELD は空。 */
    KYIELD();
    return TICKS;   /* raw TICK_HZ tick for delay import */
}

/* time_get: 現在の Unix 秒(32bit)を返す。ベクタ 0x004A。
 * 32bit read は非アトミック(複数命令)。ISR が EPOCH を更新する瞬間に割り込ま
 * れると壊れた値を読むため di/ei で囲む(数命令だけ閉じる)。
 * ※外部コマンドは 32bit 戻り値の受け渡し規約差を避けるため、
 *   KW_EPOCH_SEC を直接読んでもよい(メモリは flat)。 */
unsigned long time_get(void) __sdcccall(0)
{
    unsigned long t;
    IRQ_OFF();
    t = EPOCH;
    IRQ_ON();
    return t;
}

/* time_set: Unix 秒を設定。ベクタ 0x004D。date <秒> / 将来 NTP が使う。 */
void time_set(unsigned long sec) __sdcccall(0)
{
    IRQ_OFF();
    EPOCH = sec;
    SUBTICK = 0;
    IRQ_ON();
}
