#include "common.h"
#include "gpsp_config.h"
#include "nspire_gui_video.h"
#include "input.h"
#include "nspire_rom_load_diag.h"

#include <stdarg.h>
#include <stdio.h>

extern u32 gamepak_buffer_count;
extern void delay_us(u32 us_count);

void nspire_rom_load_diag_begin(const char *path)
{
  char line[160];

  debug_screen_start();
  debug_screen_printl("--- ROM load trace ---");
  snprintf(line, sizeof(line), "Path: %s", path ? path : "(null)");
  debug_screen_printl(line);
  snprintf(line, sizeof(line), "1MiB buffers OK: %u / %u",
           (unsigned)gamepak_buffer_count, (unsigned)ROM_BUFFER_SIZE);
  debug_screen_printl(line);
  debug_screen_update();
}

void nspire_rom_load_diag_step(const char *msg)
{
  debug_screen_printl(msg);
  debug_screen_update();
}

void nspire_rom_load_diag_fmt(const char *fmt, ...)
{
  char line[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  debug_screen_printl(line);
  debug_screen_update();
}

void nspire_rom_load_diag_fail(const char *msg)
{
  debug_screen_printl("FAILED:");
  debug_screen_printl(msg);
  debug_screen_update();
}

void nspire_rom_load_diag_press_any_key(void)
{
  gui_action_type gui_action = CURSOR_NONE;

  debug_screen_printl("Press any key to continue.");
  debug_screen_update();
  while (gui_action == CURSOR_NONE)
  {
    gui_action = get_gui_input();
    delay_us(15000);
  }
  debug_screen_end();
}
