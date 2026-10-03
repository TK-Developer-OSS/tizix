;===================================================================
; arch/common-z80/crt0-defs.inc - z80 系 crt0 の共通宣言(z80pack / z80board)
;
;   arch/<arch>/crt0.s が先頭で .include する。ボードに依らないもの ──
;   カーネル側シンボルの宣言と、block0 のワーク番地・pid の予約値 ── だけを置く。
;   crt0 の共通部分は 4 つの断片に分かれている(どれも arch の crt0.s から、
;   この順に .include される。置いた位置にそのままコードが出る):
;     crt0-defs.inc    宣言と定数(このファイル)
;     crt0-init.inc    _DATA のゼロ埋め / gsinit / PCB 初期化 / 起動時メモリチェック
;     crt0-sched.inc   ISR の後半(tick → SP 退避 → 次を選ぶ → 復帰)と _kexit
;     crt0-areas.inc   エリアの並び順と gsinit
;   ボードごとに違うもの(リセット入口とベクタの置き方、ボードの立ち上げ、
;   ISR の入口での要因の振り分け)は arch/<arch>/crt0.s に残してある。
;
;   sdasz80 に -D は無いが、.include と .if / .ifdef は使える。ここは .include
;   だけで足りている(共通部分がどちらのボードでも同じ順に並ぶので、分岐が要らない)。
;===================================================================

        .globl  _init
        .globl  _plt_interrupt
        .globl  _kprintf          ; io.c: 書式変換(0x0047)
        .globl  _kputchar         ; io.c: 1バイト出力/物理層内包(0x003E)
        .globl  _kgetchar         ; io.c: 1バイト入力/物理層内包(0x0041)
        .globl  _kexit            ; スケジューラ版 _kexit (0x003B, コマンドが jp してくる)
        .globl  _getticks         ; 100Hz tick 取得 (0x0044, コマンドの delay import)
        .globl  _time_get         ; kernel.c: Unix 秒取得 (0x004A, DATE.BIN import)
        .globl  _time_set         ; kernel.c: Unix 秒設定 (0x004D, DATE.BIN import)
        ; ---- ABI統一ランタイム(ivthelpers)を固定IVT番地に公開 (0x0052〜) ----
        ;   sdcccall(0) の外部コマンド/ドライバ/tzcc(TZVEC)が -g __divuint=0x0052 等で
        ;   この番地を直接叩くので、z80pack ⇔ z80board で 1 バイトもずらせない。
        ;   実体は src/ivthelpers.c(カーネル内)。ここでグローバル参照して実体を
        ;   リンクに取り込む(未使用でも常駐させる)。
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
;   RAM 側のレイアウトは arch に依存しない(DATA_LOC=0x8000 が z80pack と
;   z80board で揃っている前提)。
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
