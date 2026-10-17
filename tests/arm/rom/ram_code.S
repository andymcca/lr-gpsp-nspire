@ A small GBA ROM for tests/arm/dynarec_test.c: code copied into IWRAM and
@ EWRAM that branches between its own blocks (a call, a loop back to before
@ the block it is in, a jump back), then rewritten by DMA and by the CPU, as
@ games do with code they keep in RAM. gpSP's dynarec has to run all of it as
@ written, with either "Dynarec RAM / SMC" setting.
@
@ sum(n) adds up f(i) = K * i for i = n..1, so sum(10) is 55 * K. K is the
@ immediate of f's first instruction: 3 as copied, then 5 (DMA), then 7
@ (a CPU store). Results go to EWRAM at 0x0203FF00 (RESULTS):
@   +0  "DONE" once everything ran      +4  ARM in IWRAM, K = 3: 165
@   +8  after DMA, K = 5: 275           +12 after a CPU store, K = 7: 385
@   +16 Thumb in IWRAM, K = 3: 165      +20 after DMA, K = 5: 275
@   +24 ARM in EWRAM, K = 3: 165        +28 after DMA, K = 5: 275
@   +32 the step running (1-7), for a run that stops early

	.arm
	.section .text
	.global _start
_start:
	b	main
	.space	0xC0 - 4		@ the rest of the header (gpSP doesn't check it)

	.equ	RESULTS, 0x0203FF00
	.equ	IWRAM_ARM, 0x03000000
	.equ	IWRAM_THUMB, 0x03000400
	.equ	EWRAM_ARM, 0x02010000

@ ---- Copied to RAM (only relative branches inside) ----

	.align	2
arm_code_start:
arm_tail:				@ jumped back to from arm_sum
	mov	r0, r5
	ldmfd	sp!, {r4, r5, pc}
arm_sum:				@ r0 = n
	stmfd	sp!, {r4, r5, lr}
	mov	r4, r0
	mov	r5, #0
1:	mov	r0, r4
	bl	arm_f			@ another block in RAM
	add	r5, r5, r0		@ a block starts here (f returns to it)...
	subs	r4, r4, #1
	bne	1b			@ ...and loops back to before its start
	b	arm_tail
arm_f:					@ r0 = K * r0
	mov	r1, #3
	mul	r0, r1, r0
	bx	lr
arm_code_end:

	.thumb
	.align	2
thumb_code_start:
thumb_tail:
	mov	r0, r5
	pop	{r4, r5}
	pop	{r1}
	bx	r1
thumb_sum:				@ r0 = n
	push	{r4, r5, lr}
	mov	r4, r0
	mov	r5, #0
1:	mov	r0, r4
	bl	thumb_f
	add	r5, r5, r0
	sub	r4, #1
	bne	1b
	b	thumb_tail
thumb_f:
	mov	r1, #3
	mul	r0, r1
	bx	lr
	.align	2
thumb_code_end:

	.arm
	.align	2
main:
	ldr	sp, =0x03007F00
	ldr	r10, =RESULTS
	mov	r0, #0
	str	r0, [r10]

	@ 1: ARM code in IWRAM, called from the ROM
	mov	r0, #1
	str	r0, [r10, #32]
	ldr	r0, =arm_code_start
	ldr	r1, =IWRAM_ARM
	ldr	r2, =arm_code_end
	bl	copy
	ldr	r8, =IWRAM_ARM + arm_sum - arm_code_start
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #4]

	@ 2: f's K changed by DMA (gpSP's partial flush, or a full one)
	mov	r0, #2
	str	r0, [r10, #32]
	ldr	r0, =arm_mov_r1_5
	ldr	r1, =IWRAM_ARM + arm_f - arm_code_start
	mov	r2, #1
	bl	dma32
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #8]

	@ 3: f's K changed by a CPU store (gpSP flushes RAM code)
	mov	r0, #3
	str	r0, [r10, #32]
	ldr	r0, =IWRAM_ARM + arm_f - arm_code_start
	ldr	r1, =0xE3A01007		@ mov r1, #7
	str	r1, [r0]
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #12]

	@ 4: Thumb code in IWRAM
	mov	r0, #4
	str	r0, [r10, #32]
	ldr	r0, =thumb_code_start
	ldr	r1, =IWRAM_THUMB
	ldr	r2, =thumb_code_end
	bl	copy
	ldr	r8, =IWRAM_THUMB + thumb_sum - thumb_code_start + 1
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #16]

	@ 5: its K changed by a 16-bit DMA
	mov	r0, #5
	str	r0, [r10, #32]
	ldr	r0, =thumb_mov_r1_5
	ldr	r1, =IWRAM_THUMB + thumb_f - thumb_code_start
	mov	r2, #1
	bl	dma16
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #20]

	@ 6: ARM code in EWRAM
	mov	r0, #6
	str	r0, [r10, #32]
	ldr	r0, =arm_code_start
	ldr	r1, =EWRAM_ARM
	ldr	r2, =arm_code_end
	bl	copy
	ldr	r8, =EWRAM_ARM + arm_sum - arm_code_start
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #24]

	@ 7: its K changed by DMA
	mov	r0, #7
	str	r0, [r10, #32]
	ldr	r0, =arm_mov_r1_5
	ldr	r1, =EWRAM_ARM + arm_f - arm_code_start
	mov	r2, #1
	bl	dma32
	mov	r0, #10
	mov	lr, pc
	bx	r8
	str	r0, [r10, #28]

	ldr	r0, =0x454E4F44		@ "DONE"
	str	r0, [r10]
done:
	b	done

@ Copies the words from r0 up to r2 to r1.
copy:
	ldr	r3, [r0], #4
	str	r3, [r1], #4
	cmp	r0, r2
	blo	copy
	bx	lr

@ DMA 3 now: r2 words or halfwords from r0 to r1.
dma32:
	orr	r2, r2, #0x84000000
	b	1f
dma16:
	orr	r2, r2, #0x80000000
1:	ldr	r3, =0x040000D4
	str	r0, [r3]
	str	r1, [r3, #4]
	str	r2, [r3, #8]
	bx	lr

arm_mov_r1_5:
	mov	r1, #5
thumb_mov_r1_5:
	.hword	0x2105			@ Thumb mov r1, #5
	.hword	0

	.ltorg
