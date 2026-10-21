/* The menus' drawing kit: a 320x240 landscape frame in 5:6:5 (the LCD's own
 * format), turned onto the LCD by flip_screen_ui. Smooth text from pre-drawn
 * glyphs (nspire_ui_font.h), rounded rectangles with smooth corners, alpha
 * blending, pictures scaled down by area averaging.
 *
 * The look ("Clean"): near-black, white text, the picked line on a white
 * bar with dark text, one indigo accent for what is on. Colors are the
 * UI_* tokens below; sizes and places are in nspire_ui_screens.c. */
#ifndef NSPIRE_UI_H
#define NSPIRE_UI_H

#include "common.h"

#define UI_W 320
#define UI_H 240

#define UI_RGB(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Colors */
#define UI_BG      UI_RGB(0x0E, 0x0F, 0x13)   /* the screen */
#define UI_SURFACE UI_RGB(0x22, 0x25, 0x2D)   /* chips, empty pictures */
#define UI_LINE    UI_RGB(0x2B, 0x2E, 0x37)   /* dividers */
#define UI_KEYLINE UI_RGB(0x45, 0x49, 0x55)   /* key cap outlines */
#define UI_TEXT    UI_RGB(0xF1, 0xF2, 0xF5)   /* text, the picked line's bar */
#define UI_TEXT2   UI_RGB(0xA2, 0xA7, 0xB2)   /* values, secondary text */
#define UI_TEXT3   UI_RGB(0x6D, 0x72, 0x7D)   /* hints, labels */
#define UI_INK     UI_RGB(0x10, 0x12, 0x17)   /* text on the white bar */
#define UI_CHIP    UI_RGB(0xD5, 0xD8, 0xDF)   /* a number chip on the bar */
#define UI_ACCENT  UI_RGB(0x8B, 0x7C, 0xFF)   /* on, newest, the page tab */
#define UI_OFF     UI_RGB(0x3A, 0x3E, 0x48)   /* a switch that is off */
#define UI_OFF_PICKED UI_RGB(0xA9, 0xAD, 0xB7)
#define UI_WARN    UI_RGB(0xFF, 0xB4, 0x54)   /* errors and warnings */

/* Characters drawn from control codes */
#define UI_ELLIPSIS "\x01"
#define UI_DOT      "\x02"   /* middle dot, between facts */
#define UI_LEFT     "\x03"   /* single angle quotes */
#define UI_RIGHT    "\x04"
#define UI_ARROW_L  "\x05"
#define UI_ARROW_U  "\x06"
#define UI_ARROW_R  "\x07"
#define UI_ARROW_D  "\x08"

struct ui_glyph
{
  u32 bits;       /* first byte of its rows in the font's bits */
  s8 x, y;        /* top-left from the pen (y from the baseline) */
  u8 w, h;
};

struct ui_kern
{
  u16 pair;       /* left << 8 | right */
  s16 adjust;     /* 1/64 pixel */
};

struct ui_font
{
  const struct ui_glyph *glyphs;   /* [char][phase] */
  const u8 *bits;                  /* 4 bits a pixel, rows padded to bytes */
  const u16 *advance;              /* 1/64 pixel */
  const struct ui_kern *kern;
  u16 kern_count;
  u8 ascent, descent;
};

extern const struct ui_font ui_font_title;   /* 15 px semibold: screen titles */
extern const struct ui_font ui_font_body;    /* 12 px: lines */
extern const struct ui_font ui_font_bold;    /* 12 px semibold: the picked line */
extern const struct ui_font ui_font_small;   /* 11 px: values, help */
extern const struct ui_font ui_font_caps;    /* 9 px semibold: key caps */
extern const struct ui_font ui_font_num;     /* 10 px bold: number chips */

/* The frame being drawn (nspire_landscape). */
extern u16 *ui_fb;
/* What the frame on the screen shows, for the PC tests: "main:2" (the main
 * menu, its third line picked), "Graphics:0", "list:5", "folders:1"; ""
 * when it isn't one of these screens. */
extern char ui_shown[32];

void ui_begin(void);
/* Shows the frame (flip_screen_ui). */
void ui_present(void);

void ui_fill(u16 color);
void ui_rect(int x, int y, int w, int h, u16 color);
/* alpha: 0 (none) to 16 (all) */
void ui_rect_alpha(int x, int y, int w, int h, u16 color, int alpha);
void ui_rrect(int x, int y, int w, int h, int r, u16 color);
void ui_rrect_alpha(int x, int y, int w, int h, int r, u16 color, int alpha);
/* A 1-pixel rounded outline. */
void ui_rframe(int x, int y, int w, int h, int r, u16 color);
void ui_hline(int x, int y, int w, u16 color);

/* Text with its baseline at y; returns the x after it. */
int ui_text(int x, int y, const char *s, const struct ui_font *f, u16 color);
int ui_text_right(int right, int y, const char *s, const struct ui_font *f, u16 color);
int ui_text_center(int center, int y, const char *s, const struct ui_font *f, u16 color);
int ui_text_width(const char *s, const struct ui_font *f);
/* s cut with an ellipsis to fit max_w; returns out. */
char *ui_fit(const char *s, const struct ui_font *f, int max_w, char *out, size_t size);
/* Draws s in lines of at most max_w (breaking at spaces; '\n' counts as a
 * space), at most max_lines; returns how many lines it took. */
int ui_wrap(int x, int y, int line_h, int max_w, int max_lines, const char *s, const struct ui_font *f,
            u16 color);
/* The same, each line centered on center. */
int ui_wrap_center(int center, int y, int line_h, int max_w, int max_lines, const char *s,
                   const struct ui_font *f, u16 color);

/* A key cap ("enter") at x, top y, 13 pixels high; returns its right edge. */
int ui_keycap(int x, int y, const char *key);
/* Key hints along the bottom: pairs of key and what it does, spread out. */
void ui_footer(const char *const *pairs, int count);
/* < > around a value, chevrons 5x9 with their tops at y. */
void ui_chevrons(int left, int right, int y, u16 color);
/* A 24x13 switch. */
void ui_switch(int x, int y, int on, int picked);
/* A small arrow up (9x9). */
void ui_arrow_up(int x, int y, u16 color);

/* Pictures in 5:6:5. */
void ui_blit(int x, int y, const u16 *src, int w, int h, int pitch);
/* With smooth rounded corners over what is behind it. */
void ui_blit_rounded(int x, int y, const u16 *src, int w, int h, int pitch, int r);
/* Area-averaged scaling down (or the same size). */
void ui_scale(u16 *dst, int dw, int dh, const u16 *src, int sw, int sh, int pitch);

/* The paused game behind the main menu: the picture as it was played (3:2,
 * lines 13-226), blurred and darkened. Made once when the menu opens. */
void ui_backdrop_make(const u16 *frame, int pitch);
void ui_backdrop_clear(void);
void ui_backdrop_draw(void);

/* A game's file name made readable: "Legend of Zelda, The - The Minish Cap
 * (USA).zip.tns" -> title "The Legend of Zelda: The Minish Cap", tags
 * "USA". */
void ui_game_title(const char *file_name, char *title, size_t title_size, char *tags, size_t tags_size);

#endif
