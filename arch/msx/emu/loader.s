;===================================================================
; loader - the part of the emulator boot cartridge that runs in RAM (page 2, 0x9000).
; Reads sector 0 of drive A to 0xC000 and calls 0xC01E, as a real MSX disk ROM does
; at cold boot.  C-BIOS does not boot from disks, so this stands in for that step.
;===================================================================
        .module loader
        .area   _LOADER (ABS)
        .org    0x9000

        di
        ld      sp, #0x9F00
        ld      de, #0
        ld      hl, #0xC000
        ld      b, #1
        or      a               ; carry = 0: read
        call    do_dskio
        jp      c, fail
        ; as a real disk ROM calls the boot sector: the BIOS in page 0, the disk ROM in page 1
        ld      b, #0
        ld      a, (EXPTBL)
        and     #0x80
        call    set_page
        ld      b, #1
        ld      a, (DRVTBL1)
        call    set_page
        call    0xC01E
fail:   di
        halt

        .include "../dskio.inc"
