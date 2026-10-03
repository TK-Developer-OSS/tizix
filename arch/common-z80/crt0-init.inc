;===================================================================
; arch/common-z80/crt0-init.inc - RAM とスケジューラ表の初期化(z80pack / z80board 共通)
;
;   arch/<arch>/crt0.s の start: の中、ボードの立ち上げ(ベクタの設置 / UART の
;   ブリングアップ)が済んだ位置で .include する。SP は張り済みであること。
;   続けて arch 側が call _init する。
;   ローカルラベル 1$ / 2$ / 3$ を使う(直前の通常ラベルからの範囲で有効)。
;===================================================================

        ; --- 未初期化 static (_DATA 域) をゼロクリア ---------------------
        ;   SDCC 標準 crt0 が必ずやる処理。この自作 crt0 は gsinit(初期値
        ;   コピー)しか持たず、_DATA を放置していた。実機 SRAM は電源 ON 時
        ;   ゴミなので、ff.c の FatFs[](登録済み FATFS* ポインタ)や FATFS fs
        ;   の各フィールドがゴミ値で起動する。fat_init→f_mount が
        ;     cfs = FatFs[0];  if (cfs) cfs->fs_type = 0;
        ;   を実行し、ゴミポインタを deref してカーネルコードへ 0 を書き込む
        ;   → 以降その関数がワイルドジャンプ。cpmsim では malloc の
        ;   ゼロページ次第でフレークに見えるが、実機では毎回踏む。
        ;   _INITIALIZED (s__DATA+l__DATA〜) は別エリアで直後の gsinit が
        ;   埋めるため、ここで消すのは _DATA 本体だけ。
        ld      bc, #l__DATA
        ld      a, b
        or      a, c
        jr      Z, 1$
        ld      hl, #s__DATA
        ld      (hl), #0x00
        dec     bc
        ld      a, b
        or      a, c
        jr      Z, 1$
        ld      de, #s__DATA + 1
        ldir
1$:

        call    gsinit

        ; --- scheduler: PCB 初期化 ---
        ;   実機 RAM は起動時ゴミ。pid_tbl[1..7] を 0(free) にクリアし、
        ;   block0 を idle(占有)、block1 を DRIVER 予約として登録する。
        ;   これを怠ると kexec が「空きブロック無し」や DRIVER 上書きになる。
        ;
        ;   ブロック割り当て(kmem.h と一致):
        ;     block0        = idle/シェル文脈      pid = 1        (常時 runnable)
        ;     block1        = DRIVER 常駐(飛び地)  pid = PID_DRIVER(sched/kexec が明示スキップ)
        ;     block2..7     = プロセス枠           pid = 0(free)  (kexec が掴む)
        xor     a
        ld      (KW_CURRENT), a        ; current = 0 (block0)
        ld      hl, #KW_PIDTAB + 2     ; &pid_tbl[2]
        ld      b, #6                  ; block 2..7 を 0 クリア(プロセス枠)
2$:
        ld      (hl), a                ; pid_tbl[n] = 0 (free)
        inc     hl
        djnz    2$
        ld      a, #1
        ld      (KW_PIDTAB + 0), a     ; pid_tbl[0] = 1 (idle, 常時 runnable)
        ld      a, #PID_DRIVER
        ld      (KW_PIDTAB + 1), a     ; pid_tbl[1] = PID_DRIVER (block1 予約)

        ; --- #64 起動時メモリチェック: プロセス枠 block2..7(0xA000-0xFFFF)---
        ;   1 バイトずつ 0x55 / 0xAA を書いて読み戻す(固着ビット・欠けたチップ)。
        ;   食い違ったブロックは pid_tbl を PID_BAD にして **二度と使わせない**
        ;   (kexec は非 0 を使用中とみなし、sched_pick は PID_BAD を飛ばす)。
        ;   /bin/free のマップに 'x' で出る。アドレス線の短絡までは見ない。
        ;   24KB で実機 8MHz 約 0.2 秒。
        ld      hl, #0xA000
        ld      de, #KW_PIDTAB + 2
mc_blk:
        ld      bc, #0x1000
mc_byte:
        ld      a, #0x55
        ld      (hl), a
        cp      (hl)
        jr      nz, mc_bad
        cpl                            ; 0xAA
        ld      (hl), a
        cp      (hl)
        jr      nz, mc_bad
        inc     hl
        dec     bc
        ld      a, b
        or      c
        jr      nz, mc_byte
        jr      mc_next
mc_bad:
        ld      a, #PID_BAD
        ld      (de), a                ; pid_tbl[n] = PID_BAD
        ld      a, h
        or      #0x0F
        ld      h, a
        ld      l, #0xFF
        inc     hl                     ; 残りを飛ばして次のブロックの先頭へ
mc_next:
        inc     de
        ld      a, h
        or      a                      ; block7 の後で HL は 0x0000 に一周する
        jr      nz, mc_blk

        ; 0x851A..0x8544 を 0 クリア(43B)。RAM ゴミ対策。内訳:
        ;   KW_OUTROUTE[8] (0x851A) : out_route。ゴミが ROUTE_PIPE(2) だと
        ;      kputchar が非アクティブな pipe_putc に飛んでスケジューラ状態を壊す。
        ;   0x8522-0x8528 : epoch/subtick/ticks(直後の _init→kernel_init で再設定。二重でも無害)
        ;   KW_BLOCKED[8] / KW_WAKEPEND[8] / kpipe ヘッダ(11B 全部)/ KW_CONRAW(0x8544)
        ld      hl, #KW_OUTROUTE
        ld      b, #43
        xor     a
3$:
        ld      (hl), a
        inc     hl
        djnz    3$
