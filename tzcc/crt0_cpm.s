    .module crt0
    .globl _main
    .globl _putchar
    .globl _putc
    .globl _printf
    .globl _puts
    .globl _fputs
    .globl _fputc
    .globl _getchar
    .globl _fgetc
    .globl _strlen
    .globl _strcpy
    .globl _strncpy
    .globl _strcat
    .globl _strcmp
    .globl _strncmp
    .globl _memset
    .globl _memcpy
    .globl _memcmp
    .globl _atoi
    .globl _abs
    .globl _rand
    .globl _srand
    .globl _fopen
    .globl _fclose
    .globl _fread
    .globl _fwrite
    .globl _fseek
    .globl _ftell
    .globl _feof
    .globl _ferror
    .globl _fflush
    .globl _kbhit
    .globl _socket
    .globl _send
    .globl _recv

    .area _HEADER (ABS)
    .org 0x0100
_start::
    ld hl, #dummy_argv
    push hl
    ld hl, #1
    push hl
    call _main
    pop af
    pop af
    jp 0x0000

    .area _CODE

_putc::
_putchar::
    ld ix, #0
    add ix, sp
    ld a, 2(ix)
    out (1), a
    ret

; int printf(const char *fmt, ...)
;   対応書式: %d %i %u %x %X %c %s %%   （幅/精度/フラグは読み飛ばして無視）
;   引数はすべて tzcc が 16bit で push する。%c は下位8bit、%s は char*。
;   スタック: 0(sp)=戻り番地, 2(sp)=fmt, 4(sp)=第1可変引数 ...
_printf::
    ld hl, #2
    add hl, sp
    ld e, (hl)
    inc hl
    ld d, (hl)
    inc hl                 ; hl = sp+4 = 最初の可変引数スロット
    ld (_pf_arg), hl
    ex de, hl              ; hl = fmt
    ld (_pf_ptr), hl
_pf_loop:
    ld hl, (_pf_ptr)
    ld a, (hl)
    inc hl
    ld (_pf_ptr), hl
    or a
    ret z
    cp #'%'
    jp nz, _pf_char
    ; '%' の次の文字
    ld hl, (_pf_ptr)
    ld a, (hl)
    inc hl
    ld (_pf_ptr), hl
    or a
    ret z
    cp #'%'
    jp z, _pf_char
_pf_pct_skip:
    ; 幅/精度/フラグ ( 0-9 . - + # 空白 ) を読み飛ばす
    cp #' '
    jr z, _pf_pct_next
    cp #'#'
    jr z, _pf_pct_next
    cp #'+'
    jr z, _pf_pct_next
    cp #'-'
    jr z, _pf_pct_next
    cp #'.'
    jr z, _pf_pct_next
    cp #'0'
    jr c, _pf_disp
    cp #':'                ; '9'+1
    jr c, _pf_pct_next
    jr _pf_disp
_pf_pct_next:
    ld hl, (_pf_ptr)
    ld a, (hl)
    inc hl
    ld (_pf_ptr), hl
    or a
    ret z
    jr _pf_pct_skip
_pf_disp:
    cp #'d'
    jp z, _pf_d
    cp #'i'
    jp z, _pf_d
    cp #'u'
    jp z, _pf_u
    cp #'x'
    jp z, _pf_x
    cp #'X'
    jp z, _pf_x
    cp #'c'
    jp z, _pf_c
    cp #'s'
    jp z, _pf_s
    ; 未知の書式は '%' + その文字をそのまま出す
    push af
    ld a, #'%'
    out (1), a
    pop af
_pf_char:
    out (1), a
    jp _pf_loop

; de <- 次の可変引数, _pf_arg += 2
_pf_next_arg:
    ld hl, (_pf_arg)
    ld e, (hl)
    inc hl
    ld d, (hl)
    inc hl
    ld (_pf_arg), hl
    ret

_pf_c:
    call _pf_next_arg
    ld a, e
    out (1), a
    jp _pf_loop

_pf_s:
    call _pf_next_arg
    ex de, hl
_pf_s_loop:
    ld a, (hl)
    or a
    jp z, _pf_loop
    out (1), a
    inc hl
    jr _pf_s_loop

_pf_u:
    call _pf_next_arg
    ex de, hl
    call _pf_print_u16
    jp _pf_loop

_pf_d:
    call _pf_next_arg      ; de = 値
    ld a, d
    bit 7, a
    jr z, _pf_d_pos
    ld a, #'-'
    out (1), a
    ld hl, #0
    or a
    sbc hl, de            ; hl = -de
    jr _pf_d_go
_pf_d_pos:
    ex de, hl
_pf_d_go:
    call _pf_print_u16
    jp _pf_loop

_pf_x:
    call _pf_next_arg
    ex de, hl
    call _pf_print_x16
    jp _pf_loop

; hl を符号なし10進で出力 (前ゼロ抑制、0 は "0")
_pf_print_u16:
    xor a
    ld (_pf_lz), a
    ld de, #10000
    call _pf_digit
    ld de, #1000
    call _pf_digit
    ld de, #100
    call _pf_digit
    ld de, #10
    call _pf_digit
    ld a, l
    add a, #'0'
    out (1), a
    ret
_pf_digit:               ; de = 桁の重み, hl = 残り。1桁出力し hl を剰余に更新
    ld a, #'0'
_pf_digit_sub:
    or a
    sbc hl, de
    jr c, _pf_digit_rest
    inc a
    jr _pf_digit_sub
_pf_digit_rest:
    add hl, de
    cp #'0'
    jr nz, _pf_digit_show
    ld a, (_pf_lz)
    or a
    ret z                ; 前ゼロ抑制
    ld a, #'0'
_pf_digit_show:
    push af
    ld a, #1
    ld (_pf_lz), a
    pop af
    out (1), a
    ret

; hl を 16bit 16進で出力 (前ゼロ抑制、0 は "0")
_pf_print_x16:
    xor a
    ld (_pf_lz), a
    ld a, h
    call _pf_hexbyte
    ld a, l
    call _pf_hexbyte
    ld a, (_pf_lz)
    or a
    ret nz
    ld a, #'0'
    out (1), a
    ret
_pf_hexbyte:             ; a の上位/下位ニブルを出力
    push af
    rrca
    rrca
    rrca
    rrca
    and #0x0f
    call _pf_hexnib
    pop af
    and #0x0f
    call _pf_hexnib
    ret
_pf_hexnib:              ; a = 0..15
    or a
    jr nz, _pf_hn_show
    ld a, (_pf_lz)
    or a
    ret z                ; 前ゼロ抑制
    xor a
_pf_hn_show:
    push af
    ld a, #1
    ld (_pf_lz), a
    pop af
    cp #10
    jr c, _pf_hn_dec
    add a, #0x57         ; a=10..15 -> 'a'..'f'  ('a'-10 = 0x57)
    out (1), a
    ret
_pf_hn_dec:
    add a, #'0'
    out (1), a
    ret

_puts::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
_puts_loop:
    ld a, (hl)
    or a
    jr z, _puts_nl
    out (1), a
    inc hl
    jr _puts_loop
_puts_nl:
    ld a, #10
    out (1), a
    ret

; int fputs(const char *s, FILE *f)
;   f を見て動作を切替える。stdout/stderr ならコンソール、それ以外は
;   _fputc 経由で FCB/BDOS の書き込みルートに流す。puts と違い \n は付けない。
;   2(ix)=s, 4(ix)=f
_fputs::
    ld ix, #0
    add ix, sp
    ld l, 4(ix)
    ld h, 5(ix)
    ld (_io_fp), hl
    ld l, 2(ix)
    ld h, 3(ix)
_fputs_loop:
    ld a, (hl)
    or a
    jr z, _fputs_done
    push hl                 ; 文字列ポインタを保存 (_fputc が hl を壊す)
    ld hl, (_io_fp)
    push hl                 ; arg2: FILE*
    ld h, #0
    ld l, a
    push hl                 ; arg1: 文字
    call _fputc
    pop af
    pop af
    pop hl
    inc hl
    jr _fputs_loop
_fputs_done:
    ld hl, #0
    ret

_getchar::
    in a, (1)
    ld l, a
    ld h, #0
    ret

_strlen::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    ld bc, #0
_strlen_loop:
    ld a, (hl)
    or a
    jr z, _strlen_done
    inc bc
    inc hl
    jr _strlen_loop
_strlen_done:
    ld l, c
    ld h, b
    ret

_strcpy::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    push de
_strcpy_loop:
    ld a, (hl)
    ld (de), a
    or a
    jr z, _strcpy_done
    inc hl
    inc de
    jr _strcpy_loop
_strcpy_done:
    pop hl
    ret

_strncpy::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    ld c, 6(ix)
    ld b, 7(ix)
    push de
_strncpy_loop:
    ld a, b
    or c
    jr z, _strncpy_done
    ld a, (hl)
    ld (de), a
    or a
    jr z, _strncpy_done
    inc hl
    inc de
    dec bc
    jr _strncpy_loop
_strncpy_done:
    pop hl
    ret

_strcat::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    push de
_strcat_find:
    ld a, (de)
    or a
    jr z, _strcat_copy
    inc de
    jr _strcat_find
_strcat_copy:
    ld a, (hl)
    ld (de), a
    or a
    jr z, _strcat_done
    inc hl
    inc de
    jr _strcat_copy
_strcat_done:
    pop hl
    ret

_strcmp::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
_strcmp_loop:
    ld a, (de)
    ld b, (hl)
    cp b
    jr nz, _strcmp_diff
    or a
    jr z, _strcmp_eq
    inc de
    inc hl
    jr _strcmp_loop
_strcmp_eq:
    ld hl, #0
    ret
_strcmp_diff:
    jr c, _strcmp_lt
    ld hl, #1
    ret
_strcmp_lt:
    ld hl, #-1
    ret
_strncmp::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    ld c, 6(ix)
    ld b, 7(ix)
_strncmp_loop:
    ld a, b
    or c
    jr z, _strncmp_eq
    ld a, (de)
    ld h, (hl)
    cp h
    jr nz, _strncmp_diff
    or a
    jr z, _strncmp_eq
    inc de
    inc hl
    dec bc
    jr _strncmp_loop
_strncmp_eq:
    ld hl, #0
    ret
_strncmp_diff:
    jr c, _strncmp_lt
    ld hl, #1
    ret
_strncmp_lt:
    ld hl, #-1
    ret

_memset::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld a, 4(ix)
    ld c, 6(ix)
    ld b, 7(ix)
    push de
_memset_loop:
    ld h, a
    ld a, b
    or c
    ld a, h
    jr z, _memset_done
    ld (de), a
    inc de
    dec bc
    jr _memset_loop
_memset_done:
    pop hl
    ret

_memcpy::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    ld c, 6(ix)
    ld b, 7(ix)
    push de
_memcpy_loop:
    ld a, b
    or c
    jr z, _memcpy_done
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    dec bc
    jr _memcpy_loop
_memcpy_done:
    pop hl
    ret

_memcmp::
    ld ix, #0
    add ix, sp
    ld e, 2(ix)
    ld d, 3(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    ld c, 6(ix)
    ld b, 7(ix)
_memcmp_loop:
    ld a, b
    or c
    jr z, _memcmp_eq
    ld a, (de)
    cp (hl)
    jr nz, _memcmp_diff
    inc de
    inc hl
    dec bc
    jr _memcmp_loop
_memcmp_eq:
    ld hl, #0
    ret
_memcmp_diff:
    jr c, _memcmp_lt
    ld hl, #1
    ret
_memcmp_lt:
    ld hl, #-1
    ret

_atoi::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
_atoi_space:
    ld a, (hl)
    cp #32
    jr z, _atoi_sp_inc
    cp #9
    jr z, _atoi_sp_inc
    jr _atoi_sign
_atoi_sp_inc:
    inc hl
    jr _atoi_space
_atoi_sign:
    ld c, #0
    cp #45
    jr nz, _atoi_plus
    ld c, #1
    inc hl
    jr _atoi_parse
_atoi_plus:
    cp #43
    jr nz, _atoi_parse
    inc hl
_atoi_parse:
    ld de, #0
_atoi_loop:
    ld a, (hl)
    sub #48
    jr c, _atoi_fin
    cp #10
    jr nc, _atoi_fin
    push hl
    ld l, a
    ld h, #0
    push hl
    ld h, d
    ld l, e
    add hl, hl
    ld b, h
    ld c, l
    add hl, hl
    add hl, hl
    add hl, bc
    pop bc
    add hl, bc
    ex de, hl
    pop hl
    inc hl
    jr _atoi_loop
_atoi_fin:
    ex de, hl
    ld a, c
    or a
    ret z
    ld a, l
    cpl
    ld l, a
    ld a, h
    cpl
    ld h, a
    inc hl
    ret

_abs::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    bit 7, h
    ret z
    ld a, l
    cpl
    ld l, a
    ld a, h
    cpl
    ld h, a
    inc hl
    ret

_srand::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    ld (_rand_seed), hl
    ret

_rand::
    ld hl, (_rand_seed)
    ld de, #25173
    call _mul16
    ld de, #13849
    add hl, de
    ld (_rand_seed), hl
    res 7, h
    ret

_mul16:
    ld b, h
    ld c, l
    ld hl, #0
_mul16_loop:
    ld a, b
    or c
    ret z
    srl b
    rr c
    jr nc, _mul16_no
    add hl, de
_mul16_no:
    ex de, hl
    add hl, hl
    ex de, hl
    jr _mul16_loop

_fopen::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    ld e, 4(ix)
    ld d, 5(ix)

    ld iy, #file_table
    ld b, #4
_fopen_find_free:
    ld a, 169(iy)
    or a
    jr z, _fopen_found_free
    ld bc, #170
    add iy, bc
    djnz _fopen_find_free
    ld hl, #0
    ret

_fopen_found_free:
    push iy
    push iy
    pop de
    ld h, d
    ld l, e
    inc de
    ld (hl), #0
    ld bc, #35
    ldir

    ld 0(iy), #0
    ld b, #11
    push iy
    pop de
    inc de
_fopen_fill_spc:
    ld a, #0x20
    ld (de), a
    inc de
    djnz _fopen_fill_spc

    ld l, 2(ix)
    ld h, 3(ix)
    push iy
    pop de
    inc de
    ld c, #8
_fopen_copy_name:
    ld a, (hl)
    or a
    jr z, _fopen_path_done
    cp #46
    jr z, _fopen_has_ext
    cp #97
    jr c, _fopen_not_lower1
    cp #123
    jr nc, _fopen_not_lower1
    sub #32
_fopen_not_lower1:
    ld (de), a
    inc hl
    inc de
    dec c
    jr nz, _fopen_copy_name

_fopen_skip_to_dot:
    ld a, (hl)
    or a
    jr z, _fopen_path_done
    cp #46
    jr z, _fopen_has_ext
    inc hl
    jr _fopen_skip_to_dot

_fopen_has_ext:
    inc hl                  ; hl -> 拡張子先頭 (ソース文字列)。保持する
    push hl
    push iy
    pop hl
    ld bc, #9
    add hl, bc             ; hl = iy + 9 (FCB 拡張子フィールド先頭 = drive1 + name8)
    ex de, hl             ; de = 書き込み先
    pop hl                 ; hl = 拡張子ソース (以前は iy に潰されて取りこぼしていた)
    ld c, #3
_fopen_copy_ext:
    ld a, (hl)
    or a
    jr z, _fopen_path_done
    cp #46                 ; 二つ目の '.' はここで打ち切り
    jr z, _fopen_path_done
    cp #97
    jr c, _fopen_not_lower2
    cp #123
    jr nc, _fopen_not_lower2
    sub #32
_fopen_not_lower2:
    ld (de), a
    inc hl
    inc de
    dec c
    jr nz, _fopen_copy_ext

_fopen_path_done:
    ld e, 4(ix)
    ld d, 5(ix)
    ld a, (de)
    cp #119
    jr z, _fopen_write_mode

_fopen_read_mode:
    push iy
    pop de
    ld c, #15
    call 0x0005
    cp #0xff
    jr z, _fopen_fail
    ld 168(iy), #1
    ld 164(iy), #128
    ld 165(iy), #0
    ld 166(iy), #0
    ld 167(iy), #0
    ld 169(iy), #1
    pop hl
    ret

_fopen_write_mode:
    push iy
    pop de
    ld c, #19
    call 0x0005
    push iy
    pop de
    ld c, #22
    call 0x0005
    cp #0xff
    jr z, _fopen_fail
    ld 168(iy), #2
    ld 164(iy), #0
    ld 165(iy), #0
    ld 166(iy), #0
    ld 167(iy), #0
    ld 169(iy), #1
    pop hl
    ret

_fopen_fail:
    pop hl
    ld hl, #0
    ret

_fgetc::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    push hl
    pop iy

    ; check for stdin
    ld hl, #_stdin_struct
    push iy
    pop de
    or a
    sbc hl, de
    jr z, _fgetc_port

    ld l, 164(iy)
    ld h, 165(iy)
    ld e, 166(iy)
    ld d, 167(iy)
    or a
    sbc hl, de
    jr c, _fgetc_read_buf

    push iy
    pop hl
    ld bc, #36
    add hl, bc
    ex de, hl
    ld c, #26
    call 0x0005

    push iy
    pop de
    ld c, #20
    call 0x0005
    or a
    jr nz, _fgetc_eof

    ld 164(iy), #0
    ld 165(iy), #0
    ld 166(iy), #128
    ld 167(iy), #0

_fgetc_read_buf:
    ld l, 164(iy)
    ld h, 165(iy)          ; hl = バッファ内インデックス
    push iy
    pop de                 ; de = iy   (以前ここに余分な push de があり
    ex de, hl             ;            ret がその値を戻り番地として拾って暴走していた)
    ld bc, #36
    add hl, bc
    add hl, de             ; hl = iy + 36 + index
    ld a, (hl)
    cp #0x1a
    jr z, _fgetc_eof

    inc 164(iy)
    jr nz, _fgetc_done_inc
    inc 165(iy)
_fgetc_done_inc:
    ld l, a
    ld h, #0
    ret

_fgetc_eof:
    ld hl, #-1
    ret

_fgetc_port:
    in a, (1)
    ld l, a
    ld h, #0
    ret

_fputc::
    ld ix, #0
    add ix, sp
    ld a, 2(ix)
    ld l, 4(ix)
    ld h, 5(ix)
    push hl
    pop iy

    ; check for stdout/stderr
    ld hl, #_stdout_struct
    push iy
    pop de
    or a
    sbc hl, de
    jr z, _fputc_port
    ld hl, #_stderr_struct
    push iy
    pop de
    or a
    sbc hl, de
    jr z, _fputc_port

    ld e, 164(iy)
    ld d, 165(iy)
    push iy
    pop hl
    ld bc, #36
    add hl, bc
    add hl, de
    ld (hl), a

    inc 164(iy)
    jr nz, _fputc_check_flush
    inc 165(iy)

_fputc_check_flush:
    ld a, 164(iy)
    cp #128
    jr c, _fputc_ok

    push iy
    pop hl
    ld bc, #36
    add hl, bc
    ex de, hl
    ld c, #26
    call 0x0005

    push iy
    pop de
    ld c, #21
    call 0x0005

    ld 164(iy), #0
    ld 165(iy), #0

_fputc_ok:
    ld a, 2(ix)
    ld l, a
    ld h, #0
    ret

_fputc_port:
    out (1), a
    ld l, a
    ld h, #0
    ret

_fclose::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    ld a, h
    or l
    ret z
    push hl
    pop iy

    ld a, 168(iy)
    cp #2
    jr nz, _fclose_close_handle

    ld a, 164(iy)
    or a
    jr z, _fclose_close_file

    ld e, 164(iy)
    ld d, 165(iy)
    push iy
    pop hl
    ld bc, #36
    add hl, bc
    add hl, de
_fclose_pad_loop:
    ld (hl), #0x1a
    inc hl
    inc e
    ld a, e
    cp #128
    jr c, _fclose_pad_loop

    push iy
    pop hl
    ld bc, #36
    add hl, bc
    ex de, hl
    ld c, #26
    call 0x0005

    push iy
    pop de
    ld c, #21
    call 0x0005

_fclose_close_file:
    push iy
    pop de
    ld c, #16
    call 0x0005

_fclose_close_handle:
    ld 169(iy), #0
    ld hl, #0
    ret

; size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
;   サブセット簡略化: nmemb をバイト数として扱い、その分だけ _fgetc する。
;   ix は _fgetc 呼び出しで壊れるので FILE* / 戻り値は静的領域へ退避しておく。
_fread::
    ld ix, #0
    add ix, sp
    ld l, 8(ix)
    ld h, 9(ix)
    ld (_io_fp), hl
    ld l, 6(ix)
    ld h, 7(ix)
    ld (_io_ret), hl
    ld c, l
    ld b, h
    ld l, 2(ix)
    ld h, 3(ix)
    push hl
    pop iy
_fread_loop:
    ld a, b
    or c
    jr z, _fread_done
    push bc
    push iy
    ld hl, (_io_fp)
    push hl
    call _fgetc
    pop af
    pop iy
    pop bc
    ld a, h
    and l
    inc a
    jr z, _fread_done
    ld (iy), l
    inc iy
    dec bc
    jr _fread_loop
_fread_done:
    ld hl, (_io_ret)
    ret

; size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
;   サブセット簡略化: nmemb をバイト数として _fputc する。ix 退避は fread と同じ。
_fwrite::
    ld ix, #0
    add ix, sp
    ld l, 8(ix)
    ld h, 9(ix)
    ld (_io_fp), hl
    ld l, 6(ix)
    ld h, 7(ix)
    ld (_io_ret), hl
    ld c, l
    ld b, h
    ld l, 2(ix)
    ld h, 3(ix)
    push hl
    pop iy
_fwrite_loop:
    ld a, b
    or c
    jr z, _fwrite_done
    push bc
    push iy
    ld hl, (_io_fp)
    push hl                 ; arg2: FILE*
    ld a, (iy)
    ld l, a
    ld h, #0
    push hl                 ; arg1: 文字
    call _fputc
    pop af
    pop af
    pop iy
    pop bc
    inc iy
    dec bc
    jr _fwrite_loop
_fwrite_done:
    ld hl, (_io_ret)
    ret

; char *fgets(char *buf, int size, FILE *f)
;   size-1 文字か、改行、EOF まで読み込み、末尾を 0 終端。ix は _fgetc で
;   壊れるので FILE* / buf 先頭は静的領域・スタックへ退避する。
;   2(ix)=buf, 4(ix)=size, 6(ix)=f
_fgets::
    ld ix, #0
    add ix, sp
    ld l, 6(ix)
    ld h, 7(ix)
    ld (_io_fp), hl
    ld c, 4(ix)
    ld b, 5(ix)
    ld a, b
    or c
    jr z, _fgets_fail
    dec bc
    ld a, b
    or c
    jr z, _fgets_fail
    ld l, 2(ix)
    ld h, 3(ix)
    ld (_io_ret), hl        ; buf 先頭 (戻り値用)
    push hl
    pop iy
_fgets_loop:
    ld a, b
    or c
    jr z, _fgets_done
    push bc
    push iy
    ld hl, (_io_fp)
    push hl
    call _fgetc
    pop af
    pop iy
    pop bc
    ld a, h
    and l
    inc a
    jr z, _fgets_done
    ld a, l
    ld (iy), a
    inc iy
    dec bc
    cp #10
    jr z, _fgets_done
    jr _fgets_loop
_fgets_done:
    ld (iy), #0
    ; 1文字も読めていなければ (iy が buf 先頭のまま) EOF とみなし NULL を返す
    push iy
    pop hl
    ld de, (_io_ret)
    or a
    sbc hl, de
    jr z, _fgets_fail
    ld hl, (_io_ret)
    ret
_fgets_fail:
    ld hl, #0
    ret

_fseek::
_ftell::
    ld hl, #0
    ret

_feof::
    ld ix, #0
    add ix, sp
    ld l, 2(ix)
    ld h, 3(ix)
    ld a, h
    or l
    jr z, _feof_yes
    push hl
    pop iy
    ld a, 164(iy)
    cp #128
    jr nc, _feof_yes
    ld hl, #0
    ret
_feof_yes:
    ld hl, #1
    ret

_ferror::
_fflush::
_kbhit::
    ld hl, #0
    ret

; ---- ソケット (サブセット・ダミー実装) ----
; 本来はプロジェクト側で _socket / _send / _recv を実装する前提。未実装のまま
; リンクした場合のフォールバックとして、ここでは I/O ポート1番の UART に
; 直結したダミーを提供する。socket() は常に fd=1 (UART) を返す。
; 実ネットワークスタックを載せるプロジェクトは同名ラベルを自前実装で置き換える。
;
; int socket(int domain, int type, int protocol)
_socket::
    ld hl, #1               ; fd = 1 (UART をソケットに見立てる)
    ret

; int send(int fd, const char *buf, int len)
;   2(ix)=fd, 4(ix)=buf, 6(ix)=len。fd は無視して UART へ len バイト出力。
;   戻り値 = 送信バイト数 (= len)。
_send::
    ld ix, #0
    add ix, sp
    ld c, 6(ix)
    ld b, 7(ix)
    ld l, 4(ix)
    ld h, 5(ix)
_send_loop:
    ld a, b
    or c
    jr z, _send_done
    ld a, (hl)
    out (1), a
    inc hl
    dec bc
    jr _send_loop
_send_done:
    ld l, 6(ix)
    ld h, 7(ix)
    ret

; int recv(int fd, char *buf, int len)
;   UART から len バイト読み込み buf へ格納。戻り値 = 受信バイト数 (= len)。
_recv::
    ld ix, #0
    add ix, sp
    ld c, 6(ix)
    ld b, 7(ix)
    ld l, 4(ix)
    ld h, 5(ix)
_recv_loop:
    ld a, b
    or c
    jr z, _recv_done
    in a, (1)
    ld (hl), a
    inc hl
    dec bc
    jr _recv_loop
_recv_done:
    ld l, 6(ix)
    ld h, 7(ix)
    ret

; 乗算ルーチン (HL = HL * DE)
_mul::
    ld b, #16
    ld hl, #0
_mul_loop:
    add hl, hl
    ex de, hl
    add hl, hl
    ex de, hl
    jr nc, _mul_skip
    add hl, de
_mul_skip:
    djnz _mul_loop
    ret

; 除算ルーチン (HL = HL / DE)
_div::
    ld b, #16
    ld a, h
    ld h, l
    ld l, #0
_div_loop:
    add hl, hl
    rla
    sub e
    ld c, a
    ld a, h
    sbc a, d
    jr c, _div_skip
    ld h, a
    ld a, c
    inc l
_div_skip:
    ld a, c
    djnz _div_loop
    ld h, l
    ld l, a
    ret

_stdin::
    .dw _stdin_struct
_stdout::
    .dw _stdout_struct
_stderr::
    .dw _stderr_struct

_stdin_struct:
    .ds 170
_stdout_struct:
    .ds 170
_stderr_struct:
    .ds 170

    .area _DATA
_rand_seed:
    .dw 1
dummy_argv0:
    .ascii "tzcc"
    .db 0
dummy_argv:
    .dw dummy_argv0
    .dw 0
file_table:
    .ds 170 * 4
; _fputs / _fgets / _fread / _fwrite が _fputc / _fgetc を呼ぶ間、ix が壊れる
; ため FILE* と戻り値カウントをここへ退避する (再入不可・単一スレッド前提)。
_io_fp:
    .ds 2
_io_ret:
    .ds 2
; printf 書式解析用スクラッチ (再入不可・単一スレッド前提)
_pf_ptr:
    .ds 2
_pf_arg:
    .ds 2
_pf_lz:
    .ds 1
