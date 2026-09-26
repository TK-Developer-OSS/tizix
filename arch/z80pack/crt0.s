.module crt0

        .globl  _init
        .globl  _plt_interrupt
        .globl  _kprintf          ; io.c: 書式変換(0x0047)
        .globl  _kputchar         ; io.c: 1バイト出力/物理層内包(0x003E)
        .globl  _kgetchar         ; io.c: 1バイト入力/物理層内包(0x0041)
        .globl  _kexit            ; スケジューラ版 _kexit (0x003B, コマンドが jp してくる)
        .globl  _getticks         ; 100Hz tick 取得 (0x0044, コマンドの delay import)
        .globl  _time_get         ; kernel.c: Unix 秒取得 (0x004A, DATE.BIN import)
        .globl  _time_set         ; kernel.c: Unix 秒設定 (0x004D, DATE.BIN import)
        ; ---- ABI統一ランタイム(libivt)を固定IVT番地に公開 (0x0052〜) ----
        ;   sdcccall(0) の外部コマンド/ドライバが -g __divuint=0x0052 等でリンク。
        ;   実体は libivt/*.rel(カーネルROM内)。ここでは boot 時に jp を張るため
        ;   グローバル参照して実体をリンクに取り込む(未使用でも常駐させる)。
        .globl  __divuint
        .globl  __moduint
        .globl  __divsint
        .globl  __modsint
        .globl  __divulong
        .globl  __mullong
        .globl  __mulint
        .globl  _memcmp
        .globl  _strcmp
        .globl  l__INITIALIZER
        .globl  s__INITIALIZER
        .globl  s__INITIALIZED
        .globl  s__DATA
        .globl  l__DATA

; ---- scheduler PCB (block0 work area, kmem.h と一致させること) ----
KW_PIDTAB  = 0x8400        ; pid_tbl[block] 0..7 (8B). 0=free
KW_SPTBL   = 0x8408        ; sp_tbl[block]  0..7 (2B each)
KW_CURRENT = 0x8418        ; current running block (1B)
KW_OUTROUTE= 0x851A        ; out_route[block] 0..7 (8B)。ROUTE_CONSOLE/DISCARD/PIPE
KW_BLOCKED = 0x8529        ; blocked[block] 0..7 (8B). 1=proc_block 中 → sched が飛ばす
KW_WAKEPEND= 0x8531        ; wakepend[block] 0..7 (8B). proc_block 前の wake 取りこぼし対策

; ---- PCB pid の予約値 (kmem.h の PID_* と一致させること) ----
;   0    = free  (kexec が掴んでよい空きブロック)
;   1    = idle  (block0, 常時 runnable)
;   0xFE = DRIVER(block1 常駐ドライバ予約。sched も kexec も明示的に飛ばす。
;                 走らない=コンテキストを持たない飛び地。0=free と区別する
;                 ことで「なぜ block1 にプロセスが載らないか」をコードに残す)
PID_DRIVER  = 0xFE
PID_CONT    = 0xFD        ; 4KB 超プロセスの継続ブロック(kmem.h/kexec.c と一致)
PID_PIPEBUF = 0xFC        ; #27: カーネルパイプの 4KB バッファブロック(kmem.h/pipe.c と一致)
PID_BAD     = 0xFB        ; #64: 起動時メモリチェックで不良だったブロック(kmem.h と一致)

; ---- 0x0100 : 最小トランポリン (boot が jp してくる固定入口) --------
; start 本体は _CODE に置く。_HEADER を 3B に固定し、boot コードが
; 何バイト伸びても --code-loc 0x0120 の _CODE と衝突しないようにする。
		.area   _HEADER (ABS)
        .org    0x0100
        jp      start

        .area   _CODE
start:
        di
        ; SP = 0x9000(block0 後端 +1)から下へ。カーネルスタックは block0 内。
        ; block1 は DRIVER 常駐(飛び地)、block7 はプロセス枠。どちらもカーネル
        ; スタックには使わない(スタックは block0 内で完結)。
        ;
        ; スタック枠は 512B(0x8E00-0x8FFF)を運用上の約束とする(kmem.h 参照)。
        ; 0x8419-0x8DFF は VFS/socket/将来枝データに開放済み。SP 初期値自体は
        ; 0x9000 のまま(512B 枠の頂上)なのでここに変更は無い。実測上カーネル
        ; 文脈の最深は FatFs(FF_FS_TINY=1: FIL=34B/DIR=40B, cp で FIL×2=68B)+
        ; ff.c 呼出深で 1KB 未満。512B を大きく超えて潜ると 0x8DFF から下の
        ; データを破壊するので、深い再帰・ISR 多重ネストには注意。溢れたら
        ; SP 起点を上げる(枠拡大)かデータ配置を下げること。
        ld      sp, #0x9000
        ld      a, #0xC3
        ld      (0x0038), a
        ld      hl, #isr
        ld      (0x0039), hl

        ; --- tizix ソフトウェア割り込みベクタ (ISR 直後 0x003B〜に集約) ---
        ;   下位 0x0000-0x0037 は将来デバイス用に完全解放。カーネルへの
        ;   全インポート入口をここへ集める。番地は固定(kputchar が .map に
        ;   依存しないよう、ISR 直後の空き地に体系的に確保)。
        ;   起点 0x003B = ISR(0x0038-0x003A, jp isr)の直後。
        ;
        ;     0x003B : jp _kexit     (コマンド終了)
        ;     0x003E : jp _kputchar  (物理層直結。1バイト出力。ドライバ/
        ;                             外部コマンドが叩く固定番地)
        ;     0x0041 : jp _kgetchar  (物理層直結。1バイト入力)
        ;     0x0044 : jp _getticks  (100Hz tick、delay 用)
        ;     0x0047 : jp _kprintf   (書式変換。putchar を使う上位)
        ;
        ;   コマンド/ドライバは -g _kputchar=0x003E ... でリンク。reloc は
        ;   絶対のまま(base を足さない)。
        ld      a, #0xC3               ; JP
        ld      (0x003B), a
        ld      hl, #_kexit
        ld      (0x003C), hl
        ld      a, #0xC3
        ld      (0x003E), a
        ld      hl, #_kputchar
        ld      (0x003F), hl
        ld      a, #0xC3
        ld      (0x0041), a
        ld      hl, #_kgetchar
        ld      (0x0042), hl
        ld      a, #0xC3
        ld      (0x0044), a
        ld      hl, #_getticks
        ld      (0x0045), hl
        ld      a, #0xC3
        ld      (0x0047), a
        ld      hl, #_kprintf
        ld      (0x0048), hl

        ; --- 時刻ベクタ(0x004A〜。epoch は 100Hz 積算、外部 DATE.BIN が叩く) ---
;     0x004A : jp _time_get  (現在 Unix 秒 32bit を返す。戻り DEHL)
;     0x004D : jp _time_set  (Unix 秒 32bit を設定。引数 DEHL)
;   カーネルは epoch を刻んで渡すだけ。変換(除算)は DATE.BIN 側。
ld      a, #0xC3
ld      (0x004A), a
ld      hl, #_time_get
ld      (0x004B), hl
ld      a, #0xC3
ld      (0x004D), a
ld      hl, #_time_set
ld      (0x004E), hl

; --- SDCC 4.x helper (0x0050: jp (hl)) ---
ld      a, #0xE9
ld      (0x0050), a
ld      a, #0xC9
ld      (0x0051), a

        ; --- libivt ランタイム IVT (0x0052〜0x006C) ---
        ;   ABI統一(--sdcccall 0)で標準ライブラリと衝突する除算/乗算/剰余/
        ;   memcmp/strcmp を自前供給し、固定番地で共有する。番地は kmem.h/
        ;   外部コマンドの -g とここだけで管理(体系的に 3B 刻み)。
        ;     0x0052 __divuint  0x0055 __moduint   (16bit unsigned / %)
        ;     0x0058 __divsint  0x005B __modsint   (16bit signed   / %)
        ;     0x005E __divulong 0x0061 __mullong   (32bit unsigned / *)
        ;     0x0064 __mulint                      (16bit *)
        ;     0x0067 _memcmp    0x006A _strcmp
        ld      a, #0xC3
        ld      (0x0052), a
        ld      hl, #__divuint
        ld      (0x0053), hl
        ld      a, #0xC3
        ld      (0x0055), a
        ld      hl, #__moduint
        ld      (0x0056), hl
        ld      a, #0xC3
        ld      (0x0058), a
        ld      hl, #__divsint
        ld      (0x0059), hl
        ld      a, #0xC3
        ld      (0x005B), a
        ld      hl, #__modsint
        ld      (0x005C), hl
        ld      a, #0xC3
        ld      (0x005E), a
        ld      hl, #__divulong
        ld      (0x005F), hl
        ld      a, #0xC3
        ld      (0x0061), a
        ld      hl, #__mullong
        ld      (0x0062), hl
        ld      a, #0xC3
        ld      (0x0064), a
        ld      hl, #__mulint
        ld      (0x0065), hl
        ld      a, #0xC3
        ld      (0x0067), a
        ld      hl, #_memcmp
        ld      (0x0068), hl
        ld      a, #0xC3
        ld      (0x006A), a
        ld      hl, #_strcmp
        ld      (0x006B), hl

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
        ;   24KB で実機 8MHz 約 0.2 秒。z80pack / z80board の crt0.s で同じ手順。
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

        call    _init
        halt

; ---- 0x0038 timer ISR : ticks++ の後に save/pick/restore を挿す ----
isr:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        call    _plt_interrupt        ; ticks++ (既存の C ハンドラ)
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
        ;   飛ばす条件を 2 つ、明示的に判定する:
        ;     pid == 0(free)        : プロセスが載っていない空きブロック
        ;     pid == PID_DRIVER     : block1 の DRIVER 予約(飛び地、走らない)
        ;   block0(idle) は常時占有(pid=1)なので、pick は必ず runnable を
        ;   見つける(無限ループにならない)。
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
        ei
        reti

; ---- _kexit : コマンド終了。現ブロックを解放し次へ切替 --------------
;   コマンドの crt0cmd が main 復帰後に jp _kexit してくる。
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

        .area   _HOME
        .area   _CODE
        .area   _INITIALIZER
        .area   _GSINIT
        .area   _GSFINAL

        .area   _DATA
        .area   _INITIALIZED
        .area   _BSEG
        .area   _BSS
        .area   _HEAP

        .area   _GSINIT
gsinit::
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      Z, gsinit_next
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
gsinit_next:

        .area   _GSFINAL
        ret
