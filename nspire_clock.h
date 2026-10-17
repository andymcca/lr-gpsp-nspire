/* The CX II's CPU speed (nspire_clock.c): the "CPU speed" setting raises the
 * clock while a game runs; menus, the file browser and saving run at the
 * calculator's own speed. */
#ifndef NSPIRE_CLOCK_H
#define NSPIRE_CLOCK_H

/* nspire_cpu_speed: 0 normal, then 408 to 504 MHz in 12 MHz steps (the
 * multiplier of the 12 MHz clock, 34 to 42), as in PocketSNES. */
#define CPU_SPEEDS 10

/* At startup: if a raised speed froze the calculator last time (its marker
 * file is still there), the speed goes back to normal and the next game
 * says so. */
void nspire_clock_check_crash(void);
/* The game that froze, for its own settings (""; gui.c). */
extern char nspire_clock_crashed_rom[512];
/* The game's speed (the setting), or back to the calculator's own. */
void nspire_clock_game(void);
void nspire_clock_normal(void);
/* Leaving gpSP cleanly: normal speed and no marker. */
void nspire_clock_left_game(void);
/* The CPU clock in MHz, measured (0 if it can't be). */
u32 nspire_cpu_mhz(void);
/* The clock register and the last switch, for the speed test's file. */
void nspire_clock_report(char *text, u32 size);

#endif
