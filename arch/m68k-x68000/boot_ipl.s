/* arch/m68k-x68000/boot_ipl.s - X68000 boot sector code (offset 0x3E..0x3FF)
 *
 * The ROM IPL loads sector 0 of the boot floppy to $2000 and jumps to $2000
 * (the 60 3C branch in the header lands here).  This code walks the FAT12
 * root directory for TIZIX.SYS, loads it at $10000 using IOCS _B_READ and
 * jumps to it.  Layout assumed (see tools/make_xdf.py): 1024 bytes/sector,
 * 1 reserved, 2 FATs x 2 sectors, 192 root entries (6 sectors), data at
 * LBA 11, 1 sector per cluster, 8 sectors/track, 2 heads.
 */
        .set    FATBUF, 0x3000
        .set    DIRBUF, 0x3800
        .set    LOADADR, 0x10000

        .text
start:
        lea     %pc@(start),%sp
        lea     %pc@(msg_boot),%a1
        bsr     print
        moveq   #-114,%d0               /* IOCS $8E: boot device */
        trap    #15
        moveq   #0x70,%d7
        lsl.w   #8,%d0
        or.w    %d0,%d7                 /* d7.w = IOCS device code for _B_READ */


        /* read both sectors of FAT #1 */
        moveq   #1,%d4
        lea     FATBUF,%a1
        bsr     rdsec
        moveq   #2,%d4
        lea     FATBUF+1024,%a1
        bsr     rdsec

        /* scan root directory, LBA 5..10 */
        moveq   #5,%d5
nextdir:
        move.l  %d5,%d4
        lea     DIRBUF,%a1
        bsr     rdsec
        lea     DIRBUF,%a0
        moveq   #31,%d6
entry:
        tst.b   (%a0)
        beq     notfound
        lea     %pc@(fname),%a2
        movea.l %a0,%a3
        moveq   #10,%d0
cmp:    cmpm.b  (%a2)+,(%a3)+
        dbne    %d0,cmp
        beq.s   found
        lea     32(%a0),%a0
        dbf     %d6,entry
        addq.l  #1,%d5
        cmp.l   #11,%d5
        blt     nextdir
notfound:
        lea     %pc@(msg_nf),%a1
        bra     fail

found:
        moveq   #0,%d3
        move.w  26(%a0),%d3
        ror.w   #8,%d3                  /* first cluster (little endian) */
        lea     LOADADR,%a2
load:
        move.l  %d3,%d4
        add.l   #9,%d4                  /* LBA = 11 + (cluster - 2) */
        movea.l %a2,%a1
        bsr     rdsec
        lea     1024(%a2),%a2
        move.l  %d3,%d0
        lsr.l   #1,%d0
        add.l   %d3,%d0                 /* byte offset = cl + cl/2 */
        lea     FATBUF,%a0
        move.b  1(%a0,%d0.l),%d1
        lsl.w   #8,%d1
        move.b  (%a0,%d0.l),%d1         /* d1.w = little-endian word */
        btst    #0,%d3
        beq.s   even
        lsr.w   #4,%d1
even:   and.w   #0x0fff,%d1
        moveq   #0,%d3
        move.w  %d1,%d3
        cmp.w   #0x0ff8,%d1
        blt.s   load
        jmp     LOADADR

/* d4 = LBA, a1 = destination; reads one 1024-byte sector.
 * IOCS _B_READ: d1.w = device, d2.l = N C H R, d3.l = bytes, a1 = buffer */
rdsec:
        movem.l %d1-%d4/%a1,-(%sp)
        move.l  %d4,%d2
        lsr.l   #3,%d2                  /* track */
        move.l  %d2,%d1
        lsr.l   #1,%d1                  /* cylinder */
        and.l   #1,%d2                  /* head */
        and.l   #7,%d4
        addq.l  #1,%d4                  /* sector, 1 origin */
        lsl.l   #8,%d1
        or.l    %d2,%d1                 /* d1 = C H */
        lsl.l   #8,%d1
        or.l    %d4,%d1                 /* d1 = C H R */
        or.l    #0x03000000,%d1         /* N = 3: 1024 bytes/sector */
        move.l  %d1,%d2
        move.w  %d7,%d1
        move.l  #1024,%d3
        moveq   #0x46,%d0               /* _B_READ */
        trap    #15
        btst    #30,%d0                 /* bit 30 = error; other bits are status */
        movem.l (%sp)+,%d1-%d4/%a1
        bne.s   rderr
        rts
rderr:
        lea     %pc@(msg_err),%a1
fail:
        bsr.s   print
1:      bra.s   1b

print:
        moveq   #0x21,%d0               /* _B_PRINT */
        trap    #15
        rts

fname:    .ascii "TIZIX   SYS"
msg_boot: .asciz "TIZIX BOOT\r\n"
msg_nf:   .asciz "TIZIX.SYS not found\r\n"
msg_err:  .asciz "disk read error\r\n"
        .even
