/* tz/drv16e.S : the DRIVER's entry, its function table, and the parts written in assembler (/bin/driver.bin).
 *   driver.bin is linked at offset 0 of its own segment (tz/drv16.ld); kexec puts that segment in every process's
 *   area (user/drvpsp.h), and a command reaches it through user/drvc.S.
 *
 *   drv_entry (offset 0): a command far-called it with BX = index * 2 (user/drvtab.h).
 *     The far return address goes to the caller's own area (DS = the caller), so nothing is kept here and every
 *     process has its own; the function is then a plain near call with its arguments where a near callee expects
 *     them. CS = the DRIVER, DS = SS = the caller: pointer arguments are used as they are.
 *   The DRIVER has no data, bss or constants (drv16.ld refuses them): whatever must be remembered per process is in
 *   the process's area.
 *
 *   Here, in assembler: putchar / getchar / drv_sys (the INT 80h door for tz/drv16.c), the string and memory
 *   functions, prnum (unsigned division), ftell / readdir_size (32-bit results in DX:AX, which tzcc cannot return),
 *   and drv_ult / drv_udiv (tzcc compares and divides 16-bit values as signed).
 *   cdecl as ia16-gcc expects it: arguments on the stack, AX (DX:AX) back, SI / DI / BP / ES / DS kept.
 */
#include "drvpsp.h"
        .code16
        .att_syntax prefix

        .section .text.entry,"ax",@progbits
        .globl  drv_entry
drv_entry:
        popw    DRV_PSP_RIP
        popw    DRV_PSP_RCS
        call    *%cs:drv_tbl(%bx)
        pushw   DRV_PSP_RCS
        pushw   DRV_PSP_RIP
        lret
drv_nosys:                              /* an index the DRIVER does not provide */
        xorw    %ax, %ax
        xorw    %dx, %dx
        ret

#define DRV(n, name)    .word name
#define DRVZ(n, name)   .word drv_nosys
#define DRVX(n)         .word drv_nosys
        .p2align 1
drv_tbl:
#include "drvtab.h"

        .text
/* ---- console and the kernel door ---- */
        .globl  putchar
putchar:                                /* putchar(c) -> c */
        movw    %sp, %bx
        movw    %ss:2(%bx), %cx
        movb    %cl, %al
        movb    $1, %ah
        int     $0x80
        movw    %cx, %ax
        ret

        .globl  getchar
getchar:                                /* getchar() -> 0..255, -1 at the end of a pipe / "< file" */
        movb    $2, %ah
        int     $0x80
        cmpw    $-1, %ax
        je      1f
        xorb    %ah, %ah
1:      ret

        .globl  drv_sys
drv_sys:                                /* drv_sys(func, al, bx, cx, si, di) -> AX (as user/usyscall.s) */
        pushw   %bp
        movw    %sp, %bp
        pushw   %si
        pushw   %di
        movb    4(%bp), %ah
        movb    6(%bp), %al
        movw    8(%bp), %bx
        movw    10(%bp), %cx
        movw    12(%bp), %si
        movw    14(%bp), %di
        int     $0x80
        popw    %di
        popw    %si
        popw    %bp
        ret

/* ---- words through a pointer: tzcc (ia16) reads and writes every pointer target as a byte ---- */
        .globl  drv_peekw
drv_peekw:                              /* drv_peekw(addr) -> the word at DS:addr */
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        movw    (%bx), %ax
        ret

        .globl  drv_pokew
drv_pokew:                              /* drv_pokew(addr, v): DS:addr = v */
        movw    %sp, %bx
        movw    %ss:4(%bx), %ax
        movw    %ss:2(%bx), %bx
        movw    %ax, (%bx)
        ret

/* printf(fmt, ...): the arguments after fmt are on the caller's stack (cdecl); tzcc copies its parameters into its
 * frame, so the address of one is not where the next lives. Hand drv_vprintf (tz/drv16.c) the real address:
 * SP + 4 here, SS = DS (the caller's segment), so DS can read it. */
        .globl  printf
printf:
        movw    %sp, %bx
        leaw    4(%bx), %ax
        pushw   %ax
        pushw   %ss:2(%bx)
        call    drv_vprintf
        addw    $4, %sp
        ret

/* ---- numbers ---- */
        .globl  prnum
prnum:                                  /* prnum(v): unsigned decimal */
        pushw   %bp
        movw    %sp, %bp
        movw    4(%bp), %ax
        movw    $10, %bx
        xorw    %cx, %cx
1:      xorw    %dx, %dx
        divw    %bx
        pushw   %dx
        incw    %cx
        testw   %ax, %ax
        jnz     1b
2:      popw    %ax
        addb    $'0', %al
        pushw   %cx
        pushw   %ax
        call    putchar
        popw    %ax
        popw    %cx
        loop    2b
        popw    %bp
        ret

        .globl  drv_ult
drv_ult:                                /* drv_ult(a, b): 1 if a < b as unsigned */
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        cmpw    %ss:4(%bx), %ax
        sbbw    %ax, %ax
        negw    %ax
        ret

        .globl  drv_udiv
drv_udiv:                               /* drv_udiv(a, b): a / b unsigned */
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        xorw    %dx, %dx
        divw    %ss:4(%bx)
        ret

/* ---- 32-bit results ---- */
        .globl  ftell
ftell:                                  /* long ftell(FILE *f): the position, -1 if f is not open */
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        testw   %bx, %bx
        jz      1f
        cmpw    $0, 6(%bx)
        je      1f
        movw    8(%bx), %ax
        movw    10(%bx), %dx
        ret
1:      movw    $-1, %ax
        movw    %ax, %dx
        ret

        .globl  readdir_size
readdir_size:                           /* unsigned long readdir_size(void): size of the last readdir entry (AH=12) */
        movb    $12, %ah
        int     $0x80
        xorw    %dx, %dx
        ret

/* ---- strings and memory (ES is set to DS for the string instructions and given back) ---- */
        .globl  strlen
strlen:
        pushw   %di
        pushw   %es
        pushw   %ds
        popw    %es
        movw    %sp, %bx
        movw    %ss:6(%bx), %di
        xorb    %al, %al
        movw    $-1, %cx
        cld
        repne scasb
        movw    $-2, %ax
        subw    %cx, %ax
        popw    %es
        popw    %di
        ret

        .globl  strcmp
strcmp:                                 /* strcmp(a, b): <0 / 0 / >0 (unsigned bytes) */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %si
        movw    %ss:8(%bx), %di
1:      movb    (%si), %al
        movb    (%di), %ah
        incw    %si
        incw    %di
        cmpb    %ah, %al
        jne     2f
        testb   %al, %al
        jnz     1b
        xorw    %ax, %ax
        jmp     3f
2:      sbbw    %ax, %ax                /* -1 if a < b */
        orw     $1, %ax
3:      popw    %di
        popw    %si
        ret

        .globl  strncmp
strncmp:                                /* strncmp(a, b, n) */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %si
        movw    %ss:8(%bx), %di
        movw    %ss:10(%bx), %cx
        xorw    %ax, %ax
        jcxz    3f
1:      movb    (%si), %al
        movb    (%di), %ah
        incw    %si
        incw    %di
        cmpb    %ah, %al
        jne     2f
        testb   %al, %al
        jz      4f
        loop    1b
4:      xorw    %ax, %ax
        jmp     3f
2:      sbbw    %ax, %ax
        orw     $1, %ax
3:      popw    %di
        popw    %si
        ret

        .globl  strcpy
strcpy:                                 /* strcpy(d, s) -> d */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %di
        movw    %ss:8(%bx), %si
        movw    %di, %dx
1:      movb    (%si), %al
        movb    %al, (%di)
        incw    %si
        incw    %di
        testb   %al, %al
        jnz     1b
        movw    %dx, %ax
        popw    %di
        popw    %si
        ret

        .globl  strncpy
strncpy:                                /* strncpy(d, s, n) -> d: copies up to n, pads with 0 */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %di
        movw    %ss:8(%bx), %si
        movw    %ss:10(%bx), %cx
        movw    %di, %dx
        jcxz    3f
1:      movb    (%si), %al
        movb    %al, (%di)
        incw    %di
        testb   %al, %al
        jz      2f
        incw    %si
2:      loop    1b
3:      movw    %dx, %ax
        popw    %di
        popw    %si
        ret

        .globl  strcat
strcat:                                 /* strcat(d, s) -> d */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %di
        movw    %ss:8(%bx), %si
        movw    %di, %dx
1:      cmpb    $0, (%di)
        je      2f
        incw    %di
        jmp     1b
2:      movb    (%si), %al
        movb    %al, (%di)
        incw    %si
        incw    %di
        testb   %al, %al
        jnz     2b
        movw    %dx, %ax
        popw    %di
        popw    %si
        ret

        .globl  strchr
strchr:                                 /* strchr(s, c): first c (c may be 0: the terminator), 0 if none */
        movw    %sp, %bx
        movb    %ss:4(%bx), %ah
        movw    %ss:2(%bx), %bx
1:      movb    (%bx), %al
        cmpb    %ah, %al
        je      2f
        incw    %bx
        testb   %al, %al
        jnz     1b
        xorw    %ax, %ax
        ret
2:      movw    %bx, %ax
        ret

        .globl  strrchr
strrchr:                                /* strrchr(s, c): last c, 0 if none */
        movw    %sp, %bx
        movb    %ss:4(%bx), %ah
        movw    %ss:2(%bx), %bx
        xorw    %dx, %dx
1:      movb    (%bx), %al
        cmpb    %ah, %al
        jne     2f
        movw    %bx, %dx
2:      incw    %bx
        testb   %al, %al
        jnz     1b
        movw    %dx, %ax
        ret

        .globl  strstr
strstr:                                 /* strstr(h, n): first n in h, 0 if none (h if n is empty) */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %dx         /* h */
        movw    %ss:8(%bx), %bx         /* n */
1:      movw    %dx, %si
        movw    %bx, %di
2:      movb    (%di), %al
        testb   %al, %al
        jz      4f                      /* n ran out: found at dx */
        cmpb    (%si), %al
        jne     3f
        incw    %si
        incw    %di
        jmp     2b
3:      movw    %dx, %si
        cmpb    $0, (%si)
        je      5f                      /* h ran out */
        incw    %dx
        jmp     1b
4:      movw    %dx, %ax
        jmp     6f
5:      xorw    %ax, %ax
6:      popw    %di
        popw    %si
        ret

        .globl  memcpy
memcpy:                                 /* memcpy(d, s, n) -> d */
        pushw   %si
        pushw   %di
        pushw   %es
        pushw   %ds
        popw    %es
        movw    %sp, %bx
        movw    %ss:8(%bx), %di
        movw    %ss:10(%bx), %si
        movw    %ss:12(%bx), %cx
        movw    %di, %ax
        cld
        rep movsb
        popw    %es
        popw    %di
        popw    %si
        ret

        .globl  memmove
memmove:                                /* memmove(d, s, n) -> d: backwards when d is above s */
        pushw   %si
        pushw   %di
        pushw   %es
        pushw   %ds
        popw    %es
        movw    %sp, %bx
        movw    %ss:8(%bx), %di
        movw    %ss:10(%bx), %si
        movw    %ss:12(%bx), %cx
        movw    %di, %ax
        cmpw    %si, %di
        jbe     1f
        addw    %cx, %si
        addw    %cx, %di
        decw    %si
        decw    %di
        std
1:      rep movsb
        cld
        popw    %es
        popw    %di
        popw    %si
        ret

        .globl  memset
memset:                                 /* memset(d, c, n) -> d */
        pushw   %di
        pushw   %es
        pushw   %ds
        popw    %es
        movw    %sp, %bx
        movw    %ss:6(%bx), %di
        movb    %ss:8(%bx), %al
        movw    %ss:10(%bx), %cx
        movw    %di, %dx
        cld
        rep stosb
        movw    %dx, %ax
        popw    %es
        popw    %di
        ret

        .globl  memcmp
memcmp:                                 /* memcmp(a, b, n): <0 / 0 / >0 (unsigned bytes) */
        pushw   %si
        pushw   %di
        movw    %sp, %bx
        movw    %ss:6(%bx), %si
        movw    %ss:8(%bx), %di
        movw    %ss:10(%bx), %cx
        xorw    %ax, %ax
        jcxz    3f
1:      movb    (%si), %al
        cmpb    (%di), %al
        jne     2f
        incw    %si
        incw    %di
        loop    1b
        xorw    %ax, %ax
        jmp     3f
2:      sbbw    %ax, %ax
        orw     $1, %ax
3:      popw    %di
        popw    %si
        ret
