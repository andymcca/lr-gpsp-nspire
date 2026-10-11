/* Screen brightness on the CX II (nspire_brightness.c).
 *
 * The setting is 0 for the calculator's own brightness (gpSP leaves the
 * backlight alone), or a level from 1 (dimmest) to 10 (brightest). The
 * calculator's own brightness comes back when gpSP closes. */
#ifndef NSPIRE_BRIGHTNESS_H
#define NSPIRE_BRIGHTNESS_H

#define BRIGHTNESS_LEVELS 10

extern u32 nspire_brightness;

/* At startup, before anything changes the backlight: notes the
 * calculator's own brightness. */
void nspire_brightness_init(void);
/* Sets the backlight for the setting (the menu calls it on every change). */
void nspire_brightness_apply(void);
/* The hotkeys: one level brighter (+1) or dimmer (-1), from the level shown
 * now; shows "Brightness 7/10". */
void nspire_brightness_step(int delta);
/* The calculator's own brightness back (gpSP is closing). */
void nspire_brightness_restore(void);

/* For the speed test's file: what the backlight register read at start. */
void nspire_brightness_report(char *text, u32 size);

/* The backlight register value for a level, and the level nearest a value. */
u32 nspire_brightness_value(u32 level);
u32 nspire_brightness_level(u32 value);

#endif
