/* The menus' drawing kit: see nspire_ui.h. */

#include "nspire_ui.h"
#include "nspire.h"
#include "nspire_gui_video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "nspire_ui_font.h"

u16 *ui_fb = nspire_landscape;
char ui_shown[32];

void ui_begin(void)
{
  ui_fb = nspire_landscape;
}

void ui_present(void)
{
  flip_screen_ui();
}

/* ---- Pixels ---- */

/* fg over bg, alpha 0-16, all three channels at once: the pixels spread to
 * 0x07E0F81F leave room for the products between the fields. */
static inline u16 blend(u16 bg, u16 fg, u32 a)
{
  u32 b = (bg | ((u32)bg << 16)) & 0x07E0F81Fu;
  u32 f = (fg | ((u32)fg << 16)) & 0x07E0F81Fu;
  u32 r = ((f * a + b * (16 - a)) >> 4) & 0x07E0F81Fu;
  return (u16)(r | (r >> 16));
}

/* 4-bit glyph coverage -> alpha 0-16 */
static const u8 alpha16[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16 };

static int clip(int *x, int *y, int *w, int *h)
{
  if (*x < 0) { *w += *x; *x = 0; }
  if (*y < 0) { *h += *y; *y = 0; }
  if (*x + *w > UI_W) *w = UI_W - *x;
  if (*y + *h > UI_H) *h = UI_H - *y;
  return *w > 0 && *h > 0;
}

void ui_fill(u16 color)
{
  u32 c = color | ((u32)color << 16), *p = (u32 *)ui_fb;
  int n = UI_W * UI_H / 2;
  while (n-- > 0)
    *p++ = c;
}

void ui_rect(int x, int y, int w, int h, u16 color)
{
  int i, j;
  if (!clip(&x, &y, &w, &h))
    return;
  for (j = 0; j < h; j++)
  {
    u16 *row = ui_fb + (y + j) * UI_W + x;
    for (i = 0; i < w; i++)
      row[i] = color;
  }
}

void ui_rect_alpha(int x, int y, int w, int h, u16 color, int alpha)
{
  int i, j;
  if (alpha >= 16)
  {
    ui_rect(x, y, w, h, color);
    return;
  }
  if (alpha <= 0 || !clip(&x, &y, &w, &h))
    return;
  for (j = 0; j < h; j++)
  {
    u16 *row = ui_fb + (y + j) * UI_W + x;
    for (i = 0; i < w; i++)
      row[i] = blend(row[i], color, (u32)alpha);
  }
}

void ui_hline(int x, int y, int w, u16 color)
{
  ui_rect(x, y, w, 1, color);
}

/* ---- Rounded rectangles ---- */

#define MAX_RADIUS 8

/* How much of each pixel of a top-left corner of radius r the rounded
 * shape covers (0-16), from 4x4 points a pixel. */
static u8 corner[MAX_RADIUS + 1][MAX_RADIUS][MAX_RADIUS];
static int corners_made;

static void make_corners(void)
{
  int r, i, j, k, l;
  for (r = 1; r <= MAX_RADIUS; r++)
    for (j = 0; j < r; j++)
      for (i = 0; i < r; i++)
      {
        int n = 0, R = 8 * r;
        for (l = 0; l < 4; l++)
          for (k = 0; k < 4; k++)
          {
            int X = 8 * i + 2 * k + 1, Y = 8 * j + 2 * l + 1;
            n += (R - X) * (R - X) + (R - Y) * (R - Y) <= R * R;
          }
        corner[r][j][i] = (u8)n;
      }
  corners_made = 1;
}

/* Coverage (0-16) of pixel (i, j) of a w x h rounded rectangle. */
static int rr_cover(int i, int j, int w, int h, int r)
{
  int ci, cj;
  if (i < 0 || j < 0 || i >= w || j >= h)
    return 0;
  if (r <= 0)
    return 16;
  ci = i < r ? i : i >= w - r ? w - 1 - i : -1;
  cj = j < r ? j : j >= h - r ? h - 1 - j : -1;
  if (ci < 0 || cj < 0)
    return 16;
  return corner[r][cj][ci];
}

void ui_rrect_alpha(int x, int y, int w, int h, int r, u16 color, int alpha)
{
  int i, j;
  if (!corners_made)
    make_corners();
  if (r > MAX_RADIUS)
    r = MAX_RADIUS;
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  for (j = 0; j < h; j++)
  {
    int py = y + j;
    u16 *row;
    if (py < 0 || py >= UI_H)
      continue;
    row = ui_fb + py * UI_W;
    for (i = 0; i < w; i++)
    {
      int px = x + i, a;
      if (px < 0 || px >= UI_W)
        continue;
      if (j >= r && j < h - r && i == r && w - r > r)
      {
        /* the straight middle of a row */
        int end = w - r, k;
        for (k = i; k < end; k++)
          if (x + k >= 0 && x + k < UI_W)
            row[x + k] = alpha >= 16 ? color : blend(row[x + k], color, (u32)alpha);
        i = end - 1;
        continue;
      }
      a = rr_cover(i, j, w, h, r) * alpha / 16;
      if (a >= 16)
        row[px] = color;
      else if (a > 0)
        row[px] = blend(row[px], color, (u32)a);
    }
  }
}

void ui_rrect(int x, int y, int w, int h, int r, u16 color)
{
  ui_rrect_alpha(x, y, w, h, r, color, 16);
}

void ui_rframe(int x, int y, int w, int h, int r, u16 color)
{
  int i, j;
  if (!corners_made)
    make_corners();
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++)
    {
      int px = x + i, py = y + j, a;
      if (j > r && j < h - 1 - r && i > 0 && i < w - 1)
      {
        i = w - 2;   /* only the edges in the middle rows */
        continue;
      }
      if (px < 0 || py < 0 || px >= UI_W || py >= UI_H)
        continue;
      a = rr_cover(i, j, w, h, r) - rr_cover(i - 1, j - 1, w - 2, h - 2, r > 1 ? r - 1 : 0);
      if (a > 0)
        ui_fb[py * UI_W + px] = blend(ui_fb[py * UI_W + px], color, (u32)a);
    }
}

/* ---- Text ---- */

static int kern(const struct ui_font *f, int a, int b)
{
  u16 pair = (u16)((a << 8) | b);
  int lo = 0, hi = f->kern_count - 1;
  while (lo <= hi)
  {
    int mid = (lo + hi) / 2;
    if (f->kern[mid].pair == pair)
      return f->kern[mid].adjust;
    if (f->kern[mid].pair < pair)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

static int code_of(unsigned char c)
{
  if ((c >= 32 && c < 127) || (c >= 1 && c <= 8))
    return c;
  return c == '\t' || c == '\n' ? ' ' : '?';
}

static void draw_glyph(const struct ui_font *f, const struct ui_glyph *g, int x, int y, u16 color)
{
  int i, j, bpr = (g->w + 1) / 2;
  const u8 *bits = f->bits + g->bits;
  for (j = 0; j < g->h; j++, bits += bpr)
  {
    int py = y + j;
    u16 *row;
    if (py < 0 || py >= UI_H)
      continue;
    row = ui_fb + py * UI_W;
    for (i = 0; i < g->w; i++)
    {
      int px = x + i, a = (bits[i >> 1] >> ((i & 1) * 4)) & 15;
      if (!a || px < 0 || px >= UI_W)
        continue;
      row[px] = a == 15 ? color : blend(row[px], color, alpha16[a]);
    }
  }
}

/* Lays s out from x (in 1/64 pixel); draws it when draw is set. Returns the
 * pen's end, in 1/64 pixel. */
static int layout(int x64, int y, const char *s, const struct ui_font *f, u16 color, int draw)
{
  int pen = x64, prev = 0;
  for (; *s; s++)
  {
    int c = code_of((unsigned char)*s);
    if (prev >= 32 && c >= 32)
      pen += kern(f, prev, c);
    if (draw)
    {
      int q = (pen * UI_FONT_PHASES + 32) >> 6;
      const struct ui_glyph *g = &f->glyphs[c * UI_FONT_PHASES + q % UI_FONT_PHASES];
      if (g->w)
        draw_glyph(f, g, q / UI_FONT_PHASES + g->x, y + g->y, color);
    }
    pen += f->advance[c];
    prev = c;
  }
  return pen;
}

int ui_text(int x, int y, const char *s, const struct ui_font *f, u16 color)
{
  return (layout(x * 64, y, s, f, color, 1) + 32) >> 6;
}

int ui_text_width(const char *s, const struct ui_font *f)
{
  return (layout(0, 0, s, f, 0, 0) + 32) >> 6;
}

int ui_text_right(int right, int y, const char *s, const struct ui_font *f, u16 color)
{
  return ui_text(right - ui_text_width(s, f), y, s, f, color);
}

int ui_text_center(int center, int y, const char *s, const struct ui_font *f, u16 color)
{
  return ui_text(center - ui_text_width(s, f) / 2, y, s, f, color);
}

char *ui_fit(const char *s, const struct ui_font *f, int max_w, char *out, size_t size)
{
  size_t len;
  snprintf(out, size, "%s", s);
  if (ui_text_width(out, f) <= max_w)
    return out;
  len = strlen(out);
  while (len > 0)
  {
    len--;
    while (len > 0 && out[len - 1] == ' ')
      len--;
    if (len + 2 > size)
      continue;
    out[len] = UI_ELLIPSIS[0];
    out[len + 1] = 0;
    if (ui_text_width(out, f) <= max_w)
      return out;
  }
  out[0] = 0;
  return out;
}

/* A line of a wrapped text: from x, or centered on it. */
static void wrap_line(int x, int y, const char *s, const struct ui_font *f, u16 color, int center)
{
  if (center)
    ui_text_center(x, y, s, f, color);
  else
    ui_text(x, y, s, f, color);
}

static int wrap(int x, int y, int line_h, int max_w, int max_lines, const char *s, const struct ui_font *f,
                u16 color, int center)
{
  char line[330], word[80], trial[330];
  int lines = 0;

  line[0] = 0;
  while (lines < max_lines)
  {
    size_t n = 0;
    while (*s == ' ' || *s == '\n')
      s++;
    while (*s && *s != ' ' && *s != '\n' && n + 1 < sizeof(word))
      word[n++] = *s++;
    word[n] = 0;
    if (!n)
      break;
    snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
    if (!line[0] || ui_text_width(trial, f) <= max_w)
    {
      snprintf(line, sizeof(line), "%s", trial);
      continue;
    }
    if (lines == max_lines - 1)
    {
      /* the last line that fits: what's left, cut short */
      char fitted[330];
      snprintf(trial, sizeof(trial), "%s %s%s", line, word, s);
#if defined(NSPIRE_HOST)
      /* For the PC tests: texts must fit their place. */
      fprintf(stderr, "gpsp-host: text cut short: %s\n", trial);
#endif
      wrap_line(x, y + lines * line_h, ui_fit(trial, f, max_w, fitted, sizeof(fitted)), f, color, center);
      return lines + 1;
    }
    wrap_line(x, y + lines * line_h, line, f, color, center);
    lines++;
    snprintf(line, sizeof(line), "%s", word);
  }
  if (line[0] && lines < max_lines)
    wrap_line(x, y + lines++ * line_h, line, f, color, center);
  return lines;
}

int ui_wrap(int x, int y, int line_h, int max_w, int max_lines, const char *s, const struct ui_font *f,
            u16 color)
{
  return wrap(x, y, line_h, max_w, max_lines, s, f, color, 0);
}

int ui_wrap_center(int center, int y, int line_h, int max_w, int max_lines, const char *s,
                   const struct ui_font *f, u16 color)
{
  return wrap(center, y, line_h, max_w, max_lines, s, f, color, 1);
}

/* ---- Small parts ---- */

int ui_keycap(int x, int y, const char *key)
{
  int w = ui_text_width(key, &ui_font_caps) + 8;
  ui_rframe(x, y, w, 13, 3, UI_KEYLINE);
  ui_text(x + 4, y + 9, key, &ui_font_caps, UI_TEXT2);
  return x + w;
}

void ui_footer(const char *const *pairs, int count)
{
  int widths[8], i, sum = 0;
  int x64 = 10 * 64, gap64;
  if (count > 8)
    count = 8;
  ui_hline(0, 218, UI_W, UI_LINE);
  for (i = 0; i < count; i++)
  {
    widths[i] = ui_text_width(pairs[2 * i], &ui_font_caps) + 8 + 4 + ui_text_width(pairs[2 * i + 1], &ui_font_small);
    sum += widths[i];
  }
  gap64 = count > 1 ? ((UI_W - 20 - sum) * 64) / (count - 1) : 0;
  for (i = 0; i < count; i++)
  {
    int x = (x64 + 32) >> 6;
    int end = ui_keycap(x, 222, pairs[2 * i]);
    ui_text(end + 4, 231, pairs[2 * i + 1], &ui_font_small, UI_TEXT2);
    x64 += widths[i] * 64 + gap64;
  }
}

static const u8 chevron[9][5] = {
  { 0, 0, 0, 1, 0 }, { 0, 0, 3, 16, 4 }, { 0, 1, 14, 11, 0 }, { 0, 10, 15, 1, 0 }, { 3, 16, 5, 0, 0 },
  { 0, 13, 13, 0, 0 }, { 0, 2, 15, 8, 0 }, { 0, 0, 6, 16, 4 }, { 0, 0, 0, 5, 0 },
};

static const u8 arrow_up[9][9] = {
  { 0, 0, 0, 0, 3, 0, 0, 0, 0 }, { 0, 0, 0, 6, 16, 10, 0, 0, 0 }, { 0, 0, 5, 16, 16, 16, 9, 0, 0 },
  { 0, 3, 15, 12, 16, 8, 16, 6, 0 }, { 0, 12, 10, 4, 16, 4, 6, 15, 1 }, { 0, 1, 0, 4, 16, 4, 0, 1, 0 },
  { 0, 0, 0, 4, 16, 4, 0, 0, 0 }, { 0, 0, 0, 4, 16, 4, 0, 0, 0 }, { 0, 0, 0, 1, 4, 1, 0, 0, 0 },
};

static void put(int x, int y, u16 color, int a)
{
  if (a <= 0 || x < 0 || y < 0 || x >= UI_W || y >= UI_H)
    return;
  ui_fb[y * UI_W + x] = a >= 16 ? color : blend(ui_fb[y * UI_W + x], color, (u32)a);
}

void ui_chevrons(int left, int right, int y, u16 color)
{
  int i, j;
  for (j = 0; j < 9; j++)
    for (i = 0; i < 5; i++)
    {
      put(left + i, y + j, color, chevron[j][i]);
      put(right - 1 - i, y + j, color, chevron[j][i]);
    }
}

void ui_arrow_up(int x, int y, u16 color)
{
  int i, j;
  for (j = 0; j < 9; j++)
    for (i = 0; i < 9; i++)
      put(x + i, y + j, color, arrow_up[j][i]);
}

void ui_switch(int x, int y, int on, int picked)
{
  ui_rrect(x, y, 24, 13, 6, on ? UI_ACCENT : picked ? UI_OFF_PICKED : UI_OFF);
  ui_rrect(on ? x + 13 : x + 2, y + 2, 9, 9, 4, UI_RGB(0xFF, 0xFF, 0xFF));
}

/* ---- Pictures ---- */

void ui_blit(int x, int y, const u16 *src, int w, int h, int pitch)
{
  int j, x0 = x, y0 = y, cw = w, ch = h;
  if (!clip(&x0, &y0, &cw, &ch))
    return;
  src += (y0 - y) * pitch + (x0 - x);
  for (j = 0; j < ch; j++)
    memcpy(ui_fb + (y0 + j) * UI_W + x0, src + j * pitch, (size_t)cw * 2);
}

void ui_blit_rounded(int x, int y, const u16 *src, int w, int h, int pitch, int r)
{
  int i, j;
  if (!corners_made)
    make_corners();
  for (j = 0; j < h; j++)
  {
    int py = y + j;
    if (py < 0 || py >= UI_H)
      continue;
    for (i = 0; i < w; i++)
    {
      int px = x + i, a;
      if (px < 0 || px >= UI_W)
        continue;
      a = rr_cover(i, j, w, h, r);
      if (a >= 16)
        ui_fb[py * UI_W + px] = src[j * pitch + i];
      else if (a > 0)
        ui_fb[py * UI_W + px] = blend(ui_fb[py * UI_W + px], src[j * pitch + i], (u32)a);
    }
  }
}

/* A 5:6:5 pixel spread over 32 bits (green on top), so the three fields
 * have room to be multiplied by up to 32 and summed at once. */
#define SPREAD_MASK 0x07E0F81Fu
#define SPREAD(p) (((u32)(p) | (u32)(p) << 16) & SPREAD_MASK)

/* The source pixels under output pixel i of n (out of m), and how much of
 * each (in 1/256 of the output pixel): at most 8. */
static int spans(int i, int n, int m, int *first, int *weight)
{
  int a = i * m * 256 / n, b = (i + 1) * m * 256 / n;   /* in 1/256 source pixel */
  int k = 0, s = a >> 8, total = b - a;
  *first = s;
  while (s * 256 < b && k < 8)
  {
    int lo = s * 256 > a ? s * 256 : a, hi = (s + 1) * 256 < b ? (s + 1) * 256 : b;
    weight[k++] = (hi - lo) * 256 / total;
    s++;
  }
  return k;
}

/* The same in 1/32, summing to 32, for spread pixels (SPREAD). */
static int spans32(int i, int n, int m, int *first, int *weight)
{
  int k = spans(i, n, m, first, weight), j, sum = 0, big = 0;
  for (j = 0; j < k; j++)
  {
    weight[j] = (weight[j] + 4) >> 3;
    sum += weight[j];
    if (weight[j] > weight[big])
      big = j;
  }
  weight[big] += 32 - sum;
  return k;
}

/* Across, then down: each source line's pixels across once (kept for the
 * output lines that share it), on spread pixels, a field's sum at most 31
 * times 32. The CPU has no divide: the spans are worked out once. */
void ui_scale(u16 *dst, int dw, int dh, const u16 *src, int sw, int sh, int pitch)
{
  static int xfirst[UI_W], xcount[UI_W], xweight[UI_W][8];
  static u32 across[8][UI_W];
  int across_of[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
  int x, y, i, j;

  if (dw > UI_W)
    return;
  for (x = 0; x < dw; x++)
    xcount[x] = spans32(x, dw, sw, &xfirst[x], xweight[x]);
  for (y = 0; y < dh; y++)
  {
    int fy, wy[8], ny = spans32(y, dh, sh, &fy, wy);
    u16 *out = dst + y * dw;
    for (j = 0; j < ny; j++)
    {
      int line = fy + j;
      u32 *a = across[line & 7];
      const u16 *in = src + line * pitch;
      if (across_of[line & 7] == line)
        continue;
      across_of[line & 7] = line;
      for (x = 0; x < dw; x++)
      {
        const u16 *p = in + xfirst[x];
        const int *w = xweight[x];
        u32 sum = 0;
        for (i = 0; i < xcount[x]; i++)
          sum += SPREAD(p[i]) * (u32)w[i];
        a[x] = (sum >> 5) & SPREAD_MASK;
      }
    }
    for (x = 0; x < dw; x++)
    {
      u32 sum = 0, v;
      for (j = 0; j < ny; j++)
        sum += across[(fy + j) & 7][x] * (u32)wy[j];
      v = (sum >> 5) & SPREAD_MASK;
      out[x] = (u16)(v | v >> 16);
    }
  }
}

/* ---- The paused game behind the menu ---- */

#define BD_W 80     /* made at a quarter of the screen, then smoothed up */
#define BD_H 60
#define BD_KEEP 82  /* of 256: how much light is left */

static u16 backdrop[UI_W * UI_H];
static int have_backdrop;

void ui_backdrop_clear(void)
{
  have_backdrop = 0;
}

void ui_backdrop_make(const u16 *frame, int pitch)
{
  static u16 picture[BD_W * 54];
  static u8 small[BD_H][BD_W][3];
  static u32 wide[BD_H][UI_W];
  int x, y, c;

  /* The picture as played: 320x213 from line 13, so 80x53 from line 3 at a
   * quarter. */
  ui_scale(picture, BD_W, 53, frame, 240, 160, pitch);
  /* the bars: the screen's near-black, darkened like the rest */
  for (y = 0; y < BD_H; y++)
    for (x = 0; x < BD_W; x++)
    {
      small[y][x][0] = 0x0E * BD_KEEP >> 8;
      small[y][x][1] = 0x0F * BD_KEEP >> 8;
      small[y][x][2] = 0x13 * BD_KEEP >> 8;
    }
  for (y = 0; y < 53; y++)
    for (x = 0; x < BD_W; x++)
    {
      u16 p = picture[y * BD_W + x];
      small[y + 3][x][0] = (u8)(((p >> 11) * 255 / 31) * BD_KEEP >> 8);
      small[y + 3][x][1] = (u8)((((p >> 5) & 63) * 255 / 63) * BD_KEEP >> 8);
      small[y + 3][x][2] = (u8)(((p & 31) * 255 / 31) * BD_KEEP >> 8);
    }
  /* one more soft pass (1-2-1 both ways) */
  for (c = 0; c < 3; c++)
  {
    static u8 tmp[BD_H][BD_W];
    for (y = 0; y < BD_H; y++)
      for (x = 0; x < BD_W; x++)
      {
        int l = small[y][x > 0 ? x - 1 : x][c], r = small[y][x < BD_W - 1 ? x + 1 : x][c];
        tmp[y][x] = (u8)((l + 2 * small[y][x][c] + r + 2) >> 2);
      }
    for (y = 0; y < BD_H; y++)
      for (x = 0; x < BD_W; x++)
      {
        int u = tmp[y > 0 ? y - 1 : y][x], d = tmp[y < BD_H - 1 ? y + 1 : y][x];
        small[y][x][c] = (u8)((u + 2 * tmp[y][x] + d + 2) >> 2);
      }
  }
  /* Bilinear back up to the screen, 4x each way: across, then down, on
   * spread pixels (SPREAD) with weights in 1/32, which the quarter steps
   * are exactly. Positions in 1/64 of a small pixel, from its center. */
  for (y = 0; y < BD_H; y++)
  {
    u32 row[BD_W];
    for (x = 0; x < BD_W; x++)
      row[x] = SPREAD(UI_RGB(small[y][x][0], small[y][x][1], small[y][x][2]));
    for (x = 0; x < UI_W; x++)
    {
      int fx = x * 16 - 24, x0, x1, w;
      if (fx < 0)
        fx = 0;
      x0 = fx >> 6;
      x1 = x0 + 1 < BD_W ? x0 + 1 : x0;
      w = (fx & 63) >> 1;
      wide[y][x] = ((row[x0] * (u32)(32 - w) + row[x1] * (u32)w) >> 5) & SPREAD_MASK;
    }
  }
  for (y = 0; y < UI_H; y++)
  {
    int fy = y * 16 - 24, y0, y1, w;
    const u32 *a, *b;
    u16 *out = backdrop + y * UI_W;
    if (fy < 0)
      fy = 0;
    y0 = fy >> 6;
    y1 = y0 + 1 < BD_H ? y0 + 1 : y0;
    w = (fy & 63) >> 1;
    a = wide[y0];
    b = wide[y1];
    for (x = 0; x < UI_W; x++)
    {
      u32 v = ((a[x] * (u32)(32 - w) + b[x] * (u32)w) >> 5) & SPREAD_MASK;
      out[x] = (u16)(v | v >> 16);
    }
  }
  have_backdrop = 1;
}

void ui_backdrop_draw(void)
{
  if (have_backdrop)
    memcpy(ui_fb, backdrop, sizeof(backdrop));
  else
    ui_fill(UI_BG);
}

/* ---- Game names ---- */

static int ends_with(const char *s, size_t len, const char *end)
{
  size_t n = strlen(end);
  return len >= n && !strncasecmp(s + len - n, end, n);
}

void ui_game_title(const char *file_name, char *title, size_t title_size, char *tags, size_t tags_size)
{
  static const char *const exts[] = { ".gba", ".gbz", ".zip", ".agb", ".bin" };
  static const char *const articles[] = { ", The", ", A", ", An" };
  char name[256], groups[8][64];
  const char *slash = strrchr(file_name, '/');
  size_t len, i;
  int ngroups = 0;
  char *dash;

  snprintf(name, sizeof(name), "%s", slash ? slash + 1 : file_name);
  len = strlen(name);
  if (ends_with(name, len, ".tns"))
    name[len -= 4] = 0;
  for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
    if (ends_with(name, len, exts[i]))
    {
      name[len -= 4] = 0;
      break;
    }
  for (i = 0; i < len; i++)
    if (name[i] == '_')
      name[i] = ' ';

  /* "(USA, Europe) (Rev 1)" and "[!]" at the end become tags */
  for (;;)
  {
    char close, open, *start;
    while (len && name[len - 1] == ' ')
      name[--len] = 0;
    if (!len || (name[len - 1] != ')' && name[len - 1] != ']'))
      break;
    close = name[len - 1];
    open = close == ')' ? '(' : '[';
    start = strrchr(name, open);
    if (!start || start == name)
      break;
    name[len - 1] = 0;
    if (ngroups < 8)
      snprintf(groups[ngroups++], sizeof(groups[0]), "%s", start + 1);
    *start = 0;
    len = (size_t)(start - name);
  }

  /* "Legend of Zelda, The - The Minish Cap" -> "The Legend of Zelda - ..." */
  for (i = 0; i < sizeof(articles) / sizeof(articles[0]); i++)
  {
    char *at = strstr(name, articles[i]);
    size_t an = strlen(articles[i]);
    if (at && (at[an] == 0 || !strncmp(at + an, " - ", 3)))
    {
      char moved[256];
      snprintf(moved, sizeof(moved), "%s %.*s%s", articles[i] + 2, (int)(at - name), name, at + an);
      snprintf(name, sizeof(name), "%s", moved);
      break;
    }
  }
  /* the first " - " separates a subtitle */
  dash = strstr(name, " - ");
  if (dash && dash != name)
  {
    char joined[256];
    snprintf(joined, sizeof(joined), "%.*s: %s", (int)(dash - name), name, dash + 3);
    snprintf(name, sizeof(name), "%s", joined);
  }
  snprintf(title, title_size, "%s", name[0] ? name : file_name);

  tags[0] = 0;
  while (ngroups > 0)
  {
    size_t used = strlen(tags);
    snprintf(tags + used, tags_size - used, "%s%s", used ? " " UI_DOT " " : "", groups[--ngroups]);
  }
}
