/* The order of the file list (gui.c's load_file), as in PocketSNES: games
 * A to Z, or in an order the player sets by moving them (tab). A folder's
 * order is kept in the saves folder (nspire_order_file: gpsp_order_gba.txt.tns
 * for My Documents/gba), one file name a line; games that aren't in it come
 * after the ones that are, A to Z. Earlier builds kept gpsp_order.txt.tns
 * in the folder itself. */
#ifndef NSPIRE_GAME_LIST_H
#define NSPIRE_GAME_LIST_H

/* A to Z, ignoring case; ".." (the folder above) first. */
void game_list_sort(char **names, unsigned count);
/* Moves the name at 'from' to 'to'; the ones between shift by one. */
void game_list_move(char **names, unsigned count, unsigned from, unsigned to);
/* Puts the names in dir's order file first, in its order (the rest keep
 * theirs). Returns 1 if dir has an order file. */
int game_list_apply_order(char **names, unsigned count, const char *dir);
/* Writes names as dir's order. Returns 0 if it can't be written. */
int game_list_save_order(char **names, unsigned count, const char *dir);
/* Back to A to Z: dir's order file goes. */
void game_list_forget_order(const char *dir);
/* A game's file got another name (the ROM manager): its place in dir's
 * order goes to the new name. */
void game_list_rename(const char *dir, const char *old_name, const char *new_name);

#endif
