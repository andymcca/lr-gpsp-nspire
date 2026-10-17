/*
 * Output for portrait LCDs (CX II). The LCD scans 320 lines of 240 pixels,
 * so frames are written turned: landscape pixel (x, y) goes to position
 * 239 - y of line x. Writing them this way, instead of letting Ndless's
 * compatibility mode turn the picture inside the panel, is what removes the
 * diagonal tearing: the panel then shows lines in the order the LCD sends
 * them, and buffers are swapped between refreshes (nspire_flip).
 *
 * The LCD stays in the OS's 5:6:5 format, which is the core's own pixel
 * format, so game frames need no color conversion; menus and the blended
 * scalers draw in gpSP's 5:5:5 menu format and are converted here.
 *
 * Everything is turned in strips of 8 landscape lines: each column of a
 * strip is 8 pixels next to each other in one LCD line, written as one
 * 16-byte burst (STM). The screen buffers are never read by the CPU, so they
 * aren't in the data cache, and single stores would each go out to memory
 * on their own.
 */

#include "common.h"
#include "nspire.h"

#define DST_W 240   /* pixels per LCD line */

static const u16 zero_row[GBA_SCREEN_WIDTH] __attribute__((aligned(4)));

/* Writes 4 words to d (4-byte aligned) as one STM; GCC would write them as 4
 * single stores. STM stores registers in register order, hence r4-r7. */
static inline void store_4_words(u32 *d, u32 a, u32 b, u32 c, u32 e)
{
#if defined(__arm__)
  register u32 r4 __asm__("r4") = a;
  register u32 r5 __asm__("r5") = b;
  register u32 r6 __asm__("r6") = c;
  register u32 r7 __asm__("r7") = e;
  __asm__ volatile ("stmia %0, {%1, %2, %3, %4}"
                    : : "r" (d), "r" (r4), "r" (r5), "r" (r6), "r" (r7) : "memory");
#else
  d[0] = a;
  d[1] = b;
  d[2] = c;
  d[3] = e;
#endif
}

/* 8 pixels of one column, lines y..y+7 (s[0] is line y), as the 4 words at
 * positions 232 - y ... 239 - y of its LCD line. */
#define COLUMN_WORDS(s0, s1, s2, s3, s4, s5, s6, s7)                  \
  (u32)(s7) | ((u32)(s6) << 16), (u32)(s5) | ((u32)(s4) << 16),       \
  (u32)(s3) | ((u32)(s2) << 16), (u32)(s1) | ((u32)(s0) << 16)

/* Row r (0-213) of the 320x214 "raw" picture: each group of three source
 * rows becomes four, the third doubled. 160 rows make 213 1/3, so the last
 * row repeats source row 159. */
static const u16 *raw_source_row(const u16 *src, u32 src_pitch, s32 r)
{
  u32 k, phase, src_y;

  if (r < 0 || r > 213)
    return zero_row;
  k = (u32)r >> 2;
  phase = (u32)r & 3;
  src_y = 3 * k + (phase == 3 ? 2 : phase);
  if (src_y > GBA_SCREEN_HEIGHT - 1)
    src_y = GBA_SCREEN_HEIGHT - 1;
  return src + src_y * src_pitch;
}

/* 240x160 -> 320x214 (the same picture as upscale_aspect_raw), turned, at
 * landscape lines 13-226; lines 12 and 227 are written black. Source columns
 * 0, 1, 2 become lines 0, 1, 1, 2 of each group of 4. src rows must be
 * 2-byte aligned. */
void nspire_turn_raw(u16 *dst, const u16 *src, u32 src_pitch)
{
  s32 y;

  for (y = 12; y < 228; y += 8)
  {
    const u16 *r0 = raw_source_row(src, src_pitch, y - 13);
    const u16 *r1 = raw_source_row(src, src_pitch, y - 12);
    const u16 *r2 = raw_source_row(src, src_pitch, y - 11);
    const u16 *r3 = raw_source_row(src, src_pitch, y - 10);
    const u16 *r4 = raw_source_row(src, src_pitch, y - 9);
    const u16 *r5 = raw_source_row(src, src_pitch, y - 8);
    const u16 *r6 = raw_source_row(src, src_pitch, y - 7);
    const u16 *r7 = raw_source_row(src, src_pitch, y - 6);
    u32 *d = (u32 *)(dst + 232 - y);
    u32 x;

    for (x = 0; x < GBA_SCREEN_WIDTH; x += 3)
    {
      u32 a0 = r0[x], a1 = r1[x], a2 = r2[x], a3 = r3[x];
      u32 a4 = r4[x], a5 = r5[x], a6 = r6[x], a7 = r7[x];
      store_4_words(d, COLUMN_WORDS(a0, a1, a2, a3, a4, a5, a6, a7));
      d += DST_W / 2;

      a0 = r0[x + 1]; a1 = r1[x + 1]; a2 = r2[x + 1]; a3 = r3[x + 1];
      a4 = r4[x + 1]; a5 = r5[x + 1]; a6 = r6[x + 1]; a7 = r7[x + 1];
      store_4_words(d, COLUMN_WORDS(a0, a1, a2, a3, a4, a5, a6, a7));
      d += DST_W / 2;
      store_4_words(d, COLUMN_WORDS(a0, a1, a2, a3, a4, a5, a6, a7));
      d += DST_W / 2;

      a0 = r0[x + 2]; a1 = r1[x + 2]; a2 = r2[x + 2]; a3 = r3[x + 2];
      a4 = r4[x + 2]; a5 = r5[x + 2]; a6 = r6[x + 2]; a7 = r7[x + 2];
      store_4_words(d, COLUMN_WORDS(a0, a1, a2, a3, a4, a5, a6, a7));
      d += DST_W / 2;
    }
  }
}

/* 240x160 unscaled, turned, at landscape (40, 40)-(279, 199). */
void nspire_turn_1x(u16 *dst, const u16 *src, u32 src_pitch)
{
  u32 y;

  for (y = 0; y < GBA_SCREEN_HEIGHT; y += 8)
  {
    const u16 *s = src + y * src_pitch;
    u32 *d = (u32 *)(dst + 40 * DST_W + 232 - (40 + y));
    u32 x;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++, s++, d += DST_W / 2)
      store_4_words(d, COLUMN_WORDS(s[0], s[src_pitch], s[2 * src_pitch], s[3 * src_pitch],
                                    s[4 * src_pitch], s[5 * src_pitch], s[6 * src_pitch],
                                    s[7 * src_pitch]));
  }
}

/* Menu colors (xBBBBBGGGGGRRRRR, legacy gpSP) -> LCD 5:6:5. */
static inline u32 lcd565_from_555(u32 p)
{
  u32 r = p & 0x1F, g = (p >> 5) & 0x1F, b = (p >> 10) & 0x1F;
  return (r << 11) | (((g << 1) | (g >> 4)) << 5) | b;
}

static inline u32 same_pixel(u32 p)
{
  return p;
}

/* One strip of up to 8 lines of a w x h landscape rectangle whose top-left
 * pixel, at landscape (x, y), is src[0]: each of its columns is one run of h
 * pixels in LCD line x + i, written two pixels at a time where 4-byte
 * aligned. A full strip at an even line is 16 bytes a column, 4-byte
 * aligned, and gets a loop of its own. */
#define TURN_STRIP(name, convert)                                                       \
static void name(u16 *dst, const u16 *src, u32 src_pitch, u32 x, u32 y, u32 w, u32 h)  \
{                                                                                       \
  const u32 first = DST_W - y - h;   /* where line y + h - 1 goes */                    \
  u32 i;                                                                                \
                                                                                        \
  if (h == 8 && !(first & 1))                                                           \
  {                                                                                     \
    const u16 *s = src;                                                                 \
    u32 *d = (u32 *)(dst + x * DST_W + first);                                          \
    const u32 p = src_pitch;                                                            \
    for (i = 0; i < w; i++, s++, d += DST_W / 2)                                        \
      store_4_words(d, COLUMN_WORDS(convert(s[0]), convert(s[p]), convert(s[2 * p]),     \
                                    convert(s[3 * p]), convert(s[4 * p]),               \
                                    convert(s[5 * p]), convert(s[6 * p]),               \
                                    convert(s[7 * p])));                                \
    return;                                                                             \
  }                                                                                     \
                                                                                        \
  for (i = 0; i < w; i++)                                                               \
  {                                                                                     \
    u16 *row = dst + (x + i) * DST_W;                                                   \
    const u16 *s = src + (h - 1) * src_pitch + i;   /* line y + h - 1 */                \
    u32 column = first, end = DST_W - y;                                                \
                                                                                        \
    if (column & 1)                                                                     \
    {                                                                                   \
      row[column++] = (u16)convert(*s);                                                 \
      s -= src_pitch;                                                                   \
    }                                                                                   \
    for (; column + 1 < end; column += 2, s -= 2 * src_pitch)                           \
      *(u32 *)(row + column) = convert(s[0]) | (convert(s[-(s32)src_pitch]) << 16);     \
    if (column < end)                                                                   \
      row[column] = (u16)convert(*s);                                                   \
  }                                                                                     \
}

TURN_STRIP(turn_strip_565, same_pixel)
TURN_STRIP(turn_strip_555, lcd565_from_555)

/* Turns a w x h landscape rectangle (src points at its top-left pixel) to
 * landscape position (x0, y0), in strips of 8 lines that start at multiples
 * of 8 so most are full and aligned. */
static void turn_rect(u16 *dst, const u16 *src, u32 src_pitch, u32 x0, u32 y0, u32 w, u32 h,
                      int from_555)
{
  while (h > 0)
  {
    u32 lines = 8 - (y0 & 7);
    if (lines > h)
      lines = h;
    if (from_555)
      turn_strip_555(dst, src, src_pitch, x0, y0, w, lines);
    else
      turn_strip_565(dst, src, src_pitch, x0, y0, w, lines);
    src += lines * src_pitch;
    y0 += lines;
    h -= lines;
  }
}

void nspire_turn_rect_565(u16 *dst, const u16 *src, u32 src_pitch,
                          u32 x0, u32 y0, u32 w, u32 h)
{
  turn_rect(dst, src, src_pitch, x0, y0, w, h, 0);
}

/* The same, converting from the menu's color format. For menus, the blended
 * scalers and the FPS counter. */
void nspire_turn_rect_555(u16 *dst, const u16 *src, u32 src_pitch,
                          u32 x0, u32 y0, u32 w, u32 h)
{
  turn_rect(dst, src, src_pitch, x0, y0, w, h, 1);
}
