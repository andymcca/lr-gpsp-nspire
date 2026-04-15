/* Blit libretro video.cc framebuffer (240-pitch RGB565) to the Nspire back buffer.
 * nspire/upscale_aspect.S assumes src stride 320 (legacy video.c); repack rows first. */

#include "common.h"
#include "video.h"
#include "nspire.h"
#include "nspire_gui_video.h"

#include <string.h>

extern u32 resolution_width;
extern u32 resolution_height;

#define NSPIRE_UPSCALE_SRC_PITCH 320

static u16 nspire_upscale_src[NSPIRE_UPSCALE_SRC_PITCH * GBA_SCREEN_HEIGHT];

/* Inverse of convert_palette() in common.h: core outputs libretro RGB packing for correct
 * blending; Nspire upscale/LCD expect xBBBBBGGGGGRRRRR like legacy gpSP. */
static inline u16 nspire_lcd_pixel_from_core(u16 p)
{
  return (u16)(((p & 0x1F) << 10) | (((p & 0x07C0) >> 1) & 0x03E0) | ((p >> 11) & 0x1F));
}

static void repack_gba_to_upscale_src(void)
{
  u32 y, x;
  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    u16 *dst = nspire_upscale_src + y * NSPIRE_UPSCALE_SRC_PITCH;
    const u16 *src = gba_screen_pixels + y * GBA_SCREEN_PITCH;
    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
      dst[x] = nspire_lcd_pixel_from_core(src[x]);
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
    u16 *dst = nspire_upscale_src + y * NSPIRE_UPSCALE_SRC_PITCH;
    u16 *prev_row = mix_prev + y * GBA_SCREEN_PITCH;
    const u16 *src = gba_screen_pixels + y * GBA_SCREEN_PITCH;
    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
      u16 rgb_curr = src[x];
      u16 rgb_prev = prev_row[x];
      prev_row[x] = rgb_curr;
      {
        u16 mixed = (u16)((rgb_curr + rgb_prev + ((rgb_curr ^ rgb_prev) & 0x821)) >> 1);
        dst[x] = nspire_lcd_pixel_from_core(mixed);
      }
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
      repack_gba_mix_to_upscale_src();
    else
      repack_gba_to_upscale_src();
    if (screen_scale == fullscreen)
      upscale_aspect_fast(nspire_screen, nspire_upscale_src);
    else if (screen_scale == scaled_raw)
      upscale_aspect_raw(nspire_screen, nspire_upscale_src);
    else
      upscale_aspect(nspire_screen, nspire_upscale_src);
  }

  clean_dcache();
  {
    void *temp = nspire_displayed_screen;
    nspire_displayed_screen = nspire_screen;
    nspire_screen = nspire_screen_2;
    nspire_screen_2 = nspire_screen_3;
    nspire_screen_3 = temp;
  }
  update_at_vblank();
#if defined(NSPIRE_LIBRETRO)
  relative_frame_count++;
#endif
}
