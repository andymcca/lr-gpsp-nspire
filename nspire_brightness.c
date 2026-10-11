/* Screen brightness on the CX II: see nspire_brightness.h.
 *
 * The OS sets the LCD backlight at 0x90130014, from 0 (brightest) to 225
 * (darkest). gpSP's levels spread over that range in equal steps, and the
 * value the OS had is written back when gpSP closes. Other calculators keep
 * their brightness: the CX's backlight is elsewhere, and untested. */

#include "common.h"
#include "nspire.h"
#include "nspire_hw.h"
#include "nspire_frontend.h"
#include "nspire_brightness.h"

#include <os.h>
#include <stdio.h>

#define DARKEST 225u

u32 nspire_brightness;

static int have_backlight;   /* a CX II, and its value was read */
static u32 os_value;         /* the calculator's own */
static int changed;          /* gpSP wrote the backlight */

u32 nspire_brightness_value(u32 level)
{
  if (level < 1)
    level = 1;
  if (level > BRIGHTNESS_LEVELS)
    level = BRIGHTNESS_LEVELS;
  return (BRIGHTNESS_LEVELS - level) * DARKEST / (BRIGHTNESS_LEVELS - 1);
}

u32 nspire_brightness_level(u32 value)
{
  if (value > DARKEST)
    value = DARKEST;
  return BRIGHTNESS_LEVELS - (value * (BRIGHTNESS_LEVELS - 1) + DARKEST / 2) / DARKEST;
}

void nspire_brightness_init(void)
{
  u32 value;

  have_backlight = changed = 0;
  if (!is_cx2)
    return;
  value = HW_READ(HW_BACKLIGHT);
  /* Something other than the OS's 0..255 means the register isn't what it
   * seems: then gpSP leaves the backlight alone. */
  if (value > 255)
    return;
  os_value = value;
  have_backlight = 1;
}

static void write_backlight(u32 value)
{
  HW_WRITE(HW_BACKLIGHT, value);
  changed = 1;
}

void nspire_brightness_apply(void)
{
  if (nspire_brightness > BRIGHTNESS_LEVELS)
    nspire_brightness = 0;
  if (!have_backlight)
    return;
  if (nspire_brightness)
    write_backlight(nspire_brightness_value(nspire_brightness));
  else if (changed)
    write_backlight(os_value);
}

void nspire_brightness_step(int delta)
{
  s32 level;

  if (!have_backlight)
  {
    nspire_osd_message("Brightness: CX II only");
    return;
  }
  level = nspire_brightness ? (s32)nspire_brightness : (s32)nspire_brightness_level(os_value);
  level += delta;
  if (level < 1)
    level = 1;
  if (level > BRIGHTNESS_LEVELS)
    level = BRIGHTNESS_LEVELS;
  nspire_brightness = (u32)level;
  nspire_brightness_apply();
  nspire_osd_message("Brightness %d/%d", (int)level, BRIGHTNESS_LEVELS);
}

void nspire_brightness_report(char *text, u32 size)
{
  if (have_backlight)
    snprintf(text, size, "backlight register %u at start (0 brightest, 225 darkest), setting %u",
             (unsigned)os_value, (unsigned)nspire_brightness);
  else
    snprintf(text, size, "backlight register not used (%s)", is_cx2 ? "it read past 255" : "not a CX II");
}

void nspire_brightness_restore(void)
{
  if (have_backlight && changed)
  {
    HW_WRITE(HW_BACKLIGHT, os_value);
    changed = 0;
  }
}
