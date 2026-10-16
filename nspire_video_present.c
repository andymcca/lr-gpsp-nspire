/* Game frames to the LCD, with the FPS counter and messages over them.
 *
 * The core draws 240x160 RGB565 frames (gba_screen_pixels, 240 pixels a
 * line). On the CX II's portrait LCD they're scaled and turned straight into
 * the next screen buffer, in one pass: nspire_turn.c for the sharp ones,
 * nspire_blend.c for the blended ones (with interframe blending on the way).
 * On landscape LCDs notaz's scalers (nspire/upscale_aspect.s, gpSP's 5:5:5
 * menu format) write the screen buffer directly, as before. */

#include "common.h"
#include "video.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern u32 resolution_width;
extern u32 resolution_height;

/* ---- Text over the picture ---- */

static char message[64];
static u32 message_until;
static int message_on;

char nspire_fps_text[64];
u16 nspire_fps_color;
char nspire_perf_text[80];

void nspire_osd_message(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vsnprintf(message, sizeof(message), format, ap);
  va_end(ap);
  message_until = nspire_ticks() + NSPIRE_TICK_HZ * 2;
  message_on = 1;
}

void nspire_osd_clear(void)
{
  message_on = 0;
  nspire_fps_text[0] = 0;
  nspire_perf_text[0] = 0;
}

static const char *visible_message(void)
{
  if (message_on && (s32)(nspire_ticks() - message_until) >= 0)
    message_on = 0;
  return message_on ? message : NULL;
}

/* Colors in the LCD's 5:6:5 (portrait) and gpSP's 5:5:5 BGR (landscape). */
#define RGB565(r, g, b) ((u16)(((r) << 11) | ((g) << 5) | (b)))
#define BGR555(r, g, b) ((u16)(((b) << 10) | ((g) << 5) | (r)))

u16 nspire_osd_color(int which)
{
  static const u16 portrait[] = { RGB565(31, 63, 31), RGB565(4, 58, 4), RGB565(31, 44, 0), RGB565(31, 8, 4) };
  static const u16 landscape[] = { BGR555(31, 31, 31), BGR555(2, 29, 2), BGR555(31, 22, 0), BGR555(31, 4, 2) };
  return nspire_lcd_portrait ? portrait[which] : landscape[which];
}

/* A 320x12 band of the landscape picture: black, with text in it. */
#define BAND_H 12
static u16 band[320 * BAND_H] __attribute__((aligned(4)));

static void band_text(const char *left, u16 left_color, const char *centered, u16 centered_color)
{
  memset(band, 0, sizeof(band));
  if (left)
    nspire_draw_text(band, 320, 2, 1, left, left_color, 0, 320);
  if (centered)
  {
    u32 w = (u32)strlen(centered) * OSD_FONT_W;
    u32 x = w < 316 ? (320 - w) / 2 : 2;
    nspire_draw_text(band, 320, x, 1, centered, centered_color, 0, 318);
  }
}

/* A text box on a black plate, for pictures that fill the screen. */
static void portrait_box(u16 *dst, u32 x, u32 y, const char *text, u16 color)
{
  u32 w = (u32)strlen(text) * OSD_FONT_W + 4;
  if (w > 320 - x)
    w = 320 - x;
  memset(band, 0, sizeof(band));
  nspire_draw_text(band, 320, 2, 1, text, color, 0, w);
  nspire_turn_rect_565(dst, band, 320, x, y, w, BAND_H);
}

/* What each screen buffer's bands hold, so that a band is only drawn again
 * when its text changes (the pictures outside fullscreen never cover them). */
#define SCREENS 3
static struct
{
  int valid;
  char top[sizeof(nspire_perf_text)];
  u16 top_color;
  char bottom[sizeof(message)];
} bands_shown[SCREENS];

void nspire_present_forget_bands(void)
{
  int i;
  for (i = 0; i < SCREENS; i++)
    bands_shown[i].valid = 0;
}

/* The counters go in the top band and messages in the bottom one, which are
 * outside the picture except in fullscreen mode. A band is drawn whenever
 * its text differs from what that buffer has, so text that goes away
 * doesn't linger in the buffers. */
static void draw_text_portrait(u16 *dst)
{
  const char *msg = visible_message();
  const char *top = nspire_perf_text[0] ? nspire_perf_text : nspire_fps_text[0] ? nspire_fps_text : NULL;
  u16 top_color = nspire_perf_text[0] ? nspire_osd_color(OSD_WHITE) : nspire_fps_color;
  int screen = nspire_screen_index() % SCREENS;

  if (screen_scale == fullscreen)
  {
    bands_shown[screen].valid = 0;
    if (top)
      portrait_box(dst, 0, 0, top, top_color);
    if (msg)
    {
      u32 w = (u32)strlen(msg) * OSD_FONT_W + 4;
      portrait_box(dst, w < 320 ? (320 - w) / 2 : 0, 240 - BAND_H, msg, nspire_osd_color(OSD_WHITE));
    }
    return;
  }
  if (!top)
    top = "";
  if (!msg)
    msg = "";
  if (!bands_shown[screen].valid || strcmp(bands_shown[screen].top, top) ||
      bands_shown[screen].top_color != top_color)
  {
    band_text(top, top_color, NULL, 0);
    nspire_turn_rect_565(dst, band, 320, 0, 0, 320, BAND_H);
    snprintf(bands_shown[screen].top, sizeof(bands_shown[screen].top), "%s", top);
    bands_shown[screen].top_color = top_color;
  }
  if (!bands_shown[screen].valid || strcmp(bands_shown[screen].bottom, msg))
  {
    band_text(NULL, 0, msg, nspire_osd_color(OSD_WHITE));
    nspire_turn_rect_565(dst, band, 320, 0, 240 - BAND_H, 320, BAND_H);
    snprintf(bands_shown[screen].bottom, sizeof(bands_shown[screen].bottom), "%s", msg);
  }
  bands_shown[screen].valid = 1;
}

static void draw_text_landscape(u16 *dst)
{
  const char *msg = visible_message();
  const char *top = nspire_perf_text[0] ? nspire_perf_text : nspire_fps_text[0] ? nspire_fps_text : NULL;

  if (top)
    nspire_draw_text(dst, 320, 2, 1, top,
                     nspire_perf_text[0] ? nspire_osd_color(OSD_WHITE) : nspire_fps_color, 0, 318);
  if (msg)
  {
    u32 w = (u32)strlen(msg) * OSD_FONT_W;
    nspire_draw_text(dst, 320, w < 316 ? (320 - w) / 2 : 2, 240 - BAND_H + 1, msg,
                     nspire_osd_color(OSD_WHITE), 0, 318);
  }
}

/* ---- Frames ---- */

#define NSPIRE_UPSCALE_SRC_PITCH 320

static u16 nspire_upscale_src[NSPIRE_UPSCALE_SRC_PITCH * GBA_SCREEN_HEIGHT];

/* Inverse of convert_palette() in common.h: core outputs libretro RGB packing for correct
 * blending; Nspire upscale/LCD expect xBBBBBGGGGGRRRRR like legacy gpSP. */
#define NSPIRE_LCD565_FROM_CORE(p) \
  ((u16)(((p) & 0x1Fu) << 10) | ((((p) & 0x07C0u) >> 1) & 0x03E0u) | (((p) >> 11) & 0x1Fu))

static void repack_gba_to_upscale_src(const u16 *frame, u32 pitch)
{
  u32 y, x;
  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    u16 *__restrict dst = nspire_upscale_src + y * NSPIRE_UPSCALE_SRC_PITCH;
    const u16 *__restrict src = frame + y * pitch;
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

static u16 mix_prev[NSPIRE_MIX_PIXELS] __attribute__((aligned(32)));
static u16 mix_out[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT] __attribute__((aligned(4)));

/* The blended pictures' interframe blending: the core draws frames into
 * frame_a (its own buffer) and frame_b in turn, and prev_frame is the one
 * drawn before (NULL: none yet). Reading the previous frame where it was
 * drawn saves copying each frame (the old way, still used by the other
 * pictures with mix_prev): a whole frame written back to memory. */
static u16 frame_b[NSPIRE_MIX_PIXELS] __attribute__((aligned(32)));
static u16 *frame_a;
static const u16 *prev_frame;

static int blends_frames_itself(void);

void nspire_video_mix_reset(void)
{
  memset(mix_prev, 0xFF, sizeof(mix_prev));
  prev_frame = NULL;
}

void nspire_video_frame_start(void)
{
  if (!gba_screen_pixels || !nspire_frame_mix_choice || !blends_frames_itself())
  {
    prev_frame = NULL;
    return;
  }
  if (!frame_a)
    frame_a = gba_screen_pixels;
  prev_frame = gba_screen_pixels;
  gba_screen_pixels = gba_screen_pixels == frame_b ? frame_a : frame_b;
}

/* Interframe blending: the same 50:50 packed RGB565 mix as libretro's
 * video_post_process_mix(), into mix_out (240 pixels a line). */
static void mix_frames(void)
{
  u32 y, x;
  for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
  {
    const u16 *__restrict src = gba_screen_pixels + y * GBA_SCREEN_PITCH;
    u16 *__restrict prev_row = mix_prev + y * GBA_SCREEN_PITCH;
    u16 *__restrict dst = mix_out + y * GBA_SCREEN_WIDTH;
    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
      u16 c = src[x], p = prev_row[x];
      prev_row[x] = c;
      dst[x] = (u16)((c + p + ((c ^ p) & 0x821u)) >> 1);
    }
  }
}

static void present_portrait(const u16 *frame, u32 pitch)
{
  u16 *dst = (u16 *)nspire_screen;
  const u16 *prev = nspire_frame_mix_choice ? prev_frame : NULL;

  switch (screen_scale)
  {
  case unscaled:
    nspire_turn_1x(dst, frame, pitch);
    break;
  case scaled_aspect:
    nspire_blend_turn_aspect(dst, gba_screen_pixels, prev);
    break;
  case fullscreen:
    nspire_blend_turn_full(dst, gba_screen_pixels, prev);
    break;
  case scaled_raw:
  default:
    nspire_turn_raw(dst, frame, pitch);
    break;
  }
  draw_text_portrait(dst);
}

/* The blended pictures on a portrait LCD do interframe blending themselves. */
static int blends_frames_itself(void)
{
  return nspire_lcd_portrait && (screen_scale == scaled_aspect || screen_scale == fullscreen);
}

static void present_landscape(const u16 *frame, u32 pitch)
{
  u16 *dst = (u16 *)nspire_screen;

  if (frame == gba_screen_pixels && screen_scale == scaled_raw)
    upscale_aspect_raw_from_gba(dst, frame);
  else
  {
    repack_gba_to_upscale_src(frame, pitch);
    if (screen_scale == fullscreen)
      upscale_aspect_fast(dst, nspire_upscale_src);
    else if (screen_scale == scaled_raw)
      upscale_aspect_raw(dst, nspire_upscale_src);
    else
      upscale_aspect(dst, nspire_upscale_src);
  }
  draw_text_landscape(dst);
}

/* Shows the frame the core just drew. */
void nspire_present_frame(void)
{
  const u16 *frame = gba_screen_pixels;
  u32 pitch = GBA_SCREEN_PITCH;

  if (resolution_width != 240 || resolution_height != 160 || !gba_screen_pixels)
    return;
  if (nspire_frame_mix_choice && !blends_frames_itself())
  {
    mix_frames();
    frame = mix_out;
    pitch = GBA_SCREEN_WIDTH;
  }
  if (nspire_lcd_portrait)
    present_portrait(frame, pitch);
  else
    present_landscape(frame, pitch);
  nspire_flip();
}
