# ia16rt/rt.s - runtime for tzcc ia16 PoC programs.
# Entered at 0x1000:0. CS=DS=ES=0x1000 (code+data), SS=0x2000 (a different segment on purpose).
        .code16
        .text
        .globl _rt_start
_rt_start:
        cli
        movw    %cs, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    $0x2000, %ax
        movw    %ax, %ss
        movw    $0xfffe, %sp
        sti
        call    main
        movw    %ax, %bx
        movw    $.Lres, %si
        call    puts_si
        movw    %bx, %ax
        call    putdec
        movb    $10, %al
        call    putc_al
        movw    $0xf4, %dx
        xorb    %al, %al
        outb    %al, %dx
2:      hlt
        jmp     2b

# AL -> serial COM1
putc_al:
        pushw   %dx
        pushw   %ax
        movw    $0x3fd, %dx
1:      inb     %dx, %al
        testb   $0x20, %al
        jz      1b
        popw    %ax
        movw    $0x3f8, %dx
        outb    %al, %dx
        popw    %dx
        ret
puts_si:
        lodsb
        testb   %al, %al
        jz      1f
        call    putc_al
        jmp     puts_si
1:      ret
# AX (signed) -> decimal
putdec:
        testw   %ax, %ax
        jns     1f
        pushw   %ax
        movb    $'-', %al
        call    putc_al
        popw    %ax
        negw    %ax
1:      xorw    %cx, %cx
        movw    $10, %bx
2:      xorw    %dx, %dx
        divw    %bx
        pushw   %dx
        incw    %cx
        testw   %ax, %ax
        jnz     2b
3:      popw    %ax
        addb    $'0', %al
        call    putc_al
        loop    3b
        ret

        .globl putchar
putchar:
        pushw   %bp
        movw    %sp, %bp
        movw    4(%bp), %ax
        call    putc_al
        popw    %bp
        ret
        .globl puts
puts:
        pushw   %bp
        movw    %sp, %bp
        pushw   %si
        movw    4(%bp), %si
        call    puts_si
        movb    $10, %al
        call    putc_al
        popw    %si
        popw    %bp
        ret
        .data
.Lres:  .asciz  "RESULT="
