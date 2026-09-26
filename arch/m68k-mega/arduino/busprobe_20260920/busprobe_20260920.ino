/* ============================================================================
   BUSPROBE.ino  -  Mega を PC(AI)から操作する 68000 バス代行プローブ
   ----------------------------------------------------------------------------
   目的: SRAM デコード回路の検証。CPU の代わりに Mega がバスを握り、
         信号を 1 本ずつ / バスサイクル単位 / 走査テスト単位で操作する。
   接続: シリアル 1000000bps。行単位コマンド、数値はすべて 16 進。
         応答の最後に必ず "> " プロンプトを出す(クライアントはこれで区切る)。
   ピン(8/20 走行実績版と同一):
     D0-7=PORTF  D8-15=PORTK  A1-7=PA1..PA7  A8-15=PORTC
     A16-19=PL0..PL3  A20=PL4 (アドレスの bit20 がそのまま A20)
     /AS=PB6 /UDS=PE4 /LDS=PE5 R/W=PG5 /DTACK=PE3 /RESET=PB4 /HALT=PH6 CLK=PB5
   68000 は /RESET,/HALT=L のまま保持、クロックは出さない。
   ============================================================================ */

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "rom.h"

#define AS_MASK    (1 << PB6)
#define RESET_MASK (1 << PB4)
#define CLK_MASK   (1 << PB5)
#define RW_MASK    (1 << PG5)
#define DTACK_MASK (1 << PE3)
#define UDS_MASK   (1 << PE4)
#define LDS_MASK   (1 << PE5)
#define HALT_MASK  (1 << PH6)
#define A20_MASK   (1 << PL4)

/* --- デコード出力を読み返すモニタ入力(ユーザー配線) ---
     D6=PH3 CE_LOWER  D7=PH4 CE_UPPER  D8=PH5 OE  D14=PJ1 WE_LOWER  D15=PJ0 WE_UPPER */
#define MON_CEL_MASK (1 << PH3)
#define MON_CEU_MASK (1 << PH4)
#define MON_OE_MASK  (1 << PH5)
#define MON_WEL_MASK (1 << PJ1)
#define MON_WEU_MASK (1 << PJ0)

#define LANE_U 2
#define LANE_L 1
#define LANE_B 3

static uint16_t g_settle = 3;   /* ストローブ保持 us (0 = 約250ns) */
static uint16_t g_settle_ms = 0;/* 追加の保持 ms (CPU 非搭載なので大きくしてよい) */
static uint8_t  g_pull   = 1;   /* 開放時のデータバス内部プルアップ */
static uint8_t  g_alt11  = 0;   /* 1 = D11 を PA0(Arduino D22) で代替 */
static uint8_t  g_freerun = 0;  /* 接続後 10 秒で自動開始、キー入力で停止 */
static uint32_t g_pass   = 0;
static uint16_t g_maxerr = 16;  /* テストで個別表示するエラー数 */

/* ---------------------------------------------------------------- 出力 */
static void hx(uint32_t v, uint8_t digits){
  const char *h = "0123456789ABCDEF";
  for(int8_t s = (digits - 1) * 4; s >= 0; s -= 4) Serial.write(h[(v >> s) & 15]);
}
static void bits16(uint16_t v){
  for(int8_t i = 15; i >= 0; i--){ Serial.write((v >> i) & 1 ? '1' : '0'); if(i == 8) Serial.write('_'); }
}
static bool key_abort(){
  if(Serial.available()){ while(Serial.available()) Serial.read(); Serial.println(F("\n[aborted]")); return true; }
  return false;
}

/* ---------------------------------------------------------------- バス原始操作 */
static void data_release(){
  DDRF = 0x00; DDRK = 0x00;
  PORTF = g_pull ? 0xFF : 0x00;
  PORTK = g_pull ? 0xFF : 0x00;
  if(g_alt11){ DDRA &= ~0x01; if(g_pull) PORTA |= 0x01; else PORTA &= ~0x01; }
}
static void data_drive(uint16_t d){
  PORTF = (uint8_t)d; PORTK = (uint8_t)(d >> 8);
  DDRF = 0xFF; DDRK = 0xFF;
  if(g_alt11){ if(d & 0x0800) PORTA |= 0x01; else PORTA &= ~0x01; DDRA |= 0x01; }
}
/* 開放・プルアップ無し(PA0 代替も含む) */
static void data_float0(){
  DDRF = 0; DDRK = 0; PORTF = 0; PORTK = 0;
  if(g_alt11){ DDRA &= ~0x01; PORTA &= ~0x01; }
}
static void set_addr(uint32_t a){
  PORTA = (PORTA & 0x01) | ((uint8_t)a & 0xFE);
  PORTC = (uint8_t)(a >> 8);
  PORTL = (uint8_t)(a >> 16);          /* PL4 = bit20 = A20 */
}
static uint32_t get_addr(){
  return ((uint32_t)PINL << 16) | ((uint32_t)PINC << 8) | (PINA & 0xFE);
}
static uint16_t get_data(){
  uint16_t v = ((uint16_t)PINK << 8) | PINF;
  if(g_alt11){ v &= ~0x0800u; if(PINA & 0x01) v |= 0x0800u; }
  return v;
}
static uint8_t ds_mask(uint8_t lanes){
  return ((lanes & LANE_U) ? UDS_MASK : 0) | ((lanes & LANE_L) ? LDS_MASK : 0);
}
static void wait_settle(){
  if(g_settle_ms) delay(g_settle_ms);
  if(g_settle) delayMicroseconds(g_settle);
  else if(!g_settle_ms) __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t");
}
/* 割り込みを止めたまま待つ(delay() は millis 割り込みに依存するので使えない)。
   delayMicroseconds は純粋なビジーループなので cli 中でも正確。 */
static void wait_ms_noint(uint16_t ms){
  while(ms--) delayMicroseconds(1000);
}

static void bus_idle(){
  PORTB |= AS_MASK;
  PORTE |= UDS_MASK | LDS_MASK;
  PORTG |= RW_MASK;
  data_release();
}

/* 68000 と同じ順序の書き込みサイクル:
   addr → /AS=L → R/W=L → data → /DS=L … /DS=H → /AS=H → R/W=H → data 開放 */
static void cyc_write(uint32_t a, uint16_t d, uint8_t lanes){
  set_addr(a);
  PORTB &= ~AS_MASK;
  PORTG &= ~RW_MASK;
  data_drive(d);
  PORTE &= ~ds_mask(lanes);
  wait_settle();
  PORTE |= UDS_MASK | LDS_MASK;
  PORTB |= AS_MASK;
  PORTG |= RW_MASK;
  data_release();
}
/* 読み込みサイクル: addr → R/W=H → /AS=L → /DS=L → 待ち → 標本 → /DS=H → /AS=H */
static uint16_t cyc_read(uint32_t a, uint8_t lanes){
  PORTG |= RW_MASK;
  data_release();
  set_addr(a);
  PORTB &= ~AS_MASK;
  PORTE &= ~ds_mask(lanes);
  wait_settle();
  uint16_t v = get_data();
  PORTE |= UDS_MASK | LDS_MASK;
  PORTB |= AS_MASK;
  return v;
}
/* 浮きビット検出: プルアップ有りで読む / 0 をプリチャージしてプルアップ無しで読む。
   SRAM が駆動しているビットは両者一致、浮いているビットは 1/0 に割れる。 */
static uint16_t cyc_read_float(uint32_t a, uint8_t lanes, uint16_t *vhi, uint16_t *vlo){
  uint8_t save = g_pull;
  g_pull = 1; *vhi = cyc_read(a, lanes);
  g_pull = 0;
  PORTG |= RW_MASK; set_addr(a);
  data_drive(0x0000); delayMicroseconds(2);   /* /AS=H のまま = 誰も駆動しない前提 */
  data_release();
  PORTB &= ~AS_MASK; PORTE &= ~ds_mask(lanes);
  wait_settle();
  *vlo = get_data();
  PORTE |= UDS_MASK | LDS_MASK; PORTB |= AS_MASK;
  g_pull = save; data_release();
  return *vhi ^ *vlo;
}

/* ---------------------------------------------------------------- 状態表示 */
static void show_pins(){
  Serial.print(F("ADDR=")); hx(get_addr(), 6);
  Serial.print(F(" (A20=")); Serial.print((PINL & A20_MASK) ? 1 : 0);
  Serial.print(F(")  DATA=")); hx(get_data(), 4);
  Serial.print(F(" ")); Serial.print((DDRF | DDRK) ? F("[driven]") : F("[input]"));
  Serial.print(F(" pull=")); Serial.println(g_pull);
  Serial.print(F("/AS=")); Serial.print((PINB & AS_MASK) ? 1 : 0);
  Serial.print(F(" /UDS=")); Serial.print((PINE & UDS_MASK) ? 1 : 0);
  Serial.print(F(" /LDS=")); Serial.print((PINE & LDS_MASK) ? 1 : 0);
  Serial.print(F(" R/W=")); Serial.print((PING & RW_MASK) ? 1 : 0);
  Serial.print(F(" /DTACK=")); Serial.print((PINE & DTACK_MASK) ? 1 : 0);
  Serial.print(F(" /RESET=")); Serial.print((PINB & RESET_MASK) ? 1 : 0);
  Serial.print(F(" /HALT=")); Serial.print((PINH & HALT_MASK) ? 1 : 0);
  Serial.print(F("  settle=")); Serial.print(g_settle); Serial.println(F("us"));
}

static uint32_t num(uint8_t i, uint32_t def);   /* 引数取り出し(定義は後方) */
static uint16_t read_after_pre(uint32_t a, uint8_t lanes, uint16_t pre);  /* 定義は後方 */

/* モニタ入力を 1 バイトに詰める: bit4=CEU bit3=CEL bit2=WEU bit1=WEL bit0=OE */
static uint8_t mon_read(){
  uint8_t h = PINH, j = PINJ, v = 0;
  if(h & MON_CEU_MASK) v |= 0x10;
  if(h & MON_CEL_MASK) v |= 0x08;
  if(j & MON_WEU_MASK) v |= 0x04;
  if(j & MON_WEL_MASK) v |= 0x02;
  if(h & MON_OE_MASK)  v |= 0x01;
  return v;
}
static void mon_print(uint8_t v){
  Serial.print((v >> 4) & 1); Serial.print(' ');
  Serial.print((v >> 3) & 1); Serial.print(' ');
  Serial.print((v >> 2) & 1); Serial.print(' ');
  Serial.print((v >> 1) & 1); Serial.print(' ');
  Serial.print(v & 1);
}
/* 74HC32(OR)としての期待値 */
static uint8_t mon_expect(uint8_t i){
  uint8_t a20 = (i >> 3) & 1, uds = (i >> 2) & 1, lds = (i >> 1) & 1, rw = i & 1;
  uint8_t ceu = a20 | uds, cel = a20 | lds;
  uint8_t v = 0;
  if(ceu) v |= 0x10;
  if(cel) v |= 0x08;
  if(ceu | rw) v |= 0x04;
  if(cel | rw) v |= 0x02;
  if(!rw) v |= 0x01;
  return v;
}
/* 真理値表: 16 通りを出して、デコード出力の実測と期待値を突き合わせる */
static void cmd_truth(){
  uint32_t a = num(1, 0) & 0x0FFFFEUL;
  uint8_t bad = 0;
  data_release();
  Serial.println(F(" i A20 UDS LDS RW | CEU CEL WEU WEL OE | expect            | data"));
  for(uint8_t i = 0; i < 16; i++){
    PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
    set_addr(a | ((i & 8) ? 0x100000UL : 0));
    if(i & 4) PORTE |= UDS_MASK; else PORTE &= ~UDS_MASK;
    if(i & 2) PORTE |= LDS_MASK; else PORTE &= ~LDS_MASK;
    if(i & 1) PORTG |= RW_MASK;  else PORTG &= ~RW_MASK;
    PORTB &= ~AS_MASK;
    delayMicroseconds(g_settle ? g_settle : 1);
    uint8_t m = mon_read(), e = mon_expect(i);
    uint16_t d = get_data();
    PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
    Serial.print(' '); hx(i, 1);
    Serial.print(F("  ")); Serial.print((i >> 3) & 1);
    Serial.print(F("   ")); Serial.print((i >> 2) & 1);
    Serial.print(F("   ")); Serial.print((i >> 1) & 1);
    Serial.print(F("   ")); Serial.print(i & 1);
    Serial.print(F(" |  ")); mon_print(m);
    Serial.print(F("  |  ")); mon_print(e);
    Serial.print(F("  | ")); hx(d, 4);
    if(m != e){
      bad |= m ^ e;
      Serial.print(F("  NG:"));
      if((m ^ e) & 0x10) Serial.print(F(" CEU"));
      if((m ^ e) & 0x08) Serial.print(F(" CEL"));
      if((m ^ e) & 0x04) Serial.print(F(" WEU"));
      if((m ^ e) & 0x02) Serial.print(F(" WEL"));
      if((m ^ e) & 0x01) Serial.print(F(" OE"));
    }
    Serial.println();
  }
  bus_idle();
  Serial.print(F("tt: "));
  if(!bad) Serial.println(F("all outputs match 74HC32(OR) expectation"));
  else { Serial.print(F("mismatch on:"));
    if(bad & 0x10) Serial.print(F(" CEU"));
    if(bad & 0x08) Serial.print(F(" CEL"));
    if(bad & 0x04) Serial.print(F(" WEU"));
    if(bad & 0x02) Serial.print(F(" WEL"));
    if(bad & 0x01) Serial.print(F(" OE"));
    Serial.println();
  }
}

static void help(){
  Serial.println(F("=== BUSPROBE (hex args; lane: b=both u=UDS l=LDS) ==="));
  Serial.println(F("-- raw (state is held until changed) --"));
  Serial.println(F(" p                  show all pins"));
  Serial.println(F(" s <sig> <0|1>      sig: as uds lds rw a20 dtack"));
  Serial.println(F(" a <addr>           put address on A1-A20"));
  Serial.println(F(" d <val> | d z      drive data bus / release"));
  Serial.println(F(" i                  idle (strobes H, R/W=H, data released)"));
  Serial.println(F("-- cycles --"));
  Serial.println(F(" r <addr> [n] [lane]      read n words"));
  Serial.println(F(" w <addr> <val> [lane]    write word"));
  Serial.println(F(" f <from> <to> <val>      fill (to exclusive)"));
  Serial.println(F(" rf <addr> [lane]         float check (driven vs floating bits)"));
  Serial.println(F(" lr <addr> [lane] / lw <addr> <val> [lane]  repeat until key (for LA)"));
  Serial.println(F(" lv [addr]          live per-bit driven monitor, wiggle chip while watching"));
  Serial.println(F(" tb <d0-15|a1-20> [period_ms]  toggle ONE pin square-wave, all else floated/0"));
  Serial.println(F(" tg <sig> [ms]      toggle ONE signal only (L=ms, H=2ms): find what follows it"));
  Serial.println(F(" tt [addr]        truth table of decode outputs (needs D6/D7/D8/D14/D15 wired)"));
  Serial.println(F(" sc [unit_ms] [addr]  coded sweep: step i lasts (i+1)*unit ms"));
  Serial.println(F(" fm [addr]        float matrix: which bits SRAM drives, 16 combos"));
  Serial.println(F(" sw [hold_ms] [addr]  LA sweep: 16 combos of A20,UDS,LDS,RW (AS=L)"));
  Serial.println(F("-- tests (any key aborts) --"));
  Serial.println(F(" ax [n_dec]         address line stress (uses gm mask) | gm <hex> set good-bit mask"));
  Serial.println(F(" bs [n_dec] [addr]  per-bit stress: write 1 and 0 n times each, read back"));
  Serial.println(F(" dt <addr>          data bus walking 1/0"));
  Serial.println(F(" at <from> <to>     address line stuck/short"));
  Serial.println(F(" bt <addr>          byte lane (UDS/LDS write separation)"));
  Serial.println(F(" mt <from> <to>     pattern test (addr-unique, then inverted)"));
  Serial.println(F("-- settings --"));
  Serial.println(F(" t <us> settle | tms <ms> extra settle | pu <0|1> pullup | me <n> max errors"));
}

/* ---------------------------------------------------------------- テスト */
static uint16_t pat_of(uint32_t a){ return (uint16_t)((a >> 1) ^ (a >> 17) ^ 0x5A00); }

static void report_err(uint32_t a, uint16_t w, uint16_t r){
  Serial.print(F("  @")); hx(a, 6);
  Serial.print(F(" W=")); hx(w, 4);
  Serial.print(F(" R=")); hx(r, 4);
  Serial.print(F(" X=")); hx(w ^ r, 4);
  Serial.println();
}

static void test_data(uint32_t a){
  uint16_t bad = 0;
  for(uint8_t pass = 0; pass < 2; pass++){
    for(uint8_t i = 0; i < 16; i++){
      uint16_t w = (uint16_t)1 << i;
      if(pass) w = ~w;
      cyc_write(a, w, LANE_B);
      uint16_t r = cyc_read(a, LANE_B);
      if(r != w){ bad |= w ^ r; report_err(a, w, r); }
    }
  }
  Serial.print(F("dt: bad bits=")); hx(bad, 4); Serial.print(F(" ")); bits16(bad);
  Serial.println(bad ? F(" NG") : F(" OK"));
}

/* Barr 流アドレス線テスト。base と base|2^k(A1..)の組で stuck / short を見る */
static void test_addr(uint32_t from, uint32_t to){
  const uint16_t P = 0xAAAA, Q = 0x5555;
  uint32_t base = from & ~1UL;
  uint8_t kmax = 0;
  for(uint8_t k = 1; k < 24; k++) if((base | (1UL << k)) < to && !(base & (1UL << k))) kmax = k;
  uint32_t stuck = 0, shorted = 0;

  for(uint8_t k = 1; k <= kmax; k++) if(!(base & (1UL << k))) cyc_write(base | (1UL << k), P, LANE_B);
  cyc_write(base, P, LANE_B);

  /* base に Q → 他が化ければ A_k が効いていない(base と同じ所を指す) */
  cyc_write(base, Q, LANE_B);
  for(uint8_t k = 1; k <= kmax; k++){
    if(base & (1UL << k)) continue;
    uint16_t r = cyc_read(base | (1UL << k), LANE_B);
    if(r != P){ stuck |= 1UL << k; Serial.print(F("  A")); Serial.print(k); Serial.print(F(": @")); hx(base | (1UL << k), 6); Serial.print(F(" reads ")); hx(r, 4); Serial.println(F(" after base write (A-line ineffective/stuck-low?)")); }
  }
  cyc_write(base, P, LANE_B);

  /* 各 2^k に Q → base や他の 2^j が化ければ短絡 / stuck-high */
  for(uint8_t k = 1; k <= kmax; k++){
    if(base & (1UL << k)) continue;
    uint32_t ak = base | (1UL << k);
    cyc_write(ak, Q, LANE_B);
    uint16_t r = cyc_read(base, LANE_B);
    if(r != P){ stuck |= 1UL << k; Serial.print(F("  A")); Serial.print(k); Serial.print(F(": base reads ")); hx(r, 4); Serial.println(F(" (A-line stuck-high?)")); }
    for(uint8_t j = 1; j <= kmax; j++){
      if(j == k || (base & (1UL << j))) continue;
      uint32_t aj = base | (1UL << j);
      r = cyc_read(aj, LANE_B);
      if(r != P){ shorted |= (1UL << k) | (1UL << j); Serial.print(F("  A")); Serial.print(k); Serial.print(F("/A")); Serial.print(j); Serial.print(F(": @")); hx(aj, 6); Serial.print(F(" reads ")); hx(r, 4); Serial.println(F(" (short?)")); }
    }
    cyc_write(ak, P, LANE_B);
  }
  Serial.print(F("at: A1..A")); Serial.print(kmax);
  Serial.print(F(" stuck=")); hx(stuck, 6);
  Serial.print(F(" short=")); hx(shorted, 6);
  Serial.println((stuck | shorted) ? F(" NG") : F(" OK"));
}

static void test_byte(uint32_t a){
  struct { uint16_t w; uint8_t lane; uint16_t expect; } seq[] = {
    { 0x1234, LANE_B, 0x1234 },
    { 0xABFF, LANE_U, 0xAB34 },   /* 上位だけ書き換わるはず */
    { 0xFFCD, LANE_L, 0xABCD },   /* 下位だけ書き換わるはず */
    { 0x0000, LANE_B, 0x0000 },
    { 0x5500, LANE_U, 0x5500 },
    { 0x00AA, LANE_L, 0x55AA },
  };
  bool ok = true;
  for(uint8_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++){
    cyc_write(a, seq[i].w, seq[i].lane);
    uint16_t r = cyc_read(a, LANE_B);
    uint16_t ru = cyc_read(a, LANE_U), rl = cyc_read(a, LANE_L);
    Serial.print(F("  write ")); hx(seq[i].w, 4);
    Serial.print(seq[i].lane == LANE_U ? F(" U ") : seq[i].lane == LANE_L ? F(" L ") : F(" B "));
    Serial.print(F("-> read B=")); hx(r, 4);
    Serial.print(F(" U=")); hx(ru, 4);
    Serial.print(F(" L=")); hx(rl, 4);
    Serial.print(F(" expect ")); hx(seq[i].expect, 4);
    if(r != seq[i].expect){ ok = false; Serial.println(F("  NG")); } else Serial.println(F("  ok"));
  }
  Serial.println(ok ? F("bt: OK") : F("bt: NG"));
}

static void test_march(uint32_t from, uint32_t to){
  from &= ~1UL;
  for(uint8_t inv = 0; inv < 2; inv++){
    uint16_t x = inv ? 0xFFFF : 0x0000;
    Serial.print(inv ? F("mt pass2 (inverted) write") : F("mt pass1 write"));
    for(uint32_t a = from; a < to; a += 2){
      cyc_write(a, pat_of(a) ^ x, LANE_B);
      if((a & 0xFFFF) == 0){ Serial.write('.'); if(key_abort()) return; }
    }
    Serial.print(F(" verify"));
    uint32_t nerr = 0; uint16_t orx = 0; uint32_t aor = 0, aand = 0xFFFFFFUL;
    for(uint32_t a = from; a < to; a += 2){
      uint16_t w = pat_of(a) ^ x, r = cyc_read(a, LANE_B);
      if(r != w){
        if(nerr == 0) Serial.println();
        if(nerr < g_maxerr) report_err(a, w, r);
        nerr++; orx |= w ^ r; aor |= a; aand &= a;
      }
      if((a & 0xFFFF) == 0){ Serial.write('.'); if(key_abort()) return; }
    }
    Serial.println();
    Serial.print(F("  errors=")); Serial.print(nerr);
    if(nerr){
      Serial.print(F(" badbits=")); hx(orx, 4); Serial.print(F(" ")); bits16(orx);
      Serial.print(F(" addrOR=")); hx(aor, 6);
      Serial.print(F(" addrAND=")); hx(aand, 6);
    }
    Serial.println();
  }
}

/* 全アドレスの W/R を 1 行ずつ出す詳細マーチ。2 パス(全域書込 -> 全域 verify)なので
   アドレスエイリアスも捕まえられる。書いて即読む方式では原理的に見つからない。 */
static void cmd_march_verbose(){
  uint32_t from = num(1, 0) & ~1UL;
  uint32_t to   = num(2, 0x100);
  uint32_t ok = 0, ng = 0;
  Serial.println(F("=== march verbose: pass1 write all -> pass2 verify (1 line/addr) ==="));
  for(uint32_t a = from; a < to; a += 2){
    cyc_write(a, pat_of(a), LANE_B);
    if((a & 0x1FFE) == 0 && key_abort()){ bus_idle(); return; }
  }
  for(uint32_t a = from; a < to; a += 2){
    uint16_t w = pat_of(a);
    uint16_t r = cyc_read(a, LANE_B);
    Serial.write(0x40); hx(a, 6);
    Serial.print(F(" W=")); hx(w, 4);
    Serial.print(F(" R=")); hx(r, 4);
    if(r == w){ Serial.println(F(" OK")); ok++; }
    else { Serial.print(F(" NG X=")); hx((uint16_t)(w ^ r), 4); Serial.println(); ng++; }
    if((a & 0xFE) == 0 && key_abort()){ bus_idle(); return; }
  }
  Serial.print(F("OK=")); Serial.print(ok);
  Serial.print(F("  NG=")); Serial.println(ng);
  Serial.println(F("=== done ==="));
  bus_idle();
}

/* フリーラン 1 パス: 64KB を全域書込 -> 全域 verify、1 アドレス 1 行。
   パスごとにパターンを変える(^ g_pass)ので毎回違うデータで叩ける。 */
static void freerun_pass(){
  const uint32_t to = 0x10000UL;
  uint32_t ok = 0, ng = 0;
  uint16_t salt = (uint16_t)(++g_pass);
  Serial.print(F("=== pass ")); Serial.print(g_pass);
  Serial.println(F(" : write all -> verify all  (any key to stop) ==="));
  for(uint32_t a = 0; a < to; a += 2){
    cyc_write(a, (uint16_t)(pat_of(a) ^ salt), LANE_B);
    if((a & 0x1FFE) == 0 && Serial.available()) { bus_idle(); return; }
  }
  for(uint32_t a = 0; a < to; a += 2){
    uint16_t w = (uint16_t)(pat_of(a) ^ salt);
    uint16_t r = cyc_read(a, LANE_B);
    Serial.write(0x40); hx(a, 6);
    Serial.print(F(" W=")); hx(w, 4);
    Serial.print(F(" R=")); hx(r, 4);
    if(r == w){ Serial.println(F(" OK")); ok++; }
    else { Serial.print(F(" NG X=")); hx((uint16_t)(w ^ r), 4); Serial.println(); ng++; }
    if((a & 0xFE) == 0 && Serial.available()) { bus_idle(); return; }
  }
  Serial.print(F("--- pass ")); Serial.print(g_pass);
  Serial.print(F(": OK=")); Serial.print(ok);
  Serial.print(F("  NG=")); Serial.println(ng);
  bus_idle();
}

/* ---------------------------------------------------------------- コマンド */
static char  line[64];
static uint8_t llen = 0;
static char *tok[6];
static uint8_t ntok;

static uint32_t num(uint8_t i, uint32_t def){ return (i < ntok) ? strtoul(tok[i], 0, 16) : def; }
static uint8_t lane_arg(uint8_t i){
  if(i >= ntok) return LANE_B;
  if(tok[i][0] == 'u') return LANE_U;
  if(tok[i][0] == 'l') return LANE_L;
  return LANE_B;
}

static void cmd_set(){
  if(ntok < 3){ Serial.println(F("usage: s <as|uds|lds|rw|a20|dtack> <0|1>")); return; }
  bool v = tok[2][0] == '1';
  const char *n = tok[1];
  volatile uint8_t *port; uint8_t m;
  if(!strcmp(n, "as"))         { port = &PORTB; m = AS_MASK; }
  else if(!strcmp(n, "uds"))   { port = &PORTE; m = UDS_MASK; }
  else if(!strcmp(n, "lds"))   { port = &PORTE; m = LDS_MASK; }
  else if(!strcmp(n, "rw"))    { port = &PORTG; m = RW_MASK; }
  else if(!strcmp(n, "a20"))   { port = &PORTL; m = A20_MASK; }
  else if(!strcmp(n, "dtack")) { port = &PORTE; m = DTACK_MASK; }
  else { Serial.println(F("unknown signal")); return; }
  if(v) *port |= m; else *port &= ~m;
  show_pins();
}

static void cmd_read(){
  uint32_t a = num(1, 0) & ~1UL;
  uint32_t n = num(2, 1); if(n == 0) n = 1;
  uint8_t lane = lane_arg(3);
  for(uint32_t i = 0; i < n; i++, a += 2){
    if((i & 7) == 0){ if(i) Serial.println(); hx(a, 6); Serial.print(':'); }
    Serial.write(' '); hx(cyc_read(a, lane), 4);
    if((i & 0xFF) == 0xFF && key_abort()) return;
  }
  Serial.println();
}

static void cmd_loop(bool wr){
  uint32_t a = num(1, 0) & ~1UL;
  uint16_t d = wr ? num(2, 0) : 0;
  uint8_t lane = lane_arg(wr ? 3 : 2);
  Serial.println(F("looping, any key to stop"));
  uint32_t n = 0; uint16_t last = 0;
  while(!Serial.available()){
    if(wr) cyc_write(a, d, lane); else last = cyc_read(a, lane);
    n++;
  }
  while(Serial.available()) Serial.read();
  Serial.print(F("cycles=")); Serial.print(n);
  if(!wr){ Serial.print(F(" last=")); hx(last, 4); }
  Serial.println();
}

/* ロジアナ用スイープ: /AS=L のまま (A20,/UDS,/LDS,R/W) の 16 通りを順に保持。
   1 周の頭で /AS を 50us だけ H にする(トリガ用の目印)。データバスは開放のまま。
   ステップ番号 i: bit3=A20 bit2=/UDS bit1=/LDS bit0=R/W */
static void cmd_sweep(){
  uint16_t hold = num(1, 5);               /* 16進で ms。既定 5ms */
  uint32_t a = num(2, 0) & 0x0FFFFEUL;     /* A20 以外のアドレス */
  Serial.println(F("sweeping, any key to stop"));
  data_release();
  uint32_t n = 0;
  while(!Serial.available()){
    /* 1 周ぶんは割り込みを止めて出す(ジッター排除)。キー検出は周回の切れ目で。 */
    cli();
    PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
    set_addr(a);                            /* 目印の間は A20=0 に戻す */
    wait_ms_noint(hold * 3);                /* 周回の頭の目印 */
    for(uint8_t i = 0; i < 16; i++){
      set_addr(a | ((i & 8) ? 0x100000UL : 0));
      if(i & 4) PORTE |= UDS_MASK; else PORTE &= ~UDS_MASK;
      if(i & 2) PORTE |= LDS_MASK; else PORTE &= ~LDS_MASK;
      if(i & 1) PORTG |= RW_MASK;  else PORTG &= ~RW_MASK;
      PORTB &= ~AS_MASK;
      wait_ms_noint(hold);
    }
    sei();
    n++;
  }
  while(Serial.available()) Serial.read();
  bus_idle();
  Serial.print(F("sweeps=")); Serial.println(n);
}

/* 浮きマトリクス: (A20,/UDS,/LDS,R/W) の 16 通り × /AS=L で、SRAM が駆動している
   ビットを調べる。プリチャージはアイドル(A20=0,/AS=/DS=H,R/W=H: 実測で誰も駆動
   しない)で行い、Mega がデータ線を開放してから組み合わせへ切り替える。 */
static uint16_t fm_probe(uint32_t a, uint8_t i, uint16_t pre){
  PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
  set_addr(a & 0x0FFFFEUL);
  PORTF = 0; PORTK = 0;
  data_drive(pre); delayMicroseconds(5);
  data_float0();
  set_addr((a & 0x0FFFFEUL) | ((i & 8) ? 0x100000UL : 0));
  if(i & 1) PORTG |= RW_MASK;  else PORTG &= ~RW_MASK;
  if(i & 4) PORTE |= UDS_MASK; else PORTE &= ~UDS_MASK;
  if(i & 2) PORTE |= LDS_MASK; else PORTE &= ~LDS_MASK;
  PORTB &= ~AS_MASK;
  delayMicroseconds(g_settle ? g_settle : 1);
  uint16_t v = get_data();
  PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
  set_addr(a & 0x0FFFFEUL);
  return v;
}
/* 幅でステップ番号が読めるスイープ: ステップ i を (i+1)*unit us 保持する。
   ロジアナ上で「幅 / unit - 1 = ステップ番号」。頭の目印は /AS=H を 200us。 */
static void cmd_sweep_coded(){
  uint16_t unit = num(1, 1);               /* 16進 ms。既定 1ms */
  uint32_t a = num(2, 0) & 0x0FFFFEUL;
  Serial.println(F("coded sweep, any key to stop"));
  data_release();
  uint32_t n = 0;
  while(!Serial.available()){
    cli();
    PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
    set_addr(a);                            /* 目印の間は A20=0 に戻す */
    wait_ms_noint(unit * 20);               /* 目印は最長ステップより長く */
    for(uint8_t i = 0; i < 16; i++){
      set_addr(a | ((i & 8) ? 0x100000UL : 0));
      if(i & 4) PORTE |= UDS_MASK; else PORTE &= ~UDS_MASK;
      if(i & 2) PORTE |= LDS_MASK; else PORTE &= ~LDS_MASK;
      if(i & 1) PORTG |= RW_MASK;  else PORTG &= ~RW_MASK;
      PORTB &= ~AS_MASK;
      wait_ms_noint((uint16_t)(i + 1) * unit);
    }
    sei();
    n++;
  }
  while(Serial.available()) Serial.read();
  bus_idle();
  Serial.print(F("sweeps=")); Serial.println(n);
}

/* 1 本だけ振る: 指定信号以外は現状のまま固定し、L を 1*ms、H を 2*ms で繰り返す。
   デューティが 1:2 なので、追従している出力は極性まで判別できる。 */
/* 1 本だけを選んでゆっくり方形波で駆動する(テスタ/LED でのプローブ用)。
   d0-d15: 対象以外のデータ線は完全に Hi-Z にする(SRAM と衝突しないよう、
   呼ぶ前にバスは待機状態にしておく)。a1-a20: 対象以外のアドレス線は 0 固定。 */
/* ライブ監視: 指定アドレスで 0x0000/0xFFFF を交互に書いて即読み、
   16bit 全部の「駆動できているか」を毎回 1 行で出し続ける。
   チップを押したり揺すったりしながら変化を見るためのもの。 */
static void cmd_livemon(){
  uint32_t a = num(1, 0) & ~1UL;
  Serial.println(F("live monitor (D15..D0, O=driven .=float), any key to stop"));
  uint32_t n = 0;
  while(!Serial.available()){
    cyc_write(a, 0x0000, LANE_B);
    uint16_t r_lo = read_after_pre(a, LANE_B, 0xFFFF);   /* 0 のはず。float なら FFFF のまま残る */
    cyc_write(a, 0xFFFF, LANE_B);
    uint16_t r_hi = read_after_pre(a, LANE_B, 0x0000);   /* 1 のはず。float なら 0 のまま残る */
    uint16_t driven = (uint16_t)(~r_lo) & r_hi;
    Serial.print('\r');
    for(int8_t i = 15; i >= 0; i--){ Serial.write((driven >> i) & 1 ? 'O' : '.'); if(i == 8) Serial.write(' '); }
    Serial.print(F("  n=")); Serial.print(n++);
    delay(150);
  }
  while(Serial.available()) Serial.read();
  bus_idle();
  Serial.println();
}

static void cmd_togglebit(){
  if(ntok < 2){ Serial.println(F("usage: tb <d0..d15|a1..a20> [period_ms]")); return; }
  const char *n = tok[1];
  uint16_t period = (ntok > 2) ? strtoul(tok[2], 0, 10) : 1000;
  int idx = atoi(n + 1);
  volatile uint8_t *port; uint8_t mask;

  bus_idle();

  if(n[0] == 'd' && idx >= 0 && idx <= 15){
    data_release();                         /* 全データ線 Hi-Z(プルアップは g_pull 次第) */
    volatile uint8_t *ddr;
    if(idx < 8){ ddr = &DDRF; port = &PORTF; mask = (uint8_t)1 << idx; }
    else       { ddr = &DDRK; port = &PORTK; mask = (uint8_t)1 << (idx - 8); }
    *ddr |= mask;                            /* この 1 本だけ出力に */
    Serial.print(F("toggling D")); Serial.print(idx);
    Serial.print(F(" period=")); Serial.print(period);
    Serial.println(F("ms (other data lines floated), any key to stop"));
    bool st = false;
    while(!Serial.available()){
      st = !st;
      if(st) *port |= mask; else *port &= ~mask;
      delay(period);
    }
    while(Serial.available()) Serial.read();
    *ddr &= ~mask;                           /* 元の Hi-Z に戻す */
    data_release();
  } else if(n[0] == 'a' && idx >= 1 && idx <= 20){
    set_addr(0);                             /* 他のアドレス線は 0 固定 */
    if(idx <= 7)       { port = &PORTA; mask = (uint8_t)1 << idx; }
    else if(idx <= 15)  { port = &PORTC; mask = (uint8_t)1 << (idx - 8); }
    else if(idx <= 19)  { port = &PORTL; mask = (uint8_t)1 << (idx - 16); }
    else                { port = &PORTL; mask = A20_MASK; }
    Serial.print(F("toggling A")); Serial.print(idx);
    Serial.print(F(" period=")); Serial.print(period);
    Serial.println(F("ms (other address lines held 0), any key to stop"));
    bool st = false;
    while(!Serial.available()){
      st = !st;
      if(st) *port |= mask; else *port &= ~mask;
      delay(period);
    }
    while(Serial.available()) Serial.read();
    set_addr(0);
  } else {
    Serial.println(F("bad name (want d0..d15 or a1..a20)"));
    return;
  }
  Serial.println(F("stopped"));
}

static void cmd_toggle(){
  if(ntok < 2){ Serial.println(F("usage: tg <as|uds|lds|rw|a20|d0|addr> [ms]")); return; }
  uint16_t ms = (ntok > 2) ? strtoul(tok[2], 0, 10) : 5;
  const char *n = tok[1];
  volatile uint8_t *port = 0; uint8_t m = 0; uint8_t kind = 0;   /* 0=ctrl 1=addr 2=data */
  if(!strcmp(n, "as"))       { port = &PORTB; m = AS_MASK; }
  else if(!strcmp(n, "uds")) { port = &PORTE; m = UDS_MASK; }
  else if(!strcmp(n, "lds")) { port = &PORTE; m = LDS_MASK; }
  else if(!strcmp(n, "rw"))  { port = &PORTG; m = RW_MASK; }
  else if(!strcmp(n, "a20")) { port = &PORTL; m = A20_MASK; }
  else if(!strcmp(n, "addr")){ kind = 1; }
  else if(!strcmp(n, "d0"))  { kind = 2; }
  else { Serial.println(F("unknown signal")); return; }
  Serial.print(F("toggling ")); Serial.print(n);
  Serial.print(F(" L=")); Serial.print(ms); Serial.print(F("ms H=")); Serial.print(ms * 2);
  Serial.println(F("ms, any key to stop"));
  uint8_t period = 0;
  while(!Serial.available()){
    cli();
    /* 4 周期ごとに /AS を H にして目印を出す(トリガ用。/AS はデコードに関与しない) */
    if((period++ & 3) == 0){
      PORTB |= AS_MASK;
      wait_ms_noint(ms * 3);
      PORTB &= ~AS_MASK;
    }
    if(kind == 1){ PORTA &= 0x01; PORTC = 0x00; }                /* アドレス全部 L */
    else if(kind == 2){ PORTF = 0x00; PORTK = 0x00; DDRF = 0xFF; DDRK = 0xFF; }
    else *port &= ~m;
    wait_ms_noint(ms);
    if(kind == 1){ PORTA |= 0xFE; PORTC = 0xFF; }                /* アドレス全部 H */
    else if(kind == 2){ PORTF = 0xFF; PORTK = 0xFF; }
    else *port |= m;
    wait_ms_noint(ms * 2);
    sei();
  }
  while(Serial.available()) Serial.read();
  if(kind == 1){ PORTA &= 0x01; PORTC = 0x00; }
  else if(kind == 2){ data_release(); }
  Serial.println(F("stopped"));
}

/* 逆値をプリチャージしてから読む(浮いた線はプリチャージ値のまま残る) */
static uint16_t read_after_pre(uint32_t a, uint8_t lanes, uint16_t pre){
  PORTB |= AS_MASK; PORTE |= UDS_MASK | LDS_MASK; PORTG |= RW_MASK;
  set_addr(a);
  data_drive(pre);
  delayMicroseconds(3);
  data_float0();
  PORTB &= ~AS_MASK;
  PORTE &= ~ds_mask(lanes);
  wait_settle();
  uint16_t v = get_data();
  PORTE |= UDS_MASK | LDS_MASK;
  PORTB |= AS_MASK;
  return v;
}

/* ビット毎の連続試験: 各ビットに 1 と 0 を count 回ずつ書き、毎回読み返す。
   読みは逆値プリチャージ付きなので「書いた値が返る」= SRAM が駆動している。 */
static void cmd_bitstress(){
  uint16_t count = (ntok > 1) ? strtoul(tok[1], 0, 10) : 100;
  uint32_t a = (ntok > 2) ? (strtoul(tok[2], 0, 16) & ~1UL) : 0;
  Serial.print(F("bit stress @")); hx(a, 6);
  Serial.print(F("  ")); Serial.print(count); Serial.println(F(" times each, both polarities"));
  Serial.println(F(" bit  write1/pre0  write0/preF  verdict"));
  uint16_t okmask = 0;
  for(uint8_t i = 0; i < 16; i++){
    uint16_t m = (uint16_t)1 << i;
    uint16_t f1 = 0, f0 = 0;
    for(uint16_t k = 0; k < count; k++){
      cyc_write(a, m, LANE_B);
      if(!(read_after_pre(a, LANE_B, 0x0000) & m)) f1++;
      cyc_write(a, (uint16_t)~m, LANE_B);
      if(read_after_pre(a, LANE_B, 0xFFFF) & m) f0++;
    }
    Serial.print(F("  D")); Serial.print(i); if(i < 10) Serial.print(' ');
    Serial.print(F("   ")); Serial.print(count - f1); Serial.print('/'); Serial.print(count);
    Serial.print(F("      ")); Serial.print(count - f0); Serial.print('/'); Serial.print(count);
    if(!f1 && !f0){ okmask |= m; Serial.println(F("      OK")); }
    else Serial.println(F("      NG"));
    if(key_abort()) return;
  }
  bus_idle();
  Serial.print(F("bs: OK bits=")); hx(okmask, 4); Serial.print(F(" ")); bits16(okmask);
  Serial.print(F("  NG bits=")); hx((uint16_t)~okmask, 4); Serial.print(F(" ")); bits16(~okmask);
  Serial.println();
}

/* アドレス線の個別試験: 生きているデータ線(g_mask)だけを見て、アドレス線を
   1 本ずつ立てた番地で書き読みを count 回繰り返す。どの本が不安定かを出す。 */
static uint16_t g_mask = 0x01ED;   /* 健全なデータ線のマスク */

static void cmd_addrstress(){
  uint16_t count = (ntok > 1) ? strtoul(tok[1], 0, 10) : 100;
  Serial.print(F("addr line stress, mask=")); hx(g_mask, 4);
  Serial.print(F("  ")); Serial.print(count); Serial.println(F(" times each"));
  Serial.println(F(" A-line  addr     pass1    pass0   verdict"));
  uint32_t bad = 0;
  for(uint8_t k = 0; k <= 19; k++){
    uint32_t a = (k == 0) ? 0 : (1UL << k);
    /* bit0 は存在しない(ワード境界)。bit1 = A1 から実在する */
    uint16_t f1 = 0, f0 = 0, first = 0xFFFF, last = 0;
    for(uint16_t i = 0; i < count; i++){
      cli();                                   /* 割り込みでサイクルが伸びるのを排除 */
      cyc_write(a, g_mask, LANE_B);
      uint16_t r1 = read_after_pre(a, LANE_B, 0x0000);
      cyc_write(a, (uint16_t)~g_mask, LANE_B);
      uint16_t r0 = read_after_pre(a, LANE_B, 0xFFFF);
      sei();
      if((r1 & g_mask) != g_mask || (r0 & g_mask)){
        if(first == 0xFFFF) first = i;
        last = i;
      }
      if((r1 & g_mask) != g_mask) f1++;
      if(r0 & g_mask) f0++;
    }
    Serial.print(F("  "));
    if(k == 0) Serial.print(F("(base)")); else { Serial.print('A'); Serial.print(k); if(k < 10) Serial.print(' '); Serial.print(F("   ")); }
    Serial.print(F("  ")); hx(a, 6);
    Serial.print(F("   ")); Serial.print(count - f1); Serial.print('/'); Serial.print(count);
    Serial.print(F("   ")); Serial.print(count - f0); Serial.print('/'); Serial.print(count);
    if(f1 || f0){
      bad |= (k ? (1UL << k) : 1);
      Serial.print(F("   NG  fail#")); Serial.print(first); Serial.print('-'); Serial.println(last);
    }
    else Serial.println(F("   OK"));
    if(key_abort()) return;
  }
  bus_idle();
  Serial.print(F("ax: NG address lines mask=")); hx(bad, 6); Serial.println();
}

/* 同一番地を連打して、失敗が起きた時刻(us)を記録する。外乱の周期を見る用。 */
static void cmd_hammer(){
  uint32_t a = num(1, 0) & ~1UL;
  uint16_t count = (ntok > 2) ? strtoul(tok[2], 0, 10) : 2000;
  Serial.print(F("hammer @")); hx(a, 6); Serial.print(' '); Serial.print(count);
  Serial.println(F(" iterations; failure times in us from start"));
  Serial.flush();          /* 送信を出し切ってから始める(自前のノイズを排除) */
  delay(20);
  uint32_t t0 = micros(), prev = 0;
  uint16_t nf = 0;
  for(uint16_t i = 0; i < count; i++){
    cli();
    cyc_write(a, g_mask, LANE_B);
    uint16_t r1 = read_after_pre(a, LANE_B, 0x0000);
    cyc_write(a, (uint16_t)~g_mask, LANE_B);
    uint16_t r0 = read_after_pre(a, LANE_B, 0xFFFF);
    uint32_t t = micros();
    sei();
    if((r1 & g_mask) != g_mask || (r0 & g_mask)){
      nf++;
      if(nf <= 40){
        uint32_t el = t - t0;
        Serial.print(F("  #")); Serial.print(i);
        Serial.print(F(" t=")); Serial.print(el);
        Serial.print(F("us dt=")); Serial.print(prev ? (el - prev) : 0);
        Serial.print(F(" r1=")); hx(r1, 4); Serial.print(F(" r0=")); hx(r0, 4);
        Serial.println();
        prev = el;
      }
    }
  }
  bus_idle();
  Serial.print(F("hm: failures=")); Serial.print(nf);
  Serial.print(F(" / ")); Serial.print(count);
  Serial.print(F("  total=")); Serial.print(micros() - t0); Serial.println(F("us"));
}

static void cmd_fmatrix(){
  uint32_t a = num(1, 0);
  Serial.println(F(" i A20 UDS LDS RW  pre0 preF  driven(0=float)"));
  for(uint8_t i = 0; i < 16; i++){
    uint16_t v0 = fm_probe(a, i, 0x0000);
    uint16_t v1 = fm_probe(a, i, 0xFFFF);
    uint16_t drv = ~(v0 ^ v1);
    Serial.print(F(" ")); hx(i, 1);
    Serial.print(F("  ")); Serial.print((i >> 3) & 1);
    Serial.print(F("   ")); Serial.print((i >> 2) & 1);
    Serial.print(F("   ")); Serial.print((i >> 1) & 1);
    Serial.print(F("   ")); Serial.print(i & 1);
    Serial.print(F("  ")); hx(v0, 4); Serial.print(F(" ")); hx(v1, 4);
    Serial.print(F("  ")); bits16(drv); Serial.println();
  }
  bus_idle();
}

static void exec(){
  ntok = 0;
  for(char *p = strtok(line, " \t"); p && ntok < 6; p = strtok(0, " \t")) tok[ntok++] = p;
  if(ntok == 0) return;
  const char *c = tok[0];

  if(!strcmp(c, "h") || !strcmp(c, "?")) help();
  else if(!strcmp(c, "p")) show_pins();
  else if(!strcmp(c, "s")) cmd_set();
  else if(!strcmp(c, "a")) { set_addr(num(1, 0)); show_pins(); }
  else if(!strcmp(c, "d")) {
    if(ntok > 1 && tok[1][0] == 'z') data_release(); else data_drive(num(1, 0));
    show_pins();
  }
  else if(!strcmp(c, "i")) { bus_idle(); show_pins(); }
  else if(!strcmp(c, "r")) cmd_read();
  else if(!strcmp(c, "w")) { cyc_write(num(1, 0) & ~1UL, num(2, 0), lane_arg(3)); Serial.println(F("ok")); }
  else if(!strcmp(c, "f")) {
    uint32_t a = num(1, 0) & ~1UL, e = num(2, 0); uint16_t d = num(3, 0);
    for(; a < e; a += 2){ cyc_write(a, d, LANE_B); if((a & 0xFFFF) == 0 && key_abort()) return; }
    Serial.println(F("ok"));
  }
  else if(!strcmp(c, "rf")) {
    uint16_t hi, lo;
    uint16_t fl = cyc_read_float(num(1, 0) & ~1UL, lane_arg(2), &hi, &lo);
    Serial.print(F("pullup=")); hx(hi, 4);
    Serial.print(F(" precharge0=")); hx(lo, 4);
    Serial.print(F(" floating=")); hx(fl, 4); Serial.print(F(" ")); bits16(fl);
    Serial.println();
  }
  else if(!strcmp(c, "lr")) cmd_loop(false);
  else if(!strcmp(c, "lw")) cmd_loop(true);
  else if(!strcmp(c, "sw")) cmd_sweep();
  else if(!strcmp(c, "fm")) cmd_fmatrix();
  else if(!strcmp(c, "sc")) cmd_sweep_coded();
  else if(!strcmp(c, "tg")) cmd_toggle();
  else if(!strcmp(c, "tb")) cmd_togglebit();
  else if(!strcmp(c, "lv")) cmd_livemon();
  else if(!strcmp(c, "bs")) cmd_bitstress();
  else if(!strcmp(c, "ax")) cmd_addrstress();
  else if(!strcmp(c, "hm")) cmd_hammer();
  else if(!strcmp(c, "gm")) { g_mask = num(1, 0x01ED); Serial.print(F("mask=")); hx(g_mask, 4); Serial.println(); }
  else if(!strcmp(c, "dt")) test_data(num(1, 0) & ~1UL);
  else if(!strcmp(c, "at")) test_addr(num(1, 0), num(2, 0x100000));
  else if(!strcmp(c, "bt")) test_byte(num(1, 0) & ~1UL);
  else if(!strcmp(c, "mt")) test_march(num(1, 0), num(2, 0x100000));
  else if(!strcmp(c, "mv")) cmd_march_verbose();
  else if(!strcmp(c, "tt")) cmd_truth();
  else if(!strcmp(c, "t"))  { g_settle = strtoul(ntok > 1 ? tok[1] : "3", 0, 10); Serial.print(F("settle=")); Serial.print(g_settle); Serial.println(F("us")); }
  else if(!strcmp(c, "tms")){ g_settle_ms = strtoul(ntok > 1 ? tok[1] : "0", 0, 10); Serial.print(F("settle_ms=")); Serial.println(g_settle_ms); }
  else if(!strcmp(c, "pu")) { g_pull = num(1, 1) ? 1 : 0; data_release(); Serial.print(F("pullup=")); Serial.println(g_pull); }
  else if(!strcmp(c, "me")) { g_maxerr = strtoul(ntok > 1 ? tok[1] : "16", 0, 10); Serial.print(F("maxerr=")); Serial.println(g_maxerr); }
  else if(!strcmp(c, "alt")) { g_alt11 = num(1, 0) ? 1 : 0; data_release(); Serial.print(F("alt D11->PA0(D22)=")); Serial.println(g_alt11); }
  else if(!strcmp(c, "xt")) {
    bus_idle();
    uint8_t sa = g_alt11; g_alt11 = 0;
    DDRF = 0; DDRK = 0; PORTF = 0; PORTK = 0;
    DDRA &= ~0x01; PORTA &= ~0x01;
    DDRK |= 0x08;
    PORTK |= 0x08; delayMicroseconds(200); uint8_t h1 = PINA & 1;
    PORTK &= ~0x08; delayMicroseconds(200); uint8_t l1 = PINA & 1;
    PORTK |= 0x08; delayMicroseconds(200); uint8_t h2 = PINA & 1;
    PORTK &= ~0x08; delayMicroseconds(200); uint8_t l2 = PINA & 1;
    DDRK &= ~0x08; PORTK = 0;
    Serial.print(F("PK3=1 -> PA0=")); Serial.print(h1); Serial.print(h2);
    Serial.print(F("   PK3=0 -> PA0=")); Serial.print(l1); Serial.print(l2);
    Serial.println((h1 && h2 && !l1 && !l2) ? F("   xt: CONNECTED") : F("   xt: NOT connected"));
    g_alt11 = sa; bus_idle();
  }
  else if(!strcmp(c, "rl")) {
    uint16_t n = (uint16_t)sizeof(rom_1);
    uint32_t base = ROM_LOAD_ADDR;
    Serial.print(F("rom_1 = ")); Serial.print(n); Serial.print(F(" bytes -> @"));
    hx(base, 6); Serial.println();
    for(uint16_t i = 0; i + 1 < n; i += 2){
      uint16_t w = ((uint16_t)pgm_read_byte(&rom_1[i]) << 8) | pgm_read_byte(&rom_1[i+1]);
      cyc_write(base + i, w, LANE_B);
    }
    uint16_t nerr = 0;
    for(uint16_t i = 0; i + 1 < n; i += 2){
      uint16_t w = ((uint16_t)pgm_read_byte(&rom_1[i]) << 8) | pgm_read_byte(&rom_1[i+1]);
      uint16_t r = cyc_read(base + i, LANE_B);
      if(r != w){ if(nerr < g_maxerr) report_err(base + i, w, r); nerr++; }
    }
    Serial.print(F("rl: errors=")); Serial.println(nerr);
    bus_idle();
  }
  else Serial.println(F("? (h for help)"));
}

void setup(){
  Serial.begin(1000000);

  /* 68000 は停止・バス開放状態に保持。クロックは出さない */
  DDRE |= DTACK_MASK; PORTE |= DTACK_MASK;
  DDRB |= RESET_MASK; PORTB &= ~RESET_MASK;
  DDRH |= HALT_MASK;  PORTH &= ~HALT_MASK;
  DDRB |= CLK_MASK;   PORTB &= ~CLK_MASK;

  /* Mega がバスマスタ */
  DDRA = 0xFF; DDRC = 0xFF; DDRL = 0xFF;
  PORTA = 0; PORTC = 0; PORTL = 0;
  DDRB |= AS_MASK; DDRE |= UDS_MASK | LDS_MASK; DDRG |= RW_MASK;
  bus_idle();

  /* デコード出力のモニタ入力(プルアップ無し) */
  DDRH &= ~(MON_CEL_MASK | MON_CEU_MASK | MON_OE_MASK);
  PORTH &= ~(MON_CEL_MASK | MON_CEU_MASK | MON_OE_MASK);
  DDRJ &= ~(MON_WEL_MASK | MON_WEU_MASK);
  PORTJ &= ~(MON_WEL_MASK | MON_WEU_MASK);

  delay(100);
  Serial.println();
  Serial.println(F("BUSPROBE ready (h for help)"));
  Serial.println(F("free-run march starts in 10s -- send any key for a command prompt"));
  {
    uint8_t stop = 0;
    for(int8_t i = 10; i > 0; i--){
      if(Serial.available()){ while(Serial.available()) Serial.read(); stop = 1; break; }
      Serial.print(i); Serial.write(0x20);
      delay(1000);
    }
    Serial.println();
    g_freerun = stop ? 0 : 1;
  }
  Serial.print(F("> "));
}

void loop(){
  if(g_freerun){
    freerun_pass();
    if(Serial.available()){
      while(Serial.available()) Serial.read();
      g_freerun = 0;
      Serial.println(F("[free-run stopped]"));
      Serial.print(F("> "));
    }
    return;
  }
  while(Serial.available()){
    char ch = Serial.read();
    if(ch == '\r' || ch == '\n'){
      if(llen == 0) continue;
      line[llen] = 0; Serial.println();
      exec();
      llen = 0;
      Serial.print(F("> "));
    } else if(ch == 8 || ch == 127){
      if(llen){ llen--; Serial.print(F("\b \b")); }
    } else if(ch >= 0x20 && ch < 0x7F && llen < sizeof(line) - 1){   /* ポート open 時の NUL 等は捨てる */
      line[llen++] = ch; Serial.write(ch);
    }
  }
}
