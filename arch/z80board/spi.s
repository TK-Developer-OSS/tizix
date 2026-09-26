; spi.s - bit-bang SPI (74HC574 ラッチ + 74HC541 バッファ経由)
;
;   実機で動作確認済みの ~/z80pack/bios/spi.s を移植。元はスタンドアロンの
;   ブリングアップ用 bios.s に #include で直接取り込まれる前提(zasm、1ファイル
;   化されるので .globl 無しでも同一モジュール内呼び出しが成立していた)。
;   tizix のカーネルビルドは sdasz80 + sdldz80 でモジュール別にリンクするため、
;   他モジュール(sdcard.s)から呼ばれる関数は .globl が必須。
;   ロジック・レジスタ規約・タイミングは実機版のまま変更していない。
;   sdasz80 向けに直した点(zasm と違い、こちらが必須):
;     ・equ ではなく =
;     ・即値は # 必須(cp/and/or/ld 問わず)
;     ・.area 宣言が必須(元は無かった)
;     ・spi_close に .globl が無かった(sdcard.s から呼ばれているのに未export)

        .module spi

        .globl  spi_open
        .globl  spi_close
        .globl  spi_transfer
        .globl  spi_rx512

; ---- SPI通信で使用するI/Oポートアドレス(hw.h SPI_PORT と一致させること) ----
SPI_IO_PORT     = 0x80

; ---- SPIピンのビット定義(74HC574/541を使ったビットバンギング向け) ----
;   データポート(0x80)の D0=MOSI, D1=MISO, D2=SCK, D3=CS。
SPI_MOSI_BIT    = 0x07
SPI_MISO_BIT    = 0x07
SPI_SCK_BIT     = 0x05
SPI_CS_BIT      = 0x06

        .area   _CODE

; ---- spi_open: SPI通信の初期化 ----
spi_open:
        push    af
        ld      a, #0xff
        out     (SPI_IO_PORT), a
        out     (SPI_IO_PORT), a
        pop     af
        ret

spi_close:
        push    af
        ld      a, #0x5F        ; CS High(bit6)、SCK/MOSI Low、bit0-4 = 1
        ;   #62: bit0-4 は ESP の TX / CTS / ~RST(同じラッチに相乗り)。以前は 0x40 で
        ;   bit0-4 = 0 を出しており、SD を閉じるたびに ESP をリセット(~RST=0)し、
        ;   TX にブレーク、CTS=0 で ESP に送信を許していた。
        out     (SPI_IO_PORT), a
        ; (#54 で一時「CS High のまま SCK 8 発」を足したが、実機で r=32 が
        ;  1 バイトも変わらず無効と確認。真因は SDSC のバイトアドレス指定
        ;  (sdcard.s sd_cmd58)だったので撤去した。)
        pop     af
        ret

; ---- spi_transfer: SPI で 1 バイト送受信(モード 0、MSB から)----
;   入力: A = 送信データ / 出力: A = 受信データ。BC/DE/HL は保存。
;
;   2026-09-19 書き直し(旧: 1 ビットごとに res/set でポート値を組み立て直して
;   約 144T/ビット → 約 73T/ビット、コードも小さい)。
;   ポート 0x80 の出力: bit7 = MOSI、bit6 = CS(0 で選択)、bit5 = SCK、
;   bit0〜4 は未使用(旧実装も毎ビット入力のゴミを出していた)なので常に 1。
;   入力: bit7 = MISO。
;   1 ビットの流れ(旧実装と同じ順序): SCK Low + MOSI を出す → SCK High
;   (カードが MOSI を取り込み、MISO が確定)→ MISO を読む。
;   `rl e` 1 命令で「受信ビットを E の bit0 へ」と「次に送るビットを carry へ」
;   を同時に行う(E は送信データが抜けた分だけ受信データで埋まっていく)。
;   L = 0x3F を rra すると A = carry<<7 | 0x1F(SCK=0, CS=0, bit0〜4=1)。
spi_transfer:
        push    bc
        push    de
        push    hl
        ld      e, a                   ; E = 送信データ
        ld      bc, #0x0800 + SPI_IO_PORT   ; B = 8 ビット、C = ポート
        ld      hl, #0x203F            ; H = SCK(bit5)、L = rra の種
        sla     e                      ; carry = 最初に送るビット(bit7)
transfer_loop:
        ld      a, l
        rra                            ; A = MOSI<<7 | 0x1F(SCK Low、CS Low)
        out     (c), a                 ; SCK Low + MOSI 確定
        or      h
        out     (c), a                 ; SCK High(立ち上がりでカードが取り込む)
        in      a, (c)                 ; bit7 = MISO
        rla                            ; carry = MISO
        rl      e                      ; E.bit0 = MISO、carry = 次に送るビット
        djnz    transfer_loop
        ld      a, #0x9F               ; アイドル: MOSI=1、CS Low、SCK Low(旧実装と同じ)
        out     (c), a
        ld      a, e                   ; 受信データ
        pop     hl
        pop     de
        pop     bc
        ret

; ---- spi_rx512: 512 バイト受信専用(sd_data_read のデータブロック用)----
;   入力: HL = 格納先 / 出力: HL = 格納先 + 512。破壊: AF, BC, DE。
;   MOSI は 1 に固定(受信中にカードへ送るのは 0xFF)なので、送信ビットの計算を
;   省き、8 ビット分を展開して約 55T/ビット(旧 spi_transfer 経由の約 2.9 倍)。
;   受信ビットは `rl (hl)` で格納先へ直接詰める(8 回回せば元の中身は全部
;   押し出されるので、事前のクリアは不要)。
spi_rx512:
        ld      c, #SPI_IO_PORT
        ld      de, #0xBF9F            ; D = SCK High、E = SCK Low(どちらも MOSI=1、CS Low)
        ld      a, #2                  ; 256 バイト × 2
rx512_page:
        push    af
        ld      b, #0                  ; djnz で 256 回
rx512_byte:
        .rept   8
        out     (c), e                 ; SCK Low
        out     (c), d                 ; SCK High
        in      a, (c)                 ; bit7 = MISO
        rla
        rl      (hl)
        .endm
        inc     hl
        djnz    rx512_byte
        pop     af
        dec     a
        jr      nz, rx512_page
        out     (c), e                 ; SCK Low で終える(アイドル 0x9F)
        ret


; spi_begin / spi_end(in で読み戻して bit を立て下げする版)は誰も呼んで
; いなかったので #71 で削除した(ROM 容量)。
