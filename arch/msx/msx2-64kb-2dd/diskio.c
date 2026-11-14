/* diskio.c - FatFs disk I/O for MSX2 (the built-in disk ROM's DSKIO through tramp.s)
 *
 *   One 2DD floppy: the FAT12 volume starts at logical sector 0 (its reserved
 *   sectors hold the boot sector and the kernel image).  FatFs sector = the disk's
 *   logical sector (512B), so no conversion is needed.  /dev/fda and /dev/fdb both
 *   reach this one disk.
 *
 *   The trampoline (page 2, at 0x8C78) swaps the ROMs in around the DSKIO call with
 *   interrupts off.  Its parameters sit at 0x8C68 (see tramp.s).  Buffers must be at
 *   0x8000 or higher (DSKIO cannot write into the ROM pages); every buffer tizix
 *   hands over is in block 0 or in a process block, so they are.
 */
#include "ff.h"
#include "diskio.h"

#define P_OP    (*(volatile unsigned char *)0x8C68)
#define P_CNT   (*(volatile unsigned char *)0x8C69)
#define P_BUF   (*(volatile unsigned *)0x8C6A)
#define P_SECT  (*(volatile unsigned *)0x8C6C)
#define P_RES   (*(volatile unsigned char *)0x8C6E)
#define MAXCNT  16                /* sectors per DSKIO call (8KB) */

/* ------------------------------------------------------------------
 * Sector cache in VRAM.  Each DSKIO call keeps interrupts off for ~0.2s (the sector has
 * to come round under the head), so keys pressed and released meanwhile were lost, and a
 * shell line that named no command read the same 8 directory/FAT sectors every time.
 *   192 slots of 512B at VRAM 0x08000-0x1FFFF (unused in SCREEN 0), slot = sector % 192.
 *   Tags (the sector held, 0xFFFF = empty) at VRAM 0x07000.  Writes go to the disk
 *   first, then into the cache (write-through), so the disk is always current.
 *   Only with 128KB of VRAM (BIOS MODE 0xFAFC bit 2); with less, every lookup misses
 *   and nothing is written to VRAM.
 *   The tags are cleared once at start (dc_init in tramp.s at 0x8C7D, called by crt0).
 *   Not handled: swapping the floppy (the cache would serve the old disk's sectors).
 *   VDP timing: two VRAM accesses need ~29 T-states between them while the screen is on
 *   (openMSX too_fast_vram_access=real dropped the first of two outs 16 T-states apart).
 *   VDP R#14 (VRAM A16-A14) is put back to 0 for the console.  Interrupts off while
 *   the VDP address is set (the vblank ISR reads the status port, resetting the latch).
 * ------------------------------------------------------------------ */

/* mode 0: return the tag of sect's slot   1: slot -> buf   2: buf -> slot, tag := sect */
static unsigned dc(unsigned sect, BYTE *buf, unsigned char mode) __naked
{
    sect; buf; mode;
    __asm
        ld      hl, #0xFFFF           ; "not cached"
        ld      a, (0xFAFC)
        and     #4
        ret     z                     ; VRAM below 128KB: no cache
        ld      a, i
        jp      pe, 1$
        ld      a, i
1$:     push    af                    ; P/V = the interrupt flag
        di
        ld      hl, #4
        add     hl, sp
        ld      e, (hl)               ; de = sect
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      c, (hl)
        inc     hl
        ld      b, (hl)
        push    bc                    ; buf
        inc     hl
        ld      a, (hl)
        push    af                    ; mode
        ld      h, d
        ld      l, e
        ld      bc, #-192
0$:     add     hl, bc
        jr      c, 0$
        ld      bc, #192
        add     hl, bc                ; l = slot = sect % 192
        pop     af
        ld      b, a                  ; b = mode
        ld      c, l                  ; c = slot
        add     hl, hl
        ld      a, h
        or      #0x30
        ld      h, a                  ; tag at VRAM 0x07000 + 2 * slot: R#14 = 1, 0x3000 + ..
        ld      a, b
        cp      #2
        jr      nz, 2$
        set     6, h
2$:     ld      a, #1
        call    dcv_sel
        ld      a, b
        cp      #2
        jr      z, 3$
        in      a, (0x98)
        ld      l, a
        ex      (sp), hl              ; spacing for the VDP (hl comes back)
        ex      (sp), hl
        in      a, (0x98)
        ld      h, a                  ; hl = tag (mode 0 returns it)
        dec     b
        jp      m, 9$                 ; mode 0 done
        jr      4$
3$:     ld      a, e
        out     (0x98), a
        ex      (sp), hl              ; spacing for the VDP
        ex      (sp), hl
        ld      a, d
        out     (0x98), a
        dec     b                     ; b = 1: write
4$:     ld      a, c                  ; slot data at VRAM 0x08000 + 512 * slot
        and     #0x1F
        add     a, a
        ld      h, a
        ld      l, #0
        bit     0, b
        jr      z, 5$
        set     6, h
5$:     ld      a, c
        rlca
        rlca
        rlca
        and     #7
        add     a, #2                 ; R#14 = 2 + slot / 32
        call    dcv_sel
        pop     hl                    ; buf
        push    hl
        ld      a, b
        ld      bc, #0x0098           ; 256 bytes a pass, data port
        ld      d, #2                 ; two passes
        or      a                     ; ini/outi + jp: ~28 T-states a byte.  inir/otir
        jr      nz, 7$                ; (23) is too fast for the VDP during display
6$:     ini                           ; (openMSX too_fast_vram_access=real lost
        jp      nz, 6$                ; bytes and broke tags)
        dec     d
        jr      nz, 6$
        jr      9$
7$:     outi
        jp      nz, 7$
        dec     d
        jr      nz, 7$
9$:     pop     bc
dcv_end:
        xor     a
        out     (0x99), a
        ld      a, #0x8E
        out     (0x99), a             ; R#14 = 0
        pop     af
        ret     po
        ei
        ret
; A = R#14, HL = address bits 13-0 (| 0x4000 to write)
dcv_sel:
        out     (0x99), a
        ld      a, #0x8E
        out     (0x99), a
        ld      a, l
        out     (0x99), a
        ld      a, h
        out     (0x99), a
        ret
    __endasm;
}

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return 0; }

/* A real function call, so the compiler saves what it keeps in registers: the
 * trampoline (and DSKIO behind it) clobbers all of them. */
static void tramp(void) __naked { __asm jp 0x8C78 __endasm; }

static DRESULT rw(BYTE *buff, unsigned lba, UINT count, unsigned char op)
{
    unsigned char n, i, m;

    while (count) {
        n = count > MAXCNT ? MAXCNT : (unsigned char)count;
        m = op ? 2 : 1;                  /* 1 = all in the cache, 2 = through the disk */
        for (i = 0; i < n; i++)
            if (dc(lba + i, 0, 0) != lba + i) m = 2;
        if (m == 2) {
            P_OP = op;
            P_CNT = n;
            P_BUF = (unsigned)buff;
            P_SECT = lba;
            tramp();
            if (P_RES) return RES_ERROR;
        }
        for (i = 0; i < n; i++) {
            dc(lba, buff, m);
            buff += 512;
            lba++;
        }
        count -= n;
    }
    return RES_OK;
}

/* one 512B sector, drive number ignored (one disk); 0 = ok */
unsigned char disk_raw_rw(unsigned char drive, unsigned char *buf,
                          unsigned sect, unsigned char op)
{
    (void)drive;
    return rw(buf, sect, 1, op) == RES_OK ? 0 : 1;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw(buff, (unsigned)sector, count, 0);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return rw((BYTE *)buff, (unsigned)sector, count, 1);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{ (void)pdrv; (void)cmd; (void)buff; return RES_OK; }
