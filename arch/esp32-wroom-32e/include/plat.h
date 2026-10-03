#ifndef _PLAT_H
#define _PLAT_H
/* ============================================================================
 * arch/esp32-wroom-32e ボードマップ
 *
 * ESP32(Xtensa LX6 デュアルコア 240MHz、SRAM 520KB)単体で tizix を動かす。
 * ESP-IDF / FreeRTOS は使わない(ベアメタル)。APP CPU(コア 1)は ROM が
 * リセットで止めたままにしておき、PRO CPU(コア 0)だけで走る。
 *
 * 起動: チップ内蔵 ROM の 1 段目ローダが flash 0x1000 の esptool 形式
 * イメージ(本来は ESP-IDF の 2 段目ブートローダが置かれる場所)を読み、
 * セグメントを IRAM/DRAM へ写して entry へ飛ぶ。tizix のカーネルをそこへ
 * 直接置くので、2 段目ブートローダは無い。
 *
 * メモリ(link-kernel.ld の MEMORY と一致させること):
 *   IRAM 0x40080000- 128KB  SRAM0。命令用(32bit アクセスのみ)
 *   DRAM 0x3FFB0000- 192KB  SRAM2 の後半。先頭 8KB(0x3FFAE000-)は ROM が使う
 *   0x3FFE0000- は SRAM1。ROM ローダの作業域とスタックが居るので使わない
 *   (起動が済めば使えるが、M1 では触らない)
 * ========================================================================= */

/* 例外フレーム(crt0.S の exc_entry が積む形。loader.c の偽コンテキストも同じ形)。
 * a0..a15 を 4 バイトずつ(a1 の席は使わない: 復帰時に a1 = フレーム番地 + CTX_SIZE)、
 * 続けて PC / PS / SAR / LBEG / LEND / LCOUNT / EXCCAUSE / EXCVADDR で 96 バイト。
 * その上に 16 バイト空ける(CTX_SIZE = 112): カーネルは windowed なので、割り込まれた
 * 関数の SP の直下 16 バイトは「その呼び出し元の a0..a3」の吐き出し先として予約されている。
 * .S からも読むので、ここだけ接尾辞(UL)無しの素の数で書く。 */
#define CTX_A(n)         ((n) * 4)
#define CTX_PC           64
#define CTX_PS           68
#define CTX_SAR          72
#define CTX_LBEG         76
#define CTX_LEND         80
#define CTX_LCOUNT       84
#define CTX_CAUSE        88
#define CTX_VADDR        92
#define CTX_SIZE         112

#define PS_EXCM          0x00000010
#define PS_UM            0x00000020
#define PS_WOE           0x00040000

/* CCOMPARE0 = CPU 内蔵タイマ 0(割込み 6 番、レベル 1)。tick はこれで作る。 */
#define TIMER_INTNO      6

#ifndef __ASSEMBLER__

#define IRAM_ORIGIN      0x40080000UL
#define DRAM_ORIGIN      0x3FFB0000UL
#define DRAM_SIZE        0x1C000UL     /* カーネルのデータ+BSS+スタック(112KB) */

/* 1 枠 = 命令ブロック 1 つ(IRAM 16KB)+ データブロック 1 つ(DRAM 16KB)。task.md #112。
 *
 * プロセス枠(slot 1..PLAT_NSLOT-1)のデータ領域。DRAM の後ろ 80KB をデータブロック 5 つ
 * (命令ブロックの数に合わせる。前の 112KB はカーネル)。
 * 下の PLAT_SLOT_ADDR(n) がこれを引く(カーネルパイプのバッファも同じ枠)。 */
#define PROC_DATA_BASE   0x3FFCC000UL
#define PROC_DATA_SIZE   0x4000UL

/* プロセス枠のコード領域。DRAM は実行できないので、IRAM(128KB)を命令ブロック 8 つと見て
 * 先頭 3 つ(48KB)をカーネル、残り 5 つを枠に割る(link-kernel.ld の iram 長と一致させること)。
 * IRAM は 32bit アクセスしか効かない(実機はバイト書きで例外)ので、loader.c は
 * ワード単位で書く。コマンドの .rodata/.data はデータ枠の側に置く(user/cmd.ld)。 */
#define PROC_TEXT_BASE   0x4008C000UL
#define PROC_TEXT_SIZE   0x4000UL

/* CPU クロック。ROM は PLL を起こさず水晶(WROOM-32E は 40MHz)のまま飛んでくる。
 * CCOUNT はこの速さで進む。 */
#define CPU_HZ           40000000UL

/* UART0(ROM が 115200bps 8N1 に設定済み。U0TXD=GPIO1 / U0RXD=GPIO3、
 * 開発ボードでは USB シリアル変換につながっている) */
#define UART0_FIFO       0x3FF40000UL  /* R: 受信 1 バイト / W: 送信 1 バイト */
#define UART0_STATUS     0x3FF4001CUL  /* bit7-0 RXFIFO_CNT / bit23-16 TXFIFO_CNT */
#define UART_FIFO_LEN    128

/* ディスク = SPI flash の後ろ 3MB(Makefile の DISK_OFF / DISK_SIZE と一致させる)。
 * 先頭 1MB はカーネル像(0x1000)と将来の予備。 */
#define DISK_FLASH_OFF   0x100000UL
#define DISK_FLASH_SIZE  0x300000UL

/* flash から直接走らせる部分(WiFi のバイナリ。PLAT_WIFI のときだけ中身がある)。
 * kmain.c がキャッシュ MMU を張り、Makefile が drom.bin / irom.bin をここへ書く。64KB 単位。 */
#define DROM_FLASH_OFF   0x040000UL    /* → 0x3F400000 */
#define DROM_FLASH_SIZE  0x030000UL
#define IROM_FLASH_OFF   0x070000UL    /* → 0x400D0000 */
#define IROM_FLASH_SIZE  0x090000UL

/* ウォッチドッグ。ROM は flash 起動時に RTC WDT と TIMG0 の MWDT を
 * 動かしたまま飛んでくるので、放っておくと数秒でリセットされる。 */
#define WDT_WKEY         0x50D83AA1UL
#define TIMG0_WDTCONFIG0 0x3FF5F048UL
#define TIMG0_WDTWPROTECT 0x3FF5F064UL
#define TIMG1_WDTCONFIG0 0x3FF60048UL
#define TIMG1_WDTWPROTECT 0x3FF60064UL
#define RTC_WDTCONFIG0   0x3FF4808CUL
#define RTC_WDTWPROTECT  0x3FF480A4UL

/* ---- 共有 src/ に対するこのポートの素性(意味は arch/m68k-mega/include/plat.h の同じ節) ----
 *   PLAT_FLAT32        gcc の 32bit フラットなポートの一族(src/ の分岐はこれを見る)
 *   PLAT_NSLOT         スロット数(slot0 = カーネル + init 込み)。唯一の定義場所
 *   PLAT_SLOT_ADDR(n)  スロット n(1..)のデータ枠の先頭(コード枠は PROC_TEXT_BASE から別にある)
 *   PROC_SLOT_KB       1 スロットの大きさ(KB)= 命令 16 + データ 16。/bin/free の表示用 */
#define PLAT_FLAT32       1
#define PLAT_NSLOT        6
#define PLAT_SLOT_ADDR(n) (PROC_DATA_BASE + ((unsigned long)(n) - 1UL) * PROC_DATA_SIZE)
#define PROC_SLOT_KB      32

/* 割込みの禁止/許可。PS.INTLEVEL を上げ下げする(15 = 全部止める、0 = 全部通す)。
 * rsil は旧値をレジスタへ返す命令なので捨て場の変数を 1 個使う。 */
#define IRQ_OFF()  do { unsigned long _ps; \
                        __asm__ volatile ("rsil %0, 15" : "=r"(_ps) :: "memory"); } while (0)
#define IRQ_ON()   do { unsigned long _ps; \
                        __asm__ volatile ("rsil %0, 0" : "=r"(_ps) :: "memory"); } while (0)
/* 待ちループから自分で譲る(m68k の TRAP #1 と同じ役)。syscall 命令を番号
 * 0xFFFFFFFF で叩く ── 例外入口(crt0.S exc_yield)が tick を進めずに
 * 次の走行可能スロットへ切り替える。 */
#define KYIELD()   do { register unsigned long _f __asm__ ("a2") = 0xFFFFFFFFUL; \
                        __asm__ volatile ("syscall" : "+r"(_f) :: "memory"); } while (0)

/* WiFi(#109、Makefile が WiFi のバイナリを見つけたときだけ PLAT_WIFI)。
 *   src/init.c の PLAT_LATE_INIT(FAT マウント後、sh の前)で /etc/wifi を読んで接続を始め、
 *   slot0 の待ちループ(PLAT_IDLE)で WiFi のスレッドに順番を回す(net/wifi.c)。 */
#ifdef PLAT_WIFI
void plat_late_init(void);
void plat_idle(void);
#define PLAT_LATE_INIT()  plat_late_init()
#define PLAT_IDLE()       plat_idle()
/* TCP クライアント(src/knet.h の syscall 36..41。実体は net/knet.c、lwIP は slot0 が回す) */
#define PLAT_NET 1
#endif

/* ---- 共有 src/ を xtensa-esp-elf-gcc で通すための移植シム(m68k-mega /
 * x86-ia16 の plat.h と同じ役目)。SDCC 専用の呼出規約属性は無意味 → 消す。 */
#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

#endif /* !__ASSEMBLER__ */

#endif
