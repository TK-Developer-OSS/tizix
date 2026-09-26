;===================================================================
; lstr.s - tizix user-command string.h library (hand PIC assembler)
;
;   Replaces the C-compiled string.c on the command build path.
;   Rationale: iy_reg_claude.py roughly DOUBLES sdcc output (measured
;   +94% on string.c: 1095 -> 2124 B).  These are all short loops, so a
;   hand-written version that is *already* position independent needs
;   zero relocation glue (~375 B for all 12 functions).
;
;   Rules that keep this module base-independent (NOT run through iy_reg):
;     * branches: only jr / djnz  (PC-relative -> base independent).
;       NO  jp <label> / call <label>  (those encode absolute offsets
;       and would jump to offset instead of base+offset at run time).
;     * no  ld hl,#<label>  (data refs would need +base too).  The only
;       memory this module touches is caller-supplied pointers.
;     * IY is the process base (README iron law) - never touched.
;       IX is caller-saved by SDCC convention - never touched.
;   ABI (--sdcccall 0): args pushed right-to-left by caller, so on entry
;     0(sp)=ret, 2(sp)=arg1lo,3=hi, 4(sp)=arg2lo,5=hi, 6(sp)=arg3lo,7=hi.
;     Caller pops the args.  Return: HL (16-bit).  Preserve nothing else.
;===================================================================
        .module lstr
        .globl _strlen, _strcpy, _strncpy, _strcat
        .globl _strcmp, _strncmp, _strchr, _strrchr
        .globl _strstr, _memset, _memcpy, _memcmp
        .area _CODE

;-------------------------------------------------------------
; size_t strlen(const char *s)
;-------------------------------------------------------------
_strlen::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s
        ld      hl, #0           ; hl = count
1$:
        ld      a, (de)
        or      a
        ret     z
        inc     de
        inc     hl
        jr      1$

;-------------------------------------------------------------
; char *strcpy(char *dest, const char *src)
;-------------------------------------------------------------
_strcpy::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = dest
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = src
        push    de               ; save dest for return
1$:
        ld      a, (bc)
        ld      (de), a
        inc     bc
        inc     de
        or      a
        jr      nz, 1$
        pop     hl
        ret

;-------------------------------------------------------------
; char *strncpy(char *dest, const char *src, size_t n)
;-------------------------------------------------------------
_strncpy::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = dest
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = src
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = n
        push    de               ; save dest
1$:                              ; copy phase
        ld      a, h
        or      l
        jr      z, 3$
        ld      a, (bc)
        or      a
        jr      z, 2$
        ld      (de), a
        inc     bc
        inc     de
        dec     hl
        jr      1$
2$:                              ; pad phase (fill rest with 0)
        ld      a, h
        or      l
        jr      z, 3$
        xor     a
        ld      (de), a
        inc     de
        dec     hl
        jr      2$
3$:
        pop     hl
        ret

;-------------------------------------------------------------
; char *strcat(char *dest, const char *src)
;-------------------------------------------------------------
_strcat::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = dest
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = src
        push    de               ; save dest
1$:                              ; advance de to dest's NUL
        ld      a, (de)
        or      a
        jr      z, 2$
        inc     de
        jr      1$
2$:
        ld      a, (bc)
        ld      (de), a
        inc     bc
        inc     de
        or      a
        jr      nz, 2$
        pop     hl
        ret

;-------------------------------------------------------------
; int strcmp(const char *s1, const char *s2)
;   returns (unsigned char)*s1 - (unsigned char)*s2, sign-extended
;-------------------------------------------------------------
_strcmp::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s1
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = s2
1$:
        ld      a, (bc)
        ld      l, a             ; l = *s2
        ld      a, (de)          ; a = *s1
        sub     l                ; a = *s1 - *s2
        jr      nz, 2$
        ld      a, (de)
        or      a
        jr      z, 3$            ; equal and both NUL -> 0
        inc     de
        inc     bc
        jr      1$
2$:
        ld      l, a
        rlca
        sbc     a, a
        ld      h, a             ; hl = sign-extend(diff)
        ret
3$:
        ld      hl, #0
        ret

;-------------------------------------------------------------
; int strncmp(const char *s1, const char *s2, size_t n)
;-------------------------------------------------------------
_strncmp::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s1
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = s2
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = n
        push    bc
        ex      (sp), hl         ; hl = s2, (sp) = n
        pop     bc               ; bc = n
1$:
        ld      a, b
        or      c
        jr      z, 3$            ; n exhausted -> 0
        dec     bc
        ld      a, (de)
        cp      (hl)             ; *s1 - *s2
        jr      nz, 2$
        ld      a, (de)
        or      a
        jr      z, 3$            ; equal and NUL -> 0
        inc     de
        inc     hl
        jr      1$
2$:
        sub     (hl)             ; a = *s1 - *s2
        ld      l, a
        rlca
        sbc     a, a
        ld      h, a
        ret
3$:
        ld      hl, #0
        ret

;-------------------------------------------------------------
; char *strchr(const char *s, int c)
;-------------------------------------------------------------
_strchr::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s
        inc     hl
        ld      c, (hl)          ; c = target (low byte of int)
1$:
        ld      a, (de)
        cp      c
        jr      z, 2$            ; hit (covers c==0 -> ptr to NUL)
        or      a
        jr      z, 3$            ; end, not found
        inc     de
        jr      1$
2$:
        ex      de, hl
        ret
3$:
        ld      hl, #0
        ret

;-------------------------------------------------------------
; char *strrchr(const char *s, int c)
;-------------------------------------------------------------
_strrchr::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s
        inc     hl
        ld      c, (hl)          ; c = target
        ld      hl, #0           ; hl = last match / NULL
1$:
        ld      a, (de)
        cp      c
        jr      nz, 2$
        ld      h, d
        ld      l, e             ; remember this position
2$:
        ld      a, (de)
        or      a
        jr      z, 3$
        inc     de
        jr      1$
3$:
        ld      a, c
        or      a
        ret     nz               ; c != 0 -> hl (last match or NULL)
        ld      h, d
        ld      l, e             ; c == 0 -> pointer to NUL
        ret

;-------------------------------------------------------------
; char *strstr(const char *haystack, const char *needle)
;-------------------------------------------------------------
_strstr::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = haystack
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = needle
        ld      a, (bc)
        or      a
        jr      nz, 1$
        ex      de, hl           ; empty needle -> haystack
        ret
1$:                              ; outer loop
        ld      a, (de)
        or      a
        jr      z, 7$            ; haystack end -> NULL
        push    de               ; save haystack cursor
        push    bc               ; save needle start
2$:
        ld      a, (bc)
        or      a
        jr      z, 5$            ; needle matched fully
        ld      a, (bc)
        ld      l, a
        ld      a, (de)
        cp      l
        jr      nz, 4$
        inc     de
        inc     bc
        jr      2$
4$:                              ; mismatch: restore and bump haystack
        pop     bc
        pop     de
        inc     de
        jr      1$
5$:                              ; match: saved haystack cursor is the hit
        pop     bc               ; discard needle start
        pop     hl               ; hl = match position
        ret
7$:
        ld      hl, #0
        ret

;-------------------------------------------------------------
; void *memset(void *s, int c, size_t n)
;-------------------------------------------------------------
_memset::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s
        inc     hl
        ld      c, (hl)          ; c = fill byte
        inc     hl
        inc     hl               ; skip high byte of int c
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = n
        push    de               ; save s
1$:
        ld      a, h
        or      l
        jr      z, 2$
        ld      a, c
        ld      (de), a
        inc     de
        dec     hl
        jr      1$
2$:
        pop     hl
        ret

;-------------------------------------------------------------
; void *memcpy(void *dest, const void *src, size_t n)
;-------------------------------------------------------------
_memcpy::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = dest
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = src
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = n
        push    de               ; save dest
        push    bc               ; save src
        ld      b, h
        ld      c, l             ; bc = n
        pop     hl               ; hl = src
        ld      a, b
        or      c
        jr      z, 1$            ; n==0: skip (ldir would copy 65536)
        ldir
1$:
        pop     hl               ; hl = dest
        ret

;-------------------------------------------------------------
; int memcmp(const void *s1, const void *s2, size_t n)
;-------------------------------------------------------------
_memcmp::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s1
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = s2
        inc     hl
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = n
        push    bc
        ex      (sp), hl         ; hl = s2, (sp) = n
        pop     bc               ; bc = n
1$:
        ld      a, b
        or      c
        jr      z, 3$            ; n==0 -> equal
        dec     bc
        ld      a, (de)
        cp      (hl)
        jr      nz, 2$
        inc     de
        inc     hl
        jr      1$
2$:
        sub     (hl)             ; a = *s1 - *s2
        ld      l, a
        rlca
        sbc     a, a
        ld      h, a
        ret
3$:
        ld      hl, #0
        ret
