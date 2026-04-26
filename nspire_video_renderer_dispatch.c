#include "common.h"
#include "main.h"
#include "video.h"

/* Canonical globals used by the standalone host and other core modules. */
u16 *gba_screen_pixels = NULL;
s32 affine_reference_x[2];
s32 affine_reference_y[2];

/* New libretro video.cc renderer symbols (renamed at compile time). */
extern void video_cc_update_scanline(void);
extern void video_cc_video_reload_counters(void);
extern u16 *video_cc_gba_screen_pixels;
extern s32 video_cc_affine_reference_x[2];
extern s32 video_cc_affine_reference_y[2];

/* Legacy C renderer symbols from old_video/video.c (renamed at compile time). */
extern void old_video_update_scanline(void);
extern void old_video_video_reload_counters(void);
extern u16 *old_video_gba_screen_pixels;
extern s32 old_video_affine_reference_x[2];
extern s32 old_video_affine_reference_y[2];

static void sync_state_to_renderer(int use_old_renderer)
{
  if (use_old_renderer) {
    old_video_gba_screen_pixels = gba_screen_pixels;
    old_video_affine_reference_x[0] = affine_reference_x[0];
    old_video_affine_reference_x[1] = affine_reference_x[1];
    old_video_affine_reference_y[0] = affine_reference_y[0];
    old_video_affine_reference_y[1] = affine_reference_y[1];
  } else {
    video_cc_gba_screen_pixels = gba_screen_pixels;
    video_cc_affine_reference_x[0] = affine_reference_x[0];
    video_cc_affine_reference_x[1] = affine_reference_x[1];
    video_cc_affine_reference_y[0] = affine_reference_y[0];
    video_cc_affine_reference_y[1] = affine_reference_y[1];
  }
}

static void sync_state_from_renderer(int use_old_renderer)
{
  if (use_old_renderer) {
    affine_reference_x[0] = old_video_affine_reference_x[0];
    affine_reference_x[1] = old_video_affine_reference_x[1];
    affine_reference_y[0] = old_video_affine_reference_y[0];
    affine_reference_y[1] = old_video_affine_reference_y[1];
  } else {
    affine_reference_x[0] = video_cc_affine_reference_x[0];
    affine_reference_x[1] = video_cc_affine_reference_x[1];
    affine_reference_y[0] = video_cc_affine_reference_y[0];
    affine_reference_y[1] = video_cc_affine_reference_y[1];
  }
}

void update_scanline(void)
{
  int use_old_renderer = (nspire_video_renderer_choice != 0);
  sync_state_to_renderer(use_old_renderer);
  if (use_old_renderer)
    old_video_update_scanline();
  else
    video_cc_update_scanline();
  sync_state_from_renderer(use_old_renderer);
}

void video_reload_counters(void)
{
  int use_old_renderer = (nspire_video_renderer_choice != 0);
  sync_state_to_renderer(use_old_renderer);
  if (use_old_renderer)
    old_video_video_reload_counters();
  else
    video_cc_video_reload_counters();
  sync_state_from_renderer(use_old_renderer);
}
