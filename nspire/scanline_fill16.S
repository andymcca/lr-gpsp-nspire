@ Fast u16 scanline fill for ARM926EJ-S (32-bit stmia stores pairs of identical RGB565).
@ void video_fill_u16_strip_arm(u16 *dst, u32 nh, u32 value);
@ r0 = dst, r1 = halfword count (0 allowed), r2 = u16 in low 16 bits

.arch armv5te
.arm
.align 2
.global video_fill_u16_strip_arm
.type video_fill_u16_strip_arm, %function

video_fill_u16_strip_arm:
	cmp	r1, #0
	moveq	pc, lr

	stmdb	sp!, {r4, r5, r6, lr}

	ldr	r12, .Lffff
	and	r12, r2, r12
	orr	r2, r12, r12, lsl #16
	mov	r3, r2
	mov	r4, r2
	mov	r5, r2

1:	cmp	r1, #8
	blo	2f
	stmia	r0!, {r2, r3, r4, r5}
	sub	r1, r1, #8
	b	1b

2:	movs	r6, r1
	beq	9f
3:	strh	r2, [r0], #2
	subs	r6, r6, #1
	bne	3b

9:	ldmia	sp!, {r4, r5, r6, pc}

.align 2
.Lffff:
	.word	0xffff
