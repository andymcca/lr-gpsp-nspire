/* The loading screen: starting a game reads its ROM into memory first (all
 * of it when the ROM buffer is big enough, 1 MB at a time), which takes a
 * while for big games; then its newest state is loaded. This shows the
 * game, a bar for the ROM, and what comes after it; or why it didn't load. */

#include "common.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* gpSP's menu colors (gui.c's color16) */
#define MENU_COLOR(r, g, b) ((u16)((((g) & 1) << 15) | ((b) << 10) | (((g) >> 1) << 5) | (r)))
#define COLOR_BACK  MENU_COLOR(2, 8, 10)
#define COLOR_TITLE MENU_COLOR(31, 63, 31)
#define COLOR_TEXT  MENU_COLOR(16, 40, 24)
#define COLOR_BAR   MENU_COLOR(13, 40, 18)
#define COLOR_EMPTY MENU_COLOR(4, 14, 16)
#define COLOR_ERROR MENU_COLOR(31, 44, 0)

#define BAR_X 30
#define BAR_Y 110
#define BAR_W 260
#define BAR_H 8

static char game_name[52];
static u32 bar_done, bar_total;

extern void (*nspire_rom_progress)(u32 done, u32 total);

/* line may have a second line after a '\n'; more goes under it. Without
 * the bar, line goes where the bar would be. */
static void draw_screen(const char *title, int bar, const char *line, u16 line_color,
                        const char *more)
{
  clear_screen(COLOR_BACK);
  print_string(title, COLOR_TEXT, COLOR_BACK, BAR_X, 80);
  print_string(game_name, COLOR_TITLE, COLOR_BACK, BAR_X, 92);
  if (bar)
  {
    fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_EMPTY);
    if (bar_total)
      fill_rect(BAR_X, BAR_Y, (u32)((u64)BAR_W * bar_done / bar_total), BAR_H, COLOR_BAR);
  }
  if (line)
    print_string(line, line_color, COLOR_BACK, BAR_X, bar ? 124 : BAR_Y);
  if (more)
    print_string(more, COLOR_TEXT, COLOR_BACK, BAR_X, 150);
  flip_screen();
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
  const char *slash = strrchr(rom_path, '/');
  size_t len;

  snprintf(game_name, sizeof(game_name), "%s", slash ? slash + 1 : rom_path);
  len = strlen(game_name);
  if (len > 4 && !strcasecmp(game_name + len - 4, ".tns"))
    game_name[len - 4] = 0;
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
    draw_screen("Stopped", 0, why, COLOR_ERROR, "Press a key.");
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
    draw(why, COLOR_ERROR, "Press a key.");
    update_at_vblank();
    keys_poll();
  } while (!any_key_down());
  keys_wait_release();
}
