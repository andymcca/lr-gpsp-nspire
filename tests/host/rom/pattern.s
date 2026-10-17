@ A GBA test ROM for gpsp-host's tests (make_test_rom.py holds it as bytes).
@ Mode 3: every pixel is (R = x & 31, G = y & 31, B = 16), so the picture
@ has no black pixel and each one says where it came from. Then, once a
@ frame (at the start of vblank), a white bar 1 pixel wide and the full
@ 160 lines tall moves one column right, wrapping at 240; the frame count
@ is kept at 0x02000000.
    .arm
    .section .text
    .global _start
_start:
    b       main
    .space  0xC0 - 4            @ the cartridge header (gpSP doesn't check it)
main:
    mov     r0, #0x04000000
    ldr     r1, =0x0403         @ mode 3, BG2 on
    strh    r1, [r0]
    mov     r2, #0x06000000
    mov     r3, #0              @ y
1:  mov     r4, #0              @ x
2:  and     r5, r4, #31
    and     r6, r3, #31
    orr     r5, r5, r6, lsl #5
    orr     r5, r5, #0x4000     @ blue 16
    strh    r5, [r2], #2
    add     r4, r4, #1
    cmp     r4, #240
    blt     2b
    add     r3, r3, #1
    cmp     r3, #160
    blt     1b
    mov     r7, #0              @ frame count
    mov     r8, #0x02000000
frame:
3:  ldrh    r1, [r0, #6]        @ VCOUNT: out of vblank first
    cmp     r1, #160
    beq     3b
4:  ldrh    r1, [r0, #6]        @ then wait for line 160
    cmp     r1, #160
    bne     4b
    @ the old column back to the pattern, the new one white
    mov     r9, r7
5:  cmp     r9, #240
    subge   r9, r9, #240
    bge     5b                  @ r9 = frame % 240
    mov     r3, #0
6:  mov     r2, #0x06000000
    mov     r10, #480
    mla     r2, r3, r10, r2     @ line y
    sub     r4, r9, #1          @ the previous column
    cmp     r4, #0
    addlt   r4, r4, #240
    and     r5, r4, #31
    and     r6, r3, #31
    orr     r5, r5, r6, lsl #5
    orr     r5, r5, #0x4000
    add     r11, r2, r4, lsl #1
    strh    r5, [r11]
    ldr     r5, =0x7FFF
    add     r11, r2, r9, lsl #1
    strh    r5, [r11]
    add     r3, r3, #1
    cmp     r3, #160
    blt     6b
    add     r7, r7, #1
    str     r7, [r8]
    b       frame
    .pool
