/* Compressing and uncompressing ROMs on the calculator (the ROM manager,
 * nspire_rom_manager.c): a .gba (or .gbz) becomes a .zip of it, as zip tools
 * make them; a .zip (or .gbz) becomes a .gba. The new file is written next to
 * the old one under a temporary name, read back and checked, and only then
 * does the old one go. The game's saves keep working: they're named after
 * the ROM (inside a zip, its file name there: nspire_states.h). */
#ifndef NSPIRE_ROM_CONVERT_H
#define NSPIRE_ROM_CONVERT_H

#include <stddef.h>

enum { ROM_OTHER, ROM_PLAIN, ROM_ZIP, ROM_GBZ };

/* What a ROM file is, from its first bytes (and a plain one's name). */
int rom_kind(const char *path);

/* Called now and then with the bytes done of the whole job (writing, then
 * checking). */
typedef void (*rom_progress_fn)(unsigned long done, unsigned long total);

/* "dir/Game.gba.tns" (or .gbz.tns) -> "dir/Game.zip.tns" with "Game.gba" in
 * it. NULL when it worked (new_path gets the new file), else why not; then
 * nothing changed. */
const char *rom_compress(const char *path, char *new_path, size_t size, rom_progress_fn progress);
/* "dir/Game.zip.tns" (or .gbz.tns) -> "dir/Game.gba.tns". The same. */
const char *rom_uncompress(const char *path, char *new_path, size_t size, rom_progress_fn progress);

#endif
