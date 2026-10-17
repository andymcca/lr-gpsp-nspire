/* Games in .zip files, as they are usually downloaded: "Game.zip" renamed to
 * "Game.zip.tns" on the calculator (nspire_zip.c, after PocketSNES's
 * romzip.cpp). The ROM is the first file in the zip ending in .gba or .agb,
 * or else its biggest file. Stored and deflated files are read (what zip
 * tools write), and the ROM's CRC-32 is checked.
 *
 * A deflated file can only be read from its start, so a zipped game is
 * unpacked into memory whole when it loads (gba_memory.c); .gbz files
 * (nspire_gbz.h) are the compressed ROMs that can be read in pieces. */
#ifndef NSPIRE_ZIP_H
#define NSPIRE_ZIP_H

#include <stddef.h>

typedef struct zip_rom zip_rom;

/* The file starts like a zip ("PK\3\4"). */
int zip_is_zip(const char *path);
/* The ROM's file name in the zip, without folders; 0 if it has none. */
int zip_rom_name(const char *path, char *name, size_t size);

/* Opens the ROM in the zip for reading from its start; NULL if there is no
 * ROM, the zip is damaged, or memory is short. */
zip_rom *zip_open(const char *path);
u32 zip_size(const zip_rom *zip);
/* The next n bytes of the ROM. 0 on success. */
int zip_read(zip_rom *zip, u8 *dst, u32 n);
/* After the whole ROM was read: 0 if it is all there and its CRC-32 is
 * right. */
int zip_check(const zip_rom *zip);
void zip_close(zip_rom *zip);

#endif
