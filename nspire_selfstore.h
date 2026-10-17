/* The settings for all games, kept inside the program file itself (as
 * PocketSNES does), so they go wherever _gpSP.tns goes. They sit in a
 * fixed-size area after the program (Ndless loads the whole file; the
 * program's loader only uses its own part), with a footer that marks it:
 *
 *   SELFSTORE_SIZE bytes: the settings file's bytes, then zeros
 *   16 bytes: "GPSPSETS", their length, SELFSTORE_SIZE (little-endian)
 *
 * The first save adds the area; later ones write over it in place, so the
 * program's own bytes are never written again. A new _gpSP.tns copied over
 * the old one has no area yet: the settings then come from the copy in the
 * saves folder (nspire_config.c). */
#ifndef NSPIRE_SELFSTORE_H
#define NSPIRE_SELFSTORE_H

#include <stddef.h>

#define SELFSTORE_SIZE 4096

/* Reads the stored bytes into data (at most size): their length, or -1
 * when the file has none. */
long selfstore_read(const char *program_path, void *data, size_t size);
/* Stores the bytes in the program file, unless it holds them already.
 * Returns 1 if the file has them afterwards. */
int selfstore_write(const char *program_path, const void *data, size_t length);

#endif
