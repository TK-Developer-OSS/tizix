    ORG $00000000
    DC.L $0000FF00      ; Initial SSP
    DC.L $00000100      ; Initial PC

    ; Vectors 2..255 fill with 0x00000200
    REPT 254
    DC.L $00000200
    ENDR

    ORG $00000100
START:
    MOVE.W #$2700, SR

    LEA BANNER(PC), A0
    BSR PUTSTR

    ; Test 1: Write Word $AABB to $00004000
    MOVE.W #$AABB, ($00004000).L
    MOVE.W ($00004000).L, D0
    CMP.W #$AABB, D0
    BNE TEST1_FAIL

    ; Test 2: Write single BYTE $11 to Upper Byte ($00004000)
    ; In MC68000, MOVE.B #$11, ($00004000).L asserts /UDS and R/W=L, but NOT /LDS.
    ; If LDS/UDS are ignored by SRAM WE#, the lower byte ($00004001) will be overwritten.
    MOVE.B #$11, ($00004000).L

    ; Read back Word from $00004000
    MOVE.W ($00004000).L, D0
    LEA MSG_T2(PC), A0
    BSR PUTSTR
    BSR PUTHEX4
    LEA CRLF(PC), A0
    BSR PUTSTR

    CMP.W #$11BB, D0
    BNE T2_FAIL_MSG
    LEA MSG_T2_OK(PC), A0
    BSR PUTSTR
    BRA TEST3

T2_FAIL_MSG:
    LEA MSG_T2_NG(PC), A0
    BSR PUTSTR

TEST3:
    ; Test 3: Write single BYTE $22 to Lower Byte ($00004001)
    ; Should change $11BB -> $1122 (or corrupt upper byte)
    MOVE.B #$22, ($00004001).L

    MOVE.W ($00004000).L, D0
    LEA MSG_T3(PC), A0
    BSR PUTSTR
    BSR PUTHEX4
    LEA CRLF(PC), A0
    BSR PUTSTR

    CMP.W #$1122, D0
    BNE T3_FAIL_MSG
    LEA MSG_T3_OK(PC), A0
    BSR PUTSTR
    BRA TEST_SUMMARY

T3_FAIL_MSG:
    LEA MSG_T3_NG(PC), A0
    BSR PUTSTR

TEST_SUMMARY:
    LEA MSG_DONE(PC), A0
    BSR PUTSTR
HALT_LOOP:
    BRA HALT_LOOP

TEST1_FAIL:
    LEA MSG_T1_NG(PC), A0
    BSR PUTSTR
    BRA HALT_LOOP

    ORG $00000200
DUMMY_HANDLER:
    RTE

; --- Helper: PUTSTR (A0 = str) ---
PUTSTR:
    MOVE.B (A0)+, D0
    BEQ PUTSTR_END
    BSR PUTCHAR
    BRA PUTSTR
PUTSTR_END:
    RTS

; --- Helper: PUTCHAR (D0.B = char) ---
PUTCHAR:
PUTCHAR_WAIT:
    MOVE.B ($001F0000).L, D1
    BTST #0, D1
    BEQ PUTCHAR_WAIT
    MOVE.B D0, ($001F0002).L
    RTS

; --- Helper: PUTHEX4 (D0.W to hex print) ---
PUTHEX4:
    MOVEM.L D0-D2/A0, -(SP)
    MOVE.W D0, D2
    ; Digit 3 (D2 >> 12)
    MOVE.W D2, D0
    LSR.W #8, D0
    LSR.W #4, D0
    BSR PUTHEX_NIBBLE
    ; Digit 2 (D2 >> 8)
    MOVE.W D2, D0
    LSR.W #8, D0
    BSR PUTHEX_NIBBLE
    ; Digit 1 (D2 >> 4)
    MOVE.W D2, D0
    LSR.W #4, D0
    BSR PUTHEX_NIBBLE
    ; Digit 0 (D2)
    MOVE.W D2, D0
    BSR PUTHEX_NIBBLE
    MOVEM.L (SP)+, D0-D2/A0
    RTS

PUTHEX_NIBBLE:
    ANDI.B #$0F, D0
    CMPI.B #$09, D0
    BLE PUTHEX_DIGIT
    ADDI.B #('A'-10), D0
    BRA PUTCHAR
PUTHEX_DIGIT:
    ADDI.B #'0', D0
    BRA PUTCHAR

BANNER:
    DC.B 13, 10, '=== 68000 BYTE WRITE HARDWARE TEST ===', 13, 10, 0
MSG_T1_NG:
    DC.B 'Test1 (Word Write $AABB) FAILED!', 13, 10, 0
MSG_T2:
    DC.B 'Test2 (Write Upper Byte $11 -> Read Word): readback = $', 0
MSG_T2_OK:
    DC.B '  [PASS] Upper Byte Write is ISOLATED (LDS protected, read=$11BB)', 13, 10, 0
MSG_T2_NG:
    DC.B '  [FAIL] Lower byte was DESTROYED / corrupted by Upper Byte write!', 13, 10, 0
MSG_T3:
    DC.B 'Test3 (Write Lower Byte $22 -> Read Word): readback = $', 0
MSG_T3_OK:
    DC.B '  [PASS] Lower Byte Write is ISOLATED (UDS protected, read=$1122)', 13, 10, 0
MSG_T3_NG:
    DC.B '  [FAIL] Upper byte was DESTROYED / corrupted by Lower Byte write!', 13, 10, 0
MSG_DONE:
    DC.B '=== TEST FINISHED ===', 13, 10, 0
CRLF:
    DC.B 13, 10, 0

    EVEN
