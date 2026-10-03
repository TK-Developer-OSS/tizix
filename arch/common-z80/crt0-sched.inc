;===================================================================
; arch/common-z80/crt0-sched.inc - スケジューラ本体と _kexit(z80pack / z80board 共通)
;
;   arch/<arch>/crt0.s の ISR の中で .include する。**AF を積んだ直後**の位置に置くこと:
;     z80pack    isr:        push af  → ここ
;     z80board   isr_timer:  (isr: で push af 済み。~RXF の受信ドレインは arch 側で済ませて) → ここ
;   残りの 5 レジスタを積み、tick を進め、SP を sp_tbl に控え、次の runnable ブロックを
;   選んで、その SP から 6 レジスタを降ろして reti する。
;
;   途中から入れるラベル:
;     tick_and_save  6 レジスタを積み終えた状態で入る(tick を進める)
;     isr_save       同上だが tick は数えない。z80board の kyield_entry(KYIELD =
;                    call 0x006D)がここへ合流する ── 時間を進めるのはタイマ割込みだけ(#71)
;     sched_pick     退避せずに次を選ぶ。_kexit(死んだ文脈は捨てる)が使う
;===================================================================

        push    bc
        push    de
        push    hl
        push    ix
        push    iy
tick_and_save:
        call    _plt_interrupt        ; ticks++ (既存の C ハンドラ)
isr_save:
        ; sp_tbl[current] = SP
        ld      a, (KW_CURRENT)
        add     a, a
        add     a, #0x08
        ld      e, a
        ld      d, #0x84
        ld      hl, #0
        add     hl, sp
        ex      de, hl
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ; pick: 次の runnable ブロックへ。
        ;   ローテは block0..7 全周(block0=idle を含む)。
        ;   走らせないもの: pid == 0(free)/ PID_DRIVER(block1 の DRIVER 予約、飛び地)/
        ;   PID_CONT(4KB 超プロセスの継続枠)/ PID_PIPEBUF(カーネルパイプのバッファ枠)/
        ;   PID_BAD(起動時メモリチェックで不良)/ blocked[block] != 0(proc_block 中)。
        ;   block0(idle) は常時占有(pid=1)で blocked 判定からも除外するので、
        ;   pick は必ず runnable を見つける(無限ループにならない)。
sched_pick:
        ld      a, (KW_CURRENT)
rr_lp:
        inc     a
        cp      #8                    ; block0..7 を巡回(block7 はプロセス枠に復帰)
        jr      c, rr_chk
        xor     a                     ; wrap to 0 (block0=idle をローテに含む)
rr_chk:
        ld      b, a                  ; b = 候補ブロック番号(スクラッチ。rr_ok/rr_next で復帰。
                                      ;   ISR/_kexit いずれも直後に pop bc するので破壊可)
        ld      l, a
        ld      h, #0x84
        ld      a, (hl)               ; a = pid_tbl[block]
        or      a
        jr      z, rr_next            ; pid==0(free) → 飛ばす
        cp      #PID_DRIVER
        jr      z, rr_next            ; pid==PID_DRIVER(block1予約) → 飛ばす
        cp      #PID_CONT
        jr      z, rr_next            ; pid==PID_CONT(4KB超プロセスの継続枠) → 飛ばす
        cp      #PID_PIPEBUF
        jr      z, rr_next            ; pid==PID_PIPEBUF(#27 カーネルパイプ 4KB 枠) → 飛ばす
        cp      #PID_BAD
        jr      z, rr_next            ; pid==PID_BAD(#64 起動時メモリチェックで不良)→ 飛ばす
        ; block0(idle/シェル)は常に runnable ── blocked 判定から除外し、
        ; 全ブロックが blocked でも pick が必ず戻る保証にする。
        ld      a, b
        or      a
        jr      z, rr_ok
        ; blocked[block] != 0 → proc_block 中。飛ばす (b = block番号, 1..7)
        add     a, #0x29             ; (KW_BLOCKED & 0xFF)。block<8 で桁上がり無し
        ld      l, a
        ld      h, #0x85             ; (KW_BLOCKED >> 8)
        ld      a, (hl)
        or      a
        jr      nz, rr_next
        jr      rr_ok                 ; それ以外 = runnable
rr_next:
        ld      a, b
        jr      rr_lp
rr_ok:
        ; --- 実機プローブ(z80board。無効化中): 切り替え先ブロック番号を最初の 16 回だけ出す ---
        ;   使うときは _DATA に isr_probe_n(.ds 1)を足して下を生かす。
        ;ld      a, (isr_probe_n)
        ;cp      #16
        ;jr      nc, rr_probe_done
        ;ld      a, b
        ;add     a, #'0'
        ;out     (0x01), a
;rr_probe_done:
        ld      a, b
        ld      (KW_CURRENT), a
        ; restore: SP = sp_tbl[current]
        add     a, a
        add     a, #0x08
        ld      l, a
        ld      h, #0x84
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl
        ld      sp, hl
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ; タイマ割込み / KYIELD(call 0x006D)のどちらから来た場合も ei してから reti。
        ei
        reti

; ---- _kexit : コマンド終了。現ブロックを解放し次へ切替 --------------
;   コマンドの crt0cmd が main 復帰後に jp _kexit してくる(低位ベクタ 0x003B 経由)。
;   save は行わない(死んだ文脈は破棄)。pid_tbl[current]=0 にして
;   sched_pick へ落ちる。block0(idle) は常時占有なので pick は必ず
;   runnable を見つける(無限ループにならない)。
_kexit:
        di
        ld      a, (KW_CURRENT)
        ld      l, a
        ld      h, #0x84              ; hl = &pid_tbl[current]
        ld      (hl), #0              ; 先頭ブロック解放 (0=free)
        ; --- 継続ブロック(PID_CONT)を末尾まで解放 ---
kx_cont:
        inc     l
        ld      a, l
        cp      #8
        jr      nc, kx_done          ; pid_tbl[] は 0..7。範囲外は打ち切り
        ld      a, (hl)
        cp      #PID_CONT
        jr      nz, kx_done          ; 継続ブロックでない → 終わり
        ld      (hl), #0             ; 継続ブロック解放
        jr      kx_cont
kx_done:
        jp      sched_pick           ; save を飛ばして次を pick→restore
