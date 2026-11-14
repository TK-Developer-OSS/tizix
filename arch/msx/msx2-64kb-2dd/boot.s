;===================================================================
; boot.s - the MSX boot sector (sector 0 of the disk, 512 bytes).
;
; The disk ROM reads it to 0xC000 and calls 0xC01E, with the BIOS in page 0 and the disk
; ROM in page 1.  Only the first 256 bytes of the sector can be counted on: on a real disk
; ROM (WebMSX's MSX2J) the rest was zeros or overwritten by the disk ROM's own stack.  So
; this code stays well below 0xC0FF and does one thing: it reads s2 (logical sector 1) to
; 0x8000 with the disk ROM's DSKIO, and jumps there.
;   0x00-0x02  jump
;   0x0B-0x1D  BPB, 0x24-0x3D extended BPB ("FAT12" at 0x36): mkfs.fat's, kept by mkdisk.py
;   0x1E-0x20  entry (call target)
;   0x3E-       stage 1 (below)
;===================================================================
        .module boot
        .area   _BOOT (ABS)
        .org    0xC000

        .db     0xEB, 0x1C, 0x90        ; jr 0xC01E
        .ds     0x1B                    ; BPB: mkdisk.py keeps mkfs.fat's

        .org    0xC01E
entry:  jp      stage1
        .org    0xC03E
stage1:
        ld      hl, #0x8000             ; buffer: page 2, RAM
        ld      de, #1                  ; logical sector 1 = s2
        ld      bc, #0x01F9             ; B = 1 sector, C = media descriptor 0xF9 (2DD)
        xor     a                       ; A = drive 0, carry clear = read
        call    0x4010                  ; DSKIO
        jp      c, fail
        jp      0x8000
fail:   di
        halt
