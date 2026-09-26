; sdcard.s - SD カード SPI モードプロトコル(CMD0/CMD8/ACMD41/CMD16/CMD58/CMD17/CMD24)
;
;   実機で動作確認済みの ~/z80pack/bios/sdcard.s を移植したもの。
;
;   ■ #71(2026-09-24)で畳んだ ── **カードへ流れるバイト列は 1 バイトも変えていない**
;     移植元は Arduino の手順を上から順に写した形で、同じ列が何度も並んでいた。
;     カーネル ROM(32KB)が z80board だけ溢れたので重複を畳んだ:
;       ・「0xFF を n 回送る」5 箇所 + 読み捨て 2 箇所 → sd_ffn(B = 回数)
;       ・「spi_open → send_cmd → R1 待ち(0xFFFF)」6 コマンド → sd_cmd_r1
;         (CS を開き直さない ACMD41 は途中の入口 sd_send_r1 から)
;       ・各コマンド関数の全レジスタ push/pop を撤去(呼ぶのは sd_init だけで、
;         sd_init を呼ぶ _sd_init は C ABI = AF/BC/DE/HL を壊してよい。IX/IY は触らない)
;       ・no-op のデバッグ表示(sd_cmd_print 等)への call を撤去
;       ・CMD0 の「一致しても不一致でも同じ所へ飛ぶ」cp/jr を撤去
;       ・_disk_raw_rw(16bit 版)は _disk_raw_rw32 の後半を共有する形にした。
;         **副次的な修正**: 旧 16bit 版は SDSC のバイトアドレス変換(×512)を
;         していなかった(/dev/fda を SDSC カードで読むと別の場所を読む)。
;     R1 待ち・データトークン待ち・書き込みビジー待ちのタイムアウト値、
;     ACMD41 のリトライ回数の数え方(send_cmd の H=CRC がカウンタ HL の上位を
;     上書きする旧来の挙動を含む)は旧実装と同じ。
;
;   ■ 移植時に直した挙動(旧コメントの要約。詳細は git log)
;     ・wait_for_response_r1: タイムアウト判定 `or l` → `or c`、halt → 0xFF 復帰
;     ・sd_data_read/write: 呼び出し側 BC(アドレス上位)を握り潰していたのを修正、
;       書き込みタイムアウトの自己ループを修正、戻り値 A(0=成功)を新設
;     ・データトークン待ちにタイムアウト(0xFD)を追加(#54)
;     ・書き込み後のビジー明け待ちを追加(#54)
;     ・CMD58 で CCS を見て SDSC ならブロック番号 ×512(#54)
;     ・ACMD41 タイムアウト時の puts+halt → 復帰

        .module sdcard

        .globl  spi_transfer
        .globl  spi_rx512
        .globl  spi_close
        .globl  spi_open

        .globl  sd_init
        .globl  sd_data_read
        .globl  sd_data_write
        .globl  _disk_raw_rw
        .globl  _disk_raw_rw32
        .globl  _sd_init

        .area   _DATA
_sd_is_hc:
        .ds     1       ; 1=SDHC/SDXC(ブロック番号そのまま) 0=SDSC(×512 要)

        .area   _CODE

;========================================================
; send_cmd: コマンド 6 バイトを送る(トランザクションは含まない)
;   入力: A = コマンドバイト / BC:DE = 引数(上位:下位)/ H = CRC
;   破壊: A(BC/DE/HL は spi_transfer が保存するのでそのまま)
;========================================================
send_cmd:
        call    spi_transfer
        ld      a, b
        call    spi_transfer
        ld      a, c
        call    spi_transfer
        ld      a, d
        call    spi_transfer
        ld      a, e
        call    spi_transfer
        ld      a, h
        jp      spi_transfer

;========================================================
; sd_cmd_r1: CS を開き、コマンドを送って R1 を待つ(タイムアウト 0xFFFF)
;   入力: send_cmd と同じ / 出力: A = R1(タイムアウト時 0x00。下の注記)
;   sd_send_r1 は CS を開き直さない入口(ACMD41 用)。
;========================================================
sd_cmd_r1:
        call    spi_open
sd_send_r1:
        call    send_cmd
sd_wait_ffff:
        ld      bc, #0xffff
        ; ここから wait_for_response_r1 へ落ちる

;========================================================
; wait_for_response_r1: R1 を待つ
;   入力: BC = タイムアウト回数 / 出力: A = R1。BC は保存する。
;   ★タイムアウト時は **A = 0x00**(`or c` の結果)で戻る。旧コメントは
;   「0xFF を返す」と書いていたが、実コードは移植当初から 0x00 = 「R1 成功」
;   扱いだった(#71 で畳むときに気付いた)。実機はこの挙動で動いているので
;   変えていない。直すなら ACMD41 のリトライ回数と sd_data_read の経路が
;   変わるので、実機確認とセットで(task.md #71)。
;========================================================
wait_for_response_r1:
        push    bc
1$:     ld      a, #0xff
        call    spi_transfer
        cp      #0xff
        jr      nz, 2$                  ; 0xFF 以外 = 応答あり
        dec     bc
        ld      a, b
        or      c
        jr      nz, 1$
2$:     pop     bc
        ret

;========================================================
; sd_ffn: 0xFF を B 回送る(ダミークロック / 応答の読み捨て)
;   出力: A = 最後に受信したバイト。B = 0。
; sd_ff4: 4 回版(各コマンドの後始末に使う)
;========================================================
sd_ff4:
        ld      b, #4
sd_ffn:
        ld      a, #0xff
        call    spi_transfer
        djnz    sd_ffn
        ret

;========================================================
; sd_init: SD カード初期化
;========================================================
sd_init:
        ; CMD0 の前に CS High のまま 74 クロック以上(0xFF を 10 回)
        call    spi_close
        ld      b, #10
        call    sd_ffn

        ; 最低 1ms 待つ(8MHz で約 1ms)
        ld      hl, #4000
1$:     dec     hl
        ld      a, h
        or      l
        jr      nz, 1$

        ; ---- CMD0: リセット ----
        ld      a, #0x40
        ld      bc, #0x0000
        ld      de, #0x0000
        ld      h, #0x95
        call    sd_cmd_r1
        call    sd_ff4
        call    spi_close

        ; ---- CMD8: 電圧チェック。R1=0x01 なら R7 の 4 バイトを読む ----
        ld      a, #0x48
        ld      bc, #0x0000
        ld      de, #0x01AA
        ld      h, #0x87
        call    sd_cmd_r1
        cp      #0x01
        jr      nz, 2$
        call    sd_ff4                  ; R7(末尾 0xAA の確認は旧来どおりしない)
2$:     call    sd_ff4
        call    spi_close

        ; ---- ACMD41(CMD55 + ACMD41)を R1=0x00 まで繰り返す ----
        call    sd_acmd41

        ; ---- CMD16: ブロック長 512 ----
        ld      a, #0x50
        ld      bc, #0x0000
        ld      de, #0x0200
        ld      h, #0x01
        call    sd_cmd_r1
        call    sd_ff4
        call    spi_close

        ; ---- CMD58: READ_OCR。OCR 1 バイト目 bit6(CCS)でカード種別を見る ----
        ;   SDSC だと CMD17/24 の引数はバイトアドレス(#54)。
        ld      a, #0x7a
        ld      bc, #0x0000
        ld      de, #0x0000
        ld      h, #0x01
        call    sd_cmd_r1
        ld      a, #0xff
        call    spi_transfer            ; OCR bit31-24
        ld      hl, #_sd_is_hc
        ld      (hl), #0
        bit     6, a
        jr      z, 3$
        ld      (hl), #1
3$:     ld      b, #3                   ; OCR 残り 3 バイトを読み捨て
        call    sd_ffn
        jp      spi_close

;========================================================
; sd_acmd41: CMD55 + ACMD41 を R1=0x00 になるまで繰り返す
;   HL がリトライのカウンタ。★send_cmd は H を CRC として送るので、
;   ループ中に H が 0x65 / 0x17 で上書きされる ── 移植元からの挙動で、
;   実質の回数は 15 回ではなく数千回になる。実機ではこれで初期化できて
;   いるので変えていない。
;========================================================
sd_acmd41:
        ld      hl, #0x0f
1$:
        ; CMD55(R1=0x01 が来るまで R1 待ちをやり直す)
        ld      a, #0x77
        ld      bc, #0x0000
        ld      de, #0x0000
        ld      h, #0x65
        call    sd_cmd_r1
2$:     cp      #0x01
        jr      z, 3$
        call    sd_wait_ffff
        jr      2$
3$:     call    sd_ff4

        ; ACMD41(CS は Low のまま = 開き直さない)
        ld      a, #0x69
        ld      de, #0x0000
        ld      bc, #0x4000
        ld      h, #0x17
        call    sd_send_r1
        call    spi_close
        or      a
        ret     z                       ; R1=0x00 = 初期化完了

        dec     hl
        ld      a, h
        or      l
        jr      nz, 1$
        ret                             ; タイムアウト(成否は伝えない。旧来どおり)

;========================================================
; _sd_init - diskio.c(C)から呼ぶ C ABI ラッパ。当面は常に 0(成功)。
;========================================================
_sd_init::
        call    sd_init
        ld      hl, #0
        ret

;========================================================
; sd_data_read: 1 セクタ読む
;   入力: HL = 格納先 / BC:DE = CMD17 引数(上位:下位)
;   出力: A = 0 成功 / 非0 エラー(R1 値、または 0xFD = データトークン無し)
;   R1 待ちがタイムアウトすると A=0 で戻るので、その場合はトークン待ちの
;   タイムアウト(0xFD)として現れる(旧来どおり)。
;========================================================
sd_data_read:
        push    de
        push    hl

        call    spi_open
        ld      a, #0x51
        call    send_cmd
        ld      bc, #10000
        call    wait_for_response_r1
        or      a                       ; R1 == 0x00 か?
        jr      nz, _read_error         ; A = R1 のまま返す(SD_DEBUG で見える)

        ; データトークン(0xFE)を待つ(#54: 無いと永遠に戻らなかった)
        ld      de, #10000
1$:     ld      a, #0xff
        call    spi_transfer
        cp      #0xfe
        jr      z, 2$
        dec     de
        ld      a, d
        or      e
        jr      nz, 1$
        ld      a, #0xfd                ; データトークンタイムアウト
        jr      _read_error

2$:     call    spi_rx512               ; 512 バイトを HL へ(HL はここまで保存されている)
        ld      b, #12                  ; CRC 2B + ダミー 10B を読み捨て
        call    sd_ffn
        xor     a                       ; 0 = 成功
_read_error:
        call    spi_close               ; A を壊さない(spi_close は af を保存)
        pop     hl
        pop     de
        ret

;========================================================
; sd_data_write: 1 セクタ書く
;   入力: HL = 書き込み元 / BC:DE = CMD24 引数(上位:下位)
;   出力: A = 0 成功 / 非0 エラー
;========================================================
sd_data_write:
        push    de
        push    hl

        call    spi_open
        ld      a, #0x58
        call    send_cmd
        ld      bc, #100
        call    wait_for_response_r1

        ld      a, #0xfe                ; データトークン
        call    spi_transfer

        ld      bc, #512
1$:     ld      a, (hl)
        call    spi_transfer
        inc     hl
        dec     bc
        ld      a, b
        or      c
        jr      nz, 1$

        ld      a, #0xff                ; ダミー CRC 2 バイト(旧来どおり 2 回目は
        call    spi_transfer            ;   1 回目の受信値をそのまま送る)
        call    spi_transfer

        ld      bc, #10000
2$:     ld      a, #0xff                ; データ応答 xxx0_0101 を待つ
        call    spi_transfer
        and     #0x1f
        cp      #0x05
        jr      z, 3$
        dec     bc
        ld      a, b
        or      c
        jr      nz, 2$
        jr      _write_error

        ; ビジー明け待ち(#54): カードは書き終えるまで MISO を Low に保つ。
        ; 待たずに次のコマンドを送ると R1 待ちが 0x00 を成功と誤認する。
3$:     ld      bc, #0
4$:     ld      a, #0xff
        call    spi_transfer
        cp      #0xff
        jr      z, _write_ok
        dec     bc
        ld      a, b
        or      c
        jr      nz, 4$
_write_error:
        ld      a, #1                   ; 非0 = エラー
        jr      _read_error             ; spi_close + 復帰は読みと共通
_write_ok:
        xor     a
        jr      _read_error

;========================================================
; _disk_raw_rw32 - diskio.c(FatFs 本体)から呼ぶ C ABI ラッパ(32bit セクタ)
;     unsigned char disk_raw_rw32(unsigned char *buf, unsigned long sect,
;                                 unsigned char op)
;   --sdcccall 0: 2(sp)=buf 4(sp)=sect下位 6(sp)=sect上位 8(sp)=op(1B)
; _disk_raw_rw - src/dev.c(/dev/fda 相当)から呼ぶ 16bit セクタ版
;     unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
;                               unsigned sect, unsigned char op)
;   --sdcccall 0: **char 引数は 1 バイトで積まれる**(sdcc は `push af /
;   inc sp`)ので 2(sp)=drive(1B) 3(sp)=buf 5(sp)=sect 7(sp)=op(1B)。
;   ★#71 で修正: 旧実装は「各引数 2B スロット」と仮定して 4/6/8(sp) を
;   読んでいた(obj/dev.asm の呼び出し側で push af / inc sp を確認済み)。
;   buf と sect が 1 バイトずつずれ、op は呼び出し元フレームのゴミを読むので、
;   **/dev/fda・/dev/fdb を読むだけで、ゴミのバッファ(ROM)をゴミのセクタへ
;   書き込み得た**(z80boardsim で sdcard.img の先頭が ROM のコードで
;   上書きされ 1.97TB に膨れたのを確認)。実機で dd if=/dev/fdb を打つと
;   SD カードを壊していたはず。32bit 版は op が末尾なので影響が無かった。
;   drive は未使用(SD 1 枚)。上位語 0 として 32bit 版と同じ後半へ合流する。
;   戻り値: HL(L のみ意味を持つ)。0=成功 / 非0=エラー。
;========================================================
_disk_raw_rw::
        ld      hl, #3
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; BC = buf        (3,4)
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = sect       (5,6)
        inc     hl
        ld      a, (hl)                 ; A  = op         (7)
        push    bc                      ; buf
        ld      bc, #0                  ; 上位語 = 0
        jr      rw_common

_disk_raw_rw32::
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; BC = buf        (2,3)
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = sect 下位語 (4,5)
        inc     hl
        push    bc                      ; buf
        ld      c, (hl)
        inc     hl
        ld      b, (hl)                 ; BC = sect 上位語 (6,7)
        inc     hl
        ld      a, (hl)                 ; A  = op         (8)

rw_common:
        ; ---- 1 セクタ転送は割り込み禁止で囲む(#63、TK: プリミティブ操作の保証)----
        ;   CS Low → コマンド → 512B → CS High の途中でプリエンプトされると、
        ;   SPI バスを共有する相手(将来の ESP 等)が割り込んだ瞬間に壊れる。
        ;   呼び出し元がすでに di のこともある(起動途中)ので、無条件に ei せず
        ;   IFF2 を `ld a,i` の P/V で保存して元へ戻す(Z84C00 = CMOS なので
        ;   NMOS 版の「ld a,i 中の割り込みで IFF2 を読み違える」問題は無い)。
        ;   代償: 1 セクタ ≒ 28ms 割り込みが止まり tick が遅れる(時計は ntpdate
        ;   等の補正前提。#59 の TK 判断)。FT245 受信は ~RXF がレベルなので
        ;   失われず遅れるだけ。cpmsim の「di 中の FatFs 禁止」は z80pack の
        ;   FDC の話で、ここ(z80board の SD)には当たらない。
        ;   BC:DE(アドレス)と HL を空けておくため、op と IFF はスタックで持つ。
        push    af                      ; op              スタック: buf op
        ld      a, i                    ; P/V = IFF2
        push    af                      ; 入る前の IFF    スタック: buf op iff
        di
        ; SDSC ならブロック番号 → バイトアドレス(×512)(#54)
        ld      a, (_sd_is_hc)
        or      a
        jr      nz, 2$
        ld      a, #9
1$:     sla     e
        rl      d
        rl      c
        rl      b
        dec     a
        jr      nz, 1$
2$:     pop     hl                      ; HL = iff        スタック: buf op
        pop     af                      ; A  = op         スタック: buf
        ex      (sp), hl                ; HL = buf        スタック: iff
        or      a
        jr      nz, 3$
        call    sd_data_read
        jr      4$
3$:     call    sd_data_write
4$:     ld      l, a                    ; L = 結果
        pop     af                      ; P/V = 入る前の IFF2
        jp      po, 5$                  ; 元が di なら di のまま
        ei
5$:     ld      h, #0
        ret
