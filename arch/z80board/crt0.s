.module crt0

;===================================================================
; arch/z80board crt0.s(実機 Z80 ボード。ROM 0x0000 から直接実行)
;
;   このファイルにあるのは z80board に固有の部分だけ:
;     ・リセット入口と低位ベクタ(0x0038〜0x006F)を **ROM 上に .org で焼く**
;       (z80pack は RAM 上なので実行時に書き込む)
;     ・ラッチと UART(FT245RL)の立ち上げ
;     ・ISR の入口: Z80_INT の要因(FT245 の受信 / タイマ)の振り分けと、
;       KYIELD の入口(call 0x006D)
;   ボードに依らない部分(宣言と定数、RAM とスケジューラ表の初期化、スケジューラ
;   本体と _kexit、エリア順と gsinit)は ../common-z80/ にあり、z80pack と共有する。
;   .include した位置にそのままコードが出る。低位ベクタの番地は z80pack と
;   揃えてある(コマンド / DRIVER / tzcc が -g で直接叩く)。
;===================================================================

        .include "../common-z80/crt0-defs.inc"

; ---- FT245 受信リング(#59。kmem.h KW_RX* と一致させること) ----
KW_RXHEAD     = 0x8D96
KW_RXTAIL     = 0x8D97
KW_RXBUF      = 0x9F00          ; #87: 256B、ページ境界(下位バイト 0)。block1 の末尾
KW_RXBUF_SIZE = 256

; ======================================================================
; ROM 版 IVT: z80pack crt0.s は起動直後に 0x0038〜0x006C へ `jp target` を
; 自己書き換え(ld (addr),a / ld (addr+1),hl)で焼くが、実機は ROM なので
; 書き込めない。等価な内容をリンク時に直接この番地へ配置する。
; RST 0(reset)と RST 0x38(IM1 割り込み)はハードウェア固定、それ以外の
; ソフトベクタ(0x003B〜)は tizix 独自の取り決め(z80pack と番地を揃える)。
; ======================================================================
        .area   _HEADER (ABS)

        .org    0x0000
        jp      start

        .org    0x0038
        jp      isr

        .org    0x003B
        jp      _kexit
        .org    0x003E
        jp      _kputchar
        .org    0x0041
        jp      _kgetchar
        .org    0x0044
        jp      _getticks
        .org    0x0047
        jp      _kprintf
        .org    0x004A
        jp      _time_get
        .org    0x004D
        jp      _time_set

        ; --- SDCC 4.x helper (0x0050: jp (hl)) ---
        .org    0x0050
        jp      (hl)
        ret

        ; --- libivt ランタイム IVT (0x0052〜0x006C, z80pack と同番地) ---
        .org    0x0052
        jp      __divuint
        .org    0x0055
        jp      __moduint
        .org    0x0058
        jp      __divsint
        .org    0x005B
        jp      __modsint
        .org    0x005E
        jp      __divulong
        .org    0x0061
        jp      __mullong
        .org    0x0064
        jp      __mulint
        .org    0x0067
        jp      _memcmp
        .org    0x006A
        jp      _strcmp

        ; --- KYIELD 専用入口 (#59) ---
        ;   0x0038 の IM1 ハード割り込みとは別の入口。KYIELD() マクロが
        ;   `call 0x006D` で入る(rst は 8 個の固定番地しか無く 0x006D には
        ;   置けないため call にした)。SYS_STAT_PORT 判別はせず、di の後
        ;   tick++ と save/pick/restore を行う(isr_timer と共有)。
        .org    0x006D
        jp      kyield_entry
        ; 0x0070 以降が空き地(memory: 低位ベクタはハードウェア用)。

        .area   _CODE
start:
        di
        ; SP = 0x9000(block0 後端 +1)。z80pack と同じスタック枠の約束
        ; (kmem.h 参照。詳細コメントは arch/z80pack/crt0.s 側)。
        ; ★下のブリングアップは push / call を使うので、その前に張る。
        ;   以前は crt0_bringup_done で張っていて、それまで SP はリセット直後の
        ;   不定値のまま使われていた(z80boardsim の TZSIM_TRACE が ROM 領域
        ;   0x0E6F への push として検出。task.md #75)。実機 Z80 は SP=FFFF で
        ;   上がることが多いので表面化していなかったと推測。
        ld      sp, #0x9000
        ; #62: ラッチ 0x80(SD と ESP の相乗り)を最初にアイドル値へ。電源投入直後の
        ; 574 は不定で、bit4=0 だと ESP がリセットに張り付き、bit0=0 だとブレークを
        ; 見せる(TK メモ)。0xDF = MOSI=1 CS=1 SCK=0 ~RST=1 CTS=1 TX=1。
        ld      a, #0xDF
        out     (0x80), a

        ;-------------------------------------------------------
        ; UART(FT245RL)ブリングアップシーケンス
        ;-------------------------------------------------------
        ; 実機で動作確認済みの boot.s(単体モニタ)の start:〜main: 冒頭を
        ; ほぼそのまま移植したもの(SD read/hex_dump/ブートストラップへの
        ; ジャンプは kernel に不要なので除く)。
        ;   ・実機はデコードを 74HC138(A4-6のみ判定)で行っており、FT245RL
        ;     自体は 0x00/0x01 を区別しない。リセット直後の不定/ごみは
        ;     避けようがない前提。
        ;   ・FT245 の USB 接続は Z80 の給電と兼用のため、電源投入から
        ;     Windows が USB デバイスを認識し TeraTerm 等が COM ポートを
        ;     開けるようになるまで実時間で数秒かかる。
        ;   ・上記のどちらが実際に効いているか(アナログ的偶発挙動含め)
        ;     未解明なため、原因を切り分けず boot.s の実績シーケンスを
        ;     丸ごと踏襲する(カウントダウンの16進表示も含めて同一)。
        ; ポート番号は src/include/hw.h の CON_PORT/SYS_STAT_PORT と
        ; 一致させること。
        ld      b, #40
uart_init_flush:
        ld      a, #0x0d
        out     (0x01), a
        ld      a, #0x0a
        out     (0x01), a
        djnz    uart_init_flush

        ld      a, #0x1b        ; ESC [ 2 J (画面クリア)
        out     (0x01), a
        ld      a, #0x5b
        out     (0x01), a
        ld      a, #0x32
        out     (0x01), a
        ld      a, #0x4a
        out     (0x01), a
        ld      a, #0x1b        ; ESC [ H (カーソルをホームへ)
        out     (0x01), a
        ld      a, #0x5b
        out     (0x01), a
        ld      a, #0x48
        out     (0x01), a

        ; boot.s の outer_delay_loop と同じ(50 x 50000)。各回 B レジスタの
        ; 値を16進2桁で表示しながら待つ(進捗が見える実績のある形をそのまま
        ; 踏襲)。
        ld      b, #50
crt0_countdown_loop:
        ld      de, #50000

        push    af
        push    bc
        push    de
        push    hl

        ld      a, b
        call    crt0_hex_to_ascii
        ld      a, d
        out     (0x01), a
        ld      a, e
        out     (0x01), a
        ld      a, #0x0d
        out     (0x01), a
        ld      a, #0x0a
        out     (0x01), a

        pop     hl
        pop     de
        pop     bc
        pop     af

crt0_countdown_inner:
        dec     de
        ld      a, d
        or      e
        jr      nz, crt0_countdown_inner
        djnz    crt0_countdown_loop

        ; boot.s の main: 冒頭(画面クリア+バナー)を移植。
        ld      b, #80
crt0_clear_loop:
        ld      a, #0x0d
        out     (0x01), a
        ld      a, #0x0a
        out     (0x01), a
        djnz    crt0_clear_loop

        ld      a, #0x1b        ; ESC [ 2 J
        out     (0x01), a
        ld      a, #0x5b
        out     (0x01), a
        ld      a, #0x32
        out     (0x01), a
        ld      a, #0x4a
        out     (0x01), a
        ld      a, #0x1b        ; ESC [ H
        out     (0x01), a
        ld      a, #0x5b
        out     (0x01), a
        ld      a, #0x48
        out     (0x01), a

        ld      hl, #crt0_str_booting
        call    crt0_puts

        ld      hl, #crt0_str_status1
        call    crt0_puts

        ; system_status(boot.s の system_status_print 相当)。
        ; 0x10 は 74LS573 ラッチで LE は OUT の間しか開かないので、**書いてから
        ; 読む**(書かずに読むと電源投入時のゴミ)。書く値の bit0 は ROMKILL
        ; (1 で ROM が消える)なので必ず 0。読める意味のあるビットは
        ; bit6 = FT245 RXF#、bit5 = TXE#、bit7 = 0(GND)。bit0-4 は浮いて 1。
        xor     a
        out     (0x10), a
        in      a, (0x10)
        push    af
        rrca
        rrca
        rrca
        rrca
        and     #0x0f
        call    crt0_nibble_to_hex_out
        pop     af
        and     #0x0f
        call    crt0_nibble_to_hex_out

        ld      hl, #crt0_str_status2
        call    crt0_puts

        ld      hl, #crt0_str_loadmbr
        call    crt0_puts

        jr      crt0_bringup_done

;--------------------------------------------------------
; boot.s 移植分のヘルパ・文字列(SD read/hex_dump/jump は含まない)
;--------------------------------------------------------
crt0_puts:
        push    af
crt0_puts_loop:
        ld      a, (hl)
        or      a
        jr      z, crt0_puts_end
        out     (0x01), a
        inc     hl
        jr      crt0_puts_loop
crt0_puts_end:
        pop     af
        ret

crt0_hex_to_ascii:
        push    af
        and     #0xf0
        rrca
        rrca
        rrca
        rrca
        call    crt0_nibble_to_ascii
        ld      d, a
        pop     af
        and     #0x0f
        call    crt0_nibble_to_ascii
        ld      e, a
        ret

crt0_nibble_to_ascii:
        cp      #0x0a
        jr      c, crt0_nibble_is_num
        add     a, #'A' - 0x0a
        ret
crt0_nibble_is_num:
        add     a, #'0'
        ret

crt0_nibble_to_hex_out:
        call    crt0_nibble_to_ascii
        out     (0x01), a
        ret

crt0_str_booting:
        .ascii  "BOOTING NOW !"
        .db     0x0d, 0x0a, 0x0d, 0x0a, 0x00
crt0_str_status1:
        .ascii  "SYSTEM STATUS CODE ["
        .db     0x00
crt0_str_status2:
        .ascii  "]"
        .db     0x0d, 0x0a, 0x00
crt0_str_loadmbr:
        .ascii  "LOAD MBR..."
        .db     0x0d, 0x0a, 0x00

crt0_bringup_done:
        ; SP は start で張り済み(#75)。

        ; --- ここから共通: _DATA のゼロ埋め / gsinit / PCB 初期化 / メモリチェック ---
        .include "../common-z80/crt0-init.inc"

        ; FT245 受信リング(#59)の head/tail を 0 クリア。ゴミのまま
        ; head!=tail になっていると起動直後に kgetchar() が幽霊バイトを
        ; 返す(バッファ本体は head==tail の間は露出しないのでクリア不要)。
        xor     a
        ld      (KW_RXHEAD), a
        ld      (KW_RXTAIL), a

        ; 割り込みモードを明示。リセット直後の Z80 は IM0 で、IM0 では
        ; 割り込み応答サイクルでデータバスから命令を拾う(未駆動なら 0xFF =
        ; RST 38h となって結果的に 0x0038 へ飛ぶ)── たまたま IM1 と同じ
        ; 挙動になるだけで保証は無いので、実機では明示的に im 1 を置く。
        ; ei は kernel_init(src/kernel.c)側で撃つ(#59。Z80_INT は FT245
        ; ~RXF と PIC GP5(タイマ、Timer0 オーバーフローでトグル)の
        ; ダイオード OR。isr が SYS_STAT_PORT で要因を判別する)。
        im      1

        call    _init
        halt

; ---- 0x0038 IM1 割り込み ISR (#59 GP5 タイマ配線後) ---------------------
;   2026-09-19: GP4 は Z80 HALT→PIC のセンス入力で Z80_INT には無関係
;   (訂正済み)。本物のタイマー信号は PIC の GP5(Timer0 オーバーフローで
;   トグル、実測 500us/500us の 1ms 周期)で、これをダイオードで
;   D1(FT245 ~RXF)と同じ並びに足し、Z80_INT へワイヤード OR 配線した。
;   CPU クロックは水晶+74HC04 で実測 8MHz(PIC の PWM 経由という当初の
;   前提は誤りだったので削除)。
;
;   Z80_INT の要因は SYS_STAT_PORT(0x10)の bit6(~RXF)で判別する:
;     bit6==0(~RXF Low = FT245 に受信データあり) → 1 バイトだけ
;       KW_RXBUF(kmem.h)へドレインして戻る(tick は数えず、save/pick/
;       restore もしない。#33 con_ung と同じ「読んで取っておくだけ」)。
;       バーストは ~RXF がレベルのままなので isr が即座に再入して続きを抜く。
;     bit6==1(~RXF High) → GP5(タイマ)起因とみなし isr_timer へ。
;       ticks++ の後 save/pick/restore(z80pack と同一アルゴリズム)。
;   GP5 は 500us Low が続くレベル信号なので、Low の間 isr が連続で再入し
;   tick が一度にまとまって進む見込み(実測 8MHz からの概算、厳密な検証
;   はまだ)。ntpdate 等の定期補正が前提なので TICK_HZ の精密較正は
;   後回しでよい、とのユーザー判断(#59)。
isr:
        push    af
        xor     a
        out     (0x10), a       ; ラッチ LE を開く(bit0=ROMKILL は必ず0を書く)
        in      a, (0x10)
        bit     6, a
        jr      nz, isr_timer   ; ~RXF High → GP5(タイマ)起因

        ; --- ~RXF Low: FT245 受信ドレイン(tick は数えない・context switch 無し) ---
        ;   割り込まれたプログラムのレジスタは AF と HL しか退避していないので、
        ;   ここでは **AF と HL 以外に触ってはいけない**。以前は BC を壊す
        ;   サブルーチンを呼んでいて、SPI 転送中(B=ビット数, C=ポート)に
        ;   割り込まれると SD RD ERR / sh.bin ロード失敗 / ゴミ出力になった。
        ;   ISR 実行中は消費側が走らない(単一 CPU・割り込み禁止)ので、
        ;   head 更新とデータ書き込みの順序は問わない。
        ;   #87: KW_RXBUF は 256B でページ境界(下位バイト 0)。head をそのまま
        ;   下位バイトに使え、inc の 8bit 桁あふれで自然に一周する(and / add 不要)。
        push    hl
        ld      a, (KW_RXHEAD)
        ld      l, a                    ; l = 旧 head(書き込み位置 = RXBUF の下位バイト)
        inc     a                       ; 256 で自然に一周
        ld      (KW_RXHEAD), a          ; 満杯チェックは持たない(ROM 優先)
        ld      h, #>(KW_RXBUF)
        in      a, (0x01)               ; CON_PORT: 受信バイトを取得
        ld      (hl), a                 ; RXBUF[旧 head] = 受信バイト
        pop     hl
        pop     af
        ei
        reti

; ---- kyield_entry: KYIELD() 専用入口(0x006D から call で入る、#59) -----
;   SYS_STAT_PORT 判別はしない。isr_timer と違い af もここで自分で
;   push する(isr: 経由ではないため)。6 レジスタ退避の後 isr_save へ
;   合流し、save/pick/restore を isr_timer と共有する。
;
;   ★tick は数えない(2026-09-24 #71 で修正)。以前は tick_and_save へ
;   合流して ticks++ していた ── タイマ未結線だった頃(#54)は「譲るたびに
;   時間を進める」以外に時間が進む手段が無かったための名残。#59 で GP5 の
;   実タイマが入った後もそのままだったので、getticks()(中で KYIELD する)を
;   回すだけで tick が進み、getc_timeout(10) が 10 回ポーリングした時点で
;   抜け(vi / sh のカーソルキー ESC [ x が 3 つの別キーに化ける)、
;   sleep N がほぼ即座に終わっていた。時間を進めるのは isr_timer だけにする。
;
;   di が必須: 呼び出し元(io.c の kgetchar/con_break 等)は IRQ_ON()(ei)
;   した直後に KYIELD() を呼ぶ。KYIELD は rst ではなく call なので、
;   isr: とは違って CPU が自動では割り込みを禁止しない。di を置かずに
;   push を6個並べると、その途中に本物の割り込み(GP5/~RXF)が割り込んだ
;   場合、isr_save が「push 途中の中途半端な SP」を sp_tbl に保存して
;   しまい、後で pop 6 個 + reti が食い違ってワイルドジャンプする
;   (実機で無操作でも散発するリセット/ガベージの実バグとして発覚、#59)。
kyield_entry:
        di
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        jr      isr_save                ; tick は数えない(上の ★)

; ---- isr_timer: GP5 起因。ticks++ の後 save/pick/restore ----------------
;   ここから先は z80pack と共通(../common-z80/crt0-sched.inc)。af は isr: で
;   既に push 済みなので、共通部分が残り 5 レジスタを積んで tick を進める。
;   上の kyield_entry が飛び込む isr_save も共通部分の中にある。
isr_timer:
        .include "../common-z80/crt0-sched.inc"

        .include "../common-z80/crt0-areas.inc"
