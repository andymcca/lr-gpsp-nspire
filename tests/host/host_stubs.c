/* What the core needs from the calculator frontend, for the PC tests: the
 * parts of nspire_input.c and nspire_video_present.c that don't touch the
 * hardware, and C versions of the ARM assembly helpers. */

#include "common.h"
#include "nspire_hw.h"

#include <stdarg.h>

static u32 old_key;

/* The calculator's registers: only the backlight is used here
 * (nspire_brightness.c); the tests look at what was written. */
uint32_t unit_backlight = 120;
long unit_backlight_writes;

uint32_t host_hw_read(uint32_t reg)
{
  return reg == HW_BACKLIGHT ? unit_backlight : 0;
}

void host_hw_write(uint32_t reg, uint32_t value)
{
  if (reg == HW_BACKLIGHT)
  {
    unit_backlight = value;
    unit_backlight_writes++;
  }
}

/* The calculator's documents folder (Ndless's get_documents_dir), where
 * the saves folder goes: the tests point it at their own folder. */
char unit_documents_dir[600] = "./";

const char *get_documents_dir(void)
{
  return unit_documents_dir;
}

/* The last message that would have been on the screen. */
char unit_osd_message[64];

void nspire_osd_message(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vsnprintf(unit_osd_message, sizeof(unit_osd_message), format, ap);
  va_end(ap);
}

bool input_check_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  return (p && bson_contains_key(p, "prevkey", BSON_TYPE_INT32));
}

bool input_read_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  if (p)
    return bson_read_int32(p, "prevkey", &old_key);
  return false;
}

unsigned input_write_savestate(u8 *dst)
{
  u8 *wbptr1, *startp = dst;
  bson_start_document(dst, "input", wbptr1);
  bson_write_int32(dst, "prevkey", old_key);
  bson_finish_document(dst, wbptr1);
  return (unsigned int)(dst - startp);
}

void video_fill_u16_strip_arm(u16 *dst, u32 nh, u32 value)
{
  while (nh--)
    *dst++ = (u16)value;
}

void nspire_fps_overlay_reset(void) {}
void nspire_video_mix_reset(void) {}

/* nspire_host_main.c */
u32 update_backup_flag = 1;

/* Frontend globals nspire_config.c works on (nspire_video_ui.c,
 * nspire_host_main.c, nspire_input.c, nspire_clock.c on the calculator). */
u32 screen_scale = 3;
u32 screen_filter;
u32 current_frameskip_type;
u32 frameskip_value = 4;
u32 random_skip;
u32 global_enable_analog;
u32 analog_sensitivity_level = 4;
char nspire_clock_crashed_rom[512];

/* The menus' frame (nspire_ui.c draws into it; the unit tests read it). */
u16 nspire_landscape[320 * 240];
void flip_screen_ui(void) {}
