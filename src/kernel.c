#include "io.h"
#include "kernel.h"
#include "kmem.h"
#if defined(PLAT_FLAT32)
#include "phdr.h"
#endif

#if defined(ARCH_X86_IA16)
/* 絶対番地ワークの実体(kmem.h が KW_* をこの配列オフセットに再定義)。 */
unsigned char kwork[0x160];
#elif defined(PLAT_FLAT32)
/* 配置も大きさも kmem.h の PLAT_FLAT32 節が積み上げで決める(スロット数は arch の plat.h)。
 * 配列そのものを 4 バイト境界に置き、その中の 32bit の項目が 4 バイト境界に
 * 乗っていることをここで確かめる ── 68000 は奇数番地、Xtensa は 4 の倍数でない番地の
 * 32bit アクセスで例外になる。エミュレータ(rocket68 / QEMU)はどちらも素通しするので、
 * 実機で初めて落ちる種類の間違いをビルドで止める(配列長が負になりコンパイルエラー)。 */
unsigned char kwork[KWORK_SIZE] __attribute__((aligned(4)));
typedef char kw_u32_aligned[((KW_O_SPTBL | KW_O_EPOCH | KW_O_TICKS | KW_O_VTREE | KW_O_PIPE) & 3UL) == 0 ? 1 : -1];
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
#elif defined(PLAT_FLAT32)
    /* タイマの設定は arch 側が済ませている(周期は arch の Makefile の TICK_HZ。#83)。
     *   m68k-mega: レベル6ベクタ(crt0.s irq6_handler)。実機は Mega の Timer5、m68ksim も同じ値
     *   esp32-wroom-32e: kmain.c が CCOMPARE0 に仕掛ける
     * ここでは割込みのマスクを解くだけでよい。 */
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
#define KCUR      (*(volatile unsigned char *)KW_CURRENT)

#if defined(PLAT_FLAT32)
/* ---- PLAT_FLAT32: 眠り / 起こすはプロセスの見出し(src/phdr.h、データ枠の先頭 32B)の欄で。
 * task.md #112。z80 / x86 の KW_BLOCKED / KW_WAKEPEND 表の代わり。期限(起きる時刻)も持てる:
 * 期限が来たらスケジューラ(slot_runnable)が欄を落として走らせる。 */
unsigned char kphdr0[PH_SIZE] __attribute__((aligned(4)));   /* slot0 の見出し */

/* wakeat = 起きる時刻(tick)。0 = 期限なし(proc_wake まで眠る)。
 * 戻り: 1 = 起こされた(または先に起こされていた)/ 0 = 期限で起きた。 */
int proc_block_until(unsigned long wakeat)
{
    unsigned char me = KCUR;

    IRQ_OFF();
    if (PH_PEND_OF(me)) {              /* 先に wake が来ていた → 消費して即戻る */
        PH_PEND_OF(me) = 0;
        IRQ_ON();
        return 1;
    }
    PH_WAKEAT_OF(me) = wakeat;
    PH_STATE_OF(me)  = PH_SLEEP;
    IRQ_ON();

    while (PH_STATE_OF(me) == PH_SLEEP)
        KYIELD();                      /* 眠っている間はスケジューラが飛ばす(下の proc_block の注釈) */
    /* 期限で起きたときは slot_runnable が PH_WAKEAT を 0 にしてから落とす */
    return PH_WAKEAT_OF(me) == 0 && wakeat != 0 ? 0 : 1;
}

void proc_block(void)
{
    /* ★#47: m68ksim(rocket68)の STOP は数秒戻らないので使わない。#94: KYIELD で譲る。 */
    (void)proc_block_until(0);
}

void proc_wake(unsigned char block)
{
    if (block > PROC_BLOCK_MAX)        /* パイプの「まだ居ない」印(0xFF)など */
        return;
    IRQ_OFF();
    if (PH_STATE_OF(block) == PH_SLEEP) {
        PH_WAKEAT_OF(block) = 1;       /* 「起こされた」印(0 は期限切れの印) */
        PH_STATE_OF(block)  = PH_RUN;
    } else
        PH_PEND_OF(block) = 1;         /* まだ眠っていない → 取りこぼし防止 */
    IRQ_ON();
}

#else
#define KBLOCKED  ((volatile unsigned char *)KW_BLOCKED)
#define KWAKEPEND ((volatile unsigned char *)KW_WAKEPEND)

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
#elif defined(PLAT_FLAT32)
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
#endif /* !PLAT_FLAT32 */

#if defined(PLAT_FLAT32)
/* ==================================================================
 * スケジューラの C 側(PLAT_FLAT32 のポート共通。slot0 = kernel/init、1.. = 外部コマンド)。
 *   arch の例外入口(m68k-mega: crt0.s の irq6_handler / trap0_handler、
 *   esp32-wroom-32e: crt0.S の exc_entry)から呼ばれる。レジスタの退避と SP の
 *   切替は asm 側、「誰に切り替えるか」だけここで計算する(x86 の _isr08 の
 *   ロジックを C に写したもの)。
 * ================================================================== */
/* スロット数 KW_NSLOT は arch の include/plat.h(PLAT_NSLOT)が唯一の定義場所(#61)。
 * ここで再定義しない ── 以前ここが 8 のままで、スロット 8 以降が永久に
 * スケジュールされなかった。 */
#define KS_PIDTAB ((volatile unsigned char *)KW_PIDTAB)
#define KS_SPTBL  ((volatile unsigned long  *)KW_SPTBL)
#define KS_CUR    (*(volatile unsigned char *)KW_CURRENT)

/* 走らせてよいスロットか。z80 の crt0.s sched_pick と同じ規則(#82):
 *   pid==0(free)と PID_PIPEBUF(カーネルパイプのバッファ枠。プロセスではない
 *   ので「切り替える」と暴走する)は飛ばす。slot0(kernel/shell)以外は
 *   眠っている(見出しの PH_STATE、src/phdr.h)スロットも飛ばす。ただし起きる時刻が
 *   来ていれば欄を落として走らせる(PH_WAKEAT を 0 にして「期限で起きた」の印に)。
 *   slot0 は眠っていても飛ばさない(期限の処理だけする)。 */
static unsigned char slot_runnable(unsigned char c)
{
    unsigned char p = KS_PIDTAB[c];

    if (p == 0 || p == PID_PIPEBUF || p == PID_CONT)   /* PID_CONT = 複数スロットのプロセスの続き(#113) */
        return 0;
    if (PH_STATE_OF(c) == PH_SLEEP) {
        unsigned long w = PH_WAKEAT_OF(c);
        if (w != 0 && (long)((unsigned long)TICKS - w) >= 0) {
            PH_WAKEAT_OF(c) = 0;
            PH_STATE_OF(c)  = PH_RUN;
        } else if (c != 0)
            return 0;
    }
    return 1;
}

/* タイマ割込み: 現在のスロットの SP を保存し、次に走らせるスロットの
 * SP を返す。走れないスロット(slot_runnable)は飛ばす。1 個も無ければ
 * 現在のスロットを維持する(slot0 は常時 runnable なので実際には起きない)。 */
unsigned long sched_tick_sp(unsigned long cur_sp)
{
    unsigned char c = KS_CUR;
    unsigned char n;

    KS_SPTBL[c] = cur_sp;
    for (n = 0; n < KW_NSLOT; n++) {
        c++;
        if (c >= KW_NSLOT) c = 0;
        if (slot_runnable(c)) {
            KS_CUR = c;
            return KS_SPTBL[c];
        }
    }
    return cur_sp;   /* 走行可能スロットが無い(異常系)。今のまま続行 */
}

/* プロセスのスロットを解放する(先頭 n と、続きの PID_CONT。#113)。exit / kill の共通の出口。 */
void proc_release(unsigned char n)
{
    if (n == 0 || n >= KW_NSLOT)
        return;
    KS_PIDTAB[n] = 0;
    while (++n < KW_NSLOT && KS_PIDTAB[n] == PID_CONT)
        KS_PIDTAB[n] = 0;
}

/* exit syscall: 終了コード(main の戻り値。arch の crt0cmd が exit に載せてくる)を控え、
 * 現在のスロットを解放してから次を探す(現コンテキストは破棄するので保存しない)。 */
unsigned long sched_exit_sp(unsigned long code)
{
    unsigned char c = KS_CUR;
    unsigned char n;

    ((volatile unsigned char *)KW_EXITCODE)[c] = (unsigned char)code;   /* #111 */
    proc_release(c);
    for (n = 0; n < KW_NSLOT; n++) {
        c++;
        if (c >= KW_NSLOT) c = 0;
        if (slot_runnable(c)) {
            KS_CUR = c;
            return KS_SPTBL[c];
        }
    }
    /* slot0(kernel/shell)は pid_tbl[0]!=0 で常時 runnable のはずなので
     * ここに来るのは PCB 初期化忘れ等の異常系。slot0 へ強制的に戻す。 */
    KS_CUR = 0;
    return KS_SPTBL[0];
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
