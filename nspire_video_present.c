/* Blit libretro video.cc framebuffer (240-pitch RGB565) to the Nspire back buffer.
 * nspire/upscale_aspect.S assumes src stride 320 (legacy video.c); repack rows first. */

#include "common.h"
#include "video.h"
#include "nspire.h"
#include "nspire_gui_video.h"

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

void nspire_present_frame(u32 skip_next_frame)
{
  if (skip_next_frame)
  {
    update_at_vblank();
    return;
  }

  if ((resolution_width == 240) && (resolution_height == 160) && gba_screen_pixels)
  {
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
}
