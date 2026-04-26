/* Blit libretro video.cc framebuffer (240-pitch RGB565) to the Nspire back buffer.
 * nspire/upscale_aspect.S assumes src stride 320 (legacy video.c); repack rows first. */

#include "common.h"
#include "video.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "font.h"

#include <stdio.h>
#include <string.h>

#if defined(NSPIRE_LIBRETRO)
static u64 nspire_fps_t0;
static u32 nspire_fps_accum;
static u32 nspire_fps_shown;

void nspire_fps_overlay_reset(void)
{
  nspire_fps_t0 = 0;
  nspire_fps_accum = 0;
  nspire_fps_shown = 0;
}

/* Visible FPS at full speed: ~59.73/(N+1) for manual (uniform or random, same long-run
 * average). Auto targets full refresh when the core keeps up with the LCD. */
static u32 nspire_fps_expected_visible(void)
{
  u32 n;

  if (!synchronize_flag)
  {
    if (current_frameskip_type == manual_frameskip)
    {
      /* nspire_frameskip_begin_frame forces used_frameskip = 4 → period (N+1) = 5. */
      n = 4u;
      return (5973u + 50u * (n + 1u)) / (100u * (n + 1u));
    }
    if (current_frameskip_type == auto_frameskip)
      return (5973u + 50u * 5u) / (100u * 5u);
    return 60u;
  }

  switch (current_frameskip_type)
  {
  case no_frameskip:
    return 60u;
  case manual_frameskip:
    n = frameskip_value;
    if (n > 99u)
      n = 99u;
    return (5973u + 50u * (n + 1u)) / (100u * (n + 1u));
  case auto_frameskip:
  default:
    return 60u;
  }
}

static void nspire_fps_overlay_update_and_draw(void)
{
  u64 now;
  char buf[40];
  u32 expected;

  if (!nspire_fps_overlay)
    return;

  get_ticks_us(&now);
  nspire_fps_accum++;
  if (nspire_fps_t0 == 0)
    nspire_fps_t0 = now;
  else
  {
    u64 dt = now - nspire_fps_t0;
    if (dt >= 500000u)
    {
      if (dt > 0u)
        nspire_fps_shown =
            (u32)(((unsigned long long)nspire_fps_accum * 1000000ull) / (unsigned long long)dt);
      nspire_fps_t0 = now;
      nspire_fps_accum = 0u;
    }
  }

  expected = nspire_fps_expected_visible();
  snprintf(buf, sizeof(buf), "FPS:%u/%u", (unsigned)nspire_fps_shown, (unsigned)expected);
  print_string_ext(buf, 0xFFFFu, 0x0000u, 2u, 2u, nspire_screen, 320u, 0u, 0u, FONT_HEIGHT);
}
#endif /* NSPIRE_LIBRETRO */

extern u32 resolution_width;
extern u32 resolution_height;

#define NSPIRE_UPSCALE_SRC_PITCH 320

static u16 nspire_upscale_src[NSPIRE_UPSCALE_SRC_PITCH * GBA_SCREEN_HEIGHT];

/* Inverse of convert_palette() in common.h: core outputs libretro RGB packing for correct
 * blending; Nspire upscale/LCD expect xBBBBBGGGGGRRRRR like legacy gpSP. */
#define NSPIRE_LCD565_FROM_CORE(p) \
  ((u16)(((p) & 0x1Fu) << 10) | ((((p) & 0x07C0u) >> 1) & 0x03E0u) | (((p) >> 11) & 0x1Fu))

static void repack_gba_to_upscale_src(void)
{
  u32 y, x;
  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    u16 *__restrict dst = nspire_upscale_src + y * NSPIRE_UPSCALE_SRC_PITCH;
    const u16 *__restrict src = gba_screen_pixels + y * GBA_SCREEN_PITCH;
    for (x = 0; x < GBA_SCREEN_WIDTH; x += 8)
    {
      dst[x + 0] = NSPIRE_LCD565_FROM_CORE(src[x + 0]);
      dst[x + 1] = NSPIRE_LCD565_FROM_CORE(src[x + 1]);
      dst[x + 2] = NSPIRE_LCD565_FROM_CORE(src[x + 2]);
      dst[x + 3] = NSPIRE_LCD565_FROM_CORE(src[x + 3]);
      dst[x + 4] = NSPIRE_LCD565_FROM_CORE(src[x + 4]);
      dst[x + 5] = NSPIRE_LCD565_FROM_CORE(src[x + 5]);
      dst[x + 6] = NSPIRE_LCD565_FROM_CORE(src[x + 6]);
      dst[x + 7] = NSPIRE_LCD565_FROM_CORE(src[x + 7]);
    }
  }
}

#define NSPIRE_MIX_PIXELS (GBA_SCREEN_PITCH * GBA_SCREEN_HEIGHT)

static u16 mix_prev[NSPIRE_MIX_PIXELS];

void nspire_video_mix_reset(void)
{
  memset(mix_prev, 0xFF, sizeof(mix_prev));
}

/* Same 50:50 packed RGB565 mix as libretro video_post_process_mix(). */
static void repack_gba_mix_to_upscale_src(void)
{
  u32 y, x;
  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    u16 *__restrict dst = nspire_upscale_src + y * NSPIRE_UPSCALE_SRC_PITCH;
    u16 *__restrict prev_row = mix_prev + y * GBA_SCREEN_PITCH;
    const u16 *__restrict src = gba_screen_pixels + y * GBA_SCREEN_PITCH;
    for (x = 0; x < GBA_SCREEN_WIDTH; x += 8)
    {
      u16 c0 = src[x + 0], p0 = prev_row[x + 0];
      u16 c1 = src[x + 1], p1 = prev_row[x + 1];
      u16 c2 = src[x + 2], p2 = prev_row[x + 2];
      u16 c3 = src[x + 3], p3 = prev_row[x + 3];
      u16 c4 = src[x + 4], p4 = prev_row[x + 4];
      u16 c5 = src[x + 5], p5 = prev_row[x + 5];
      u16 c6 = src[x + 6], p6 = prev_row[x + 6];
      u16 c7 = src[x + 7], p7 = prev_row[x + 7];
      prev_row[x + 0] = c0;
      prev_row[x + 1] = c1;
      prev_row[x + 2] = c2;
      prev_row[x + 3] = c3;
      prev_row[x + 4] = c4;
      prev_row[x + 5] = c5;
      prev_row[x + 6] = c6;
      prev_row[x + 7] = c7;
      dst[x + 0] = NSPIRE_LCD565_FROM_CORE((u16)((c0 + p0 + ((c0 ^ p0) & 0x821u)) >> 1));
      dst[x + 1] = NSPIRE_LCD565_FROM_CORE((u16)((c1 + p1 + ((c1 ^ p1) & 0x821u)) >> 1));
      dst[x + 2] = NSPIRE_LCD565_FROM_CORE((u16)((c2 + p2 + ((c2 ^ p2) & 0x821u)) >> 1));
      dst[x + 3] = NSPIRE_LCD565_FROM_CORE((u16)((c3 + p3 + ((c3 ^ p3) & 0x821u)) >> 1));
      dst[x + 4] = NSPIRE_LCD565_FROM_CORE((u16)((c4 + p4 + ((c4 ^ p4) & 0x821u)) >> 1));
      dst[x + 5] = NSPIRE_LCD565_FROM_CORE((u16)((c5 + p5 + ((c5 ^ p5) & 0x821u)) >> 1));
      dst[x + 6] = NSPIRE_LCD565_FROM_CORE((u16)((c6 + p6 + ((c6 ^ p6) & 0x821u)) >> 1));
      dst[x + 7] = NSPIRE_LCD565_FROM_CORE((u16)((c7 + p7 + ((c7 ^ p7) & 0x821u)) >> 1));
    }
  }
}

void nspire_present_frame(u32 skip_next_frame)
{
  if (skip_next_frame)
  {
    update_at_vblank();
    return;
  }

  if ((resolution_width == 240) && (resolution_height == 160) && gba_screen_pixels)
  {
    if (nspire_frame_mix_choice)
    {
      repack_gba_mix_to_upscale_src();
      if (screen_scale == fullscreen)
        upscale_aspect_fast(nspire_screen, nspire_upscale_src);
      else if (screen_scale == scaled_raw)
        upscale_aspect_raw(nspire_screen, nspire_upscale_src);
      else
        upscale_aspect(nspire_screen, nspire_upscale_src);
    }
    else
    {
      /* Option-A experiment: avoid full-frame repack on non-mix gameplay.
       * This routes all scale modes through the direct one-pass path so we can
       * measure the repack overhead impact on device. */
      upscale_aspect_raw_from_gba(nspire_screen, gba_screen_pixels);
    }
#if defined(NSPIRE_LIBRETRO)
    nspire_fps_overlay_update_and_draw();
#endif
  }

  nspire_lcd_fb_cache_clean_for_display();
  {
    void *temp = nspire_displayed_screen;
    nspire_displayed_screen = nspire_screen;
    nspire_screen = nspire_screen_2;
    nspire_screen_2 = nspire_screen_3;
    nspire_screen_3 = temp;
  }
  update_at_vblank();
}
