;===================================================================
; tzctramp.s - オーバーレイ用の DRIVER トランポリン + 共有メモリ補助 (#37)
;
;   中身は crt0_tizix.s にあるものと **同一**。なぜ二重に持つのか:
;
;   通常のコマンドは crt0_tizix.rel を明示リンクするので、そこにある実体で
;   解決される(ライブラリは未解決シンボルにしか使われないので、この
;   モジュールは引かれない = 既存コマンドのサイズは 1 バイトも変わらない)。
;
;   オーバーレイ(crt0_ovl.s)は _start も SP 張替も持たない小さな入口なので
;   crt0_tizix を持たない。そのままだと fopen / putchar / peekw … が
;   未解決になる。ライブラリ側にも実体を置いておけば、**オーバーレイのときだけ**
;   ここが引かれる。
;
;   いずれも `ld hl,(絶対); jp (hl)` か素の手書きで、IY を触らない。
;   よって tizix.c の IY 変換の対象外で、どの番地へリンクされても動く。
;===================================================================

        .module tzctramp

        .globl  _fopen, _fread, _fwrite, _fclose, _fputs, _fgets
        .globl  _kbhit, _getc_timeout, _fseek, _ftell, _feof, _ferror, _fflush
        .globl  _fgetc, _fputc
        .globl  _opendir, _readdir, _closedir, _readdir_size
        .globl  _mkdir, _unlink, _rename

        .globl  _putchar, _putc, _getchar
        .globl  _kputchar, _kgetchar

; カーネル低位ベクタ。crt0_ovl.s と同じ理由でここでも実体定義しておく
; (このモジュールが単独で引かれても解決できるように)。
        .area   _CODE

_fopen::
        ld      hl, (0x9006)
        jp      (hl)
_fwrite::
        ld      hl, (0x9008)
        jp      (hl)
_fclose::
        ld      hl, (0x900A)
        jp      (hl)
_fread::
        ld      hl, (0x900C)
        jp      (hl)
_fputs::
        ld      hl, (0x900E)
        jp      (hl)
_fgets::
        ld      hl, (0x9010)
        jp      (hl)
_kbhit::
        ld      hl, (0x9012)
        jp      (hl)
_getc_timeout::
        ld      hl, (0x9014)
        jp      (hl)
_fseek::
        ld      hl, (0x9016)
        jp      (hl)
_ftell::
        ld      hl, (0x9018)
        jp      (hl)
_feof::
        ld      hl, (0x901A)
        jp      (hl)
_ferror::
        ld      hl, (0x901C)
        jp      (hl)
_fflush::
        ld      hl, (0x901E)
        jp      (hl)
_fgetc::
        ld      hl, (0x9020)
        jp      (hl)
_fputc::
        ld      hl, (0x9022)
        jp      (hl)
_opendir::
        ld      hl, (0x902A)
        jp      (hl)
_readdir::
        ld      hl, (0x902C)
        jp      (hl)
_closedir::
        ld      hl, (0x902E)
        jp      (hl)
_readdir_size::
        ld      hl, (0x904C)
        jp      (hl)
_mkdir::
        ld      hl, (0x9030)
        jp      (hl)
_unlink::
        ld      hl, (0x9032)
        jp      (hl)
_rename::
        ld      hl, (0x9034)
        jp      (hl)

; ---- コンソール(crt0_tizix.s と同一。カーネル低位ベクタ経由) ----
;   _kputchar / _kgetchar の実体は crt0_ovl.s が定義する(明示リンクされる方)。

_putchar::
_putc::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        ld      hl, #2
        add     hl, sp
        ld      l, (hl)
        ld      h, #0
        ret

_getchar::
        call    _kgetchar
        ret

; 共有メモリ補助(getbase / peek / poke / peekw / pokew / absmove / callovl)は
; tzcshare.s(別のライブラリモジュール)にある。ここに重複して置くと、
; オーバーレイが両方を引いたときに二重定義になる。
