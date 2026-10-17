/* Where gpSP keeps its files, as PocketSNES does: one "zgpSP_saves" folder
 * in My Documents holds every game's in-game save, save states and own
 * settings, and gpSP's own files (a copy of the settings for all games, the
 * file list's last folder and game orders, the speed test's and the detailed
 * counter's logs, the CPU speed's crash marker, the GBA BIOS). gpSP's folder
 * keeps only the program and the games. The settings for all games
 * are also kept inside the program file itself (nspire_selfstore.h), so they
 * go wherever _gpSP.tns goes. The folder isn't in the ROM folder, so
 * _gpSP.tns is the first thing there (a folder is listed above every file).
 * The OS lists folders A to Z, ignoring case, so the z puts the saves
 * folder below the game folders (PocketSNES's is zPocketSNES_saves).
 * gpSP makes the folder when it starts, wherever it is (no gba folder
 * needed), so a new calculator gets it too.
 *
 * Earlier builds kept all of these next to the ROMs and next to gpSP, and
 * the build before this one in My Documents/gpSP_saves: nspire_move_old_files
 * and nspire_move_old_documents_files move them in. */
#ifndef NSPIRE_PATHS_H
#define NSPIRE_PATHS_H

#include <stddef.h>

#define SAVE_FOLDER "zgpSP_saves"
/* The saves folder's name before it got the z. */
#define OLD_SAVE_FOLDER "gpSP_saves"

/* The program file gpSP runs from (argv[0]); "" when unknown. */
extern char nspire_program_path[512];

/* "<My Documents>/zgpSP_saves/<name>" (the folder itself for name ""). */
void nspire_save_file(char *out, size_t size, const char *name);
/* Makes the saves folder if it isn't there. */
void nspire_save_dir_make(void);

/* A path in the form "/documents/gba/Game.zip.tns": the OS's own form of
 * the current folder ("\documents\gba\") turned to /, no // or / at the
 * end. Files are moved and renamed by these. */
void nspire_normalize_path(char *out, size_t size, const char *path);

/* The file with the order of the games in the ROM folder dir (the file
 * list's): "gpsp_order_gba.txt.tns" for My Documents/gba,
 * "gpsp_order_gba_rpg.txt.tns" for gba/rpg, "gpsp_order.txt.tns" for My
 * Documents itself. */
void nspire_order_file(char *out, size_t size, const char *dir);

/* Moves a game's files (in-game save, plain or gzipped, states, own
 * settings) named after from_base to the same names after to_base. A file
 * to_base already has stays, and so does the one that would replace it. */
void nspire_move_game_files(const char *from_base, const char *to_base);

/* Moves into the saves folder what earlier builds kept in the ROM folder
 * dir: the GBA games' in-game saves, states and own settings, and the
 * folder's game order (gpsp_order.txt.tns); with with_program_files, dir is
 * gpSP's own folder and its files there move in too: settings files
 * (*.cfg.tns, older builds' too), the last folder, logs, the BIOS. A file
 * the saves folder already has wins: the old one stays where it is. Cheap
 * when there's nothing to move. */
void nspire_move_old_files(const char *dir, int with_program_files);

/* Moves into the saves folder what earlier builds left in My Documents:
 * all of My Documents/gpSP_saves (the saves folder's name before; it goes
 * once it's empty: a file the saves folder already has wins and stays
 * there) and gpSP's settings files in My Documents itself (older builds
 * wrote "..cfg.tns" in whatever folder their file list had open). */
void nspire_move_old_documents_files(void);

#endif
