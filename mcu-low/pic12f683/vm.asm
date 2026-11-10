; PIC12F683: the 4 bit virtual machine of mcu-low (see ../vm/nodevm.py for the reference).
; The program is in EEPROM.  Everything but the ADC (LDA adc0-3 stops at `fault`) is implemented.
; Messages (MSG / MSGB) go to a 16 byte ring in RAM at 0x50 (the oldest byte is dropped when it is full).
; Nodes: 0 GP0, 1 GP1, 2 GP4, 3 GP5, 4 GP3 (input only), 5 timer finish, 6 timer repeat.  GP2 is kept for the link.
        list    p=12f683
        #include <p12f683.inc>
        __CONFIG _FOSC_INTOSCIO & _WDTE_OFF & _PWRTE_OFF & _MCLRE_OFF & _CP_OFF & _CPD_OFF & _BOREN_OFF & _IESO_OFF & _FCMEN_OFF

FIN     equ     0                   ; flags: the timer has finished
REP     equ     1                   ;        the timer repeats
PEND    equ     2                   ;        an interrupt event is waiting
INTR    equ     3                   ;        the intr block is running
STOP    equ     5                   ;        stopped by the host
LINE    equ     2                   ; GP2: the one wire link
BITN    equ     .220                ; the delay below is about 2000 cycles = one bit at 500 baud
NODISP  equ     4                   ;        no interrupt before the next instruction (after LDA)
VAR0    equ     0x40                ; the four 16 bit variables, little endian
OUTBUF  equ     0x50                ; the output ring

        cblock  0x20
        pc, ins, op, val, tmp, msk, nmsk, gpsh, flags, ret, soff, ioff, loff, bitv
        lim_l, lim_h, cnt_l, cnt_h
        a_l, a_h, c_l, c_h, want, idx, off, tmp2, tmp3, orp, owp, ocnt
        endc
        cblock  0x70                ; the link's variables, in the shared RAM
        d1, d2, cnt, rxb, txb, cmd, hval, eep, wtmp
        endc

        org     0
        goto    init

; ---- fetch the next instruction decode it and jump through the table
run:    btfss   GPIO, LINE          ; a start bit: the host is talking
        call    linkframe
        btfsc   flags, STOP
        goto    run
        btfss   PIR1, TMR2IF
        goto    nt
        bcf     PIR1, TMR2IF
        call    tick
nt:     btfsc   flags, NODISP
        goto    go
        btfss   flags, PEND         ; start the intr block when an event is waiting
        goto    go
        btfsc   flags, INTR
        goto    go
        incf    ioff, w
        btfsc   STATUS, Z           ; 0xFF: there is no intr block
        goto    go
        bcf     flags, PEND
        bsf     flags, INTR
        movf    pc, w
        movwf   ret
        movf    ioff, w
        movwf   pc
go:     bcf     flags, NODISP
        incf    pc, w               ; pc = 0xFF means idle (an END with no loop)
        btfsc   STATUS, Z
        goto    run
        call    fetch
        swapf   ins, w
        andlw   0x0F
        movwf   op
        movf    ins, w
        andlw   0x0F
        movwf   val
        movf    op, w
        call    disp
        goto    run
disp:   addwf   PCL, f
        goto    op_sys
        goto    op_lo
        goto    op_hi
        goto    op_in
        goto    op_up
        goto    op_ifl
        goto    op_ifh
        goto    op_tlim
        goto    op_lda
        goto    op_cmp
        goto    op_cmp
        goto    op_cmp
        goto    op_var
        goto    op_jmpf
        goto    op_jmpb
        goto    op_jmpl

bitidx: addwf   PCL, f              ; node 0-4 -> the GP bit
        retlw   0
        retlw   1
        retlw   4
        retlw   5
        retlw   3

fault:  goto    fault

; ---- EEPROM
ee_rd:  bsf     STATUS, RP0
        movwf   EEADR
        bsf     EECON1, RD
        movf    EEDAT, w
        bcf     STATUS, RP0
        return

fetch:  movf    pc, w
        call    ee_rd
        movwf   ins
        incf    pc, f
        return

; ---- SYS
op_sys: movf    val, f
        btfsc   STATUS, Z
        goto    s_end
        movlw   1
        xorwf   val, w
        btfsc   STATUS, Z
        return                 ; NOP
        movlw   2
        xorwf   val, w
        btfsc   STATUS, Z
        goto    s_reti
        movlw   3
        xorwf   val, w
        btfsc   STATUS, Z
        goto    msg
        goto    msgb
s_reti: bcf     flags, INTR
        movf    ret, w
        movwf   pc
        return
s_end:  movf    loff, w
        movwf   pc
        return

; ---- messages: MSG carries its text (the last character has bit 7 set), MSGB is built in
outc:   movwf   tmp2                ; put W into the ring
        movlw   .16
        xorwf   ocnt, w
        btfss   STATUS, Z
        goto    oc1
        incf    orp, f              ; full: drop the oldest
        movlw   0x0F
        andwf   orp, f
        goto    oc2
oc1:    incf    ocnt, f
oc2:    movlw   OUTBUF
        addwf   owp, w
        movwf   FSR
        movf    tmp2, w
        movwf   INDF
        incf    owp, f
        movlw   0x0F
        andwf   owp, f
        return

msg:    call    fetch
        movf    ins, w
        andlw   0x7F
        call    outc
        btfss   ins, 7
        goto    msg
        return

msgb:   movlw   4
        subwf   val, w
        movwf   idx
        sublw   .11
        btfss   STATUS, C
        goto    fault               ; only 12 built-in messages
        movlw   3
        movwf   PCLATH
        movf    idx, w
        call    bmoff
        clrf    PCLATH
        movwf   off
bm1:    movlw   3
        movwf   PCLATH
        movf    off, w
        call    bmstr
        clrf    PCLATH
        movwf   tmp3
        incf    off, f
        movf    tmp3, w
        andlw   0x7F
        call    outc
        btfss   tmp3, 7
        goto    bm1
        return

; ---- the pins
mkmask: movf    val, w              ; msk = 1 << bit(node), nmsk = ~msk
        call    bitidx
        movwf   tmp
        movlw   1
        movwf   msk
mm1:    movf    tmp, f
        btfsc   STATUS, Z
        goto    mm2
        bcf     STATUS, C
        rlf     msk, f
        decf    tmp, f
        goto    mm1
mm2:    comf    msk, w
        movwf   nmsk
        return

pinout: movf    gpsh, w             ; the level first, then the direction
        movwf   GPIO
        movf    nmsk, w             ; msk and nmsk are in bank 0: load W first
        bsf     STATUS, RP0
        andwf   TRISIO, f
        andwf   WPU, f
        bcf     STATUS, RP0
        return

isvirt: movlw   5                   ; C is set when the node is 5 or more
        subwf   val, w
        return

setvirt: movlw  5                   ; a timer flag node: bitv = the value
        xorwf   val, w
        btfss   STATUS, Z
        goto    sv6
        bcf     flags, FIN
        btfsc   bitv, 0
        bsf     flags, FIN
        return
sv6:    movlw   6
        xorwf   val, w
        btfss   STATUS, Z
        goto    fault
        bcf     flags, REP
        btfsc   bitv, 0
        bsf     flags, REP
        return

op_lo:  clrf    bitv
        call    isvirt
        btfsc   STATUS, C
        goto    setvirt
        call    mkmask
        movlw   4
        xorwf   val, w
        btfsc   STATUS, Z
        goto    fault               ; GP3 is input only
        movf    nmsk, w
        andwf   gpsh, f
        goto    pinout

op_hi:  movlw   1
        movwf   bitv
        call    isvirt
        btfsc   STATUS, C
        goto    setvirt
        call    mkmask
        movlw   4
        xorwf   val, w
        btfsc   STATUS, Z
        goto    fault
        movf    msk, w
        iorwf   gpsh, f
        goto    pinout

op_in:  call    isvirt
        btfsc   STATUS, C
        goto    fault
        call    mkmask
        movf    msk, w
        bsf     STATUS, RP0
        iorwf   TRISIO, f
        xorlw   0xFF                ; = nmsk
        andwf   WPU, f
        bcf     STATUS, RP0
        return

op_up:  call    isvirt
        btfsc   STATUS, C
        goto    fault
        call    mkmask
        movf    msk, w
        bsf     STATUS, RP0
        iorwf   TRISIO, f
        iorwf   WPU, f
        bcf     STATUS, RP0
        return

; ---- conditions: true runs the next instruction, false skips it
rdbit:  call    isvirt
        btfsc   STATUS, C
        goto    rdv
        call    mkmask
        movf    GPIO, w
        andwf   msk, w
        movlw   0                   ; movlw leaves Z alone
        btfss   STATUS, Z
        movlw   1
        movwf   tmp
        return
rdv:    clrf    tmp
        movlw   5
        xorwf   val, w
        btfss   STATUS, Z
        goto    rd6
        btfsc   flags, FIN
        incf    tmp, f
        return
rd6:    movlw   6
        xorwf   val, w
        btfss   STATUS, Z
        goto    fault
        btfsc   flags, REP
        incf    tmp, f
        return

skipnxt: movf   pc, w               ; skip the next instruction, whole (a MSG has text after it)
        call    ee_rd
        movwf   tmp
        incf    pc, f
        movlw   3
        xorwf   tmp, w
        btfss   STATUS, Z
        return
sk1:    movf    pc, w
        call    ee_rd
        movwf   tmp
        incf    pc, f
        btfss   tmp, 7
        goto    sk1
        return

op_ifl: call    rdbit               ; true when the node is low
        movf    tmp, f
        btfsc   STATUS, Z
        return
        goto    skipnxt
op_ifh: call    rdbit               ; true when the node is high
        movf    tmp, f
        btfss   STATUS, Z
        return
        goto    skipnxt

; ---- LDA, compares, variables
varadr: movf    val, w              ; FSR = VAR0 + 2 * (bits 3-2 of val)
        andlw   0x0C
        movwf   tmp
        bcf     STATUS, C
        rrf     tmp, w
        addlw   VAR0
        movwf   FSR
        return

op_lda: movlw   4
        subwf   val, w
        btfss   STATUS, C
        goto    fault               ; adc0-3: not yet
        movwf   tmp
        sublw   3
        btfss   STATUS, C
        goto    fault
        movf    tmp, w
        addwf   tmp, w
        addlw   VAR0
        movwf   FSR
        movf    INDF, w
        movwf   a_l
        incf    FSR, f
        movf    INDF, w
        movwf   a_h
        bsf     flags, NODISP       ; no interrupt between LDA and its compare
        return

getc:   movf    val, w              ; c = constant (val and 7); constant 0 is 0
        andlw   7
        movwf   tmp
        clrf    c_l
        clrf    c_h
        movf    tmp, f
        btfsc   STATUS, Z
        return
        addwf   tmp, w
        addlw   2
        movwf   tmp
        call    ee_rd
        movwf   c_l
        incf    tmp, w
        call    ee_rd
        movwf   c_h
        return

cmp16:  movf    c_h, w              ; tmp = 0 a == c, 1 a > c, 2 a < c
        subwf   a_h, w
        btfss   STATUS, Z
        goto    cm_hi
        movf    c_l, w
        subwf   a_l, w
        btfsc   STATUS, Z
        goto    cm_eq
        btfsc   STATUS, C
        goto    cm_gt
        goto    cm_lt
cm_hi:  btfsc   STATUS, C
        goto    cm_gt
cm_lt:  movlw   2
        movwf   tmp
        return
cm_gt:  movlw   1
        movwf   tmp
        return
cm_eq:  clrf    tmp
        return

op_cmp: call    getc
        call    cmp16
        movlw   9
        subwf   op, w               ; 0 gt, 1 lt, 2 eq
        addlw   1                   ; the result that makes it true: gt 1, lt 2, eq 3 -> 0
        movwf   want
        movlw   3
        xorwf   want, w
        btfsc   STATUS, Z
        clrf    want
        movf    tmp, w
        xorwf   want, w
        movlw   0
        btfsc   STATUS, Z
        movlw   1
        movwf   want                ; 1 when the comparison holds
        movlw   1
        btfsc   val, 3
        xorwf   want, f             ; bit 3 negates
        movf    want, f
        btfss   STATUS, Z
        return
        goto    skipnxt

op_var: call    varadr
        movf    val, w
        andlw   3
        movwf   tmp
        movf    tmp, f
        btfsc   STATUS, Z
        goto    v_inc
        decfsz  tmp, f
        goto    v_2
        goto    v_dec
v_2:    decfsz  tmp, f
        goto    fault
        clrf    INDF                ; clear
        incf    FSR, f
        clrf    INDF
        return
v_inc:  incfsz  INDF, f
        return
        incf    FSR, f
        incf    INDF, f
        return
v_dec:  movf    INDF, f
        btfsc   STATUS, Z
        goto    vd_b
        decf    INDF, f
        return
vd_b:   decf    INDF, f
        incf    FSR, f
        decf    INDF, f
        return

; ---- jumps
op_jmpf: movf   val, w
        addwf   pc, f
        return
op_jmpb: movf   val, w
        subwf   pc, f
        return
op_jmpl: movlw  7                   ; 0-7 forward 16 * n, 8-15 back 16 * (n - 7)
        subwf   val, w
        btfss   STATUS, C
        goto    jl_f
        movwf   tmp
        swapf   tmp, w
        subwf   pc, f
        return
jl_f:   swapf   val, w
        addwf   pc, f
        return

; ---- the timer: TLIM k loads constant k (0 stops), tick counts milliseconds
op_tlim: movf   val, f
        btfss   STATUS, Z
        goto    tl1
        clrf    lim_l
        clrf    lim_h
        clrf    cnt_l
        clrf    cnt_h
        return
tl1:    movf    val, w
        addwf   val, w              ; 2k
        addlw   2                   ; the constants start at 4: constant k is at 2k+2
        movwf   tmp
        call    ee_rd
        movwf   lim_l
        incf    tmp, w
        call    ee_rd
        movwf   lim_h
        movf    lim_l, w
        movwf   cnt_l
        movf    lim_h, w
        movwf   cnt_h
        return

tick:   movf    lim_l, w
        iorwf   lim_h, w
        btfsc   STATUS, Z
        return                      ; stopped
        movf    cnt_l, f
        btfsc   STATUS, Z
        decf    cnt_h, f
        decf    cnt_l, f
        movf    cnt_l, w
        iorwf   cnt_h, w
        btfss   STATUS, Z
        return
        bsf     flags, FIN
        bsf     flags, PEND
        btfss   flags, REP
        goto    tk1
        movf    lim_l, w
        movwf   cnt_l
        movf    lim_h, w
        movwf   cnt_h
        return
tk1:    clrf    lim_l
        clrf    lim_h
        return

; ---- the link: one wire, half duplex, 500 baud, 8N1.  The host sends a command byte (bit 7 = write, the low 7 bits
; the node), a write also a value byte; the device answers with exactly one byte.  Nothing is buffered.
;   nodes 0-6: the bit nodes of the machine (write 0 low, 1 high, 2 input, 3 pull-up; read 0/1; 4 = GP3 is read only)
;   0x20 EEPROM pointer (write)   0x21 EEPROM data (read or write, the pointer then moves on)
;   0x22 the output ring (read: the oldest byte, 0 when empty)   0x23 control (write 0 stop, 1 restart; read 1 = stopped)
;   answers: write 00 ok / FF refused;  read: the value, FF refused
linkframe: call rxbyte
        movf    rxb, w
        movwf   cmd
        btfss   cmd, 7
        goto    lread
        call    rxbyte
        movf    rxb, w
        movwf   hval
        movf    cmd, w
        andlw   0x7F
        movwf   val
        movlw   0x20
        subwf   val, w
        btfsc   STATUS, C
        goto    lw_sys
        movlw   7
        subwf   val, w
        btfsc   STATUS, C
        goto    lbad                ; there are nodes 0-6
        movlw   4
        xorwf   val, w
        btfsc   STATUS, Z
        goto    lbad                ; GP3 is read only
        movf    hval, w
        sublw   3
        btfss   STATUS, C
        goto    lbad                ; the value is 0-3
        movlw   5
        subwf   val, w
        btfss   STATUS, C
        goto    lw_do               ; a pin
        movf    hval, w
        sublw   1
        btfss   STATUS, C
        goto    lbad                ; a flag takes 0 or 1
lw_do:  movf    hval, f
        btfsc   STATUS, Z
        goto    lw_0
        decf    hval, f
        btfsc   STATUS, Z
        goto    lw_1
        decf    hval, f
        btfsc   STATUS, Z
        goto    lw_2
        call    op_up
        goto    lok
lw_0:   call    op_lo
        goto    lok
lw_1:   call    op_hi
        goto    lok
lw_2:   call    op_in
lok:    movlw   0
        goto    lreply
lbad:   movlw   0xFF
lreply: call    txbyte
        return

lw_sys: movlw   0x20
        xorwf   val, w
        btfss   STATUS, Z
        goto    lw_21
        movf    hval, w
        movwf   eep
        goto    lok
lw_21:  movlw   0x21
        xorwf   val, w
        btfss   STATUS, Z
        goto    lw_23
        call    eewr
        goto    lok
lw_23:  movlw   0x23
        xorwf   val, w
        btfss   STATUS, Z
        goto    lbad
        movf    hval, f
        btfss   STATUS, Z
        goto    lw_run
        bsf     flags, STOP
        goto    lok
lw_run: decf    hval, f
        btfss   STATUS, Z
        goto    lbad
        movlw   0
        call    txbyte
        goto    init                ; restart the machine from the program in EEPROM

lread:  movf    cmd, w
        movwf   val
        movlw   0x20
        subwf   val, w
        btfsc   STATUS, C
        goto    lr_sys
        movlw   7
        subwf   val, w
        btfsc   STATUS, C
        goto    lbad
        call    rdbit
        movf    tmp, w
        goto    lreply
lr_sys: movlw   0x21
        xorwf   val, w
        btfss   STATUS, Z
        goto    lr_22
        movf    eep, w
        call    ee_rd
        incf    eep, f
        goto    lreply
lr_22:  movlw   0x22
        xorwf   val, w
        btfss   STATUS, Z
        goto    lr_23
        movf    ocnt, f
        btfsc   STATUS, Z
        goto    lreply0
        movlw   OUTBUF
        addwf   orp, w
        movwf   FSR
        movf    INDF, w
        movwf   wtmp
        incf    orp, f
        movlw   0x0F
        andwf   orp, f
        decf    ocnt, f
        movf    wtmp, w
        goto    lreply
lreply0: movlw  0
        goto    lreply
lr_23:  movlw   0x23
        xorwf   val, w
        btfss   STATUS, Z
        goto    lbad
        movlw   0
        btfsc   flags, STOP
        movlw   1
        goto    lreply

eewr:   movf    hval, w             ; write hval to the EEPROM at eep, then move on
        movwf   wtmp
        movf    eep, w
        bsf     STATUS, RP0
        movwf   EEADR
        bcf     STATUS, RP0
        movf    wtmp, w
        bsf     STATUS, RP0
        movwf   EEDAT
        bsf     EECON1, WREN
        movlw   0x55
        movwf   EECON2
        movlw   0xAA
        movwf   EECON2
        bsf     EECON1, WR
ew1:    btfsc   EECON1, WR
        goto    ew1
        bcf     EECON1, WREN
        bcf     STATUS, RP0
        incf    eep, f
        return

; ---- bits on the wire (see node.asm)
rxbyte: btfsc   GPIO, LINE
        goto    rxbyte
        call    half
        movlw   8
        movwf   cnt
rx1:    call    bit
        bcf     STATUS, C
        btfsc   GPIO, LINE
        bsf     STATUS, C
        rrf     rxb, f
        decfsz  cnt, f
        goto    rx1
        call    bit
        goto    half                ; the host lets go of the line at the end of its stop bit

txbyte: movwf   txb
        bsf     GPIO, LINE
        bsf     STATUS, RP0
        bcf     TRISIO, LINE
        bcf     STATUS, RP0
        bcf     GPIO, LINE          ; start bit
        call    bit
        movlw   8
        movwf   cnt
tx1:    rrf     txb, f
        btfsc   STATUS, C
        bsf     GPIO, LINE
        btfss   STATUS, C
        bcf     GPIO, LINE
        call    bit
        decfsz  cnt, f
        goto    tx1
        bsf     GPIO, LINE          ; stop bit
        call    bit
        bsf     STATUS, RP0
        bsf     TRISIO, LINE        ; let go of the line
        bcf     STATUS, RP0
        return

bit:    movlw   3
        movwf   d1
b1:     movlw   BITN
        movwf   d2
b2:     decfsz  d2, f
        goto    b2
        decfsz  d1, f
        goto    b1
        return

half:   movlw   .166
        movwf   d2
h1:     decfsz  d2, f
        goto    h1
        movlw   .165
        movwf   d2
h2:     decfsz  d2, f
        goto    h2
        return

; ---- start
init:   bsf     STATUS, RP0
        clrf    ANSEL
        movlw   0xFF
        movwf   TRISIO              ; every pin is an input
        movlw   1 << LINE
        movwf   WPU                 ; the link idles high even with no host attached
        bcf     OPTION_REG, NOT_GPPU
        movlw   .249
        movwf   PR2                 ; Timer2: 4 MHz / 4 / 4 / 250 = 1 kHz
        bcf     STATUS, RP0
        movlw   7
        movwf   CMCON0
        movlw   b'00000101'
        movwf   T2CON               ; Timer2 on, prescaler 1:4
        movlw   1 << 2
        movwf   gpsh                ; the link (GP2) latch stays high
        movwf   GPIO
        clrf    flags
        clrf    lim_l
        clrf    lim_h
        clrf    cnt_l
        clrf    cnt_h
        clrf    eep
        clrf    a_l
        clrf    a_h
        clrf    orp
        clrf    owp
        clrf    ocnt
        movlw   VAR0
        movwf   FSR
v_clr:  clrf    INDF
        incf    FSR, f
        movlw   VAR0 + 8
        xorwf   FSR, w
        btfss   STATUS, Z
        goto    v_clr
        movlw   0
        call    ee_rd
        movwf   soff
        movlw   1
        call    ee_rd
        movwf   ioff
        movlw   2
        call    ee_rd
        movwf   loff
        movf    soff, w
        movwf   pc
        incf    soff, w
        btfss   STATUS, Z
        goto    run                 ; there is a setup block
        movf    loff, w
        movwf   pc
        goto    run

; ---- the built-in messages (flash, page 3: PCLATH is 3 while these are read)
        org     0x300
bmoff:  addwf   PCL, f              ; the start of message n in bmstr
        retlw   0                   ; OK
        retlw   2                   ; ERROR
        retlw   7                   ; READY
        retlw   .12                 ; START
        retlw   .17                 ; STOP
        retlw   .21                 ; DONE
        retlw   .25                 ; TIMER
        retlw   .30                 ; ON
        retlw   .32                 ; OFF
        retlw   .35                 ; HIGH
        retlw   .39                 ; LOW
        retlw   .42                 ; ALARM
bmstr:  addwf   PCL, f              ; the text, the last character has bit 7 set
        dt      "O", "K"|0x80
        dt      "E", "R", "R", "O", "R"|0x80
        dt      "R", "E", "A", "D", "Y"|0x80
        dt      "S", "T", "A", "R", "T"|0x80
        dt      "S", "T", "O", "P"|0x80
        dt      "D", "O", "N", "E"|0x80
        dt      "T", "I", "M", "E", "R"|0x80
        dt      "O", "N"|0x80
        dt      "O", "F", "F"|0x80
        dt      "H", "I", "G", "H"|0x80
        dt      "L", "O", "W"|0x80
        dt      "A", "L", "A", "R", "M"|0x80

        org     0x2100              ; the program: generated by vm_test.py (prog.inc)
        #include "prog.inc"
        end
