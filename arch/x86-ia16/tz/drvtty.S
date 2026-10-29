/* tz/drvtty.S : the serial port in the DRIVER (task #245). /dev/ttyS1 = COM2 (0x2F8, IRQ3); ttyS0 = COM1 stays the console's.
 *   The receive interrupt is taken here, not in the kernel: tty_open hooks the vector, the ISR fills a 1KB ring that
 *   lives in the DRIVER's own image (addressed with CS, as the ISR runs under whatever DS / SS it interrupted). The
 *   ring fills whatever the file system, the BIOS or another process is doing, so a long INT 13h no longer loses bytes.
 *   RTS is dropped when the ring is 3/4 full and raised again at 1/4, so a modem that honours RTS/CTS cannot overrun it.
 *   One owner at a time: its DS (every process has its own segment). A second tty_open fails while the owner runs.
 *   Calls (user/drvtab.h 79..84), cdecl, AX back:
 *     tty_open(unit, divisor)  0 / -1 (unit not 1, or owned by a running process: syscall 56 asks the kernel; an owner that
 *                              ended without closing is replaced)
 *     tty_close()              0 / -1 (not the owner)
 *     tty_getc()               0..255, -1 when the ring is empty (never waits)
 *     tty_putc(c)              0 / -1 (the transmitter did not become ready)
 *     tty_avail()              bytes waiting
 *     tty_drops()              bytes lost to a full ring since the last open
 */
        .code16
        .att_syntax prefix

#define COM_BASE  0x2F8
#define COM_VEC   (0x0B * 4)
#define PIC_BIT   0x08
#define RING      1024
#define RMASK     (RING - 1)

        .text
        .globl  tty_open
tty_open:                               /* tty_open(unit, divisor) */
        pushw   %bp
        movw    %sp, %bp
        cmpw    $1, 4(%bp)
        jne     to_fail
        movw    %ds, %ax
        movw    %cs:tty_owner, %dx
        testw   %dx, %dx
        jz      to_go
        cmpw    %ax, %dx
        je      to_mine
        pushw   %si                     /* the owner may have ended without closing: is its segment still running? */
        movw    %dx, %si
        movb    $56, %ah
        int     $0x80
        popw    %si
        testw   %ax, %ax
        jnz     to_fail                 /* alive: another process has it */
        movw    %ds, %ax
        jmp     to_go                   /* gone: take it over */
to_mine:
        xorw    %ax, %ax                /* already ours: nothing to redo */
        jmp     to_end
to_go:  movw    %ax, %cs:tty_owner
        movw    $0, %cs:tty_head
        movw    $0, %cs:tty_tail
        movb    $1, %cs:tty_rts
        movw    $0, %cs:tty_lost
        pushw   %es
        pushw   %di
        cli
        xorw    %ax, %ax
        movw    %ax, %es
        movw    $COM_VEC, %di
        movw    %es:(%di), %ax          /* keep the old vector for tty_close */
        movw    %ax, %cs:tty_ovec
        movw    %es:2(%di), %ax
        movw    %ax, %cs:tty_ovec + 2
        movw    $tty_isr, %ax
        movw    %ax, %es:(%di)
        movw    %cs, %ax
        movw    %ax, %es:2(%di)
        sti
        popw    %di
        popw    %es
        movw    $COM_BASE + 3, %dx      /* LCR: divisor latch on */
        movb    $0x80, %al
        outb    %al, %dx
        movw    $COM_BASE, %dx
        movw    6(%bp), %ax
        outb    %al, %dx                /* DLL */
        incw    %dx
        movb    %ah, %al
        outb    %al, %dx                /* DLM */
        movw    $COM_BASE + 3, %dx      /* 8N1 */
        movb    $0x03, %al
        outb    %al, %dx
        movw    $COM_BASE + 2, %dx      /* FIFO on, cleared, interrupt at 1 byte */
        movb    $0x07, %al
        outb    %al, %dx
        movw    $COM_BASE + 4, %dx      /* DTR RTS OUT2 (OUT2 gates the IRQ) */
        movb    $0x0B, %al
        outb    %al, %dx
        movw    $COM_BASE, %dx          /* drop anything already waiting */
        inb     %dx, %al
        movw    $COM_BASE + 1, %dx      /* IER: receive data */
        movb    $0x01, %al
        outb    %al, %dx
        inb     $0x21, %al              /* unmask IRQ3 */
        andb    $~PIC_BIT, %al
        outb    %al, $0x21
        xorw    %ax, %ax
        jmp     to_end
to_fail:
        movw    $-1, %ax
to_end: popw    %bp
        ret

/* the common close: IER off, IRQ3 masked, old vector back, no owner */
tty_shut:
        movw    $COM_BASE + 1, %dx
        xorb    %al, %al
        outb    %al, %dx
        movw    $COM_BASE + 4, %dx
        outb    %al, %dx
        inb     $0x21, %al
        orb     $PIC_BIT, %al
        outb    %al, $0x21
        pushw   %es
        pushw   %di
        cli
        xorw    %ax, %ax
        movw    %ax, %es
        movw    $COM_VEC, %di
        movw    %cs:tty_ovec, %ax
        movw    %ax, %es:(%di)
        movw    %cs:tty_ovec + 2, %ax
        movw    %ax, %es:2(%di)
        sti
        popw    %di
        popw    %es
        movw    $0, %cs:tty_owner
        ret

        .globl  tty_close
tty_close:
        movw    %cs:tty_owner, %ax
        movw    %ds, %dx
        cmpw    %ax, %dx
        jne     tc_fail
        testw   %ax, %ax
        jz      tc_fail
        call    tty_shut
        xorw    %ax, %ax
        ret
tc_fail:
        movw    $-1, %ax
        ret

        .globl  tty_getc
tty_getc:
        movw    %ds, %ax
        cmpw    %cs:tty_owner, %ax
        jne     tg_none
        movw    %cs:tty_tail, %bx
        cmpw    %cs:tty_head, %bx
        je      tg_none
        movb    %cs:tty_buf(%bx), %al
        xorb    %ah, %ah
        incw    %bx
        andw    $RMASK, %bx
        movw    %bx, %cs:tty_tail
        cmpb    $0, %cs:tty_rts         /* RTS is down: raise it once the ring is at 1/4 */
        jne     tg_end
        movw    %cs:tty_head, %dx
        subw    %bx, %dx
        andw    $RMASK, %dx
        cmpw    $256, %dx
        ja      tg_end
        movb    $1, %cs:tty_rts
        pushw   %ax
        movw    $COM_BASE + 4, %dx
        movb    $0x0B, %al
        outb    %al, %dx
        popw    %ax
tg_end: ret
tg_none:
        movw    $-1, %ax
        ret

        .globl  tty_drops
tty_drops:
        movw    %cs:tty_lost, %ax
        ret

        .globl  tty_avail
tty_avail:
        movw    %cs:tty_head, %ax
        subw    %cs:tty_tail, %ax
        andw    $RMASK, %ax
        ret

        .globl  tty_putc
tty_putc:                               /* tty_putc(c) */
        movw    %ds, %ax
        cmpw    %cs:tty_owner, %ax
        jne     tp_fail
        movw    %sp, %bx
        movb    %ss:2(%bx), %ah
        movw    $COM_BASE + 5, %dx
        movw    $0xFFFF, %cx
tp_w:   inb     %dx, %al
        testb   $0x20, %al
        jnz     tp_go
        loop    tp_w
        jmp     tp_fail
tp_go:  movw    $COM_BASE, %dx
        movb    %ah, %al
        outb    %al, %dx
        xorw    %ax, %ax
        ret
tp_fail:
        movw    $-1, %ax
        ret

/* the receive interrupt: every byte the UART holds goes into the ring (a full ring drops the newest) */
tty_isr:
        pushw   %ax
        pushw   %bx
        pushw   %dx
ti_lp:  movw    $COM_BASE + 5, %dx
        inb     %dx, %al
        testb   $1, %al
        jz      ti_eoi
        movw    $COM_BASE, %dx
        inb     %dx, %al
        movw    %cs:tty_head, %bx
        movb    %al, %cs:tty_buf(%bx)
        incw    %bx
        andw    $RMASK, %bx
        cmpw    %cs:tty_tail, %bx
        jne     ti_ok
        incw    %cs:tty_lost            /* full: the byte is lost, the head stays */
        jmp     ti_lp
ti_ok:  movw    %bx, %cs:tty_head
        cmpb    $0, %cs:tty_rts         /* 3/4 full: stop the sender */
        je      ti_lp
        subw    %cs:tty_tail, %bx
        andw    $RMASK, %bx
        cmpw    $768, %bx
        jb      ti_lp
        movb    $0, %cs:tty_rts
        movw    $COM_BASE + 4, %dx
        movb    $0x09, %al
        outb    %al, %dx
        jmp     ti_lp
ti_eoi: movb    $0x20, %al
        outb    %al, $0x20
        popw    %dx
        popw    %bx
        popw    %ax
        iret

/* the state lives in the image, after the code; written through CS */
        .p2align 1
tty_owner: .word 0
tty_ovec:  .word 0, 0
tty_lost:  .word 0
tty_head:  .word 0
tty_tail:  .word 0
tty_rts:   .byte 1
           .p2align 1
tty_buf:   .space RING
