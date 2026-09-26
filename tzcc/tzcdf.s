;===================================================================
; tzcdf.s - drv_tbl[47] kfs_df のトランポリン (tizix #56 /bin/df)
;
;   unsigned long kfs_df(unsigned sel)  … sel=0 総 KB / 1 空き KB(DE:HL)。
;   crt0_tizix.s の他のトランポリン(fopen 等)と同じ形。crt0_tizix.rel は
;   全コマンドが明示リンクするのでそこへ置くと全員が 4B 太る ── df しか
;   使わないのでライブラリの 1 モジュールにして、参照したコマンドだけが引く。
;===================================================================
        .module tzcdf
        .globl  _kfs_df

        .area   _CODE

_kfs_df::
        ld      hl, (0x905E)
        jp      (hl)
