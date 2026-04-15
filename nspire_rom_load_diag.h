/* Verbose ROM load trace for NSPIRE_LIBRETRO (debug screen + wait for input). */
#ifndef NSPIRE_ROM_LOAD_DIAG_H
#define NSPIRE_ROM_LOAD_DIAG_H

void nspire_rom_load_diag_begin(const char *path);
void nspire_rom_load_diag_step(const char *msg);
void nspire_rom_load_diag_fmt(const char *fmt, ...);
void nspire_rom_load_diag_fail(const char *msg);
void nspire_rom_load_diag_press_any_key(void);

#endif
