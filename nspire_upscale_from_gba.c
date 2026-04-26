/*
 * One-pass 240x160 libretro RGB565 -> 320x240 Nspire layout + upscale_aspect_raw.
 * Avoids the separate repack into nspire_upscale_src (stride 320) + second read.
 */

#include "common.h"
#include "video.h"

/* Same as nspire_lcd_pixel_from_core in nspire_video_present.c */
static inline u16 nspire_rgb_from_libretro(u16 p)
{
  return (u16)(((p & 0x1F) << 10) | (((p & 0x07C0) >> 1) & 0x03E0) | ((p >> 11) & 0x1F));
}

static inline u32 swizzle_pair(const u16 *p)
{
  u32 lo = nspire_rgb_from_libretro(p[0]);
  u32 hi = nspire_rgb_from_libretro(p[1]);
  return lo | (hi << 16);
}

/* C port of upscale_aspect_raw in nspire/upscale_aspect.s: src stride 320 with
 * row padding (320-240)*2 bytes after each 240-pixel row. Here src is contiguous
 * 240-wide (stride GBA_SCREEN_PITCH), so inter-block r1 skips become 0. */
static void raw_batch12(u32 **pr0, const u16 **pr1)
{
  u32 *r0 = *pr0;
  const u16 *r1 = *pr1;
  u32 r2 = swizzle_pair(r1);
  u32 r4 = swizzle_pair(r1 + 2);
  u32 r5 = swizzle_pair(r1 + 4);
  u32 r6 = swizzle_pair(r1 + 6);
  u32 r8 = swizzle_pair(r1 + 8);
  u32 r9 = swizzle_pair(r1 + 10);
  u32 r3, r4p, r7, r8p;

  r1 += 12;

  r3 = (r2 >> 16) | (r4 << 16);
  r4p = (r4 >> 16) | (r5 << 16);
  *r0++ = r2;
  *r0++ = r3;
  *r0++ = r4p;
  *r0++ = r5;

  r7 = (r6 >> 16) | (r8 << 16);
  r8p = (r8 >> 16) | (r9 << 16);
  *r0++ = r6;
  *r0++ = r7;
  *r0++ = r8p;
  *r0++ = r9;

  *pr0 = r0;
  *pr1 = r1;
}

void upscale_aspect_raw_from_gba(u16 *dst, const u16 *gba)
{
  u32 *r0 = (u32 *)((u8 *)dst + 320 * 2 * 13);
  const u16 *r1 = gba;
  int r10 = 54;

  for (;;)
  {
    int r11;

    /* loop_raw_2 */
    for (r11 = 20; r11 > 0; r11--)
      raw_batch12(&r0, &r1);

    /* asm: add r1, #320*2 - 240*2 after each 240-pixel row in 320-pitch buf */
    r1 += 0;

    /* loop_raw_3 */
    for (r11 = 20; r11 > 0; r11--)
      raw_batch12(&r0, &r1);

    r10--;
    if (r10 == 0)
      return;

    r1 += 0;

    {
      u32 *r12 = r0 + (320 * 2) / 4;

      for (r11 = 20; r11 > 0; r11--)
      {
        u32 r2 = swizzle_pair(r1);
        u32 r4 = swizzle_pair(r1 + 2);
        u32 r5 = swizzle_pair(r1 + 4);
        u32 r6 = swizzle_pair(r1 + 6);
        u32 r8 = swizzle_pair(r1 + 8);
        u32 r9 = swizzle_pair(r1 + 10);
        u32 r3, r4p, r7, r8p;

        r1 += 12;

        r3 = (r2 >> 16) | (r4 << 16);
        r4p = (r4 >> 16) | (r5 << 16);
        *r0++ = r2;
        *r0++ = r3;
        *r0++ = r4p;
        *r0++ = r5;
        *r12++ = r2;
        *r12++ = r3;
        *r12++ = r4p;
        *r12++ = r5;

        r7 = (r6 >> 16) | (r8 << 16);
        r8p = (r8 >> 16) | (r9 << 16);
        *r0++ = r6;
        *r0++ = r7;
        *r0++ = r8p;
        *r0++ = r9;
        *r12++ = r6;
        *r12++ = r7;
        *r12++ = r8p;
        *r12++ = r9;
      }

      r1 += 0;
      r0 = r12;
    }
  }
}
