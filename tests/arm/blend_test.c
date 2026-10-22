/*
 * The one-pass blended pictures (nspire_blend.c) against the four-pass path
 * gpSP had (frame mixing, the copy into the old 5:5:5 format, notaz's ARM
 * scalers in nspire/upscale_aspect.s, nspire_turn_rect_555), both built for
 * the calculator's CPU and run under qemu-arm (tests/arm/run.sh).
 *
 * Every picture pixel must have the same red and blue as before, and green
 * within 3 of 63: the new path keeps green's sixth bit, the old one dropped
 * it (and fullscreen's blended corners averaged twice at 5 bits). Every
 * pixel must also match, exactly, a plain per-pixel version of the math
 * written from nspire_blend.c's description. The 3:2 picture is one line
 * higher now (lines 12-224, with 225-227 black); nothing else of the screen
 * buffer may be written.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;

void upscale_aspect(u16 *dst, u16 *src);
void upscale_aspect_fast(u16 *dst, u16 *src);
void nspire_turn_rect_555(u16 *dst, const u16 *src, u32 src_pitch, u32 x0, u32 y0, u32 w, u32 h);
void nspire_blend_turn_aspect(u16 *dst, const u16 *src, const u16 *prev);
void nspire_blend_turn_full(u16 *dst, const u16 *src, const u16 *prev);
/* blend_ref.c: nspire_blend.c before the fused 3:2 picture (prev got each
 * frame then). */
void ref_blend_turn_aspect(u16 *dst, const u16 *src, u16 *prev);
void ref_blend_turn_full(u16 *dst, const u16 *src, u16 *prev);

#define W 240
#define H 160
#define SENTINEL 0x5A5A

static u16 frame[W * H] __attribute__((aligned(32)));
static u16 old_mix_prev[W * H], old_mixed[W * H];
static u16 new_mix_prev[W * H] __attribute__((aligned(32)));
static u16 ref_mix_prev[W * H] __attribute__((aligned(32)));
static u16 prev_copy[W * H];
static u16 ref_screen[240 * 320] __attribute__((aligned(32)));
static u16 upscale_src[320 * H] __attribute__((aligned(32)));
static u16 landscape[320 * 240] __attribute__((aligned(32)));
static u16 old_screen[240 * 320] __attribute__((aligned(32)));
static u16 new_screen[240 * 320] __attribute__((aligned(32)));

static int failures, checks, worst_green;

static void check(int ok, const char *what)
{
  checks++;
  if (!ok)
  {
    failures++;
    printf("FAIL  %s\n", what);
  }
}

/* ---- The old path, as nspire_video_present.c had it ---- */

static void old_mix(void)
{
  u32 i;
  for (i = 0; i < W * H; i++)
  {
    u16 c = frame[i], p = old_mix_prev[i];
    old_mix_prev[i] = c;
    old_mixed[i] = (u16)((c + p + ((c ^ p) & 0x821u)) >> 1);
  }
}

static void old_repack(const u16 *src)
{
  u32 x, y;
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
    {
      u32 p = src[y * W + x];
      upscale_src[y * 320 + x] = (u16)(((p & 0x1Fu) << 10) | (((p & 0x07C0u) >> 1) & 0x03E0u) | ((p >> 11) & 0x1Fu));
    }
}

static void old_aspect(int mix)
{
  if (mix)
    old_mix();
  old_repack(mix ? old_mixed : frame);
  upscale_aspect(landscape, upscale_src);
  nspire_turn_rect_555(old_screen, landscape + 13 * 320, 320, 0, 13, 320, 213);
}

static void old_full(int mix)
{
  if (mix)
    old_mix();
  old_repack(mix ? old_mixed : frame);
  upscale_aspect_fast(landscape, upscale_src);
  nspire_turn_rect_555(old_screen, landscape, 320, 0, 0, 320, 240);
}

/* ---- Comparing ---- */

static u16 at(const u16 *screen, u32 x, u32 y)
{
  return screen[x * 240 + 239 - y];
}

/* The old picture's line y_old against the new one's y_new, for h lines. */
static int same_picture(u32 y_old, u32 y_new, u32 h, const char *name)
{
  u32 x, y;
  int bad = 0;

  for (y = 0; y < h; y++)
    for (x = 0; x < 320; x++)
    {
      u32 o = at(old_screen, x, y_old + y), n = at(new_screen, x, y_new + y);
      int dg = (int)((n >> 5) & 63) - (int)((o >> 5) & 63);
      if (dg < 0)
        dg = -dg;
      if (dg > worst_green)
        worst_green = dg;
      if ((o & 0xF81Fu) != (n & 0xF81Fu) || dg > 3)
      {
        if (!bad)
          printf("  %s: (%u, %u) was %04x, now %04x\n", name, (unsigned)x, (unsigned)(y_new + y),
                 (unsigned)o, (unsigned)n);
        bad = 1;
      }
    }
  return !bad;
}

/* ---- The math, a pixel at a time ---- */

static const u16 *ref_src;   /* the frame the pictures are made of (mixed) */

static u32 channel(u32 p, int c)
{
  return c == 0 ? p >> 11 : c == 1 ? (p >> 5) & 63 : p & 31;
}

static u32 from_channels(const u32 *v)
{
  return (v[0] << 11) | (v[1] << 5) | v[2];
}

static u32 src_px(u32 x, u32 y)
{
  return ref_src[y * W + x];
}

/* Along one direction, picture position i of a group of 4 from source 3k..:
 * the weights (out of 8) of the source positions 3k, 3k+1, 3k+2. */
static const u32 weights_3_2[4][3] = { { 8, 0, 0 }, { 3, 5, 0 }, { 0, 5, 3 }, { 0, 0, 8 } };

static u32 ref_aspect(u32 x, u32 y)
{
  u32 gx = x / 4 * 3, gy = y / 4 * 3, px = x % 4, py = y % 4, v[3];
  int c;

  if (y == 212)
    py = 0;   /* the last source line, blended across only */
  for (c = 0; c < 3; c++)
  {
    u32 i, j, sum = 0;
    if ((px == 1 || px == 2) && (py == 1 || py == 2))
    {
      /* 14:7:7:4: the middle pixel, its neighbours toward the corner, the corner */
      u32 ex = px == 1 ? gx : gx + 2, ey = py == 1 ? gy : gy + 2;
      sum = 14 * channel(src_px(gx + 1, gy + 1), c) + 7 * channel(src_px(gx + 1, ey), c) +
            7 * channel(src_px(ex, gy + 1), c) + 4 * channel(src_px(ex, ey), c);
      v[c] = (sum + 16) >> 5;
      continue;
    }
    for (j = 0; j < 3; j++)
      for (i = 0; i < 3; i++)
        if (weights_3_2[py][j] && weights_3_2[px][i])
          sum += weights_3_2[py][j] * weights_3_2[px][i] * channel(src_px(gx + i, gy + j), c);
    v[c] = (sum + 32) >> 6;
  }
  return from_channels(v);
}

static u32 average_channels(u32 a, u32 b)
{
  u32 v[3];
  int c;
  for (c = 0; c < 3; c++)
    v[c] = (channel(a, c) + channel(b, c)) >> 1;
  return from_channels(v);
}

/* Across: a b (b+c)/2 c; down: a (a+b)/2 b. */
static u32 ref_full_line(u32 x, u32 sy)
{
  u32 gx = x / 4 * 3;
  switch (x % 4)
  {
  case 0: return src_px(gx, sy);
  case 1: return src_px(gx + 1, sy);
  case 2: return average_channels(src_px(gx + 1, sy), src_px(gx + 2, sy));
  default: return src_px(gx + 2, sy);
  }
}

static u32 ref_full(u32 x, u32 y)
{
  u32 sy = y / 3 * 2;
  switch (y % 3)
  {
  case 0: return ref_full_line(x, sy);
  case 1: return average_channels(ref_full_line(x, sy + 1), ref_full_line(x, sy));
  default: return ref_full_line(x, sy + 1);
  }
}

static int matches_math(u32 (*ref)(u32, u32), u32 top, u32 h, const char *name)
{
  u32 x, y;
  for (y = 0; y < h; y++)
    for (x = 0; x < 320; x++)
    {
      u32 want = ref(x, y), got = at(new_screen, x, top + y);
      if (want != got)
      {
        printf("  %s: (%u, %u) should be %04x, is %04x\n", name, (unsigned)x, (unsigned)(top + y),
               (unsigned)want, (unsigned)got);
        return 0;
      }
    }
  return 1;
}

static int lines_are(const u16 *screen, u32 y0, u32 y1, u16 value)
{
  u32 x, y;
  for (y = y0; y < y1; y++)
    for (x = 0; x < 320; x++)
      if (at(screen, x, y) != value)
        return 0;
  return 1;
}

/* ---- Frames ---- */

static u32 seed = 12345;

static u32 next_random(void)
{
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

static void make_frame(int kind)
{
  static const u16 extremes[] = { 0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x0821, 0xF7DE };
  u32 x, y;

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
    {
      u16 p;
      switch (kind)
      {
      case 0: p = (u16)next_random(); break;
      case 1: p = 0xFFFF; break;
      case 2: p = ((x ^ y) & 1) ? 0xFFFF : 0x0000; break;
      case 3: p = (u16)(((x % 32) << 11) | ((y % 64) << 5) | ((x + y) % 32)); break;
      default: p = extremes[next_random() % (sizeof(extremes) / sizeof(extremes[0]))]; break;
      }
      frame[y * W + x] = p;
    }
}

static const char *const kind_names[] = { "noise", "white", "checkerboard", "gradients", "extreme colors" };
#define KINDS 5

int main(void)
{
  char name[128];
  int kind, mix, n;

  for (kind = 0; kind < KINDS; kind++)
  {
    make_frame(kind);

    old_aspect(0);
    for (n = 0; n < 240 * 320; n++)
      new_screen[n] = SENTINEL;
    nspire_blend_turn_aspect(new_screen, frame, NULL);
    for (n = 0; n < 240 * 320; n++)
      ref_screen[n] = SENTINEL;
    ref_blend_turn_aspect(ref_screen, frame, NULL);
    snprintf(name, sizeof(name), "3:2, %s: as before, exactly", kind_names[kind]);
    check(!memcmp(new_screen, ref_screen, sizeof(new_screen)), name);
    snprintf(name, sizeof(name), "3:2, %s", kind_names[kind]);
    check(same_picture(13, 12, 213, name), name);
    ref_src = frame;
    snprintf(name, sizeof(name), "3:2, %s: the math", kind_names[kind]);
    check(matches_math(ref_aspect, 12, 213, name), name);
    snprintf(name, sizeof(name), "3:2, %s: lines 225-227 black", kind_names[kind]);
    check(lines_are(new_screen, 225, 228, 0), name);
    snprintf(name, sizeof(name), "3:2, %s: the text bands untouched", kind_names[kind]);
    check(lines_are(new_screen, 0, 12, SENTINEL) && lines_are(new_screen, 228, 240, SENTINEL), name);

    old_full(0);
    nspire_blend_turn_full(new_screen, frame, NULL);
    snprintf(name, sizeof(name), "fullscreen, %s", kind_names[kind]);
    check(same_picture(0, 0, 240, name), name);
    snprintf(name, sizeof(name), "fullscreen, %s: the math", kind_names[kind]);
    check(matches_math(ref_full, 0, 240, name), name);
  }

  /* Interframe blending: a few frames in a row, each path with its own
   * previous frame (all start from 0xFFFF). The new path only reads it:
   * the caller keeps the frame drawn before (nspire_video_present.c draws
   * into two buffers in turn), here by copying. */
  for (mix = 0; mix < 2; mix++)
  {
    memset(old_mix_prev, 0xFF, sizeof(old_mix_prev));
    memset(new_mix_prev, 0xFF, sizeof(new_mix_prev));
    memset(ref_mix_prev, 0xFF, sizeof(ref_mix_prev));
    for (n = 0; n < 4; n++)
    {
      make_frame(n == 2 ? 2 : n == 3 ? 4 : 0);
      memcpy(prev_copy, new_mix_prev, sizeof(prev_copy));
      memset(new_screen, 0x5A, sizeof(new_screen));
      memset(ref_screen, 0x5A, sizeof(ref_screen));
      if (mix == 0)
      {
        old_aspect(1);
        nspire_blend_turn_aspect(new_screen, frame, new_mix_prev);
        ref_blend_turn_aspect(ref_screen, frame, ref_mix_prev);
        snprintf(name, sizeof(name), "3:2 with interframe blending, frame %d: as before, exactly", n + 1);
        check(!memcmp(new_screen, ref_screen, sizeof(new_screen)), name);
        snprintf(name, sizeof(name), "3:2 with interframe blending, frame %d", n + 1);
        check(same_picture(13, 12, 213, name), name);
        ref_src = old_mixed;
        snprintf(name, sizeof(name), "3:2 with interframe blending, frame %d: the math", n + 1);
        check(matches_math(ref_aspect, 12, 213, name), name);
      }
      else
      {
        old_full(1);
        nspire_blend_turn_full(new_screen, frame, new_mix_prev);
        ref_blend_turn_full(ref_screen, frame, ref_mix_prev);
        snprintf(name, sizeof(name), "fullscreen with interframe blending, frame %d: as before, exactly", n + 1);
        check(!memcmp(new_screen, ref_screen, sizeof(new_screen)), name);
        snprintf(name, sizeof(name), "fullscreen with interframe blending, frame %d", n + 1);
        check(same_picture(0, 0, 240, name), name);
        ref_src = old_mixed;
        snprintf(name, sizeof(name), "fullscreen with interframe blending, frame %d: the math", n + 1);
        check(matches_math(ref_full, 0, 240, name), name);
      }
      snprintf(name, sizeof(name), "%s: the previous frame only read, frame %d", mix ? "fullscreen" : "3:2", n + 1);
      check(!memcmp(new_mix_prev, prev_copy, sizeof(prev_copy)), name);
      memcpy(new_mix_prev, frame, sizeof(frame));
    }
  }

  printf("%s: %d of %d checks passed; green differed by up to %d (of 63)\n",
         failures ? "FAIL" : "PASS", checks - failures, checks, worst_green);
  return failures != 0;
}
