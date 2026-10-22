/* Save state slots 0 to 999.
 *
 * A game's states are in the saves folder (nspire_paths.h), named the way
 * gpSP always named them: "<ROM file name without .tns><slot>.svs.tns", so
 * slot 12 of "Pokemon.gba.tns" is "Pokemon.gba12.svs.tns". A compressed ROM
 * ("Pokemon.gbz.tns") or a zip with "Pokemon.gba" in it uses the same names
 * as the plain one. Earlier builds kept them next to the ROM.
 *
 * States are written zlib-compressed behind a 32-byte header ("GPSZ") that
 * also counts saves, so the newest state is the one with the highest count;
 * with Compress saves off, the header says "stored" and the state follows
 * it as it is.
 * Older gpSP states (416 KB, uncompressed) still load. They count as older
 * than this build's, except while a game has none of this build's (it has
 * only states from older builds, or the first QoL build's, whose headers
 * lack the flag): then the newest is the most recently modified file. */
#ifndef NSPIRE_STATES_H
#define NSPIRE_STATES_H

#include <stddef.h>

#define STATE_SLOTS 1000

/* The name a game's own files are built from: the ROM's file name without
 * its last extension, with ".gbz" turned into ".gba" ("Pokemon.gba"); for a
 * zip, the ROM's name in it. */
void nspire_game_name(const char *rom_path, char *out, size_t size);
/* That name in the saves folder ("<saves folder>/Pokemon.gba"). */
void nspire_game_base(const char *rom_path, char *out, size_t size);
/* Where builds before the saves folder kept them: next to the ROM
 * ("dir/Pokemon.gba"). */
void nspire_game_old_base(const char *rom_path, char *out, size_t size);
/* Moves what earlier builds left in the game's folder (this game's files
 * and the other GBA games', and the folder's game order) into the saves
 * folder. Before a game's files are opened. */
void nspire_game_move_old_files(const char *rom_path);

/* Points the slot functions at a ROM and looks up which slots it has. */
void states_open_game(const char *rom_path);
void states_file_name(int slot, char *out, size_t size);

/* A state's picture of the game (newer states have one): STATE_PIC_W x
 * STATE_PIC_H in 5:6:5, 3/5 of the GBA's size. */
#define STATE_PIC_W 144
#define STATE_PIC_H 96
/* 1 if the slot's state has a picture (put in pic), 0 if not. */
int states_picture(int slot, unsigned short *pic);

/* Another game's states, for the game list, leaving the open game's as they
 * are: how many, the newest (-1: none), and its picture (1 if it has one). */
int states_peek(const char *rom_path, int *count, int *newest, unsigned short *pic);

int states_exists(int slot);
int states_count(void);
/* The most recently saved slot, or -1 if the game has none. */
int states_newest(void);
/* Where an auto-increment save goes: the slot after the highest one used, or
 * the lowest free slot once 999 is used. -1 when all 1000 are used. */
int states_next_new_slot(void);

/* 1 on success, 0 if the state couldn't be written (out of memory or space). */
int states_save(int slot);
/* 1 on success, 0 if the slot is empty, -1 if the file isn't a state gpSP
 * can read (damaged, or from another emulator). */
int states_load(int slot);
/* The same for any state file (0: it can't be opened). */
int states_load_file(const char *path);

/* For the save manager: a state file's size and whether it's compressed
 * (0 if the slot is empty), packing or unpacking one (1 if it is that way
 * afterwards; its place among the newest stays), and deleting one. */
int states_file_info(int slot, long *bytes, int *compressed);
int states_set_compressed(int slot, int compress);
int states_delete(int slot);

/* The game's in-game save, the same way (nspire_backup.c): its file is the
 * plain .sav or the gzipped .sav.gz. Deleting it leaves the game's memory
 * alone; the file comes back when the game saves again. */
int nspire_backup_info(long *bytes, int *compressed);
int nspire_backup_set_compressed(int compressed);
int nspire_backup_delete(void);

/* How long the last saves and loads took, by part, to
 * gpsp_states_time.txt.tns in the saves folder (when gpSP closes). */
void states_timing_flush(void);

/* The game's state in memory, for the speed test: states_snapshot returns a
 * buffer (free() it) or NULL; states_restore puts it back. */
void *states_snapshot(void);
int states_restore(const void *snapshot);

#endif
