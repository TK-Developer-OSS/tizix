/* ============================================================================
   MEGA2560_68000_DTACK_TEST.ino  --  TIZIX HOST (M1: SD カード無し)
   ----------------------------------------------------------------------------
   tizix arch/m68k-mega のカーネル(rom.h = kernel.bin)を SRAM へ転送し、
   68000 を走らせ、走行中は /DTACK と A20=1 空間の MMIO を代行する。

   系譜と変更点:
     ・バスサイクル(cyc_write/cyc_read 相当) … BUSPROBE から移植。
       ★旧 走行版(bk/..._stepd_4mhz_stable.ino_bk)は /UDS,/LDS を駆動して
       いなかった。現在のデコードは CE# = A20 | DS なので、DS を出さないと
       SRAM が一度も選択されない(ユーザーの tizix68k_memtest.ino が動かな
       かったのと同一原因)。ここが今回の本質的な修正。
     ・走行中の loop … 旧 走行版の構造を踏襲。ただし
         (a) /DTACK はパルスで返し、その後 /AS が戻るまでデータを保持する
             (保持方式に変えたら取りこぼした。dtack_pulse() のコメント参照)
         (b) A1-A4 をデコードして UART と SD レジスタを区別(下記)
         (c) 走行中のデバッグ print を全廃(シリアルは 68000 のコンソール)
         (d) /HALT 監視(ダブルバスフォルトの可視化)

   MMIO(A20=1 がデバイス空間。上位 1 本で装置を選ぶ):
     A23=1 UART  0x900000 偶数/UDS = DATA  (W:送信 / R:受信1バイト)
                 0x900001 奇数/LDS = STATUS(bit0=TXRDY bit1=RXRDY)
     A22=1 SD    0x500000 から plat.h の SD_* 6 本
     A21=1       (将来 ESP32 用に予約)
   各装置内は A1-A3 + バイトレーンでレジスタを選ぶ。それ以外のアドレス
   ビットは見ていないのでエイリアスする。SD の CS は D53(Mega が駆動)。

   シリアルは 115200 bps(TeraTerm が 1Mbps で化けたため 2026-09-21 に変更)。
   転送ログの後はそのまま tizix のコンソールになる。
   ============================================================================ */

#include <Arduino.h>
#include "rom.h"

/* 1 にするとレベル6(25Hz、tizix #85)タイマ割込みを出す。tizix の tick/プリエンプション
   に必要。IACK 経路を切り分けたいときだけ 0 にする。
   ★周期はカーネルの TICK_HZ(arch/m68k-mega/Makefile、既定 25)と一致させること。
   経緯: 1Hz → 100Hz → 25Hz。実機で 100/50/25/12/6Hz を測り、応答が一番速かった 25Hz にした
   (10ms ごとの割込み処理が遅いバス代行の上では重い。tizix task.md #85)。 */
#define ENABLE_TIMER_IRQ 1

/* 1 にすると転送と診断だけ行い、68000 を解放しない(診断出力を CPU の
   コンソール出力と混ぜたくないとき) */
#define HOLD_CPU 0

/* バスサイクル・トレース。TRACE_BOOT=1 でリセット直後の 64 サイクルを吐く。
   TRACE_REPEAT はその後 2 秒おきに何回取り直すか(暴走位置の特定用)。
   通常運用は両方 0 ── コンソール出力に混ざるので。 */
#define TRACE_BOOT   0
#define TRACE_REPEAT 0

/* 1 にするとバスサイクル数を数えて 4 秒ごとに [perf ...] を吐く。
   速度を数字で見るための計測用。済んだら 0 に戻す(コンソールに混ざる)。 */
#define TRACE_PERF   0

/* ---- ピン(README 2. の対応表 / BUSPROBE と同一) ---------------------- */
#define AS_MASK    (1 << PB6)   /* D12 /AS      */
#define CLK_MASK   (1 << PB5)   /* D11 CLK(OC1A) */
#define RESET_MASK (1 << PB4)   /* D10 /RESET   */
#define HALT_MASK  (1 << PH6)   /* D9  /HALT    */
#define RW_MASK    (1 << PG5)   /* D4  R/W      */
#define DTACK_MASK (1 << PE3)   /* D5  /DTACK   */
#define UDS_MASK   (1 << PE4)   /* D2  /UDS     */
#define LDS_MASK   (1 << PE5)   /* D3  /LDS     */
#define A20_MASK   (1 << PL4)   /* D45 A20 : 1 = デバイス空間      */
#define A21_MASK   (1 << PL5)   /* D44 A21 : (将来 ESP32 用に予約) */
#define A22_MASK   (1 << PL6)   /* D43 A22 : 1 = SD カード         */
#define A23_MASK   (1 << PL7)   /* D42 A23 : 1 = UART              */
/* D0-7=PORTF  D8-15=PORTK  A1-7=PA1..PA7  A8-15=PORTC  A16-19=PL0..PL3 */

#define FC0_MASK  (1 << PH0)    /* D17 */
#define FC1_MASK  (1 << PD3)    /* D18 */
#define FC2_MASK  (1 << PD2)    /* D19 */
#define IPL0_MASK (1 << PG0)    /* D41 */
#define IPL1_MASK (1 << PG1)    /* D40 */
#define IPL2_MASK (1 << PG2)    /* D39 */
#define VPA_MASK  (1 << PJ0)    /* D15 (基板側にプルアップ有り) */

#define IS_IACK()  ((PINH & FC0_MASK) && (PIND & FC1_MASK) && (PIND & FC2_MASK))
#define IPL_NONE() do{ PORTG |=  (IPL0_MASK|IPL1_MASK|IPL2_MASK); }while(0)
#define IPL_LV6()  do{ PORTG = (PORTG | IPL0_MASK) & ~(IPL1_MASK|IPL2_MASK); }while(0)
#define VPA_NEGATE() (PORTJ |= VPA_MASK)

#define CLK_OCR1A  1            /* 16MHz/(2*(1+1)) = 4MHz */
#define VEC_LEVEL6 30           /* ベクタ番号(crt0.s の irq6_handler) */

/* #85: 未処理の tick 数(以前は 0/1 のフラグ)。68000 が割込みを受け付ける(IACK)前に
   次の周期が来ると、フラグでは 1 回分が消えてカーネルの時計が遅れる。1Hz では起きなかったが、
   100Hz だと SD を読む syscall(割込み禁止)の間に数周期たまる。IACK ごとに 1 つ減らす。 */
volatile uint8_t g_irq_req = 0;
static uint8_t   g_ipl_on = 0;           /* IPL6 を出している(IACK 待ち) */
static uint8_t   g_halt_seen = 0;
static uint16_t  g_lateas = 0;  /* /AS の H 窓を取りこぼした回数 */
static uint16_t  g_stuck = 0;   /* /AS が戻らなかった回数 */

#if TRACE_PERF
static uint16_t  g_cyc16  = 0;  /* ホットパス用の 16bit カウンタ */
static uint32_t  g_cyc    = 0;
static uint32_t  g_perf_t = 0;
#define PERF_TICK() (g_cyc16++)
#else
#define PERF_TICK() do{}while(0)
#endif

static void hx(uint32_t v, uint8_t digits){
  const char *h = "0123456789ABCDEF";
  for(int8_t s = (digits - 1) * 4; s >= 0; s -= 4) Serial.write(h[(v >> s) & 15]);
}

/* ======================================================================
   フェーズA: Mega がバスを握って SRAM へ転送する
   ====================================================================== */
static void bus_set_addr(uint32_t a){
  PORTA = (PORTA & 0x01) | ((uint8_t)a & 0xFE);
  PORTC = (uint8_t)(a >> 8);
  PORTL = (uint8_t)(a >> 16);           /* PL4 = A20 */
}

/* 68000 と同じ順序: addr -> /AS=L -> R/W=L -> data -> /DS=L ... -> 解放
   ★/UDS,/LDS を必ず駆動する(CE#=A20|DS, WE#=CE#|R/W のため) */
static void sram_write16(uint32_t a, uint16_t d){
  bus_set_addr(a);
  PORTB &= ~AS_MASK;
  PORTG &= ~RW_MASK;
  PORTF = (uint8_t)d;
  PORTK = (uint8_t)(d >> 8);
  DDRF = 0xFF; DDRK = 0xFF;
  PORTE &= ~(UDS_MASK | LDS_MASK);
  delayMicroseconds(2);                 /* WE# 幅。55ns SRAM に十分な余裕 */
  PORTE |= (UDS_MASK | LDS_MASK);
  PORTB |= AS_MASK;
  PORTG |= RW_MASK;
  DDRF = 0x00; PORTF = 0xFF;
  DDRK = 0x00; PORTK = 0xFF;
}

/* lanes: bit1=/UDS(上位, 偶数バイト) bit0=/LDS(下位, 奇数バイト) */
static uint16_t sram_read_lane(uint32_t a, uint8_t lanes){
  uint8_t ds = (uint8_t)(((lanes & 2) ? UDS_MASK : 0) | ((lanes & 1) ? LDS_MASK : 0));
  PORTG |= RW_MASK;
  DDRF = 0x00; PORTF = 0xFF;
  DDRK = 0x00; PORTK = 0xFF;
  bus_set_addr(a);
  PORTB &= ~AS_MASK;
  PORTE &= ~ds;
  delayMicroseconds(3);                 /* 整定待ち(BUSPROBE の既定値と同じ) */
  uint16_t v = ((uint16_t)PINK << 8) | (uint16_t)PINF;
  PORTE |= (UDS_MASK | LDS_MASK);
  PORTB |= AS_MASK;
  return v;
}

static uint16_t sram_read16(uint32_t a){ return sram_read_lane(a, 3); }

/* 68000 のバイト書き込み: 偶数番地 = /UDS = D8-15、奇数番地 = /LDS = D0-7 */
static void sram_write8(uint32_t a, uint8_t v){
  uint8_t odd = (uint8_t)(a & 1);
  bus_set_addr(a & ~1UL);
  PORTB &= ~AS_MASK;
  PORTG &= ~RW_MASK;
  PORTF = v; PORTK = v;                 /* 使われる側のレーンだけ有効 */
  DDRF = 0xFF; DDRK = 0xFF;
  PORTE &= ~(odd ? LDS_MASK : UDS_MASK);
  delayMicroseconds(2);
  PORTE |= (UDS_MASK | LDS_MASK);
  PORTB |= AS_MASK;
  PORTG |= RW_MASK;
  DDRF = 0x00; PORTF = 0xFF;
  DDRK = 0x00; PORTK = 0xFF;
}

static bool load_image(void){
  uint16_t n = (uint16_t)sizeof(rom_1);
  uint32_t ok = 0, ng = 0;

  Serial.print(F("image = ")); Serial.print(n);
  Serial.print(F(" bytes -> 0x")); hx(ROM_LOAD_ADDR, 6); Serial.println();
  Serial.println(F("SRAM transfer start"));

  for(uint16_t i = 0; i < n; i += 2){
    uint8_t  hi = pgm_read_byte(&rom_1[i]);
    uint8_t  lo = ((uint16_t)(i + 1) < n) ? pgm_read_byte(&rom_1[i + 1]) : 0x00;
    uint16_t d  = ((uint16_t)hi << 8) | lo;      /* 68000 はビッグエンディアン */
    uint32_t a  = ROM_LOAD_ADDR + i;
    sram_write16(a, d);
    uint16_t r = sram_read16(a);
    if(r == d) ok++;
    else {
      ng++;
      if(ng <= 16){
        Serial.print(F("  @")); hx(a, 6);
        Serial.print(F(" W=")); hx(d, 4);
        Serial.print(F(" R=")); hx(r, 4);
        Serial.print(F(" X=")); hx((uint16_t)(d ^ r), 4);
        Serial.println(F(" NG"));
      }
    }
  }
  Serial.print(F("  verify OK=")); Serial.print(ok);
  Serial.print(F(" NG=")); Serial.println(ng);
  return ng == 0;
}

/* ---- バイトレーン検証 --------------------------------------------------
   ワード(両 DS)での verify が通っても、68000 の実行時アクセスの大半は
   バイト(片 DS)なので、片レーンだけ壊れていると「転送 OK なのに動かない」
   になる。CE# = A20|DS のデコードを片側ずつ踏む形で確かめる。
   ここは Mega がバスマスタなので、CPU 側の不具合と切り分けられる。 */
static void verify_lanes(void){
  uint16_t n = (uint16_t)sizeof(rom_1);
  uint32_t ngu = 0, ngl = 0;
  uint8_t  shown = 0;

  Serial.println(F("byte-lane verify (UDS / LDS separately)"));
  for(uint16_t i = 0; i < n; i += 2){
    uint32_t a  = ROM_LOAD_ADDR + i;
    uint8_t  eu = pgm_read_byte(&rom_1[i]);                       /* 偶数 = 上位 */
    uint8_t  el = ((uint16_t)(i + 1) < n) ? pgm_read_byte(&rom_1[i + 1]) : 0x00;
    uint8_t  gu = (uint8_t)(sram_read_lane(a, 2) >> 8);           /* UDS のみ */
    uint8_t  gl = (uint8_t)(sram_read_lane(a, 1) & 0xFF);         /* LDS のみ */
    if(gu != eu){ ngu++; if(shown < 8){ shown++;
      Serial.print(F("  UDS @")); hx(a, 6);
      Serial.print(F(" exp=")); hx(eu, 2); Serial.print(F(" got=")); hx(gu, 2); Serial.println(); } }
    if(gl != el){ ngl++; if(shown < 8){ shown++;
      Serial.print(F("  LDS @")); hx(a + 1, 6);
      Serial.print(F(" exp=")); hx(el, 2); Serial.print(F(" got=")); hx(gl, 2); Serial.println(); } }
  }
  Serial.print(F("  UDS NG=")); Serial.print(ngu);
  Serial.print(F("  LDS NG=")); Serial.println(ngl);
}

/* バイト書き込みが効くか(crt0 の BSS クリアは clr.b = バイト書き込み) */
static void test_byte_write(void){
  const uint32_t a = 0x00080000UL;      /* カーネル像から離れた空き番地 */
  Serial.println(F("byte-write test @080000"));
  sram_write16(a, 0x0000);
  sram_write8(a,     0x5A);             /* 偶数 = UDS = D8-15 */
  uint16_t r1 = sram_read16(a);
  sram_write8(a + 1, 0xA5);             /* 奇数 = LDS = D0-7  */
  uint16_t r2 = sram_read16(a);
  Serial.print(F("  after even write = ")); hx(r1, 4);
  Serial.println(r1 == 0x5A00 ? F("  (expect 5A00) OK") : F("  (expect 5A00) NG"));
  Serial.print(F("  after odd  write = ")); hx(r2, 4);
  Serial.println(r2 == 0x5AA5 ? F("  (expect 5AA5) OK") : F("  (expect 5AA5) NG"));
}

static void dump_one(uint32_t a, const __FlashStringHelper *name){
  uint32_t w = ((uint32_t)sram_read16(a) << 16) | sram_read16(a + 2);
  Serial.print(F("  ")); Serial.print(name);
  Serial.print(F(" = ")); hx(w, 8); Serial.println();
}

static void dump_vectors(void){
  dump_one(0x000000UL, F("SSP  "));
  dump_one(0x000004UL, F("PC   "));
  dump_one(0x000078UL, F("vec30"));   /* level6 = irq6_handler */
  dump_one(0x000080UL, F("vec32"));   /* TRAP #0 = trap0_handler */
}

/* ======================================================================
   フェーズB: クロック起動 -> バス開放 -> CPU 解除
   ====================================================================== */
static void clock_start(void){
  DDRB |= CLK_MASK;
  TCCR1A = (1 << COM1A0);               /* OC1A トグル */
  TCCR1B = (1 << WGM12) | (1 << CS10);  /* CTC, 分周なし */
  OCR1A  = CLK_OCR1A;
}

static void clock_stop(void){
  TCCR1A = 0; TCCR1B = 0;
  DDRB |= CLK_MASK; PORTB &= ~CLK_MASK; /* 停止時は L で固定 */
}

/* ★リセット中に一瞬クロックを与えて 68000 にバスを手放させる。
   これが無いと「前のファームで走行中の 68000 を載せたまま Mega だけ
   リセット/再書込」したとき、クロックが止まった 68000 がバスサイクルの
   途中でデータバスを掴んだままになり、Mega の SRAM 書込と衝突して
   転送が全滅する(読み返しが全部 0000 になる)。実際に踏んだ。 */
static void cpu_force_tristate(void){
  clock_start();
  delay(20);                            /* 4MHz × 20ms = 8万クロック */
  clock_stop();
  delay(1);
}

static void release_bus(void){
  PORTB |= AS_MASK;
  PORTG |= RW_MASK;
  PORTE |= (UDS_MASK | LDS_MASK);
  DDRA = 0x00; PORTA = 0x00;            /* A1-7  入力(プルアップ無し) */
  DDRC = 0x00; PORTC = 0x00;            /* A8-15 */
  DDRL = 0x00; PORTL = 0x00;            /* A16-20 */
  DDRF = 0x00; PORTF = 0x00;            /* D0-7  */
  DDRK = 0x00; PORTK = 0x00;            /* D8-15 */
  DDRB &= ~AS_MASK;  PORTB |= AS_MASK;  /* /AS 入力 + プルアップ */
  DDRG &= ~RW_MASK;  PORTG |= RW_MASK;  /* R/W 入力 + プルアップ */
  DDRE &= ~(UDS_MASK | LDS_MASK);       /* /UDS,/LDS 入力 */
  PORTE |= (UDS_MASK | LDS_MASK);       /*   + プルアップ */
  DDRH &= ~FC0_MASK; DDRD &= ~(FC1_MASK | FC2_MASK);   /* FC0-2 入力 */
}

/* ======================================================================
   フェーズC: 走行中の代行
   ====================================================================== */
/* ---- /AS を 125ns 周期で見張る -----------------------------------------
   ★地雷 5(「/AS の H を待つと自己維持ループになる」)への対処がここ。
   破綻した原因は 2 つあって、どちらも潰せる:
     (a) ポーリング周期。C で書くと dec/brne が付いて 1 周 ~310ns になり、
         サイクル間の /AS=H (1 クロック = 250ns) を跨いでしまう。
         → 8 回展開して 1 標本 2 サイクル = 125ns にする。窓の中で 2 回取れる。
     (b) 割り込み。Serial の ISR が 1 回入れば窓は確実に飛ぶ。
         → dtack_cycle() が cli したまま呼ぶ。
   この 2 つを潰すと /AS の観測は完全に信頼できるようになり(実測で
   取りこぼし 0 回/秒)、固定時間待ちが要らなくなる。
   返り値 0 = タイムアウト(サイクルが終わらなかった)。 */
#define AS_SETTLE 2             /* 2*8 標本 = 32 サイクル = 約 2us で打ち切り */

static inline __attribute__((always_inline)) uint8_t as_wait_high(uint8_t g){
  __asm__ __volatile__(
    "1: \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "sbic %2,%3 \n\t rjmp 2f \n\t"
    "dec %0 \n\t"
    "brne 1b \n\t"
    "2: \n\t"
    : "=d"(g) : "0"(g), "I"(_SFR_IO_ADDR(PINB)), "I"(PB6));
  return g;
}

static inline __attribute__((always_inline)) uint8_t as_wait_low(uint8_t g){
  __asm__ __volatile__(
    "1: \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "sbis %2,%3 \n\t rjmp 2f \n\t"
    "dec %0 \n\t"
    "brne 1b \n\t"
    "2: \n\t"
    : "=d"(g) : "0"(g), "I"(_SFR_IO_ADDR(PINB)), "I"(PB6));
  return g;
}

/* ★/DTACK は 68000 本来のハンドシェイクで返す。
   アサート → /AS が H に戻る(= サイクル終了)のを見る → ネゲート。

   ★地雷 4 は「これをやると次のサイクルが残った /DTACK で 0 ウェイト完走
   してしまう」と書いてあるが、それは監視ループが遅かったからである。
   /AS の H を 125ns 周期・割り込み禁止で見ていれば、H を観測してから
   ネゲートするまでは 3 命令(~190ns)。次のサイクルが /AS を下げるのは
   H になってから 250ns 後(S0,S1)、その /DTACK 標本点はさらに 375ns 後
   なので、60ns 以上の余裕を残して確実に落ちている。

   ★固定幅パルス(旧実装の 8 nop = 500ns)をやめた理由は速度ではなく正確さ。
   パルス方式だと「パルスを出している 500ns の間にサイクルが終わり、
   次のサイクルまで始まってしまう」ことが MMIO で頻繁に起き(実測で
   MMIO サイクルの 15000 回/4 秒)、/AS の H を一度も観測できないまま
   タイムアウトしていた。ハンドシェイクにすると取りこぼしは 0 になる。

   ★cli で囲む理由(地雷 6): 割り込みが入るとネゲートが遅れて上の 60ns の
   余裕を食い潰す。禁止区間は通常 600-800ns、最悪 AS_SETTLE = 2us。 */
static inline __attribute__((always_inline)) void dtack_cycle(void){
  cli();
  PORTE &= ~DTACK_MASK;                 /* /DTACK アサート */
  if(!as_wait_high(AS_SETTLE)) g_lateas++;
  PORTE |=  DTACK_MASK;                 /* サイクル終了を見てからネゲート */
  sei();
}

/* /DTACK を握ったまま /AS が H に戻るのを待つ(68000 の正規ハンドシェイク)。
   固定幅パルスだと「読みデータを CPU がラッチする前に手を離す」危険と
   「1 サイクルを 2 回サービスする」危険がある。上限付きで空回りを防ぐ。 */
/* 上限は短く。長くすると「バスが固まった」状態で Mega が延々と空回りし、
   定期処理(診断出力)にすら到達しなくなる。実際に踏んだ。 */
#define AS_GUARD 3000          /* 約 0.7ms @16MHz */

static uint8_t g_stuck_shown = 0;

static void report_stuck(const __FlashStringHelper *where){
  if(g_stuck_shown >= 6) return;
  g_stuck_shown++;
  uint32_t a = ((uint32_t)PINL << 16) | ((uint32_t)PINC << 8) | (uint32_t)(PINA & 0xFE);
  Serial.print(F("\r\n[STUCK ")); Serial.print(where);
  Serial.print(F(" A=")); hx(a, 6);
  Serial.print(F(" AS=")); Serial.write((PINB & AS_MASK) ? '1' : '0');
  Serial.print(F(" UDS=")); Serial.write((PINE & UDS_MASK) ? '1' : '0');
  Serial.print(F(" LDS=")); Serial.write((PINE & LDS_MASK) ? '1' : '0');
  Serial.print(F(" RW="));  Serial.write((PING & RW_MASK) ? '1' : '0');
  Serial.print(F(" HALT=")); Serial.write((PINH & HALT_MASK) ? '1' : '0');
  Serial.print(F(" FC="));
  Serial.write((PINH & FC0_MASK) ? '1' : '0');
  Serial.write((PIND & FC1_MASK) ? '1' : '0');
  Serial.write((PIND & FC2_MASK) ? '1' : '0');
  Serial.print(F(" n=")); Serial.print(g_stuck);
  Serial.println(F("]"));
}


/* /UDS か /LDS が出るまで待って PINE を返す。
   ★68000 は /AS を S2 で、データストローブを S4 で出す(書きサイクルでは
   データが確定してから)。/AS を見た瞬間に PINE を読むと両方まだ H なので
   「偶数(UDS)アクセス」に誤判定し、UART の DATA と STATUS が入れ替わる。
   書きデータも DS 確定前だと化ける。必ずここで待ってから判定する。 */
static inline uint8_t wait_ds(void){
  uint16_t guard = 0;
  uint8_t  pe;
  for(;;){
    pe = PINE;
    if((pe & (UDS_MASK | LDS_MASK)) != (UDS_MASK | LDS_MASK)) return pe;
    if(++guard >= AS_GUARD) { g_stuck++; report_stuck(F("DS")); return pe; }
  }
}

static inline uint8_t uart_status(void){
  uint8_t st = 0;
  if(Serial.availableForWrite() > 0) st |= 0x01;   /* TXRDY */
  if(Serial.available() > 0)         st |= 0x02;   /* RXRDY */
  return st;
}

/* ======================================================================
   SD カード(M2): Mega が SPI で生セクタを読み書きし、68000 へ中継する
   ----------------------------------------------------------------------
   役割分担は z80pack/z80board と同じ。FAT の解釈は 68000 側の FatFs が行い、
   Mega は「LBA を渡されたら 512B を出す/受け取る」だけの物理層に徹する。

   配線(ハードウェア SPI):
     SCK=D52(PB1)  MOSI=D51(PB2)  MISO=D50(PB3)  CS=D53(PB0)
   ★CS に 68000 の A21-A23(D44/D43/D42)を使ってはいけない。あれは CPU の
     出力に直結しており、Mega から駆動すると衝突する。D53/D38/D22 が空き。

   68000 から見たレジスタ(plat.h SD_*、0x100010 から 6 バイト):
     +0 LBA_HI  +1 LBA_MID  +2 LBA_LO  +3 CMD(1=read 2=write)
     +4 STATUS(bit0=ERROR bit1=DATA_RDY)  +5 DATA
   diskio.c の手順は「CMD を書く → DATA_RDY を待って DATA を 512 回 →
   最後に STATUS の ERROR を見る」。そこで Mega 側は
     ・CMD=1: その場で 1 セクタ読んで 512B バッファに貯め、以後 DATA_RDY を
       立てっぱなしにしてバッファから出す
     ・CMD=2: バッファに 512B 受け取り切った時点で実際に書く
   とする。SPI 転送中は 68000 が /DTACK 待ちで止まるだけなので問題ない。

   ★カードが無い/初期化に失敗した場合も、DATA_RDY は立てて 512B を
     吐かせ(中身は 0)、ERROR を立てる。**黙ってしまうと diskio.c の
     「DATA_RDY 待ち」が無限ループになり起動時に無言ハングする。**
   ====================================================================== */
/* ★CS をどちらで作るか。ハンダの都合で決めてよい。
     1 = 実機のインバータ(A22 の反転)が CS。配線追加なし。ただし
         (a) A22 にプルダウンが要る(リセット中/転送中はバスが Hi-Z)
         (b) 1 トランザクション = 1 バスサイクル厳守
         (c) 初期化の 74 クロックは CS=H でないと打てないので、Mega の
             起動時に流し、CMD0 以降は最初の SD アクセス時に遅延実行する
     0 = Mega の D53(PB0)が CS。配線 1 本増えるが上の制約が全部消える。 */
#define SD_CS_FROM_A22 0

#define SD_CS_MASK   (1 << PB0)      /* D53。SD_CS_FROM_A22=0 のときだけ使う */
#if SD_CS_FROM_A22
#define SD_CS_LOW()  do{}while(0)
#define SD_CS_HIGH() do{}while(0)
#else
#define SD_CS_LOW()  (PORTB &= ~SD_CS_MASK)
#define SD_CS_HIGH() (PORTB |=  SD_CS_MASK)
#endif

static uint8_t  sd_ok   = 0;         /* 初期化済み */
static uint8_t  sd_hc   = 0;         /* 1 = SDHC/SDXC(ブロックアドレス) */
static uint8_t  sd_buf[512];
static uint16_t sd_pos  = 0;
static uint8_t  sd_mode = 0;         /* 0=idle 1=read 2=write */
static uint8_t  sd_err  = 0;
static uint32_t sd_lba  = 0;
static uint32_t sd_retry_at = 0;

static uint8_t spi_xfer(uint8_t d){
  SPDR = d;
  while(!(SPSR & (1 << SPIF))) {}
  return SPDR;
}

/* slow: 初期化中は 400kHz 以下でないといけない(fosc/128 = 125kHz)。
   通常は fosc/4 = 4MHz。手配線なので fosc/2 まで上げずに余裕を取る。 */
static void spi_setup(uint8_t slow){
  DDRB |= SD_CS_MASK | (1 << PB1) | (1 << PB2);   /* CS, SCK, MOSI = 出力 */
  DDRB &= ~(1 << PB3);                            /* MISO = 入力 */
  PORTB |= (1 << PB3);                            /* MISO プルアップ */
  SD_CS_HIGH();
  SPSR = 0;
  SPCR = slow ? ((1 << SPE) | (1 << MSTR) | (1 << SPR1) | (1 << SPR0))
              : ((1 << SPE) | (1 << MSTR));
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc){
  uint8_t r, n = 12;
  spi_xfer(0xFF);
  spi_xfer(0x40 | cmd);
  spi_xfer((uint8_t)(arg >> 24));
  spi_xfer((uint8_t)(arg >> 16));
  spi_xfer((uint8_t)(arg >> 8));
  spi_xfer((uint8_t)arg);
  spi_xfer(crc);
  do { r = spi_xfer(0xFF); } while((r & 0x80) && --n);
  return r;
}

/* ★CS=H のまま 74 クロック以上打つ、という SPI モード移行の儀式。
   SD_CS_FROM_A22=1 のときは Mega が CS を上げられないので、**CPU がまだ
   リセット中でアドレスバスが Hi-Z(=A22 はプルダウンで 0、CS=H)のうちに**
   ここだけ済ませておく。CMD0 以降は CS=L が要るので、最初の SD アクセスの
   バスサイクルの中で行う(そのとき A22=1 なので CS=L になっている)。 */
static void sd_idle_clocks(void){
  uint8_t i;
  spi_setup(1);
  SD_CS_HIGH();
  for(i = 0; i < 12; i++) spi_xfer(0xFF);
}

static uint8_t sd_init(void){
  uint8_t  r = 0xFF, v2 = 0;
  uint16_t i;
  uint32_t t0, ocr;

  sd_ok = 0; sd_hc = 0;
  spi_setup(1);
  SD_CS_LOW();

  for(i = 0; i < 64; i++){ r = sd_cmd(0, 0, 0x95); if(r == 0x01) break; }
  if(r != 0x01){ SD_CS_HIGH(); return 0; }       /* CMD0: idle にならない */

  r = sd_cmd(8, 0x000001AAUL, 0x87);             /* CMD8: v2 判定 */
  if(r == 0x01){
    ocr = 0;
    for(i = 0; i < 4; i++) ocr = (ocr << 8) | spi_xfer(0xFF);
    if((ocr & 0xFFF) != 0x1AA){ SD_CS_HIGH(); return 0; }
    v2 = 1;
  }

  t0 = millis();
  do {
    sd_cmd(55, 0, 0x65);
    r = sd_cmd(41, v2 ? 0x40000000UL : 0UL, 0x77);
    if(millis() - t0 > 2000){ SD_CS_HIGH(); return 0; }
  } while(r != 0x00);

  if(v2 && sd_cmd(58, 0, 0xFD) == 0){            /* CMD58: CCS で HC 判定 */
    ocr = 0;
    for(i = 0; i < 4; i++) ocr = (ocr << 8) | spi_xfer(0xFF);
    sd_hc = (ocr & 0x40000000UL) ? 1 : 0;
  }
  if(!sd_hc) sd_cmd(16, 512, 0xFF);              /* SDSC はブロック長を明示 */

  SD_CS_HIGH(); spi_xfer(0xFF);
  spi_setup(0);
  sd_ok = 1;
  return 1;
}

/* ★SDSC はバイトアドレス、SDHC 以降はブロックアドレス。ここを間違えると
   「最初のセクタだけ読める」ような紛らわしい壊れ方をする。 */
static uint8_t sd_read_block(uint32_t lba, uint8_t *buf){
  uint32_t a = sd_hc ? lba : (lba << 9);
  uint32_t t0;
  uint16_t i;
  uint8_t  tok;

  SD_CS_LOW();
  if(sd_cmd(17, a, 0xFF) != 0){ SD_CS_HIGH(); return 0; }
  t0 = millis();
  do {
    tok = spi_xfer(0xFF);
    if(millis() - t0 > 300){ SD_CS_HIGH(); return 0; }
  } while(tok == 0xFF);
  if(tok != 0xFE){ SD_CS_HIGH(); return 0; }
  for(i = 0; i < 512; i++) buf[i] = spi_xfer(0xFF);
  spi_xfer(0xFF); spi_xfer(0xFF);                /* CRC は読み捨て */
  SD_CS_HIGH(); spi_xfer(0xFF);
  return 1;
}

static uint8_t sd_write_block(uint32_t lba, const uint8_t *buf){
  uint32_t a = sd_hc ? lba : (lba << 9);
  uint32_t t0;
  uint16_t i;
  uint8_t  r;

  SD_CS_LOW();
  if(sd_cmd(24, a, 0xFF) != 0){ SD_CS_HIGH(); return 0; }
  spi_xfer(0xFF);
  spi_xfer(0xFE);                                /* データトークン */
  for(i = 0; i < 512; i++) spi_xfer(buf[i]);
  spi_xfer(0xFF); spi_xfer(0xFF);                /* ダミー CRC */
  r = spi_xfer(0xFF);
  if((r & 0x1F) != 0x05){ SD_CS_HIGH(); return 0; }   /* data accepted */
  t0 = millis();
  while(spi_xfer(0xFF) == 0x00){                 /* ビジー(L)が明けるまで */
    if(millis() - t0 > 500){ SD_CS_HIGH(); return 0; }
  }
  SD_CS_HIGH(); spi_xfer(0xFF);
  return 1;
}

/* idx は SDCARD_MMIO_BASE からのバイトオフセット(0=LBA_HI 3=CMD 4=STATUS 5=DATA) */
static uint8_t sd_reg_read(uint8_t idx){
  uint8_t v;
  if(idx == 4)                                   /* STATUS */
    return (uint8_t)((sd_mode ? 0x02 : 0x00) | (sd_err ? 0x01 : 0x00));
  if(idx == 5){                                  /* DATA */
    v = (sd_pos < 512) ? sd_buf[sd_pos] : 0x00;
    if(++sd_pos >= 512) sd_mode = 0;             /* 512B 出し切ったら idle */
    return v;
  }
  return 0x00;
}

static void sd_reg_write(uint8_t idx, uint8_t v){
  switch(idx){
    case 0: sd_lba = (sd_lba & 0x0000FFFFUL) | ((uint32_t)v << 16); break;
    case 1: sd_lba = (sd_lba & 0x00FF00FFUL) | ((uint32_t)v << 8);  break;
    case 2: sd_lba = (sd_lba & 0x00FFFF00UL) | (uint32_t)v;         break;
    case 3:                                      /* CMD = トリガ */
      sd_err = 0; sd_pos = 0;
      if(!sd_ok && (millis() - sd_retry_at) > 2000){
        sd_retry_at = millis();
        sd_init();                               /* 後からカードを挿した場合 */
      }
      if(v == 1){                                /* read */
        sd_mode = 1;
        if(!sd_ok || !sd_read_block(sd_lba, sd_buf)){
          memset(sd_buf, 0, sizeof(sd_buf));
          sd_err = 1;                            /* 512B 吐かせてから ERROR */
        }
      } else if(v == 2){                         /* write */
        sd_mode = 2;
        if(!sd_ok) sd_err = 1;
      } else {
        sd_mode = 0;
      }
      break;
    case 5:                                      /* DATA(書き込み) */
      if(sd_pos < 512) sd_buf[sd_pos] = v;
      if(++sd_pos >= 512){
        if(sd_ok && !sd_write_block(sd_lba, sd_buf)) sd_err = 1;
        sd_mode = 0;
      }
      break;
    default: break;
  }
}

/* 起動時の確認: カードを初期化し、セクタ0 を読んで正体を報告する。
   FatFs は FF_MULTI_PARTITION=0 なので、セクタ0 が FAT の VBR そのもの
   (=イメージを raw 書き込みした状態)であることを期待している。 */
static void sd_probe(void){
  sd_idle_clocks();                 /* CS=H で 74 クロック以上(両モード共通) */
#if SD_CS_FROM_A22
  /* CS を Mega が下げられないので、ここでは CMD0 を打てない。
     最初の SD アクセス(A22=1 のバスサイクル)まで初期化を遅延する。 */
  Serial.println(F("SD: 74clk done, CMD0 deferred to first access (CS=/A22)"));
  return;
#else
  Serial.print(F("SD init ... "));
  if(!sd_init()){
    Serial.println(F("NO CARD -- FAT will fail, shell still boots"));
    return;
  }
  Serial.print(sd_hc ? F("OK (SDHC/blk addr)") : F("OK (SDSC/byte addr)"));
  if(!sd_read_block(0, sd_buf)){
    Serial.println(F("  but sector0 read FAILED"));
    return;
  }
  Serial.print(F("  sec0:"));
  for(uint8_t i = 0; i < 12; i++){ Serial.write(' '); hx(sd_buf[i], 2); }
  Serial.println();
  if(sd_buf[510] == 0x55 && sd_buf[511] == 0xAA){
    /* VBR なら先頭が JMP(EB/E9)。MBR なら 0x1BE からのパーティション表 */
    if(sd_buf[0] == 0xEB || sd_buf[0] == 0xE9)
      Serial.println(F("  -> looks like a FAT VBR (correct: raw image)"));
    else
      Serial.println(F("  -> looks like an MBR. rewrite disk.img as raw"));
  } else {
    Serial.println(F("  -> no 55AA. unformatted or bad write"));
  }
#endif
}

/* ---- 起動直後のバスサイクル・トレース ----------------------------------
   リセット解除後の最初の TRACE_N サイクルを丸ごと記録して吐く。
   ベクタフェッチ(0x000000/0x000004)から最初の命令フェッチまでが見えるので、
   「CPU が本当に 0x400 から実行しているか」「どのデータを読んでいるか」を
   推測ではなく観測できる。読みサイクルは SRAM が駆動している最中に
   データバスを標本化しているので、フェッチした語がそのまま見える。 */
#define TRACE_N 64
static uint8_t  tr_a[TRACE_N][3];    /* PINA, PINC, PINL */
static uint8_t  tr_f[TRACE_N];       /* b0=LDS b1=UDS b2=READ b3=IACK */
static uint8_t  tr_dl[TRACE_N], tr_dh[TRACE_N];
static uint8_t  tr_n = 0;
static uint8_t  tr_done = TRACE_BOOT ? 0 : 1;

static inline void trace_cycle(uint8_t pe, uint8_t iack){
  if(tr_done) return;
  tr_a[tr_n][0] = PINA; tr_a[tr_n][1] = PINC; tr_a[tr_n][2] = PINL;
  uint8_t f = 0;
  if(!(pe & LDS_MASK)) f |= 1;
  if(!(pe & UDS_MASK)) f |= 2;
  if(PING & RW_MASK)   f |= 4;
  if(iack)             f |= 8;
  tr_f[tr_n]  = f;
  tr_dl[tr_n] = PINF;
  tr_dh[tr_n] = PINK;
  tr_n++;
}

/* 1 回吐いた後も何度か再武装する。CPU が「どこで回っているか」を
   時間を空けて覗くため(起動が進んでいるのか止まっているのかが分かる)。 */

static uint8_t  tr_left = TRACE_REPEAT;
static uint32_t tr_when = 0;

static void trace_dump(void){
  tr_done = 1;
  tr_when = millis();
  Serial.println(F("\r\n---- bus trace (first cycles after reset) ----"));
  for(uint8_t i = 0; i < TRACE_N; i++){
    uint32_t a = ((uint32_t)tr_a[i][2] << 16) | ((uint32_t)tr_a[i][1] << 8)
               | (uint32_t)(tr_a[i][0] & 0xFE);
    Serial.print(F("  "));
    if(i < 10) Serial.write(' ');
    Serial.print(i); Serial.print(F(" A="));
    hx(a, 6);
    Serial.print((tr_f[i] & 4) ? F(" R ") : F(" W "));
    Serial.write((tr_f[i] & 2) ? 'U' : '-');
    Serial.write((tr_f[i] & 1) ? 'L' : '-');
    if(tr_f[i] & 8) Serial.print(F(" IACK"));
    Serial.print(F(" D="));
    hx(((uint16_t)tr_dh[i] << 8) | tr_dl[i], 4);
    Serial.println();
  }
  Serial.println(F("---- end of trace ----"));
}

ISR(TIMER5_COMPA_vect){ if(g_irq_req < 255) g_irq_req++; }

static void timer_irq_start(void){
  DDRG |= (IPL0_MASK | IPL1_MASK | IPL2_MASK);
  IPL_NONE();
  DDRJ |= VPA_MASK; VPA_NEGATE();      /* ベクタード応答なので /VPA は使わない */
#if ENABLE_TIMER_IRQ
  cli();
  TCCR5A = 0;
  TCCR5B = (1 << WGM52) | (1 << CS51) | (1 << CS50);  /* CTC, 64分周 */
  OCR5A  = 9999;                                      /* 16MHz/64/10000 = 25Hz(#85。100Hz は 2499、旧 1024分周・15624 = 1Hz) */
  TCNT5  = 0;
  TIMSK5 = (1 << OCIE5A);
  g_irq_req = 0;
  g_ipl_on = 0;
  sei();
#endif
}

void setup(){
  Serial.begin(115200);
  while(!Serial);
  delay(200);
  for(uint8_t i = 0; i < 8; i++) Serial.write('\n');
  Serial.write(27); Serial.print(F("[2J"));
  Serial.write(27); Serial.print(F("[H"));
  Serial.println(F("=== tizix 68000 host (Mega2560) ==="));

  /* CPU を止めたままバスを占有(クロックはまだ出さない) */
  DDRE |= DTACK_MASK;  PORTE |= DTACK_MASK;     /* /DTACK=H */
  DDRB |= RESET_MASK;  PORTB &= ~RESET_MASK;    /* /RESET=L */
  DDRH |= HALT_MASK;   PORTH &= ~HALT_MASK;     /* /HALT=L  */
  delay(300);
  cpu_force_tristate();                         /* バスを確実に明け渡させる */

  DDRA |= 0xFE; DDRC = 0xFF; DDRL = 0xFF;
  DDRB |= AS_MASK;  PORTB |= AS_MASK;
  DDRG |= RW_MASK;  PORTG |= RW_MASK;
  DDRE |= (UDS_MASK | LDS_MASK); PORTE |= (UDS_MASK | LDS_MASK);
  DDRF = 0x00; PORTF = 0xFF;
  DDRK = 0x00; PORTK = 0xFF;
  PORTA &= 0x01; PORTC = 0; PORTL = 0;
  delay(10);

  if(!load_image()){
    Serial.println(F("TRANSFER FAILED -- CPU held in reset."));
    Serial.println(F("(suspect the SRAM board? flash BUSPROBE and run mt / fm / rl)"));
    while(1);
  }
  verify_lanes();
  test_byte_write();
  sd_probe();
  dump_vectors();

#if HOLD_CPU
  Serial.println(F("HOLD_CPU=1 : diagnostics only, CPU not released."));
  while(1);
#endif

  clock_start();
  delay(1);
  release_bus();
  delay(10);
  timer_irq_start();

  Serial.println(F("---- release CPU ----"));
  Serial.flush();
  DDRH &= ~HALT_MASK; PORTH |= HALT_MASK;   /* /HALT 解放 */
  delayMicroseconds(100);
  PORTB |= RESET_MASK;                      /* /RESET 解除 */
#if TRACE_PERF
  g_perf_t = millis();
#endif
}


static void housekeeping(void){
#if TRACE_PERF
  g_cyc += g_cyc16; g_cyc16 = 0;
  {
    uint32_t now = millis();
    uint32_t dt  = now - g_perf_t;
    if(dt >= 4000){
      g_perf_t = now;
      Serial.print(F("\r\n[perf cyc=")); Serial.print(g_cyc);
      Serial.print(F(" ms="));   Serial.print(dt);
      Serial.print(F(" cps="));  Serial.print(g_cyc / (dt / 1000UL));
      Serial.print(F(" stuck="));Serial.print(g_stuck);
      Serial.print(F(" late=")); Serial.print(g_lateas);
      Serial.println(F("]"));
      g_cyc = 0;
      g_lateas = 0;
    }
  }
#endif
  if(!(PINH & HALT_MASK) && !g_halt_seen){
    g_halt_seen = 1;
    Serial.print(F("\r\n*** CPU /HALT asserted (double bus fault?) ***\r\n"));
  }
  if(tr_done && tr_left && (millis() - tr_when) > 2000){
    tr_left--; tr_n = 0; tr_done = 0;       /* トレース再武装 */
    Serial.print(F("\r\n[re-arm trace, stuck=")); Serial.print(g_stuck);
    Serial.println(F("]"));
  }
}

/* ★定期処理は「バスがアイドルのとき」だけでは回せない。Mega の代行は
   68000 より遅いので、CPU が走っている間は次のサイクルが常に始まっており、
   /AS がネゲートされている瞬間をほぼ観測できない(アイドル分岐に置いたら
   一度も実行されなかった)。そこで
     ・サービスしたバスサイクル SVC_N 回ごと … CPU が走っているとき
     ・/AS が長く来なかったとき             … CPU が止まっているとき
   の両方から呼ぶ。前者は dec/brne の 2 サイクルしか食わない。 */
#define SVC_N 64
static uint8_t g_svc = SVC_N;

static void periodic(void){
  if(g_irq_req && !g_ipl_on){ IPL_LV6(); g_ipl_on = 1; }   /* 減らすのは IACK 側 */
  housekeeping();
}

/* ★Arduino の main() は loop() から戻るたびに serialEventRun() を呼ぶ。
   この関数はスケッチが serialEvent() を定義していなければ何もしないのだが、
   4 本ぶんの NULL 比較が残っていて call/ret 込み 30 サイクル(1.9us)ある。
   1 バスサイクル 8us のうちの 1.9us がこれだった。自前の for(;;) で回す。 */
void loop(){
  for(;;){
    /* --- /AS = L を待つ。来なければアイドルとして定期処理へ --- */
    if(!as_wait_low(200)){                  /* 200*8 標本 = 約 400us */
      periodic();
      continue;
    }

    if(!(PINL & A20_MASK)){
      /* --- SRAM 空間: Mega は /DTACK を返すだけ(圧倒的多数) ---
         ★IACK 判定をここより前に置いてはいけない…わけではないが、置くと
         毎サイクル FC 線を 3 ポート読むぶん(5-8 サイクル)損をする。
         IACK は A4-A23 が全部 H なので必ず A20=1 側に落ちる。 */
#if TRACE_BOOT
      if(!tr_done){
        uint8_t pe = wait_ds();
        delayMicroseconds(1);               /* SRAM の出力が出揃うまで */
        trace_cycle(pe, 0);
      }
#endif
      dtack_cycle();

    } else if(IS_IACK()){
      /* --- 割り込みアクノリッジ --- */
      IPL_NONE();
      g_ipl_on = 0;
      { uint8_t s = SREG; cli(); if(g_irq_req) g_irq_req--; SREG = s; }  /* 残りがあれば periodic() がまた IPL6 を出す。
                                                                            割込み許可の状態は元に戻す(ここが cli 区間の中でも壊さない) */
      PORTF = VEC_LEVEL6; PORTK = 0x00;
      DDRF = 0xFF; DDRK = 0xFF;
#if TRACE_BOOT
      if(!tr_done) trace_cycle(PINE, 1);
#endif
      dtack_cycle();
      DDRF = 0x00; PORTF = 0xFF;
      DDRK = 0x00; PORTK = 0xFF;

    } else {
      /* --- MMIO 空間(A20=1) --- */
      uint8_t  pe   = wait_ds();            /* データストローブ確定まで待つ */
      uint8_t  lds  = !(pe & LDS_MASK);     /* 奇数バイト = D0-7 */
      uint8_t  hiad = PINL;                 /* b4=A20 b5=A21 b6=A22 b7=A23 */
      /* 装置内レジスタ番号 = A1-A3 とバイトレーンから。plat.h のバイト
         オフセットに一致する(SD なら 0=LBA_HI 3=CMD 4=STATUS 5=DATA)。 */
      uint8_t  idx  = (uint8_t)((((PINA >> 1) & 0x07) << 1) | (lds ? 1 : 0));

      if(PING & RW_MASK){                   /* 読み */
        uint8_t lo = 0, hi = 0;
        if(hiad & A22_MASK){                /* SD カード(0x500000-) */
          uint8_t v = sd_reg_read(idx);
          if(lds) lo = v; else hi = v;
        } else if(hiad & A23_MASK){         /* UART(0x900000-) */
          if(lds) lo = uart_status();
          else    hi = (Serial.available() > 0) ? (uint8_t)Serial.read() : 0x00;
        }                                   /* それ以外は 0 を返して DTACK */
        PORTF = lo; PORTK = hi;
        DDRF = 0xFF; DDRK = 0xFF;
#if TRACE_BOOT
        if(!tr_done) trace_cycle(pe, 0);
#endif
        dtack_cycle();
        DDRF = 0x00; PORTF = 0xFF;
        DDRK = 0x00; PORTK = 0xFF;
      } else {                              /* 書き */
        uint8_t v = lds ? PINF : PINK;
#if TRACE_BOOT
        if(!tr_done) trace_cycle(pe, 0);
#endif
        if(hiad & A22_MASK)                  sd_reg_write(idx, v);
        else if((hiad & A23_MASK) && !lds)   Serial.write(v);  /* UART DATA */
        dtack_cycle();
      }
    }

    if(--g_svc == 0){
      g_svc = SVC_N;
#if TRACE_PERF
      g_cyc16 += SVC_N;                     /* 数えるのはここ。ホットパスは無料 */
#endif
      periodic();
    }
#if TRACE_BOOT
    if(!tr_done && tr_n >= TRACE_N) trace_dump();
#endif
  }
}
