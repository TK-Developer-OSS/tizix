;===================================================================
; espspi.s - ESP-WROOM-02 (WiFi) 用 bit-bang SPI 物理層
;
;   SD カードの spi.s と同じ手口・同じビット割り当てで、ポートだけが違う
;   2 本目の SPI チャネル(ESP_IO_PORT)。SD とはラッチ/バッファが別なので
;   バス共有の排他は要らない。
;
;   ★ここは **ユーザーコマンド(net.bin)側** にリンクされる。カーネルでは
;     ない(z80board の ROM は残り数十バイトしか無い)。したがって
;     lstr.s / lstd.s と同じ「手書き PIC」の掟が全部かかる:
;       ・分岐は jr / djnz だけ。**jp <label> / call <label> は禁止**
;         (絶対番地を埋め込むので base≠0 で暴走する。= モジュール内の
;          サブルーチン呼び出しも作れない。転送ループはインライン)
;       ・ld hl,#<label> 等のデータ参照を持たない(ポートとレジスタだけ)
;       ・IY はプロセス base(触らない)、IX は呼び出し側の物(触らない)
;   ABI (--sdcccall 0): 引数は右→左で push。0(sp)=戻り番地、2(sp)=arg1 lo、
;   3(sp)=arg1 hi。戻り値は HL。引数は呼び出し側が捨てる。
;
;   ポート値(spi.s と同一):
;     OUT bit7 = MOSI / bit6 = CS(1 = 非選択)/ bit5 = SCK / bit0-4 = 1 固定
;     IN  bit7 = MISO
;   SPI モード 0、MSB ファースト。SCK Low + MOSI 確定 → SCK High で
;   スレーブが取り込む → MISO を読む。
;===================================================================
        .module espspi

        .globl  _esp_cs
        .globl  _esp_rst
        .globl  _esp_xfer

; ---- hw.h の ESP_PORT と一致させること(sdasz80 は C ヘッダを読めない) ----
ESP_IO_PORT     = 0x81

; CS Low(選択中)のアイドル値: MOSI=1, CS=0, SCK=0, bit0-4=1
ESP_IDLE_SEL    = 0x9F
; CS High(非選択)のアイドル値: MOSI=1, CS=1, SCK=0, bit0-4=1
ESP_IDLE_DESEL  = 0xDF
; CS Low + ~RST Low(bit0 = 0): ESP をリセット保持
ESP_RESET_ON    = 0x9E

        .area   _CODE

;-------------------------------------------------------------
; void esp_cs(int on)   on != 0 → CS Low(トランザクション開始)
;                       on == 0 → CS High(終了)
;   SCK は常に Low の状態で CS を動かす(モード 0 の作法)。
;-------------------------------------------------------------
_esp_cs::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        or      (hl)             ; A = lo | hi (0 なら非選択)
        jr      z, 1$
        ld      a, #ESP_IDLE_SEL
        out     (ESP_IO_PORT), a
        ret
1$:
        ld      a, #ESP_IDLE_DESEL
        out     (ESP_IO_PORT), a
        ret

;-------------------------------------------------------------
; void esp_rst(int on)  on != 0 → ~RST Low(リセット保持)
;                       on == 0 → ~RST High(解除)
;   どちらも **CS Low** で出す: ESP8266 の HSPI スレーブ CS は GPIO15 固定で、
;   GPIO15 は起動時に Low でなければ ESP が起動しない(espat.h 参照)。
;   リセット解除の瞬間に CS = Low にしておけば必ず正しく立ち上がる。
;-------------------------------------------------------------
_esp_rst::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        or      (hl)
        jr      z, 2$
        ld      a, #ESP_RESET_ON         ; CS Low + ~RST Low
        out     (ESP_IO_PORT), a
        ret
2$:
        ld      a, #ESP_IDLE_SEL         ; CS Low + ~RST High
        out     (ESP_IO_PORT), a
        ret

;-------------------------------------------------------------
; int esp_xfer(int tx)   1 バイト送受信(CS は呼び出し側が Low にしておく)
;   戻り: HL = 受信バイト(0..255)
;
;   spi.s の spi_transfer をそのまま移したもの(約 73T/ビット)。
;   `out (c),a` / `in a,(c)` は上位アドレスに B(ループカウンタ)が出るが、
;   実機の I/O デコードは下位 8 ビットしか見ないので問題ない
;   (spi.s が実機で動作実績のあるやり方)。djnz はフラグを変えないので
;   `rl e` が残した carry(次に送るビット)が生き残る。
;-------------------------------------------------------------
_esp_xfer::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)                ; E = 送信データ
        ld      bc, #0x0800 + ESP_IO_PORT   ; B = 8 ビット、C = ポート
        ld      hl, #0x203F            ; H = SCK(bit5)、L = rra の種
        sla     e                      ; carry = 最初に送るビット(bit7)
1$:
        ld      a, l
        rra                            ; A = MOSI<<7 | 0x1F(SCK Low、CS Low)
        out     (c), a
        or      h
        out     (c), a                 ; SCK High(立ち上がりで相手が取り込む)
        in      a, (c)                 ; bit7 = MISO
        rla                            ; carry = MISO
        rl      e                      ; E.bit0 = MISO、carry = 次に送るビット
        djnz    1$
        ld      a, #ESP_IDLE_SEL
        out     (c), a                 ; SCK Low で終える
        ld      l, e
        ld      h, #0
        ret
