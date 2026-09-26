; boot.s - SPI/コンソール単体ブリングアップ用モニタ(実機 SD/UART 配線確認用)
;
;   実機で動作確認済みの ~/z80pack/bios/bios.s を移植。元はスタンドアロンで
;   zasm により1ファイル化されてビルドされていた(#include で spi.s/sdcard.s
;   を直接取り込み、即値に # を必要としない zasm 方言)。
;   tizix は全アーキ共通で sdasz80 + sdldz80 を使う(arch/z80pack が基準)ため、
;   spi.s/sdcard.s も他のモジュールと同じ規約に合わせ、別コンパイル+リンクにする。
;   sdasz80 向けに直した点(zasm と違い、こちらが必須):
;     ・equ ではなく =
;     ・即値は # 必須(cp/and/or/ld 問わず)
;     ・.area 宣言が必須(元は無かった)。boot.s はカーネルの crt0.s と違い
;       全体が固定アドレス配置の「単体モニタ」なので、ベクタ部だけでなく
;       ファイル全体を単一の .area _HEADER (ABS) に置く。
;     ・spi.s/sdcard.s は #include せず、spi.rel/sdcard.rel を別途リンクする
;       (カーネルビルドと同じ流儀。Makefile 側で boot.rel と一緒にリンクする)。
;   ロジック・レジスタ規約・タイミングは実機版のまま変更していない。

.module bios

.globl putc
.globl puts
.globl hex_to_ascii
.globl system_status_print
.globl spi_close
.globl spi_transfer
.globl sd_init
.globl sd_data_read
.globl hex_dump

console = 0x01
SPI_IO_PORT = 0x80

system_status = 0x10

; 512バイトのデータバッファ（RAM領域）
data_buf = 0xc000

boostrap_offset = 0x5b

; sdld は _DATA の定義を要求する(無いと警告+exit 1 で make が止まる)。
; boot.s は C ランタイムのグローバル変数を持たないので空のまま宣言するだけでよい
; (crt0.s も同じ理由で標準 SDCC エリア一式を宣言している)。
.area _DATA

.area _HEADER (ABS)

.org 0x0000

	im 1

	jp start

.org 0x38
	jp intr_handler
	reti



.org 0x0066
	jp nmi_handler
	halt


.org 0x0080

start:

    ;-------------------------------------------------------
    ; UART(FT245RL)初期化フラッシュ
    ;-------------------------------------------------------
    ; 実機はデコードを 74HC138(A4-6のみ判定)で行っており、FT245RL 自体は
    ; 0x00/0x01 を区別しない。つまりリセット直後の不定/ごみは避けようがない
    ; 前提で、適当に文字(改行)を40個ほど流してから ESC でカーソルを
    ; ホームへ戻す。ごみは全部この中で流れ切るので以降のログ表示には
    ; 影響しない。rept は使わない(0x0080-0x0100 のコード領域が128バイト
    ; しかなく、rept で静的展開すると main へのジャンプ先org手前で溢れる
    ; ため、djnz によるランタイムループにする)。
    ld b, #40
uart_init_flush:
    ld a, #0x0d
    call putc
    ld a, #0x0a
    call putc
    djnz uart_init_flush

    ; 画面クリア (ESC [ 2 J)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x32
    call putc
    ld a, #0x4a
    call putc

    ; カーソルをホームポジションに移動 (ESC [ H)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x48
    call putc

;	rept 100
;	nop
;	endm


    ld b, #50      ; 外側ループ回数 (50回)


outer_delay_loop:
    ld de, #50000  ; 内側ループ回数 (5万回)


	; コンソールにも表示
	push af
	push bc
	push de
	push hl

	ld a, b
	call hex_to_ascii

	ld a, d
	call putc

	ld a, e
	call putc


	ld a, #0x0d
	call putc

	ld a, #0x0a
	call putc

	pop hl
	pop de
	pop bc
	pop af


inner_delay_loop:
    dec de
    ld a, d
    or e
    jr nz, inner_delay_loop

    djnz outer_delay_loop

; 10秒間の遅延終了

	ld hl, #dbg_countdown_done
	call puts

	call main

	halt


.org 0x0100

;--------------------------------------------------------
; main
;--------------------------------------------------------
main:

	; disable interrupt
;	di


;	ld hl, #intr_lock_addr
;	ld (hl), #0x00			; intr_lock_addr[0x8000]を0に初期化
;	inc hl
;	ld (hl), #0x00			; intr_lock_addr[0x8001]を0に初期化



	;-----------------------
    ; 画面クリア
	;-----------------------
	.rept 80
	ld a, #0x0d
	call putc
	ld a, #0x0a
	call putc
	.endm

    ; 画面クリア (ESC [ 2 J)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x32
    call putc
    ld a, #0x4a
    call putc

    ; カーソルをホームポジションに移動 (ESC [ H)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x48
    call putc
;

	ld hl, #dbg_main_enter
	call puts

	;-------------------------------
	; system status set & print
	;-------------------------------

	; romkillは初期値で0なので意味ない
	; 仮に読み込んでも値は取れない
	;ld a, #0x00
	;out (system_status), a

	ld hl, #str_test
	call puts

	ld hl, #systen_status_print_str1
	call puts

	call system_status_print

	ld hl, #systen_status_print_str2
	call puts




	;--------------------------------------
	; SDカード初期化
	;--------------------------------------
	ld hl, #dbg_sd_init_call
	call puts

	call sd_init

	ld hl, #dbg_sd_init_ret
	call puts

	ld hl, #msg_load_mbr
	call puts


	;--------------------------------------
	; SDカードVBRをロード
	;--------------------------------------

	; アドレス0x00000000から読み込み
	ld hl, #data_buf      ; HLにRAMバッファのアドレス0xc000を設定
	ld bc, #0x0000        ; SDカードアドレスの上位16ビット
	ld de, #0x0000        ; SDカードアドレスの下位16ビット

	push hl
	ld hl, #dbg_sd_read_call
	call puts
	pop hl

	call sd_data_read

	ld hl, #dbg_sd_read_ret
	call puts

	ld hl, #data_buf
	call hex_dump



	;-------------------------------------------
	;
	; ロードしたVBRのブートストラップにジャンプ
	;
	;-------------------------------------------

	ld hl, #dbg_jump_bootstrap
	call puts

	jp data_buf + boostrap_offset


	halt
	halt
	halt
	halt




str_test:
	.ascii "BOOTING NOW !"
	.db 0x0d, 0x0a, 0x0d, 0x0a, 0x00

systen_status_print_str1:
	.ascii "SYSTEM STATUS CODE ["
	.db 0x00

systen_status_print_str2:
	.ascii "]"
	.db 0x0d, 0x0a, 0x00


msg_load_mbr:
	.ascii "LOAD MBR..."
	.db 0x0d, 0x0a, 0x00


;--------------------------------------------------------
; デバッグ用ステータス文字列(処理の進行状況を把握するための各所プリント)
;--------------------------------------------------------
dbg_countdown_done:
	.ascii "[BOOT] countdown done, calling main"
	.db 0x0d, 0x0a, 0x00

dbg_main_enter:
	.ascii "[MAIN] entered"
	.db 0x0d, 0x0a, 0x00

dbg_sd_init_call:
	.ascii "[SD] sd_init call"
	.db 0x0d, 0x0a, 0x00

dbg_sd_init_ret:
	.ascii "[SD] sd_init returned"
	.db 0x0d, 0x0a, 0x00

dbg_sd_read_call:
	.ascii "[SD] sd_data_read call (VBR)"
	.db 0x0d, 0x0a, 0x00

dbg_sd_read_ret:
	.ascii "[SD] sd_data_read returned"
	.db 0x0d, 0x0a, 0x00

dbg_jump_bootstrap:
	.ascii "[BOOT] jumping to bootstrap"
	.db 0x0d, 0x0a, 0x00

dbg_bios_call_prefix:
	.ascii "[BIOS] call="
	.db 0x00



;--------------------------------------------------------
; puts
;--------------------------------------------------------
puts:
	push af
	push hl

puts_loop:
	ld a, (hl)
	or a
	jr z, puts_end
	call putc
	inc hl
	jr puts_loop

puts_end:

	pop hl
	pop af

	ret


;--------------------------------------------------------
; putc
;--------------------------------------------------------
putc:
	out (console), a
	ret


;--------------------------------------------------------
; intr_handler
;--------------------------------------------------------
intr_handler:
	di

	push af


	in a, (console)
	call putc



	pop af

	ei

	ret

;--------------------------------------------------------
; nmi_handler
;--------------------------------------------------------
nmi_handler:
	push af
	push hl

	ld hl, #str_nmi
	call puts

	pop hl
	pop af

	retn

str_nmi:
	.ascii "nmi detected."
	.db 0x0d, 0x0a, 0x00


;--------------------------------------------------------
; clear_screen  未使用
;--------------------------------------------------------
clear_screen:
    push af             ; レジスタを退避

	; clear screen
	.rept 80
	ld a, #0x0d
	call putc
	ld a, #0x0a
	call putc
	.endm


    ; 画面クリア (ESC [ 2 J)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x32
    call putc
    ld a, #0x4a
    call putc

    ; カーソルをホームポジションに移動 (ESC [ H)
    ld a, #0x1b
    call putc
    ld a, #0x5b
    call putc
    ld a, #0x48
    call putc

    pop af              ; レジスタを復帰
    ret                 ; サブルーチンから戻る


;--------------------------------------------------------
; ステータス値を画面表示
;--------------------------------------------------------
system_status_print:
    in    a, (system_status) ; ポート$10の値を読み込み、アキュムレータ(a)へ
    push  af                     ; aレジスタの値をスタックに一時保存

    ; --- 上位4ビットの変換と出力 ---
    rrca                         ; Aレジスタを右へ4回論理シフト
    rrca
    rrca
    rrca
    and   #0x0f                    ; 下位4ビット以外をクリア
    call  nibble_to_hex_and_print

    ; --- 下位4ビットの変換と出力 ---
    pop   af                     ; スタックからaレジスタの値を復元
    and   #0x0f                    ; 下位4ビット以外をクリア
    call  nibble_to_hex_and_print

    ret

; サブルーチン: 4ビットの値を16進数のASCII文字に変換してputcで出力する
; 入力: aレジスタの下位4ビット (0-15)
nibble_to_hex_and_print:
    cp    #10                     ; 10未満か？
    jr    c, is_digit_hex        ; 10未満なら数字へジャンプ

    ; 10以上(A-F)の場合
    add   a, #'A' - 10            ; 'A'を足して10を引く
    jr    print_hex              ; 出力へジャンプ

is_digit_hex:
    ; 10未満(0-9)の場合
    add   a, #'0'                 ; '0'を足す

print_hex:
    call  putc                   ; putcルーチンを呼び出して1文字出力
    ret


;========================================================
; hex_to_ascii
; 1バイトを2文字のHEX ASCIIに変換
; 入力: Aレジスタ (変換するバイト)
; 出力: Dレジスタ (上位ニブルのASCII)
;       Eレジスタ (下位ニブルのASCII)
;========================================================
hex_to_ascii:
    push af             ; Aをスタックに保存

    ; 上位ニブルを変換
    and #0xf0            ; 上位4ビットだけを残す
    rrca                ; Aレジスタのビットを右にローテート
    rrca                ; 4回ローテートで上位ニブルを下位に移動
    rrca
    rrca
    call nibble_to_ascii
    ld d, a             ; 結果をDに保存

    pop af              ; Aをスタックから復元

    ; 下位ニブルを変換
    and #0x0f            ; 下位ニブルだけを残す
    call nibble_to_ascii
    ld e, a             ; 結果をEに保存

    ret

;========================================================
; sd_nibble_to_ascii
; 4ビット（ニブル）をHEX ASCIIに変換
; 入力: Aレジスタ (0x00-0x0fのニブル)
; 出力: Aレジスタ (ASCII文字)
;========================================================
nibble_to_ascii:
    cp #0x0a             ; 10以上かチェック
    jr c, is_numeric    ; 10未満なら数字

    ; 10以上（A-F）の場合
    add a, #'A' - 0x0a   ; 'A'-10を加算
    ret

is_numeric:
    ; 10未満（0-9）の場合
    add a, #'0'          ; '0'を加算
    ret


	.org 0x3000


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; data_bufの512バイトをHEX文字列に変換してFT245に出力
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hex_dump:
	push af
	push bc
	push de
	push hl

;    ld hl, #data_buf     ; HLにバッファの開始アドレスをロード
    ld bc, #512          ; BCに書き込むバイト数(512)をセット

	ld de, #0

dump_loop:
    ld a, (hl)          ; バッファから1バイト読み込み
    call hex_to_ascii   ; 1バイトを2文字のHEX ASCIIに変換
                        ; 結果はDEレジスタに格納される

    ; FT245に出力 (上位ニブル)
    ld a, d
    out (console), a

    ; FT245に出力 (下位ニブル)
    ld a, e
    out (console), a

	ld a, #','
	call putc

	ld a, l
	and #0x0f
	cp #0x0f
	jp nz, dump_loop_next

	ld a, #0x0d
	call putc
	ld a, #0x0a
	call putc


dump_loop_next:

    inc hl              ; 次のバイトへ進む
    dec bc              ; カウンタをデクリメント
    ld a, b             ; BCが0かチェック
    or c
    jr nz, dump_loop    ; ループカウンタが0でなければ続行


	pop hl
	pop de
	pop bc
	pop af
	ret




;========================================================
;
; BIOS ファンクションコールのエントリー ver 0.0001
;
;========================================================


	.org 0x4000

bios_call:

	push ix
	ld ix, #0x0000
	add ix, sp

	ld a, 4(ix)

check_call_putc:
	cp #0x00
	jp nz, check_call_puts
	ld a, 5(ix)
	call putc
	jp bios_call_end


check_call_puts:
	cp #0x01
	jp nz, check_call_spi_open
	ld l, 5(ix)
	ld h, 6(ix)
	call puts
	jp bios_call_end

check_call_spi_open:
	cp #0x02
	jp nz, check_call_spi_close
	call spi_transfer
	jp bios_call_end

check_call_spi_close:
	cp #0x03
	jp nz, check_call_spi_transfer
	call spi_transfer
	jp bios_call_end

check_call_spi_transfer:
	cp #0x04
	jp nz, check_call_sd_init
	ld a, 5(ix)
	call spi_transfer
	ld b, a
	pop af
	ld a, b
	push af
	jp bios_call_end


check_call_sd_init:
	cp #0x05
	jp nz, check_call_sd_read
	call sd_init
	jp bios_call_end


check_call_sd_read:
	cp #0x06
	jp nz, check_call_sd_write
	ld c, 5(ix)
	ld b, 6(ix)
	ld e, 7(ix)
	ld d, 8(ix)
	ld l, 9(ix)
	ld h, 10(ix)

	call sd_data_read

	jp bios_call_end

check_call_sd_write:
	cp #0x07
	jp nz, check_call_hex_dump
;;;;	call sd_data_write
	jp bios_call_end


check_call_hex_dump:
	cp #0x08
	jp nz, check_call_hex_to_ascii_print
	ld l, 5(ix)
	ld h, 6(ix)
	call hex_dump
	jp bios_call_end

check_call_hex_to_ascii_print:
	cp #0x09
	jp nz, bios_call_end
	ld a, 5(ix)
	call hex_to_ascii

	ld a, d
	call putc

	ld a, e
	call putc


	jp bios_call_end




bios_call_end:


	pop ix

	ret
