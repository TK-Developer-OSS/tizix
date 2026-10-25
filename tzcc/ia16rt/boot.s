# ia16rt/boot.s - boot sector for the tzcc ia16 PoC harness.
# Loads NSECT sectors (LBA 1..) of the floppy to 0x1000:0000 and jumps there.
        .code16
        .text
        .globl _start
_start:
        cli
        xorw    %ax, %ax
        movw    %ax, %ds
        movw    %ax, %ss
        movw    $0x7c00, %sp
        sti
        movw    $0x1000, %ax
        movw    %ax, %es
        xorw    %bx, %bx
        movw    $1, %si                 # si = LBA
1:
        movw    %si, %ax                # LBA -> CHS (18 spt, 2 heads)
        xorw    %dx, %dx
        movw    $18, %cx
        divw    %cx                     # ax = LBA/18, dx = LBA%18
        incw    %dx
        movb    %dl, %cl                # sector
        xorw    %dx, %dx
        pushw   %cx
        movw    $2, %cx
        divw    %cx                     # ax = cyl, dx = head
        popw    %cx
        movb    %al, %ch                # cylinder
        movb    %dl, %dh                # head
        xorb    %dl, %dl                # drive 0
        movw    $0x0201, %ax
        int     $0x13
        jc      9f
        addw    $512, %bx
        incw    %si
        cmpw    $NSECT+1, %si
        jbe     1b
        ljmp    $0x1000, $0
9:      hlt
        jmp     9b
        .org    510
        .word   0xaa55
