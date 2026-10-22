/* The menus' screens: see nspire_ui_screens.h. Places are in pixels of the
 * 320x240 frame; text y is its baseline. */

#include "nspire_ui_screens.h"
#include "nspire_states.h"

#include <stdio.h>
#include <string.h>

/* ---- Shared parts ---- */

/* The picked line: a white bar. */
static void pill(int x, int y, int w, int h)
{
  ui_rrect(x, y, w, h, 6, UI_TEXT);
}

/* A picture with a hairline frame and rounded corners. */
static void framed_picture(int x, int y, const u16 *pic, int w, int h, int pitch, u16 frame, int alpha)
{
  ui_rrect_alpha(x - 1, y - 1, w + 2, h + 2, 5, frame, alpha);
  ui_blit_rounded(x, y, pic, w, h, pitch, 4);
}

/* An empty picture: the surface color, with a word in the middle. */
static void empty_picture(int x, int y, int w, int h, const char *what)
{
  ui_rrect(x, y, w, h, 4, UI_SURFACE);
  ui_text_center(x + w / 2, y + h / 2 + 4, what, &ui_font_small, UI_TEXT3);
}

static void status_line(const char *status)
{
  /* over the key hints: what just happened matters more */
  ui_rect(0, 219, UI_W, UI_H - 219, UI_BG);
  ui_text(10, 231, status, &ui_font_small, UI_TEXT);
}

/* ---- The game list ---- */

#define L_X 6
#define L_W 172
#define L_Y 28
#define L_RH 23
#define P_X 188      /* the details panel */
#define P_Y 30
#define P_W 122

static void number_chip(int x, int y, int n, int picked)
{
  char s[12];
  snprintf(s, sizeof(s), "%d", n);
  ui_rrect(x, y, 14, 13, 3, picked ? UI_CHIP : UI_SURFACE);
  ui_text_center(x + 7, y + 10, s, &ui_font_num, picked ? UI_INK : UI_TEXT2);
}

static void folder_glyph(int x, int y, u16 color)
{
  ui_rrect(x, y, 5, 3, 1, color);
  ui_rrect(x, y + 2, 12, 8, 2, color);
}

static void list_details(const struct ui_list_view *v)
{
  const struct ui_list_details *d = v->details;
  const char *file = v->rows[v->picked_row].file;
  char title[160], tags[96], fit[160], line[64];
  const char *ext;
  int y, lines;

  if (d && d->ready && d->has_picture)
    framed_picture(P_X, P_Y, d->picture, 120, 80, 120, UI_LINE, 16);
  else
    empty_picture(P_X, P_Y, 120, 80, d && d->ready ? "No saved state" : "");

  ui_game_title(file, title, sizeof(title), tags, sizeof(tags));
  y = P_Y + 97;
  lines = ui_wrap(P_X, y, 15, P_W, 2, title, &ui_font_bold, UI_TEXT);
  y += lines * 15 - 1;
  if (tags[0])
  {
    ui_text(P_X, y, ui_fit(tags, &ui_font_small, P_W, fit, sizeof(fit)), &ui_font_small, UI_TEXT3);
    y += 18;
  }
  else
    y += 4;
  if (!d || !d->ready)
    return;

  ui_text(P_X, y, "Newest state", &ui_font_small, UI_TEXT3);
  if (d->newest >= 0)
    snprintf(line, sizeof(line), "Slot %03d", d->newest);
  else
    snprintf(line, sizeof(line), "None");
  ui_text_right(312, y, line, &ui_font_small, UI_TEXT2);
  y += 15;
  ui_text(P_X, y, "States", &ui_font_small, UI_TEXT3);
  snprintf(line, sizeof(line), "%d", d->states);
  ui_text_right(312, y, line, &ui_font_small, UI_TEXT2);
  y += 15;
  ext = strstr(file, ".zip") ? "ZIP" : strstr(file, ".gbz") ? "GBZ" : "GBA";
  ui_text(P_X, y, "File", &ui_font_small, UI_TEXT3);
  if (d->bytes >= 1024 * 1024)
    snprintf(line, sizeof(line), "%s " UI_DOT " %ld.%ld MB", ext, d->bytes / (1024 * 1024),
             d->bytes % (1024 * 1024) * 10 / (1024 * 1024));
  else
    snprintf(line, sizeof(line), "%s " UI_DOT " %ld KB", ext, (d->bytes + 1023) / 1024);
  ui_text_right(312, y, line, &ui_font_small, UI_TEXT2);
}

static void list_folders(const struct ui_list_view *v)
{
  int i, y = P_Y - 2;
  char fit[96];

  ui_text(P_X, y + 9, "Folders", &ui_font_small, UI_TEXT3);
  y += 16;
  for (i = v->dir_first; i < v->dir_count && i < v->dir_first + UI_LIST_ROWS; i++, y += 21)
  {
    int picked = i == v->dir_picked;
    const char *name = v->dirs[i];
    int up = !strcmp(name, "..");
    u16 fg = picked ? UI_INK : UI_TEXT;
    if (picked)
      pill(P_X - 6, y, 312 - P_X + 8, 20);
    if (up)
      ui_arrow_up(P_X + 1, y + 6, picked ? UI_INK : UI_TEXT3);
    else
      folder_glyph(P_X, y + 5, picked ? UI_INK : UI_TEXT3);
    ui_text(P_X + 18, y + 14, ui_fit(up ? "Up a folder" : name, picked ? &ui_font_bold : &ui_font_body,
                                     312 - P_X - 22, fit, sizeof(fit)),
            picked ? &ui_font_bold : &ui_font_body, fg);
  }
}

void ui_list_draw(const struct ui_list_view *v)
{
  static const char *const keys_games[] = { "enter", "Play", "shift", "Fresh start", "tab", "Move",
                                            UI_ARROW_R, "Folders" };
  static const char *const keys_no_dirs[] = { "enter", "Play", "shift", "Fresh start", "tab", "Move",
                                              "menu", "Back" };
  static const char *const keys_moving[] = { UI_ARROW_U " " UI_ARROW_D, "Move it", "enter", "Put it here",
                                             "esc", "Cancel" };
  static const char *const keys_dirs[] = { "enter", "Open", "esc", "Up a folder", UI_ARROW_L, "Games",
                                           "menu", "Back" };
  char head[96], fit[160];
  int r;

  ui_begin();
  ui_fill(UI_BG);

  ui_text(10, 18, "Games", &ui_font_title, UI_TEXT);
  snprintf(head, sizeof(head), "%s " UI_DOT " %d game%s", v->folder, v->games, v->games == 1 ? "" : "s");
  ui_text_right(310, 17, ui_fit(head, &ui_font_small, 200, fit, sizeof(fit)), &ui_font_small, UI_TEXT3);

  if (!v->games)
  {
    ui_text(L_X + 10, L_Y + 15, "No games in this folder.", &ui_font_body, UI_TEXT2);
    ui_wrap(L_X + 10, L_Y + 33, 14, L_W - 10, 3,
            "Put .gba, .gbz or .zip files (with .tns on the end) here, or open another folder.",
            &ui_font_small, UI_TEXT3);
  }

  for (r = 0; r < UI_LIST_ROWS && v->rows[r].file; r++)
  {
    int y = L_Y + r * L_RH, x = L_X + 26;
    int picked = r == v->picked_row;
    const struct ui_font *f = picked ? &ui_font_bold : &ui_font_body;
    char title[160], tags[96];
    u16 fg = picked ? UI_INK : UI_TEXT;

    if (picked && v->moving)
    {
      ui_rrect(L_X, y, L_W, L_RH - 2, 6, UI_ACCENT);
      fg = UI_TEXT;
    }
    else if (picked)
      pill(L_X, y, L_W, L_RH - 2);
    else if (r == v->marked_row)
      ui_rframe(L_X, y, L_W, L_RH - 2, 6, UI_KEYLINE);
    if (v->rows[r].number >= 0)
      number_chip(L_X + 6, y + 4, v->rows[r].number, picked && !v->moving);
    ui_game_title(v->rows[r].file, title, sizeof(title), tags, sizeof(tags));
    ui_text(x, y + 15, ui_fit(title, f, L_W - 32, fit, sizeof(fit)), f, fg);
  }

  /* more above or below: a fade at the list's edge */
  if (v->first > 0)
    ui_rect_alpha(L_X, L_Y - 2, L_W, 1, UI_TEXT, 3);
  if (v->first + UI_LIST_ROWS < v->games)
    ui_rect_alpha(L_X, L_Y + UI_LIST_ROWS * L_RH - 1, L_W, 1, UI_TEXT, 3);

  if (v->dir_picked >= 0)
    list_folders(v);
  else if (v->picked_row >= 0 && v->rows[v->picked_row].file)
    list_details(v);

  if (v->moving)
    ui_footer(keys_moving, 3);
  else if (v->dir_picked >= 0)
    ui_footer(keys_dirs, 4);
  else
    ui_footer(v->dir_count ? keys_games : keys_no_dirs, 4);
  if (v->status && v->status[0])
    status_line(v->status);

  if (v->dir_picked >= 0)
    snprintf(ui_shown, sizeof(ui_shown), "folders:%d", v->dir_picked);
  else
    snprintf(ui_shown, sizeof(ui_shown), "list:%d", v->picked_row >= 0 ? v->first + v->picked_row : -1);
  ui_present();
}

/* ---- The save manager and the ROM manager ---- */

/* A 12x12 box: a check in it when marked. */
static void check_box(int x, int y, int marked, int picked)
{
  static const u8 tick[7][9] = {
    { 0, 0, 0, 0, 0, 0, 0, 5, 12 }, { 0, 0, 0, 0, 0, 0, 5, 16, 6 }, { 0, 0, 0, 0, 0, 5, 16, 6, 0 },
    { 6, 5, 0, 0, 5, 16, 6, 0, 0 }, { 6, 16, 5, 5, 16, 6, 0, 0, 0 }, { 0, 6, 16, 16, 6, 0, 0, 0, 0 },
    { 0, 0, 6, 6, 0, 0, 0, 0, 0 },
  };
  int i, j;
  if (!marked)
  {
    ui_rframe(x, y, 12, 12, 3, picked ? UI_TEXT3 : UI_KEYLINE);
    return;
  }
  ui_rrect(x, y, 12, 12, 3, UI_ACCENT);
  for (j = 0; j < 7; j++)
    for (i = 0; i < 9; i++)
      if (tick[j][i])
        ui_rect_alpha(x + 2 + i, y + 3 + j, 1, 1, UI_RGB(0xFF, 0xFF, 0xFF), tick[j][i]);
}

void ui_manager_draw(const struct ui_manager_view *v)
{
  static const char *const keys[] = { "enter", "Mark", "A", "Mark all", "esc", "Back" };
  char fit[160];
  int r, y;

  ui_begin();
  ui_fill(UI_BG);
  ui_text(10, 18, v->title, &ui_font_title, UI_TEXT);
  ui_text_right(310, 17, ui_fit(v->subtitle, &ui_font_small, 170, fit, sizeof(fit)), &ui_font_small, UI_TEXT3);

  if (!v->count)
    ui_text(L_X + 10, L_Y + 15, v->empty, &ui_font_body, UI_TEXT2);
  for (r = 0; r < UI_LIST_ROWS && v->first + r < v->count; r++)
  {
    const struct ui_manager_row *row = &v->rows[r];
    int y0 = L_Y + r * L_RH, picked = v->first + r == v->picked;
    const struct ui_font *f = picked ? &ui_font_bold : &ui_font_body;
    int size_w = ui_text_width(row->size, &ui_font_small);
    if (picked)
      pill(L_X, y0, L_W, L_RH - 2);
    check_box(L_X + 7, y0 + 4, row->marked, picked);
    ui_text(L_X + 26, y0 + 15, ui_fit(row->label, f, L_W - 40 - size_w, fit, sizeof(fit)), f,
            picked ? UI_INK : UI_TEXT);
    ui_text_right(L_X + L_W - 8, y0 + 15, row->size, &ui_font_small, picked ? UI_INK : UI_TEXT3);
  }
  if (v->first > 0)
    ui_rect_alpha(L_X, L_Y - 2, L_W, 1, UI_TEXT, 3);
  if (v->first + UI_LIST_ROWS < v->count)
    ui_rect_alpha(L_X, L_Y + UI_LIST_ROWS * L_RH - 1, L_W, 1, UI_TEXT, 3);

  if (v->count)
  {
    if (v->picture)
      framed_picture(P_X, P_Y, v->picture, 120, 80, 120, UI_LINE, 16);
    else
      empty_picture(P_X, P_Y, 120, 80, v->pic_empty ? v->pic_empty : "");
    y = P_Y + 97;
    y += ui_wrap(P_X, y, 15, P_W, 2, v->head, &ui_font_bold, UI_TEXT) * 15 + 2;
    for (r = 0; r < 2; r++)
      if (v->fact_names[r][0])
      {
        ui_text(P_X, y, v->fact_names[r], &ui_font_small, UI_TEXT3);
        ui_text_right(312, y, v->facts[r], &ui_font_small, UI_TEXT2);
        y += 15;
      }
    /* what the keys do to it */
    y += 4;
    for (r = 0; v->actions && v->actions[r]; r += 2, y += 17)
    {
      int end = ui_keycap(P_X, y - 10, v->actions[r]);
      ui_text(end + 5, y, v->actions[r + 1], &ui_font_small, UI_TEXT2);
    }
  }

  ui_footer(keys, 3);
  if (v->status && v->status[0])
  {
    status_line(v->status);
    if (v->warn)
      ui_text(10, 231, v->status, &ui_font_small, UI_WARN);
  }
  if (v->percent >= 0)
  {
    ui_rect(0, 218, UI_W, 1, UI_LINE);
    ui_rect(0, 218, UI_W * v->percent / 100, 1, UI_ACCENT);
  }
  snprintf(ui_shown, sizeof(ui_shown), "%s:%d", v->name, v->picked);
  ui_present();
}

/* ---- The speed test's results ---- */

#define SPEED_BAR_W 288
#define SPEED_BAR_MS 250   /* the bar's width: 25 ms, in tenths */

/* A part of the bar: tenths of ms from at, n long. */
static void time_segment(int x, int y, unsigned at, unsigned n, u16 color)
{
  int x0 = x + (int)(SPEED_BAR_W * (at > SPEED_BAR_MS ? SPEED_BAR_MS : at) / SPEED_BAR_MS);
  int x1 = x + (int)(SPEED_BAR_W * (at + n > SPEED_BAR_MS ? SPEED_BAR_MS : at + n) / SPEED_BAR_MS);
  if (x1 > x0)
    ui_rect(x0, y, x1 - x0, 6, color);
}

void ui_speed_draw(const struct ui_speed_view *v)
{
  static const char *const keys[] = { "any key", "Back" };
  const u16 gfx_color = UI_RGB(0xC5, 0xBE, 0xFF), out_color = UI_TEXT3;
  char line[96];
  int i, y = 30;

  ui_begin();
  ui_fill(UI_BG);
  ui_text(10, 18, v->stopped ? "Speed test (stopped)" : "Speed test", &ui_font_title, UI_TEXT);
  snprintf(line, sizeof(line), "CPU %u MHz", v->mhz);
  ui_text_right(310, 17, line, &ui_font_small, UI_TEXT3);

  for (i = 0; i < v->count; i++, y += 46)
  {
    const struct ui_speed_part *p = &v->parts[i];
    ui_text(16, y + 12, p->name, &ui_font_bold, UI_TEXT);
    snprintf(line, sizeof(line), "%u.%u", p->game_fps / 10, p->game_fps % 10);
    ui_text_right(282, y + 13, line, &ui_font_title, UI_TEXT);
    ui_text(286, y + 13, "fps", &ui_font_small, UI_TEXT3);
    if (p->fast_forward)
    {
      snprintf(line, sizeof(line), "game frames a second; %u.%u pictures shown", p->drawn_fps / 10,
               p->drawn_fps % 10);
      ui_text(16, y + 31, line, &ui_font_small, UI_TEXT3);
      continue;
    }
    /* where a frame's time goes, against the 16.7 ms a frame has */
    ui_rrect(16, y + 19, SPEED_BAR_W, 6, 2, UI_SURFACE);
    time_segment(16, y + 19, 0, p->cpu_ms, UI_ACCENT);
    time_segment(16, y + 19, p->cpu_ms, p->gfx_ms, gfx_color);
    time_segment(16, y + 19, p->cpu_ms + p->gfx_ms, p->out_ms, out_color);
    ui_rect(16 + SPEED_BAR_W * 167 / SPEED_BAR_MS, y + 16, 1, 12, UI_TEXT2);
    snprintf(line, sizeof(line), "game %u.%u " UI_DOT " drawing %u.%u " UI_DOT " showing %u.%u ms a frame",
             p->cpu_ms / 10, p->cpu_ms % 10, p->gfx_ms / 10, p->gfx_ms % 10, p->out_ms / 10, p->out_ms % 10);
    ui_text(16, y + 38, line, &ui_font_small, UI_TEXT3);
  }

  ui_footer(keys, 1);
  ui_text_right(310, 231, "Saved to gpsp_results.txt", &ui_font_small, UI_TEXT3);
  snprintf(ui_shown, sizeof(ui_shown), "speed:%d", v->count);
  ui_present();
}

/* ---- Menus ---- */

/* main menu */
#define M_X 6
#define M_W 140
#define M_Y 30
#define M_RH 20
#define M_PX 164     /* the picture beside it */
#define M_PY 32

/* settings pages */
#define S_Y 32
#define S_RH 19
#define S_SHOWN 7
#define S_HELP_Y 182

static void main_menu(const struct ui_menu_view *v)
{
  int i, y = M_Y;
  char fit[160];

  ui_backdrop_draw();
  ui_text(12, 20, ui_fit(v->title, &ui_font_bold, 240, fit, sizeof(fit)), &ui_font_bold, UI_TEXT);
  if (!v->no_game)
    ui_text_right(308, 20, "Paused", &ui_font_small, UI_TEXT3);

  for (i = 0; i < v->count; i++)
  {
    const struct ui_row *row = &v->rows[i];
    int picked = i == v->picked;
    if (row->gap_before)
    {
      ui_rect_alpha(M_X + 8, y + 3, M_W - 16, 1, UI_TEXT, 2);
      y += 7;
    }
    if (picked)
      pill(M_X, y, M_W, M_RH - 1);
    ui_text(M_X + 10, y + 14, row->label, picked ? &ui_font_bold : &ui_font_body, picked ? UI_INK : UI_TEXT);
    if (row->value[0])
    {
      if (picked && row->kind == UI_ROW_PICK)
      {
        int w = ui_text_width(row->value, &ui_font_small);
        ui_text_right(M_X + M_W - 18, y + 14, row->value, &ui_font_small, UI_INK);
        ui_chevrons(M_X + M_W - 18 - w - 10, M_X + M_W - 7, y + 5, UI_INK);
      }
      else
        ui_text_right(M_X + M_W - 10, y + 14, row->value, &ui_font_small, picked ? UI_INK : UI_TEXT3);
    }
    y += M_RH;
  }

  /* beside it: the picture (the state's, or the game as paused) */
  if (v->picture)
    framed_picture(M_PX, M_PY, v->picture, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W, UI_TEXT, 5);
  else
    empty_picture(M_PX, M_PY, STATE_PIC_W, STATE_PIC_H, v->pic_empty ? v->pic_empty : "");
  y = M_PY + STATE_PIC_H + 18;
  if (v->pic_head[0])
  {
    ui_text(M_PX, y, ui_fit(v->pic_head, &ui_font_bold, 144, fit, sizeof(fit)), &ui_font_bold, UI_TEXT);
    y += 15;
  }
  if (v->pic_sub[0])
  {
    ui_text(M_PX, y, ui_fit(v->pic_sub, &ui_font_small, 144, fit, sizeof(fit)), &ui_font_small, UI_TEXT2);
    y += 15;
  }
  y += 6;
  if (v->status && v->status[0])
    ui_wrap(M_PX, y, 14, 144, 4, v->status, &ui_font_small, UI_TEXT);
  else if (v->help)
    ui_wrap(M_PX, y, 14, 144, 4, v->help, &ui_font_small, UI_TEXT2);
}

static void tabs(const struct ui_menu_view *v)
{
  int i, x = 10;
  for (i = 0; i < v->tab_count; i++)
  {
    int on = i == v->page;
    int end = ui_text(x, 19, v->tabs[i], on ? &ui_font_bold : &ui_font_body, on ? UI_TEXT : UI_TEXT3);
    if (on)
      ui_rrect(x, 24, end - x, 2, 1, UI_ACCENT);
    x = end + 14;
  }
  ui_hline(0, 26, UI_W, UI_LINE);
}

/* A key on the Controls page: the picked one filled. */
static void key_value(int x, int y, const char *key, int row_picked, int picked, int capturing)
{
  char fit[40];
  const char *k = capturing ? "press a key" : key[0] ? key : picked ? "none" : "";
  int w;

  if (!k[0])
    return;
  ui_fit(k, &ui_font_small, 66, fit, sizeof(fit));
  w = ui_text_width(fit, &ui_font_small) + 12;
  if (picked)
  {
    ui_rrect(x, y + 2, w, 15, 4, capturing ? UI_ACCENT : UI_INK);
    ui_text(x + 6, y + 13, fit, &ui_font_small, UI_TEXT);
  }
  else
    ui_text(x + 6, y + 13, fit, &ui_font_small, row_picked ? UI_INK : UI_TEXT2);
}

static void about_page(void)
{
  static const char *const lines[] = {
    "gpSP for the TI-Nspire",
    "The libretro gpSP core, ported by andymcca.",
    "This build: tear-free CX II output, 1000 compressed",
    "save slots with pictures, zipped games, settings per",
    "game, CPU speed, brightness and a new look.",
  };
  int y = 50;
  size_t i;

  ui_text(16, y, lines[0], &ui_font_title, UI_TEXT);
  y += 20;
  for (i = 1; i < sizeof(lines) / sizeof(lines[0]); i++, y += 15)
    ui_text(16, y, lines[i], &ui_font_small, i == 1 ? UI_TEXT2 : UI_TEXT3);
  y += 14;
  ui_text(16, y, "Thanks to", &ui_font_small, UI_TEXT3);
  ui_text(90, y, "Exophase " UI_DOT " David G.F. " UI_DOT " calc84maniac", &ui_font_small, UI_TEXT2);
  y += 15;
  ui_text(90, y, "Vogtinator " UI_DOT " notaz " UI_DOT " the libretro team", &ui_font_small, UI_TEXT2);
}

static void settings_page(const struct ui_menu_view *v)
{
  static const char *const keys_pick[] = { UI_ARROW_L " " UI_ARROW_R, "Page", "enter", "Change", "esc", "Back" };
  static const char *const keys_editing[] = { UI_ARROW_L " " UI_ARROW_R, "Change", "enter", "Keep", "esc",
                                              "Cancel" };
  static const char *const keys_switch[] = { UI_ARROW_L " " UI_ARROW_R, "Page", "enter", "Switch", "esc",
                                             "Back" };
  static const char *const keys_action[] = { UI_ARROW_L " " UI_ARROW_R, "Page", "enter", "Do it", "esc", "Back" };
  static const char *const keys_keys[] = { "enter", "Set", "del", "Clear", UI_ARROW_L " " UI_ARROW_R,
                                           "Key 1 / 2", "esc", "Done" };
  static const char *const keys_about[] = { UI_ARROW_L " " UI_ARROW_R, "Page", "esc", "Back" };
  int i, y = S_Y, first = 0;
  char fit[160];

  ui_fill(UI_BG);
  tabs(v);

  if (v->about)
  {
    about_page();
    ui_footer(keys_about, 2);
    return;
  }

  if (v->picked >= S_SHOWN)
    first = v->picked - S_SHOWN + 1;
  /* a scroll bar only when the page is longer than the screen */
  if (v->count > S_SHOWN)
  {
    int track = S_SHOWN * S_RH - 4;
    int bar = track * S_SHOWN / v->count;
    int top = (track - (bar < 12 ? 12 : bar)) * first / (v->count - S_SHOWN);
    if (bar < 12)
      bar = 12;
    ui_rrect_alpha(UI_W - 5, y + 2, 2, track, 1, UI_TEXT, 2);
    ui_rrect_alpha(UI_W - 5, y + 2 + top, 2, bar, 1, UI_TEXT, 7);
  }

  for (i = first; i < v->count && i < first + S_SHOWN; i++, y += S_RH)
  {
    const struct ui_row *row = &v->rows[i];
    int picked = i == v->picked;
    u16 fg = picked ? UI_INK : UI_TEXT;
    if (picked)
      pill(6, y, UI_W - 16, S_RH - 1);
    ui_text(16, y + 13, ui_fit(row->label, picked ? &ui_font_bold : &ui_font_body, 180, fit, sizeof(fit)),
            picked ? &ui_font_bold : &ui_font_body, fg);
    switch (row->kind)
    {
    case UI_ROW_SWITCH:
      ui_switch(UI_W - 46, y + 3, row->on, picked);
      break;
    case UI_ROW_PICK:
      ui_fit(row->value, &ui_font_small, 110, fit, sizeof(fit));
      if (picked && v->editing)
      {
        /* being changed: the value in the accent, between arrows */
        int w = ui_text_width(fit, &ui_font_small);
        ui_rrect(UI_W - 34 - w - 17, y + 2, w + 35, S_RH - 5, 4, UI_ACCENT);
        ui_text_right(UI_W - 34, y + 13, fit, &ui_font_small, UI_TEXT);
        ui_chevrons(UI_W - 34 - w - 11, UI_W - 22, y + 4, UI_TEXT);
      }
      else if (picked)
        ui_text_right(UI_W - 22, y + 13, fit, &ui_font_small, UI_INK);
      else
        ui_text_right(UI_W - 22, y + 13, fit, &ui_font_small, UI_TEXT2);
      break;
    case UI_ROW_KEYS:
      key_value(UI_W - 168, y, row->value, picked, picked && v->editing && row->key_column == 0,
                picked && row->key_column == 0 && v->capturing);
      key_value(UI_W - 92, y, row->value2, picked, picked && v->editing && row->key_column == 1,
                picked && row->key_column == 1 && v->capturing);
      break;
    default:
      ui_text_right(UI_W - 22, y + 13, UI_RIGHT, &ui_font_body, picked ? UI_INK : UI_TEXT3);
      break;
    }
  }

  if (v->status && v->status[0])
    ui_wrap(14, S_HELP_Y, 14, UI_W - 28, 3, v->status, &ui_font_small, UI_TEXT);
  else if (v->help)
    ui_wrap(14, S_HELP_Y, 14, UI_W - 28, 3, v->help, &ui_font_small, UI_TEXT3);

  if (!v->count)
    ui_footer(keys_about, 2);
  else if (v->rows[v->picked].kind == UI_ROW_KEYS)
  {
    if (v->editing)
      ui_footer(keys_keys, 4);
    else
      ui_footer(keys_pick, 3);
  }
  else if (v->rows[v->picked].kind == UI_ROW_ACTION || v->rows[v->picked].kind == UI_ROW_SUBMENU)
    ui_footer(keys_action, 3);
  else if (v->rows[v->picked].kind == UI_ROW_SWITCH)
    ui_footer(keys_switch, 3);
  else
    ui_footer(v->editing ? keys_editing : keys_pick, 3);
}

void ui_menu_draw(const struct ui_menu_view *v)
{
  ui_begin();
  if (v->page < 0)
    main_menu(v);
  else
    settings_page(v);
  snprintf(ui_shown, sizeof(ui_shown), "%s:%d", v->page < 0 ? "main" : v->tabs[v->page], v->picked);
  ui_present();
}
