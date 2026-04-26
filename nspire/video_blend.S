.arch armv5te
.cpu arm926ej-s
.arm

.align 2

.global expand_blend
.global expand_normal

@ Input:
@   r0 = screen_src_ptr
@   r1 = screen_dest_ptr
@   r2 = start
@   r3 = end

6:
  .word io_registers
  .word palette_ram_converted
  .word 0x04000200                @ combine test mask
#ifdef USE_XBGR1555_FORMAT
  .word 0x03E07C1F                @ clamp mask (XBGR1555)
#else
  .word 0x07E0F81F                @ clamp mask (RGB565)
#endif
  .word 0x000003FE                @ palette index mask
#ifdef USE_XBGR1555_FORMAT
  .word 0x04008020                @ saturation mask (XBGR1555)
#else
  .word 0x08010020                @ saturation mask (RGB565)
#endif

expand_blend:
  stmdb sp!, { r4, r5, r6, r7, r8, r9, r10, r11, r14 }

  add r0, r0, r2, lsl #2          @ screen_src_ptr += start
  add r1, r1, r2, lsl #1          @ screen_dest_ptr += start
  sub r2, r3, r2                  @ r2 = end - start
  ldr r3, 6b                      @ r3 = io_registers
  ldrh r3, [r3, #0x52]            @ r3 = bldalpha
  mov r4, r3, lsr #8              @ r4 = bldalpha >> 8
  and r3, r3, #0x1F               @ r3 = blend_a
  and r4, r4, #0x1F               @ r4 = blend_b
  cmp r3, #16                     @ if(blend_a > 16)
  movgt r3, #16                   @   blend_a = 16
  cmp r4, #16                     @ if(blend_b > 16)
  movgt r4, #16                   @   blend_b = 16

  ldr r14, 6b + 4                 @ r14 = palette_ram_converted
  ldr r12, 6b + 8                 @ r12 = 0x04000200
  ldr r11, 6b + 12                @ r11 = clamp mask
  ldr r10, 6b + 16                @ r10 = 0x000003FE

  add r5, r3, r4                  @ r5 = blend_a + blend_b
  cmp r5, #16                     @ if((blend_a + blend_b) > 16)
  bgt 3f                          @   goto loop w/saturation


  @ loop w/o saturation
1:
  pld     [r0, #32]
  ldr r5, [r0], #4                @ r5 = pixel_pair, screen_src_ptr++
  bics r6, r12, r5                @ if(0x04000200 & ~r5)
  bne 2f                          @   goto no_blend

  and r6, r10, r5, lsl #1         @ r6 = (pixel_pair & 0x1FF) << 1
  ldrh r6, [r14, r6]              @ r6 = pixel_top
  
  and r5, r10, r5, lsr #15        @ r5 = ((pixel_pair >> 16) & 0x1FF) << 1
  ldrh r5, [r14, r5]              @ r5 = pixel_bottom
  
  orr r6, r6, r6, lsl #16         @ r6 = pixel_top | (pixel_top << 16)
  and r6, r6, r11                 @ r6 = pixel_top_dilated

  orr r5, r5, r5, lsl #16         @ r5 = pixel_bottom | (pixel_bottom << 16)
  and r5, r5, r11                 @ r5 = pixel_bottom_dilated

  mul r5, r4, r5                  @ r5 = pixel_bottom * blend_b = bottom_mul
  mla r5, r3, r6, r5              @ r5 = (pixel_top * blend_a) + bottom_mul

  and r5, r11, r5, lsr #4         @ r5 = (color_dilated >> 4) & clamp mask
  orr r5, r5, r5, lsr #16         @ r5 = color_dilated | (color_dilated >> 16)

  strh r5, [r1], #2               @ *screen_dest_ptr = r5, screen_dest_ptr++
  subs r2, r2, #1                 @ counter--
  bne 1b                          @ go again

  ldmia sp!, { r4, r5, r6, r7, r8, r9, r10, r11, pc }

2:
  and r5, r10, r5, lsl #1         @ r5 = (pixel_pair & 0x1FF) << 1
  ldrh r5, [r14, r5]              @ r5 = pixel_top
  strh r5, [r1], #2               @ *screen_dest_ptr = r5, screen_dest_ptr++

  subs r2, r2, #1                 @ counter--
  bne 1b                          @ go again

  ldmia sp!, { r4, r5, r6, r7, r8, r9, r10, r11, pc }

@ loop w/saturation

3:
  ldr r9, 6b + 20                 @ r9 = saturation mask

4:
  pld     [r0, #32]
  ldr r5, [r0], #4                @ r5 = pixel_pair, screen_src_ptr++
  bics r6, r12, r5                @ if(0x04000200 & ~r5)
  bne 5f                          @   goto no_blend

  and r6, r10, r5, lsl #1         @ r6 = (pixel_pair & 0x1FF) << 1
  ldrh r6, [r14, r6]              @ r6 = pixel_top
  
  and r5, r10, r5, lsr #15        @ r5 = ((pixel_pair >> 16) & 0x1FF) << 1
  ldrh r5, [r14, r5]              @ r5 = pixel_bottom
  
  orr r6, r6, r6, lsl #16         @ r6 = pixel_top | (pixel_top << 16)
  and r6, r6, r11                 @ r6 = pixel_top_dilated

  orr r5, r5, r5, lsl #16         @ r5 = pixel_bottom | (pixel_bottom << 16)
  and r5, r5, r11                 @ r5 = pixel_bottom_dilated

  mul r5, r4, r5                  @ r5 = pixel_bottom * blend_b = bottom_mul
  mla r5, r3, r6, r5              @ r5 = (pixel_top * blend_a) + bottom_mul

  mov r6, r5, lsr #4              @ r6 = shifted blended color
  and r5, r9, r6                  @ r5 = overflow bits
#ifdef USE_XBGR1555_FORMAT
  and r7, r5, #0x04000000         @ green overflow bit
  sub r7, r7, r7, lsr #5          @ -> 0x03E00000 saturation mask
  and r8, r5, #0x00008000         @ red overflow bit
  sub r8, r8, r8, lsr #5          @ -> 0x00007C00 saturation mask
  orr r7, r7, r8
  and r8, r5, #0x00000020         @ blue overflow bit
  sub r8, r8, r8, lsr #5          @ -> 0x0000001F saturation mask
  orr r7, r7, r8
#else
  and r7, r5, #0x08000000         @ green overflow bit
  sub r7, r7, r7, lsr #6          @ -> 0x07E00000 saturation mask (6-bit green)
  and r8, r5, #0x00010000         @ red overflow bit
  sub r8, r8, r8, lsr #5          @ -> 0x0000F800 saturation mask
  orr r7, r7, r8
  and r8, r5, #0x00000020         @ blue overflow bit
  sub r8, r8, r8, lsr #5          @ -> 0x0000001F saturation mask
  orr r7, r7, r8
#endif
  orr r5, r6, r7                  @ apply channel saturation bits

  and r5, r11, r5                 @ r5 = (color_dilated >> 4) & clamp mask
  orr r5, r5, r5, lsr #16         @ r5 = color_dilated | (color_dilated >> 16)
  strh r5, [r1], #2               @ *screen_dest_ptr = r5, screen_dest_ptr++

  subs r2, r2, #1                 @ counter--
  bne 4b                          @ go again

  ldmia sp!, { r4, r5, r6, r7, r8, r9, r10, r11, pc }

5:
  and r5, r10, r5, lsl #1         @ r5 = (pixel_pair & 0x1FF) << 1
  ldrh r5, [r14, r5]              @ r5 = pixel_top
  strh r5, [r1], #2               @ *screen_dest_ptr = r5, screen_dest_ptr++

  subs r2, r2, #1                 @ counter--
  bne 4b                          @ go again

  ldmia sp!, { r4, r5, r6, r7, r8, r9, r10, r11, pc }



@ The following function isn't complete (only works on run multiples of 8),
@ but unfortunately I don't see much potential for actually being able to
@ use it..

#define expand_pixel_pair(reg, temp)                                         ;\
  and temp, r3, reg, lsr #15                                                 ;\
  ldrh temp, [r2, temp]                                                      ;\
                                                                             ;\
  and reg, r3, reg, lsl #1                                                   ;\
  ldrh reg, [r2, reg]                                                        ;\
                                                                             ;\
  orr reg, reg, temp, lsl #16                                                ;\


@ Input:
@   r0 = screen_ptr
@   r1 = start
@   r2 = end

1:
  .word palette_ram_converted
  .word 0x3FE

expand_normal:
  stmdb sp!, { r4, r5, r6, r7, r14 }

  add r0, r0, r1, lsl #1          @ screen_ptr += start
  sub r1, r2, r1                  @ r1 = end - start
  ldr r2, 1b                      @ r2 = palette_ram_converted
  ldr r3, 1b + 4                  @ r3 = 0x3FE

2:
  pld     [r0, #32]
  ldmia r0, { r4, r5, r6, r7 }

  expand_pixel_pair(r4, r14)
  expand_pixel_pair(r5, r14)
  expand_pixel_pair(r6, r14)
  expand_pixel_pair(r7, r14)

  stmia r0!, { r4, r5, r6, r7 }

  subs r1, r1, #8
  bne 2b

  ldmia sp!, { r4, r5, r6, r7, pc }

