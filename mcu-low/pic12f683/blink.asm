; PIC12F683: blink GP0 (pin 7). Step 1 of mcu-low: check that the assembler and gpsim work.
        list    p=12f683
        #include <p12f683.inc>
        __CONFIG _FOSC_INTOSCIO & _WDTE_OFF & _PWRTE_OFF & _MCLRE_OFF & _CP_OFF & _CPD_OFF & _BOREN_OFF & _IESO_OFF & _FCMEN_OFF

        cblock  0x20
        d1, d2
        endc

        org     0
        bsf     STATUS, RP0         ; bank 1
        clrf    ANSEL               ; all pins digital
        bcf     TRISIO, 0           ; GP0 is an output
        bcf     STATUS, RP0         ; bank 0
        movlw   7
        movwf   CMCON0              ; comparators off
loop:   movlw   1
        xorwf   GPIO, f             ; flip GP0
        call    wait
        goto    loop

wait:   movlw   .200
        movwf   d1
w1:     movlw   .250
        movwf   d2
w2:     decfsz  d2, f
        goto    w2
        decfsz  d1, f
        goto    w1
        return
        end
