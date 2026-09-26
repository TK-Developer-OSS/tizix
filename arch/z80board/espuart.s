;===================================================================
; espuart.s - ESP-WROOM-02 (WiFi) 用 bit-bang UART 物理層(#62)
;
;   SD カードと同じラッチ(OUT 0x80 = 74HC574 / IN 0x80 = 74HC541)に相乗りする
;   (2026-09-23 TK 設計・実配線済み。新規デコード不要):
;     OUT bit7 = MOSI(SD) / bit6 = CS(SD、1 = 非選択) / bit5 = SCK(SD)
;         bit4 = ~RST(ESP)  / bit1 = CTS(ESP、0 = 送ってよい) / bit0 = TX(ESP の RXD)
;     IN  bit7 = MISO(SD) / bit0 = RX(ESP の TXD)
;   SD のアイドル値 0xDF は TX=1(マーク)・CTS=1(止める)・~RST=1 なので、SD へ
;   アクセスしても ESP の線を乱さない。
;
;   9600bps 8N1。8MHz で 1 ビット = 833.3T。送受信の 1 ビットのループを 831〜834T
;   に合わせてある(下の T 数のコメント)。**ビット列の途中で割込みが入ると
;   ビットがずれる**ので、1 バイトの送信・まとまった受信の間は di する
;   (ISR は context switch まで行うので数百 T では済まない)。
;
;   半二重 + CTS(TK 設計): 送信中は CTS=1 で ESP を黙らせ、受信するときだけ
;   CTS=0 にする。ESP 側は `AT+UART_CUR=9600,8,1,0,2`(CTS フロー制御)を
;   netesp.c が起動時に送る。ESP は CTS=1 の間は送らず、UART FIFO(128B)に貯める。
;
;   ★ここは **ユーザーコマンド(net.bin)側** にリンクされる手書き PIC:
;     分岐は jr / djnz だけ(jp / call 禁止)、データ番地を持たない、
;     IY / IX は触らない(espspi.s と同じ掟)。
;   ABI (--sdcccall 0): 引数は右→左で push。2(sp)=arg1。戻り値は HL。
;===================================================================
        .module espuart

        .globl  _esp_rst
        .globl  _esp_tx
        .globl  _esp_rxbuf

UART_PORT       = 0x80          ; hw.h の SPI_PORT と同じラッチ
UART_IDLE       = 0xDF          ; MOSI=1 CS=1 SCK=0 ~RST=1 CTS=1 TX=1
UART_TX0        = 0xDE          ; TX=0(スタートビット / 0 のデータビット)
UART_RXON       = 0xDD          ; CTS=0(送ってよい)、TX=1
UART_RSTON      = 0xCF          ; ~RST=0(ESP をリセット保持)、他はアイドル

TXK             = 60            ; 送信 1 ビット = 51 + 13*TXK = 831T
RXK             = 61            ; 受信サンプル間隔 = 41 + 13*RXK = 834T
RXK1            = 92            ; スタート検出 → bit0 中央 ≒ 1250T(検出遅れ 0-48T 込み)
IDLEWAIT        = 350           ; バイト間の待ち(48T × 350 ≒ 2 バイト分)

        .area   _CODE

;-------------------------------------------------------------
; void esp_rst(int on)  on != 0 → ~RST Low(リセット保持) / 0 → 解除(アイドル値)
;-------------------------------------------------------------
_esp_rst::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        or      (hl)
        ld      a, #UART_RSTON
        jr      nz, 1$
        ld      a, #UART_IDLE
1$:
        out     (UART_PORT), a
        ret

;-------------------------------------------------------------
; void esp_tx(int c)    1 バイト送信(8N1、LSB ファースト)。CTS は 1 のまま。
;-------------------------------------------------------------
_esp_tx::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)                 ; E = 送るバイト
        di
        ld      a, #UART_TX0
        out     (UART_PORT), a          ; スタートビット
        ld      d, #8                   ; 7
        nop                             ; 4  (最初の区間だけ dec/jr の 16T が無いので詰める)
        nop                             ; 4
1$:
        ld      b, #TXK                 ; 7
2$:
        djnz    2$                      ; 13*(TXK-1)+8
        rr      e                       ; 8   carry = 次のデータビット
        ld      a, #UART_TX0            ; 7
        adc     a, #0                   ; 7   0xDE / 0xDF
        out     (UART_PORT), a          ; 11
        dec     d                       ; 4
        jr      nz, 1$                  ; 12 / 7
        ld      b, #TXK+2               ; 7   最後のデータビットの残り
3$:
        djnz    3$
        ld      a, #UART_IDLE           ; 7
        out     (UART_PORT), a          ; 11  ストップビット
        ld      b, #64                  ; ストップビットを 1 ビット分保つ
4$:
        djnz    4$
        ei
        ret

;-------------------------------------------------------------
; int esp_rxbuf(unsigned char *buf, int max, int wait)
;   CTS=0 にして ESP からの受信を buf へ最大 max バイト(<= 255)読む。
;   最初のスタートビットは wait 回(1 回 48T)まで待つ。以後はバイト間の
;   待ちが IDLEWAIT を超えたら終わる。残り 2 バイトの時点で CTS=1 に
;   戻す(ESP が送り始めていた分はそのまま受ける)。戻り HL = 読んだ数。
;   戻るときは必ず CTS=1。
;-------------------------------------------------------------
_esp_rxbuf::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = buf
        inc     hl
        ld      a, (hl)                 ; A = max(下位)
        inc     hl
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; BC = wait
        ex      de, hl                  ; HL = buf
        push    hl                      ; 先頭を覚えておく(数を返すため)
        ld      d, a                    ; D = 残り
        or      a
        jr      z, 9$
        di
        ld      a, #UART_RXON
        out     (UART_PORT), a          ; CTS=0: 送ってよい
5$:
        in      a, (UART_PORT)          ; 11
        rrca                            ; 4   carry = RX
        jr      nc, 6$                  ; 7 / 12  0 = スタートビット
        dec     bc                      ; 6
        ld      a, b                    ; 4
        or      c                       ; 4
        jr      nz, 5$                  ; 12  (1 周 48T)
        jr      9$                      ; 待ち切れ
6$:
        ld      b, #RXK1                ; 7
7$:
        djnz    7$                      ; bit0 の中央まで
        ld      c, #8                   ; 7
8$:
        in      a, (UART_PORT)          ; 11  サンプル
        rrca                            ; 4
        rr      e                       ; 8   LSB ファーストで組む
        ld      b, #RXK                 ; 7
10$:
        djnz    10$                     ; 13*(RXK-1)+8
        dec     c                       ; 4
        jr      nz, 8$                  ; 12 / 7
        ld      (hl), e
        inc     hl
        dec     d
        jr      z, 9$                   ; 満杯
        ld      a, d
        cp      #2
        jr      nz, 11$
        ld      a, #UART_IDLE           ; 残り 2: CTS=1(送りかけの分は受ける)
        out     (UART_PORT), a
11$:
        ld      bc, #IDLEWAIT
        jr      5$
9$:
        ld      a, #UART_IDLE
        out     (UART_PORT), a          ; CTS=1 で終える
        ei
        pop     de                      ; 先頭
        or      a
        sbc     hl, de                  ; HL = 読んだ数
        ret
