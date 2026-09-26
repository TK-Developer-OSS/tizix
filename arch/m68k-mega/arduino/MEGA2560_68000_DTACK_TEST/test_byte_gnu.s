	.org 0x00000000
	.long 0x0000FF00
	.long 0x00000100

	.org 0x00000100
start:
	move.w #0x2700, %sr

	lea banner(%pc), %a0
	bsr putstr

	/* Test 1: Write Word 0xAABB to 0x00004000 */
	move.w #0xAABB, 0x00004000
	move.w 0x00004000, %d0
	cmp.w #0xAABB, %d0
	bne test1_fail

	/* Test 2: Write single BYTE 0x11 to Upper Byte (0x00004000) */
	move.b #0x11, 0x00004000

	/* Read back Word from 0x00004000 */
	move.w 0x00004000, %d0
	move.w %d0, %d3               /* save readback */

	lea msg_t2(%pc), %a0
	bsr putstr
	move.w %d3, %d0
	bsr puthex4
	lea crlf(%pc), %a0
	bsr putstr

	cmp.w #0x11BB, %d3
	bne t2_fail_msg
	lea msg_t2_ok(%pc), %a0
	bsr putstr
	bra test3

t2_fail_msg:
	lea msg_t2_ng(%pc), %a0
	bsr putstr

test3:
	/* Test 3: Write single BYTE 0x22 to Lower Byte (0x00004001) */
	move.b #0x22, 0x00004001

	move.w 0x00004000, %d0
	move.w %d0, %d3

	lea msg_t3(%pc), %a0
	bsr putstr
	move.w %d3, %d0
	bsr puthex4
	lea crlf(%pc), %a0
	bsr putstr

	cmp.w #0x1122, %d3
	bne t3_fail_msg
	lea msg_t3_ok(%pc), %a0
	bsr putstr
	bra test_summary

t3_fail_msg:
	lea msg_t3_ng(%pc), %a0
	bsr putstr

test_summary:
	lea msg_done(%pc), %a0
	bsr putstr
halt_loop:
	bra halt_loop

test1_fail:
	lea msg_t1_ng(%pc), %a0
	bsr putstr
	bra halt_loop

	.org 0x00000200
dummy_handler:
	rte

putstr:
	move.b (%a0)+, %d0
	beq putstr_end
	bsr putchar
	bra putstr
putstr_end:
	rts

putchar:
putchar_wait:
	move.b 0x001F0000, %d1
	btst #0, %d1
	beq putchar_wait
	move.b %d0, 0x001F0002
	rts

puthex4:
	movem.l %d0-%d2/%a0, -(%sp)
	move.w %d0, %d2
	/* Digit 3 */
	move.w %d2, %d0
	lsr.w #8, %d0
	lsr.w #4, %d0
	bsr puthex_nibble
	/* Digit 2 */
	move.w %d2, %d0
	lsr.w #8, %d0
	bsr puthex_nibble
	/* Digit 1 */
	move.w %d2, %d0
	lsr.w #4, %d0
	bsr puthex_nibble
	/* Digit 0 */
	move.w %d2, %d0
	bsr puthex_nibble
	movem.l (%sp)+, %d0-%d2/%a0
	rts

puthex_nibble:
	andi.w #0x000F, %d0
	cmpi.b #9, %d0
	ble puthex_digit
	addi.b #('A'-10), %d0
	bra putchar
puthex_digit:
	addi.b #'0', %d0
	bra putchar

banner:
	.asciz "\r\n=== 68000 BYTE WRITE HARDWARE TEST ===\r\n"
msg_t1_ng:
	.asciz "Test1 (Word Write 0xAABB) FAILED!\r\n"
msg_t2:
	.asciz "Test2 (Write Upper Byte 0x11 -> Read Word): readback = $"
msg_t2_ok:
	.asciz "  [PASS] Upper Byte Write is ISOLATED (read=$11BB)\r\n"
msg_t2_ng:
	.asciz "  [FAIL] Lower byte was DESTROYED / corrupted by Upper Byte write!\r\n"
msg_t3:
	.asciz "Test3 (Write Lower Byte 0x22 -> Read Word): readback = $"
msg_t3_ok:
	.asciz "  [PASS] Lower Byte Write is ISOLATED (read=$1122)\r\n"
msg_t3_ng:
	.asciz "  [FAIL] Upper byte was DESTROYED / corrupted by Lower Byte write!\r\n"
msg_done:
	.asciz "=== TEST FINISHED ===\r\n"
crlf:
	.asciz "\r\n"

	.even
