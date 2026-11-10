; PIC12F683, internal 4 MHz: tizix nodes over a one-wire, half duplex, 500 baud line (8N1, LSB first).
; The line is push-pull (totem pole): the device drives it only while it answers and lets go afterwards;
; the host holds it high the rest of the time (a USB serial TX through a 1k-4.7k series resistor is enough).
; The host is the master.  A frame is one command byte, then (for a write) one value byte; the device
; answers with exactly one byte.  Nothing is buffered: every byte is handled when it arrives and then dropped.
;
;   command byte: bit 7 = 1 write, 0 read;  bits 6..0 = the node number (= the GP pin number)
;   write: value byte 0 (low), 1 (high), 2 (input), 3 (input with the weak pull-up);  reply 00 ok, FF no
;   read : reply 00 or 01 (the pin level), FF for a node that cannot be read
;   nodes: GP0, GP1, GP4, GP5 read and write; GP3 reads only (input only); GP2 is the line itself: no.
;   GP0 and GP1 are the programmer's pins as well, so use them with care when a programmer is attached.
        list    p=12f683
        #include <p12f683.inc>
        __CONFIG _FOSC_INTOSCIO & _WDTE_OFF & _PWRTE_OFF & _MCLRE_OFF & _CP_OFF & _CPD_OFF & _BOREN_OFF & _IESO_OFF & _FCMEN_OFF

LINE    equ     2
BITN    equ     .220                ; inner count: the delay below is about 2000 cycles

        cblock  0x70                ; the shared RAM: reachable from either bank
        d1, d2, cnt, rxb, txb, cmd, val, msk, nmsk, gpsh
        endc

        org     0
        bsf     STATUS, RP0
        clrf    ANSEL
        movlw   0xFF
        movwf   TRISIO              ; every pin is an input: the line is released
        clrf    WPU
        bcf     OPTION_REG, NOT_GPPU    ; weak pull-ups work where WPU says so
        bcf     STATUS, RP0
        movlw   7
        movwf   CMCON0
        movlw   1 << LINE
        movwf   gpsh                ; the latches: the line is high, the rest low
        movwf   GPIO

main:   call    rxbyte
        movf    rxb, w
        movwf   cmd
        btfss   cmd, 7
        goto    doread
        call    rxbyte              ; a write has a value byte
        movf    rxb, w
        movwf   val

; ---- write
        call    mask
        iorlw   0
        btfss   STATUS, Z
        goto    bad
        movf    msk, w
        andlw   b'00110011'         ; GP0, GP1, GP4, GP5
        btfsc   STATUS, Z
        goto    bad
        movf    val, w
        sublw   3
        btfss   STATUS, C           ; C is clear when val > 3
        goto    bad
        btfsc   val, 1
        goto    asin
        movf    nmsk, w             ; an output: the level first, then the direction
        andwf   gpsh, f
        btfss   val, 0
        goto    put
        movf    msk, w
        iorwf   gpsh, f
put:    movf    gpsh, w
        movwf   GPIO
        bsf     STATUS, RP0
        movf    nmsk, w
        andwf   TRISIO, f
        andwf   WPU, f
        bcf     STATUS, RP0
        goto    ok
asin:   bsf     STATUS, RP0         ; an input, with the pull-up when val is 3
        movf    msk, w
        iorwf   TRISIO, f
        movf    nmsk, w
        andwf   WPU, f
        btfss   val, 0
        goto    asin2
        movf    msk, w
        iorwf   WPU, f
asin2:  bcf     STATUS, RP0
        goto    ok

; ---- read
doread: call    mask
        iorlw   0
        btfss   STATUS, Z
        goto    bad
        movf    msk, w
        andlw   b'00111011'         ; GP0, GP1, GP3, GP4, GP5
        btfsc   STATUS, Z
        goto    bad
        movf    GPIO, w
        andwf   msk, w
        movlw   0                   ; movlw leaves Z alone
        btfss   STATUS, Z
        movlw   1
        goto    reply

ok:     movlw   0
        goto    reply
bad:    movlw   0xFF
reply:  call    txbyte
        goto    main

; ---- msk = 1 << node, nmsk = its complement; W = 0 when the node is 0..5, else 1
mask:   movf    cmd, w
        andlw   0x7F
        movwf   cnt
        sublw   5
        btfss   STATUS, C
        retlw   1
        movlw   1
        movwf   msk
mk1:    movf    cnt, f
        btfsc   STATUS, Z
        goto    mk2
        bcf     STATUS, C
        rlf     msk, f
        decf    cnt, f
        goto    mk1
mk2:    comf    msk, w
        movwf   nmsk
        retlw   0

; ---- wait for a start bit, then read 8 bits into rxb; returns at the end of the stop bit
rxbyte: btfsc   GPIO, LINE
        goto    rxbyte
        call    half                ; to the middle of the start bit
        movlw   8
        movwf   cnt
rx1:    call    bit                 ; to the middle of the next bit
        bcf     STATUS, C
        btfsc   GPIO, LINE
        bsf     STATUS, C
        rrf     rxb, f
        decfsz  cnt, f
        goto    rx1
        call    bit                 ; the middle of the stop bit
        goto    half                ; the host lets go of the line at its end

; ---- send W: take the line (it starts high), send, let go
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

; ---- delays: bit = about 2000 cycles, half = about 1000
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
        end
