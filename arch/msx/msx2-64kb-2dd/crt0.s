.module crt0

;===================================================================
; arch/msx/msx2-64kb-2dd crt0.s.  s2.s (the boot loader) copies the kernel image
; to 0x0100 with pages 0 and 1 switched to RAM and jumps to 0x0100.
;
;   MSX-specific here:
;     - block 7 (0xF000-0xFFFF) is not touched: the BIOS and disk ROM work area
;       live there (MEMCHK_TOP_H)
;     - the disk trampoline (tramp.s) at 0x8C78 (block 0's free gap) is put there by s2.s
;     - the interrupt is the VDP vblank (60Hz): reading VDP status acknowledges it
;   The rest (vectors at 0x0038.., scheduler, init) is shared with the other z80 ports.
;===================================================================

; 0xF000 and above (block 7) is the BIOS / disk ROM work area: neither the memory check
; nor the process slots touch it (MEMCHK_TOP_H in common-z80/crt0-init.inc).
MEMCHK_TOP_H = 0xF0
        .include "../../common-z80/crt0-defs.inc"

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
        ; --- the fixed vectors 0x0038-0x006C, copied from a table (vec_table below) ---
        ;     0x0038 isr        0x003B _kexit      0x003E _kputchar   0x0041 _kgetchar
        ;     0x0044 _getticks  0x0047 _kprintf    0x004A _time_get   0x004D _time_set
        ;     0x0050 jp (hl)    (SDCC 4.x helper)
        ;     0x0052 __divuint  0x0055 __moduint   0x0058 __divsint   0x005B __modsint
        ;     0x005E __divulong 0x0061 __mullong   0x0064 __mulint
        ;     0x0067 _memcmp    0x006A _strcmp
        ;   The addresses are fixed: external commands link against them (-g _kputchar=0x003E ...).
        ld      hl, #vec_table
        ld      de, #0x0038
        ld      bc, #0x0035
        ldir

        ; --- ここから共通: _DATA のゼロ埋め / gsinit / PCB 初期化 / メモリチェック ---
        .include "../../common-z80/crt0-init.inc"

        call    0x8C7D                ; dc_init in tramp.s: VRAM sector cache all empty (diskio.c)
        call    _init
        halt

vec_table:
        jp      isr
        jp      _kexit
        jp      _kputchar
        jp      _kgetchar
        jp      _getticks
        jp      _kprintf
        jp      _time_get
        jp      _time_set
        .db     0xE9, 0xC9              ; 0x0050: jp (hl) ; ret
        jp      __divuint
        jp      __moduint
        jp      __divsint
        jp      __modsint
        jp      __divulong
        jp      __mullong
        jp      __mulint
        jp      _memcmp
        jp      _strcmp

; ---- 0x0038 vblank ISR : ticks++ then save/pick/restore ----
        .globl  _kb_isr
isr:
        push    af
        in      a, (0x99)             ; VDP status read acknowledges the interrupt
        push    bc
        push    de
        push    hl
        call    _kb_isr               ; keyboard scan (C; IX is kept by the callee, IY is reserved)
        pop     hl
        pop     de
        pop     bc
        .include "../../common-z80/crt0-sched.inc"

        .include "../../common-z80/crt0-areas.inc"
