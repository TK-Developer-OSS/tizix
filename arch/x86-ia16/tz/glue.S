/* tz/glue.s - the few things tzcc-compiled kernel C cannot express itself. */
/* tzcc cannot share data with other objects or use inline asm, so each is a tiny function here. */
        .code16
        .text
        .globl  tz_scstk_base
tz_scstk_base:                          # the file system stack (fs_tramp); there is no syscall stack
        movw    $fs_stack, %ax
        ret
        .globl  tz_bss_end
tz_bss_end:
        movw    $__bss_end, %ax
        ret
        .globl  tz_pool_base
tz_pool_base:                           # first 4KB block after the kernel (physical >> 12) = high byte of its segment
        movw    $__bss_end + KSEG_DEF * 16 + 0xFFF, %ax
        movb    %ah, %al
        movb    $4, %cl
        shrb    %cl, %al
        xorb    %ah, %ah
        ret
        .globl  tz_sp
tz_sp:
        movw    %sp, %ax                # callers SP after this call returns
        addw    $2, %ax
        ret
        .globl  tz_set_nslot_rt
tz_set_nslot_rt:
        movw    %sp, %bx                # 8086 cannot address through SP
        movb    %ss:2(%bx), %al
        movb    %al, x86_nslot_rt
        ret
        .globl  tz_halt
tz_halt:
        hlt
        ret

/* plat.hs IRQ_OFF/IRQ_ON nesting state (was defined at the end of kmain.c) */
        .bss
        .globl  x86_irq_depth
        .globl  x86_irq_was
x86_irq_depth: .space 1
x86_irq_was:   .space 1

/* ---- port I/O, VGA and interrupt-flag primitives for tz/console.c ---- */
/* Arguments are on the stack (cdecl); 8086 cannot address through SP, so BX is the frame pointer here.
 * [bx] defaults to DS, so every argument read carries %ss: (in an ISR SS != DS). */
        .text
        .globl  tz_outb
tz_outb:                                # tz_outb(port, val)
        movw    %sp, %bx
        movw    %ss:2(%bx), %dx
        movb    %ss:4(%bx), %al
        outb    %al, %dx
        ret
        .globl  tz_inb
tz_inb:                                 # tz_inb(port) -> 0..255
        movw    %sp, %bx
        movw    %ss:2(%bx), %dx
        xorw    %ax, %ax
        inb     %dx, %al
        ret
        .globl  tz_io_delay
tz_io_delay:
        xorb    %al, %al
        outb    %al, $0x80
        ret
        .globl  tz_vwrite
tz_vwrite:                              # tz_vwrite(byte_offset, cell): B800:offset = cell
        movw    %sp, %bx
        movw    %ss:2(%bx), %dx
        movw    %ss:4(%bx), %ax
        pushw   %es
        pushw   %ax
        movw    $0xB800, %ax
        movw    %ax, %es
        popw    %ax
        movw    %dx, %bx
        movw    %ax, %es:(%bx)
        popw    %es
        ret
        .globl  tz_vscroll
tz_vscroll:                             # move rows 1..24 up one row (the last row is cleared by the caller)
        pushw   %ds
        pushw   %es
        pushw   %si
        pushw   %di
        movw    $0xB800, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    $160, %si
        xorw    %di, %di
        movw    $1920, %cx
        cld
        rep movsw
        popw    %di
        popw    %si
        popw    %es
        popw    %ds
        ret
        .globl  tz_irq_save
tz_irq_save:                            # returns FLAGS, then CLI
        pushfw
        popw    %ax
        cli
        ret
        .globl  tz_irq_restore
tz_irq_restore:                         # tz_irq_restore(flags)
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        pushw   %ax
        popfw
        ret
        .globl  tz_qemu_exit
tz_qemu_exit:
        xorb    %al, %al
        movw    $0xF4, %dx
        outb    %al, %dx
        ret

/* ---- for tz/init.c ---- */
        .text
        .globl  tz_kwork
tz_kwork:
        movw    $kwork, %ax
        ret
        .globl  tz_kyield
tz_kyield:                              # KYIELD: let the timer ISR run
        sti
        hlt
        cli
        ret

/* ---- for tz/kernel.c ---- */
/* kwork[]: working area shared with crt0.s (pid table, sp/ss tables, tick/epoch, block/wake flags ...) */
        .bss
        .globl  kwork
kwork:  .space  KWORK_SIZE

        .text
        .globl  tz_irq_off
tz_irq_off:                             # IRQ_OFF() with nesting (plat.h)
        pushfw
        popw    %ax
        cli
        movb    x86_irq_depth, %dl
        incb    x86_irq_depth
        testb   %dl, %dl
        jnz     1f
        movb    $0, x86_irq_was
        testw   $0x200, %ax
        jz      1f
        movb    $1, x86_irq_was
1:      ret
        .globl  tz_irq_on
tz_irq_on:                              # IRQ_ON() with nesting (plat.h)
        cmpb    $0, x86_irq_depth
        jne     1f
        sti
        ret
1:      decb    x86_irq_depth
        jnz     2f
        cmpb    $0, x86_irq_was
        je      2f
        sti
2:      ret
        .globl  tz_park
tz_park:                                # sti; hlt  (proc_block)
        sti
        hlt
        ret
        .globl  tz_kw_w
tz_kw_w:                                # tz_kw_w(off) -> word at kwork+off
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        movw    kwork(%bx), %ax
        ret
        .globl  tz_kw_setw
tz_kw_setw:                             # tz_kw_setw(off, value)
        movw    %sp, %bx
        movw    %ss:4(%bx), %ax
        movw    %ss:2(%bx), %bx
        movw    %ax, kwork(%bx)
        ret
        .globl  tz_peekb
tz_peekb:                               # tz_peekb(seg, off) -> byte at seg:off (pipe buffer in a pool block)
        pushw   %es
        movw    %sp, %bx
        movw    %ss:4(%bx), %es
        movw    %ss:6(%bx), %bx
        xorw    %ax, %ax
        movb    %es:(%bx), %al
        popw    %es
        ret
        .globl  tz_pokeb
tz_pokeb:                               # tz_pokeb(seg, off, val): seg:off = val
        pushw   %es
        movw    %sp, %bx
        movw    %ss:4(%bx), %es
        movb    %ss:8(%bx), %al
        movw    %ss:6(%bx), %bx
        movb    %al, %es:(%bx)
        popw    %es
        ret
        .globl  time_get
time_get:                               # unsigned long -> DX:AX (epoch seconds)
        pushfw
        cli
        movw    kwork+KW_EPOCH_O, %ax
        movw    kwork+KW_EPOCH_O+2, %dx
        popfw
        ret
        .globl  time_set
time_set:                               # time_set(unsigned long sec): epoch = sec, sub-tick = 0
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        movw    %ss:4(%bx), %dx
        pushfw
        cli
        movw    %ax, kwork+KW_EPOCH_O
        movw    %dx, kwork+KW_EPOCH_O+2
        movb    $0, kwork+KW_SUB_O
        popfw
        ret



/* ---- for tz/diskio.c ---- */
        .text
        .globl  tz_boot_drive
tz_boot_drive:                          /* boot drive number left by stage2 at 0000:0510 */
        pushw   %es
        xorw    %ax, %ax
        movw    %ax, %es
        movb    %es:0x510, %al
        popw    %es
        ret

/* ---- for tz/kexec.c: the slot tables (crt0.s reads x86_slot_seg) ---- */
        .data
        .globl  x86_slot_seg
        .globl  x86_slot_blk
x86_slot_seg:   .word   KSEG_DEF                /* slot 0 = the kernel segment */
                .space  2 * (MAXSLOT - 1)
x86_slot_blk:   .space  2 * MAXSLOT
        .text
        .globl  tz_seg_get
tz_seg_get:                             /* tz_seg_get(k) */
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        shlw    $1, %bx
        movw    x86_slot_seg(%bx), %ax
        ret
        .globl  tz_seg_set
tz_seg_set:                             /* tz_seg_set(k, v) */
        movw    %sp, %bx
        movw    %ss:4(%bx), %ax
        movw    %ss:2(%bx), %bx
        shlw    $1, %bx
        movw    %ax, x86_slot_seg(%bx)
        ret
        .globl  tz_blk_get
tz_blk_get:
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        shlw    $1, %bx
        movw    x86_slot_blk(%bx), %ax
        ret
        .globl  tz_blk_set
tz_blk_set:
        movw    %sp, %bx
        movw    %ss:4(%bx), %ax
        movw    %ss:2(%bx), %bx
        shlw    $1, %bx
        movw    %ax, x86_slot_blk(%bx)
        ret

/* ---- for the software memory limit (tz/kmain.c) ---- */
        .text
        .globl  tz_set_lowword
tz_set_lowword:                         /* tz_set_lowword(off, v): 0000:off = v */
        movw    %sp, %bx
        movw    %ss:4(%bx), %ax
        movw    %ss:2(%bx), %bx
        pushw   %es
        pushw   %ax
        xorw    %ax, %ax
        movw    %ax, %es
        popw    %ax
        movw    %ax, %es:(%bx)
        popw    %es
        ret

/* ---- for tz/fs.c, tz/sysfile.c: what tzcc cannot do on 16-bit values ---- */
        .text
        .globl  ult
ult:                                    /* ult(a, b): 1 if a < b as unsigned (tzcc compares signed) */
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        cmpw    %ss:4(%bx), %ax
        sbbw    %ax, %ax
        negw    %ax
        ret
        .globl  tz_udiv
tz_udiv:                                /* tz_udiv(a, b): a / b unsigned (tzcc divides signed) */
        movw    %sp, %bx
        movw    %ss:2(%bx), %ax
        xorw    %dx, %dx
        divw    %ss:4(%bx)
        ret
        .globl  tz_exitcode
tz_exitcode:                            /* tz_exitcode(n): x86_exitcode[n] (crt0.s) */
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        xorw    %ax, %ax
        movb    x86_exitcode(%bx), %al
        ret
        .globl  tz_set_exitcode
tz_set_exitcode:                        /* tz_set_exitcode(n, v) */
        movw    %sp, %bx
        movb    %ss:4(%bx), %al
        movw    %ss:2(%bx), %bx
        movb    %al, x86_exitcode(%bx)
        ret
        .globl  tz_time_lo
tz_time_lo:
        movw    kwork+KW_EPOCH_O, %ax
        ret
        .globl  tz_time_hi
tz_time_hi:
        movw    kwork+KW_EPOCH_O+2, %ax
        ret
        .globl  x86_lowword
x86_lowword:                            /* x86_lowword(off): word at 0000:off */
        movw    %sp, %bx
        movw    %ss:2(%bx), %bx
        pushw   %es
        xorw    %ax, %ax
        movw    %ax, %es
        movw    %es:(%bx), %ax
        popw    %es
        ret

/* strcmp / memcpy / memset: src/libc/libc.o (ia16-gcc). Their arguments are on the caller's stack, which gcc code
 * reads through SS, and the pointers are the kernel's own (DS). */

/* ---------------- the shared FatFs stack (ia16-gcc code: SS == DS is required) ----------------
 * The tzcc kernel runs on the caller's stack (SS != DS), but FatFs / fatcmd / vfs are compiled by ia16-gcc, which takes
 * the address of its locals as DS pointers. T_<name> (called as a plain cdecl function by tzcc code) switches to fs_stack
 * in the kernel segment, copies the arguments, calls <name> and switches back. While kpreempt is non-zero the timer
 * interrupt only counts the tick (crt0.s _isr08), so there is one user of fs_stack at a time and the file system and
 * the BIOS disk calls never re-enter: this is the file system exclusion (#212). A call made when SS == DS already
 * (FatFs calling kprintf, which calls back into a T_ function) is a plain jump. */
        .bss
        .globl  kpreempt
kpreempt:       .space  2
fs_oss:         .space  2
fs_osp:         .space  2
        .p2align 1
fs_stack:       .space  1280
fs_stack_top:
        .text
fs_tramp:                               # AX = target, [sp] = return address, then the arguments (caller's stack)
        movw    %ax, %bx
        movw    %ss, %ax
        movw    %ds, %dx
        cmpw    %dx, %ax
        jne     1f
        jmp     *%bx                    # already on a kernel stack
1:      pushw   %bp
        movw    %sp, %bp                # [bp] bp, [bp+2] return, [bp+4..] arguments
        pushw   %si
        pushw   %di
        pushw   %es
        incb    kpreempt
        movw    %ss, fs_oss
        movw    %sp, fs_osp
        movw    %ds, %ax
        movw    %ax, %es
        leaw    4(%bp), %si             # the arguments: 6 words (never more than 5 are used)
        movw    $fs_stack_top - 12, %di
        movw    $6, %cx
        cld
        rep movsw %ss:(%si), %es:(%di)
        movw    %ax, %ss
        movw    $fs_stack_top - 12, %sp
        call    *%bx
        movw    fs_osp, %si
        movw    fs_oss, %cx
        movw    %cx, %ss
        movw    %si, %sp
        decb    kpreempt
        popw    %es
        popw    %di
        popw    %si
        popw    %bp
        ret
        .macro  FSX name, target
        .globl  T_\name
T_\name:
        movw    $\target, %ax
        jmp     fs_tramp
        .endm
        FSX vfs_resolve, fsif_resolve
        FSX vfs_init, vfs_init
        FSX fat_init, fat_init
        FSX dev_open, fsif_dev_open
        FSX dev_read, fsif_dev_read
        FSX dev_write, fsif_dev_write
        FSX fsb_open, fsb_open
        FSX fsb_close, fsb_close
        FSX fsb_read, fsb_read
        FSX fsb_write, fsb_write
        FSX fsb_lseek, fsif_lseek
        FSX fsb_seek_hi, fsif_seek_hi
        FSX fsb_unlink, fsb_unlink
        FSX fsb_mkdir, fsb_mkdir
        FSX fsb_rename, fsif_rename
        FSX kfs_df, kfs_df
        FSX kdir_open_abs, kdir_open_abs
        FSX kdir_read, kdir_read
        FSX kdir_size, kdir_size
        FSX kdir_close, kdir_close
        FSX kpath, kpath
        FSX kchdir, kchdir
        FSX kgetcwd, kgetcwd
        FSX klog_write, klog_write
        FSX redir_begin, redir_begin
        FSX redir_end, redir_end
        FSX redir_sink, redir_sink
        FSX in_begin, in_begin
        FSX in_end, in_end
        FSX in_src, in_src
        FSX f_open, f_open
        FSX f_read, f_read
        FSX f_close, f_close
