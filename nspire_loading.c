/* The loading screen: starting a game reads its ROM into memory first (all
 * of it when the ROM buffer is big enough, 1 MB at a time), which takes a
 * while for big games; then its newest state is loaded. This shows the
 * game, a bar for the ROM, and what comes after it; or why it didn't load. */

#include "common.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"
#include "nspire_states.h"
#include "nspire_ui.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define COLOR_TEXT  UI_TEXT2
#define COLOR_ERROR UI_WARN

/* The bar: under the game's name */
#define BAR_W 160
#define BAR_H 4

static char title[160], tags[96];
static u16 picture[STATE_PIC_W * STATE_PIC_H];
static int has_picture;
static u32 bar_done, bar_total;

extern void (*nspire_rom_progress)(u32 done, u32 total);

/* The game (the picture of the state it resumes from, if it does), its
 * name, the bar, and a line under it; or, without the bar, why it stopped
 * and a key to go on. */
static void draw_screen(const char *what, int bar, const char *line, u16 line_color, const char *more)
{
  int y = has_picture ? 152 : 104;
  char fit[160];

  ui_begin();
  ui_fill(UI_BG);
  if (has_picture)
  {
    ui_rrect_alpha((UI_W - STATE_PIC_W) / 2 - 1, 29, STATE_PIC_W + 2, STATE_PIC_H + 2, 5, UI_TEXT, 5);
    ui_blit_rounded((UI_W - STATE_PIC_W) / 2, 30, picture, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W, 4);
  }
  ui_text_center(UI_W / 2, y - 18, what, &ui_font_small, UI_TEXT3);
  ui_text_center(UI_W / 2, y, ui_fit(title, &ui_font_title, 290, fit, sizeof(fit)), &ui_font_title, UI_TEXT);
  if (tags[0])
    ui_text_center(UI_W / 2, y + 15, tags, &ui_font_small, UI_TEXT3);
  y += 30;
  if (bar)
  {
    int x = (UI_W - BAR_W) / 2;
    ui_rrect(x, y, BAR_W, BAR_H, 2, UI_SURFACE);
    if (bar_total)
      ui_rrect(x, y, (int)((u64)BAR_W * bar_done / bar_total), BAR_H, 2, UI_ACCENT);
    y += 20;
  }
  else
    y += 6;
  if (line)
    ui_wrap_center(UI_W / 2, y, 14, 280, 3, line, &ui_font_small, line_color);
  if (more)
  {
    /* [any key] Back to the list, in the middle at the bottom */
    int w = ui_text_width("any key", &ui_font_caps) + 12 + ui_text_width(more, &ui_font_small);
    int end = ui_keycap(UI_W / 2 - w / 2, 222, "any key");
    ui_text(end + 4, 231, more, &ui_font_small, UI_TEXT2);
  }
  snprintf(ui_shown, sizeof(ui_shown), "%s", bar ? "loading" : "stopped");
  ui_present();
}

static void draw(const char *line, u16 line_color, const char *more)
{
  draw_screen("Loading", 1, line, line_color, more);
}

static void rom_progress(u32 done, u32 total)
{
  char line[60];
  bar_done = done;
  bar_total = total;
  snprintf(line, sizeof(line), "%u of %u MB", (unsigned)done, (unsigned)total);
  draw(line, COLOR_TEXT, NULL);
}

void nspire_loading_start(const char *rom_path)
{
  int count, newest;

  ui_game_title(rom_path, title, sizeof(title), tags, sizeof(tags));
  /* the moment it carries on from */
  has_picture = nspire_load_on_start && !nspire_skip_state_load &&
                states_peek(rom_path, &count, &newest, picture);
  bar_done = bar_total = 0;
  nspire_rom_progress = rom_progress;
  draw("Reading the game...", COLOR_TEXT, NULL);
}

void nspire_loading_note(const char *text)
{
  draw(text, COLOR_TEXT, NULL);
}

void nspire_loading_end(void)
{
  nspire_rom_progress = NULL;
}

void nspire_game_stopped(const char *why)
{
  keys_wait_release();
  do
  {
    draw_screen("Stopped", 0, why, COLOR_ERROR, "Back to the list");
    update_at_vblank();
    keys_poll();
  } while (!any_key_down());
  keys_wait_release();
}

void nspire_loading_failed(const char *why)
{
  nspire_rom_progress = NULL;
  bar_total = 0;
  keys_wait_release();
  do
  {
    draw_screen("Couldn't start it", 0, why, COLOR_ERROR, "Back to the list");
    update_at_vblank();
    keys_poll();
  } while (!any_key_down());
  keys_wait_release();
}
