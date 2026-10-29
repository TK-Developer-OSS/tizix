/* arch/x86-ia16/user/drvc.S : the command side of the DRIVER (tz build), linked into every command instead of
 *   ulibc.o + libc.o. As on the z80 (user/stdio.h drv_tbl), the stdio and string functions exist once, in
 *   /bin/driver.bin, resident in the pool; a command carries only a 5-byte stub per function it calls (each stub has
 *   its own section, so the link drops the unused ones) and drv_call below.
 *
 *   drv_call: the caller made a near cdecl call to the stub, BX = index * 2.
 *     It keeps the near return address in this process's own bss, far-calls the DRIVER entry through the far pointer
 *     kexec left at DS:DRV_PSP_ENTRY, and jumps back. The DRIVER function then sees its arguments exactly where a near
 *     callee would, and DS = SS = this process, so pointer arguments need no change.
 *     Only one call is ever in flight per process (no threads, the DRIVER does not call back), so one save word is
 *     enough; another process has its own.
 *   syscall5 stays here: commands call the kernel through it directly.
 */
#include "drvpsp.h"
        .code16
        .att_syntax prefix

#define DRV(n, name)                    \
        .section .text.drv.name,"ax",@progbits ; \
        .globl  name ;                  \
name:   movw    $(n) * 2, %bx ;         \
        jmp     drv_call
#define DRVZ(n, name) DRV(n, name)
#define DRVX(n)
#include "drvtab.h"

        .section .text.drv_call,"ax",@progbits
drv_call:
        popw    drv_nret
        lcall   *DRV_PSP_ENTRY
        jmp     *drv_nret

        .section .bss
        .p2align 1
drv_nret: .space 2

/* unsigned syscall5(func, al, bx, cx, si, di): AH = func, AL = al, BX / CX / SI / DI, INT 80h, AX back (usyscall.s) */
        .section .text.syscall5,"ax",@progbits
        .globl  syscall5
syscall5:
        pushw   %bp
        movw    %sp, %bp
        pushw   %si
        pushw   %di
        pushw   %bx
        movb    4(%bp), %ah
        movb    6(%bp), %al
        movw    8(%bp), %bx
        movw    10(%bp), %cx
        movw    12(%bp), %si
        movw    14(%bp), %di
        int     $0x80
        popw    %bx
        popw    %di
        popw    %si
        popw    %bp
        ret
