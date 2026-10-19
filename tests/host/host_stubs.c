/* What the core needs from the calculator frontend, for the PC tests: the
 * parts of nspire_input.c and nspire_video_present.c that don't touch the
 * hardware, and C versions of the ARM assembly helpers. */

#include "common.h"
#include "nspire_hw.h"

#include <stdarg.h>

/* Mutable hardware stubs let frontend unit tests feed realistic touch swipes
 * and calculator-key combinations through the real nspire_input.c polling code. */
static uint32_t unit_keypad[4];
static touchpad_report_t unit_touchpad_report;
static u32 unit_ticks;

void unit_set_key(unsigned key, int down)
{
  unsigned bit;
  if (!key || key > 128)
    return;
  bit = key - 1;
  if (down)
    unit_keypad[bit >> 5] |= 1u << (bit & 31);
  else
    unit_keypad[bit >> 5] &= ~(1u << (bit & 31));
}

void unit_set_touchpad(int contact, unsigned x, unsigned y)
{
  unit_touchpad_report.contact = contact ? 1 : 0;
  unit_touchpad_report.x = (unsigned short)x;
  unit_touchpad_report.y = (unsigned short)y;
  unit_touchpad_report.arrow = TPAD_ARROW_NONE;
  unit_touchpad_report.pressed = 0;
}

/* The calculator's registers: only the backlight is used here
 * (nspire_brightness.c); the tests look at what was written. */
uint32_t unit_backlight = 120;
long unit_backlight_writes;

uint32_t host_hw_read(uint32_t reg)
{
  if (reg == HW_BACKLIGHT)
    return unit_backlight;
  if (reg >= HW_KEYPAD && reg < HW_KEYPAD + 16 && !(reg & 3))
    return unit_keypad[(reg - HW_KEYPAD) >> 2];
  return 0;
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


/* Host-side substitutes for the Ndless touchpad and timer APIs. */
touchpad_info_t *touchpad_getinfo(void)
{
  static touchpad_info_t info = { 320, 240 };
  return &info;
}

int touchpad_scan(touchpad_report_t *report)
{
  *report = unit_touchpad_report;
  return 0;
}

int on_key_pressed(void)
{
  return 0;
}

u32 nspire_ticks(void)
{
  return ++unit_ticks;
}

void nspire_wait_until(u32 tick)
{
  unit_ticks = tick;
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
char nspire_clock_crashed_rom[512];
