@ A GBA ROM for tests/arm/badjump_test.c: it jumps where no code can run,
@ as an Advance Wars from a bad dump does when it starts (a function
@ pointer read as 0x910803DF instead of 0x080803DF). Built twice: with
@ THUMB=1 it jumps there in Thumb state (a bx to an odd address, as Advance
@ Wars does), with THUMB=0 in ARM state, to the cartridge's save memory.
@ It writes 1 to EWRAM 0x0203FF00 first, to show it ran.

	.arm
	.section .text
	.global _start
_start:
	b	main
	.space	0xC0 - 4		@ the rest of the header (gpSP doesn't check it)

main:
	ldr	sp, =0x03007F00
	ldr	r1, =0x0203FF00
	mov	r0, #1
	str	r0, [r1]
.if THUMB
	ldr	r0, =0x910803DF
.else
	ldr	r0, =0x0E000000
.endif
	bx	r0
	.ltorg
