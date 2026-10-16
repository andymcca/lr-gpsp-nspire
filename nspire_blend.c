/*
 * The blended pictures on portrait LCDs (CX II), drawn in one pass: the GBA
 * frame is read once, scaled with blending, and written straight into the
 * turned screen buffer (landscape pixel (x, y) at position 239 - y of LCD
 * line x, as in nspire_turn.c). Interframe blending is done on the way, a few
 * source lines at a time.
 *
 * gpSP used to do this in four passes over memory: frame mixing, a copy into
 * the old 5:5:5 menu format, notaz's scaler (nspire/upscale_aspect.s) into a
 * landscape buffer, and turning that back into 5:6:5. Each pass read a whole
 * picture back from memory (about 120 CPU cycles for every 32 bytes, as the
 * data cache is only 8 KB), about 10 ms a frame in all.
 *
 * The math is notaz's, in the LCD's 5:6:5: red and blue come out exactly as
 * before, green keeps its sixth bit (the old path dropped it).
 *   3:2 (240x160 -> 320x213): each 3x3 block of source pixels becomes 4x4.
 *     Along each direction the four are: the first source pixel, 3:5 of the
 *     first and the middle one, 5:3 of the middle and the last one, the last
 *     one. Where both directions are blended: 14:7:7:4 of the middle pixel,
 *     its two neighbours toward that corner, and the corner pixel. The last
 *     source line is only blended across.
 *   fullscreen (240x160 -> 320x240): across, each 3 pixels become a b (b+c)/2
 *     c; down, each 2 lines become a (a+b)/2 b (rounded down).
 */

#include "common.h"
#include "nspire.h"
#include "nspire_frontend.h"

#define DST_W 240   /* pixels per LCD line */
#define SRC_W GBA_SCREEN_PITCH

/* Pixels "unpacked" with room to add them up: 000000gg gggg0000 rrrrr000 00bbbbb
 * (green in bits 21-26, red 11-15, blue 0-4); sums of up to 32 of them fit. */
#define UNPACKED_MASK 0x07E0F81Fu
#define UNPACKED_ONES 0x00200801u   /* 1 in each color */

static inline u32 unpack(u32 p)
{
  return (p | (p << 16)) & UNPACKED_MASK;
}

/* (3 a + 5 b) / 8, rounded (notaz's do_3_5) */
static inline u32 blend_3_5(u32 a, u32 b)
{
  return ((a + (a << 1) + b + (b << 2) + (UNPACKED_ONES << 2)) >> 3) & UNPACKED_MASK;
}

/* (14 c + 7 (h + v) + 4 d) / 32, rounded (notaz's do_14_7_7_4) */
static inline u32 blend_14_7_7_4(u32 c, u32 h, u32 v, u32 d)
{
  u32 s = (c << 1) + h + v;
  return (((s << 3) - s + (d << 2) + (UNPACKED_ONES << 4)) >> 5) & UNPACKED_MASK;
}

/* The word holding unpacked pixels lo (the lower address) and hi. */
static inline u32 pack_pair(u32 lo, u32 hi)
{
  return (((lo | (lo >> 16)) << 16) >> 16) | ((hi | (hi << 16)) & 0xFFFF0000u);
}

/* (a + b) / 2 for each color of two 5:6:5 pixels a word, rounded down */
static inline u32 average_down(u32 a, u32 b)
{
  return (a & b) + (((a ^ b) >> 1) & 0x7BEF7BEFu);
}

/* The same, rounded up: libretro's gpsp_frame_mixing. */
static inline u32 average_up(u32 a, u32 b)
{
  return (a | b) - (((a ^ b) >> 1) & 0x7BEF7BEFu);
}

/* Stores as one STM (one burst): the screen buffers aren't in the data
 * cache, and GCC would write the words one at a time. */
static inline void store_2_words(u32 *d, u32 a, u32 b)
{
#if defined(__arm__)
  register u32 r4 __asm__("r4") = a;
  register u32 r5 __asm__("r5") = b;
  __asm__ volatile ("stmia %0, {%1, %2}" : : "r" (d), "r" (r4), "r" (r5) : "memory");
#else
  d[0] = a;
  d[1] = b;
#endif
}

static inline void store_3_words(u32 *d, u32 a, u32 b, u32 c)
{
#if defined(__arm__)
  register u32 r4 __asm__("r4") = a;
  register u32 r5 __asm__("r5") = b;
  register u32 r6 __asm__("r6") = c;
  __asm__ volatile ("stmia %0, {%1, %2, %3}" : : "r" (d), "r" (r4), "r" (r5), "r" (r6) : "memory");
#else
  d[0] = a;
  d[1] = b;
  d[2] = c;
#endif
}

/* Source lines with interframe blending done, for the lines being scaled. */
static u16 mixed_lines[4 * SRC_W] __attribute__((aligned(32)));

/* Interframe blending of n lines from src (SRC_W a line) with the previous
 * frame's (prev) into mixed_lines. */
static const u16 *mix_lines(const u16 *src, const u16 *prev, u32 n)
{
  const u32 *s = (const u32 *)src, *p = (const u32 *)prev;
  u32 *d = (u32 *)mixed_lines;
  u32 i;

  for (i = 0; i < n * SRC_W / 2; i++)
    d[i] = average_up(s[i], p[i]);
  return mixed_lines;
}

/* ---- scaled/blended 3:2 ---- */

/* The 320x213 picture is at landscape lines 12-224, and 225-227 are black:
 * a group of 4 picture lines then starts at a multiple of 4 positions in
 * each LCD line, so a column's 4 pixels are two aligned words. (gpSP had it
 * one line lower; the text bands are lines 0-11 and 228-239.) */
#define ASPECT_TOP 12

/* A source pixel of a word of two (the lower address in the low half),
 * unpacked. */
static inline u32 unpack_lo(u32 w)
{
  u32 t = w << 16;
  return (t | (t >> 16)) & UNPACKED_MASK;
}

static inline u32 unpack_hi(u32 w)
{
  u32 t = w >> 16;
  return (t | (t << 16)) & UNPACKED_MASK;
}

/* A 3x3 block of source pixels (columns a, b, c; rows 0-2, unpacked) -> its
 * 4 picture columns of 4 pixels, each column's bottom two pixels, then its
 * top two, at *d and the next 3 LCD lines. */
static inline __attribute__((always_inline)) void aspect_block(u32 **dp, u32 a0, u32 a1, u32 a2,
                                                                u32 b0, u32 b1, u32 b2,
                                                                u32 c0, u32 c1, u32 c2)
{
  u32 *d = *dp;
  store_2_words(d, pack_pair(a2, blend_3_5(a2, a1)), pack_pair(blend_3_5(a0, a1), a0));
  d += DST_W / 2;
  store_2_words(d, pack_pair(blend_3_5(a2, b2), blend_14_7_7_4(b1, b2, a1, a2)),
                pack_pair(blend_14_7_7_4(b1, b0, a1, a0), blend_3_5(a0, b0)));
  d += DST_W / 2;
  store_2_words(d, pack_pair(blend_3_5(c2, b2), blend_14_7_7_4(b1, b2, c1, c2)),
                pack_pair(blend_14_7_7_4(b1, b0, c1, c0), blend_3_5(c0, b0)));
  d += DST_W / 2;
  store_2_words(d, pack_pair(c2, blend_3_5(c2, c1)), pack_pair(blend_3_5(c0, c1), c0));
  d += DST_W / 2;
  *dp = d;
}

/* Source lines s, s + SRC_W, s + 2 SRC_W (with interframe blending: averaged
 * with the previous frame's, p) -> 4 picture lines, whose bottom one is at
 * position pos of each LCD line. Six source columns at a time: three words
 * a line, two 3x3 blocks. */
static inline __attribute__((always_inline)) void aspect_group(u16 *dst, const u16 *s, const u16 *p,
                                                               u32 pos, int mix)
{
  const u32 *r = (const u32 *)s, *q = (const u32 *)p;
  u32 *d = (u32 *)(dst + pos);
  u32 x;

#define ROW(n) (SRC_W / 2 * (n))
  for (x = 0; x < GBA_SCREEN_WIDTH; x += 6, r += 3, q += 3)
  {
    u32 w00 = r[0], w01 = r[1], w02 = r[2];
    u32 w10 = r[ROW(1)], w11 = r[ROW(1) + 1], w12 = r[ROW(1) + 2];
    u32 w20 = r[ROW(2)], w21 = r[ROW(2) + 1], w22 = r[ROW(2) + 2];
    if (mix)
    {
      w00 = average_up(w00, q[0]);
      w01 = average_up(w01, q[1]);
      w02 = average_up(w02, q[2]);
      w10 = average_up(w10, q[ROW(1)]);
      w11 = average_up(w11, q[ROW(1) + 1]);
      w12 = average_up(w12, q[ROW(1) + 2]);
      w20 = average_up(w20, q[ROW(2)]);
      w21 = average_up(w21, q[ROW(2) + 1]);
      w22 = average_up(w22, q[ROW(2) + 2]);
    }
    aspect_block(&d, unpack_lo(w00), unpack_lo(w10), unpack_lo(w20),
                 unpack_hi(w00), unpack_hi(w10), unpack_hi(w20),
                 unpack_lo(w01), unpack_lo(w11), unpack_lo(w21));
    aspect_block(&d, unpack_hi(w01), unpack_hi(w11), unpack_hi(w21),
                 unpack_lo(w02), unpack_lo(w12), unpack_lo(w22),
                 unpack_hi(w02), unpack_hi(w12), unpack_hi(w22));
  }
#undef ROW
}

/* The last source line (blended across only) at landscape line 224, and
 * black at 225-227: positions 12-15 of each LCD line. */
static void aspect_last_line(u16 *dst, const u16 *s)
{
  u32 *d = (u32 *)(dst + DST_W - 1 - (ASPECT_TOP + 215));
  u32 x;

  for (x = 0; x < GBA_SCREEN_WIDTH; x += 3, s += 3)
  {
    u32 a = unpack(s[0]), b = unpack(s[1]), c = unpack(s[2]);
    store_2_words(d, 0, pack_pair(0, a));
    d += DST_W / 2;
    store_2_words(d, 0, pack_pair(0, blend_3_5(a, b)));
    d += DST_W / 2;
    store_2_words(d, 0, pack_pair(0, blend_3_5(c, b)));
    d += DST_W / 2;
    store_2_words(d, 0, pack_pair(0, c));
    d += DST_W / 2;
  }
}

static void aspect_groups_mixed(u16 *dst, const u16 *src, const u16 *prev)
{
  u32 group;
  for (group = 0; group < GBA_SCREEN_HEIGHT / 3; group++)
    aspect_group(dst, src + group * 3 * SRC_W, prev + group * 3 * SRC_W,
                 DST_W - 1 - (ASPECT_TOP + group * 4 + 3), 1);
}

static void aspect_groups(u16 *dst, const u16 *src)
{
  u32 group;
  for (group = 0; group < GBA_SCREEN_HEIGHT / 3; group++)
    aspect_group(dst, src + group * 3 * SRC_W, src, DST_W - 1 - (ASPECT_TOP + group * 4 + 3), 0);
}

/* The scaled/blended 3:2 picture of src (240x160, GBA_SCREEN_PITCH pixels a
 * line), turned into dst at landscape lines 12-224 (225-227 black). With
 * prev (the previous frame, only read), frames are blended with it, in the
 * same pass: the source is read once and nothing but the picture is
 * written. */
void nspire_blend_turn_aspect(u16 *dst, const u16 *src, const u16 *prev)
{
  const u16 *last = src + (GBA_SCREEN_HEIGHT - 1) * SRC_W;

  if (prev)
  {
    aspect_groups_mixed(dst, src, prev);
    last = mix_lines(last, prev + (GBA_SCREEN_HEIGHT - 1) * SRC_W, 1);
  }
  else
    aspect_groups(dst, src);
  aspect_last_line(dst, last);
}

/* ---- fullscreen/blended ---- */

/* Source lines a1, b1, a2, b2 (s, s + SRC_W, ...) -> 6 picture lines, the
 * bottom one at position pos of each LCD line: 3 aligned words a column. */
static void full_group(u16 *dst, const u16 *s, u32 pos)
{
  u32 *d = (u32 *)(dst + pos);
  u32 x;

  for (x = 0; x < GBA_SCREEN_WIDTH; x += 3, s += 3)
  {
    /* Each register holds two lines' pixels of one column: a1 | b1 << 16
     * and a2 | b2 << 16. */
    u32 p1 = s[0] | ((u32)s[SRC_W] << 16), p2 = s[2 * SRC_W] | ((u32)s[3 * SRC_W] << 16);
    u32 q1 = s[1] | ((u32)s[SRC_W + 1] << 16), q2 = s[2 * SRC_W + 1] | ((u32)s[3 * SRC_W + 1] << 16);
    u32 r1 = s[2] | ((u32)s[SRC_W + 2] << 16), r2 = s[2 * SRC_W + 2] | ((u32)s[3 * SRC_W + 2] << 16);
    u32 h1 = average_down(q1, r1), h2 = average_down(q2, r2);

#define FULL_COLUMN(c1, c2)                                                   \
    {                                                                         \
      u32 m1 = average_down(c1, c1 >> 16) & 0xFFFF;                           \
      u32 m2 = average_down(c2, c2 >> 16) & 0xFFFF;                           \
      /* from the bottom: b2, (a2+b2)/2, a2, b1, (a1+b1)/2, a1 */             \
      store_3_words(d, (c2 >> 16) | (m2 << 16), (c2 & 0xFFFF) | (c1 & 0xFFFF0000u), \
                    m1 | (c1 << 16));                                         \
      d += DST_W / 2;                                                         \
    }
    FULL_COLUMN(p1, p2)
    FULL_COLUMN(q1, q2)
    FULL_COLUMN(h1, h2)
    FULL_COLUMN(r1, r2)
#undef FULL_COLUMN
  }
}

/* The fullscreen/blended picture of src, turned into all of dst. prev as in
 * nspire_blend_turn_aspect. */
void nspire_blend_turn_full(u16 *dst, const u16 *src, const u16 *prev)
{
  u32 group;

  for (group = 0; group < GBA_SCREEN_HEIGHT / 4; group++)
  {
    const u16 *s = src + group * 4 * SRC_W;
    if (prev)
      s = mix_lines(s, prev + group * 4 * SRC_W, 4);
    full_group(dst, s, DST_W - 1 - (group * 6 + 5));
  }
}
