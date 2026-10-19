/* The calculator-only pieces of gpSP's frontend, for gpsp-host: the ARM
 * assembly routines (nspire/*.S, *.s) and the CPU clock (nspire_clock.c).
 * The scalers draw the same rows as notaz's ARM ones (nearest pixels instead
 * of blended ones), which is what the tests look at. */

#include "common.h"
#include "nspire_clock.h"

#include <stdio.h>

uint32_t not_crt0_savedsp;

void enableAlignmentExceptions(void) {}
void clean_dcache(void) {}

void video_fill_u16_strip_arm(u16 *dst, u32 nh, u32 value)
{
  while (nh--)
    *dst++ = (u16)value;
}

/* src: 240x160 with 320 pixels a line (nspire_video_present.c's repack). */
static void scale(u16 *dst, const u16 *src, u32 top, u32 height)
{
  u32 x, y;
  for (y = 0; y < height; y++)
    for (x = 0; x < 320; x++)
      dst[(top + y) * 320 + x] = src[(y * 160 / height) * 320 + x * 240 / 320];
}

void upscale_aspect(u16 *dst, u16 *src)
{
  scale(dst, src, 13, 213);
}

void upscale_aspect_fast(u16 *dst, u16 *src)
{
  scale(dst, src, 0, 240);
}

void upscale_aspect_raw(u16 *dst, u16 *src)
{
  scale(dst, src, 13, 214);
}

/* ---- The CPU clock: there is none to change here ---- */

char nspire_clock_crashed_rom[512];

void nspire_clock_check_crash(void) {}
void nspire_clock_game(void) {}
void nspire_clock_normal(void) {}
void nspire_clock_left_game(void) {}
int nspire_clock_is_cx2(void) { return 1; }

u32 nspire_cpu_mhz(void)
{
  return 0;
}

void nspire_clock_report(char *text, u32 size)
{
  snprintf(text, size, "clock: PC test build");
}
