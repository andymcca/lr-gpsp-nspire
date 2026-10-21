/* The menus' screens, drawn with the kit (nspire_ui.h): the game list, the
 * main menu over the paused game, and the settings pages. gui.c keeps what
 * the menus do and fills in these views; this only draws them. */
#ifndef NSPIRE_UI_SCREENS_H
#define NSPIRE_UI_SCREENS_H

#include "nspire_ui.h"

/* ---- The game list ---- */

#define UI_LIST_ROWS 8   /* games on the screen at once */

struct ui_list_game
{
  const char *file;       /* file name, NULL past the end */
  int number;             /* its number key (1-9, 0), -1 for none */
};

/* The picked game's details, looked up when it stays picked (gui.c). */
struct ui_list_details
{
  int ready;
  int states, newest;     /* newest: slot, -1 for none */
  int has_picture;
  u16 picture[120 * 80];
  long bytes;
};

struct ui_list_view
{
  const char *folder;           /* "gba" */
  struct ui_list_game rows[UI_LIST_ROWS];
  int picked_row;               /* among rows, -1 when on the folders */
  int marked_row;               /* the game picked while on the folders, or -1 */
  int games;                    /* in the folder */
  int first;                    /* the first row's place in the list */
  int moving;                   /* the picked game is being moved */
  const struct ui_list_details *details;
  /* folders, shown instead of the details while picked */
  const char *const *dirs;
  int dir_count, dir_first, dir_picked;   /* dir_picked -1: not on them */
  const char *status;           /* what the last key did, or "" */
};

void ui_list_draw(const struct ui_list_view *v);

/* ---- The save manager and the ROM manager ---- */

struct ui_manager_row
{
  char label[64];
  char size[16];
  u8 marked;
};

struct ui_manager_view
{
  const char *title;            /* "Save manager" */
  const char *name;             /* one word for it, for the tests (ui_shown) */
  char subtitle[64];            /* "17 files, 1.2 MB" */
  struct ui_manager_row rows[UI_LIST_ROWS];
  int count, first, picked;     /* the list's length, its first row, picked */
  int marked;                   /* how many are marked */
  const char *empty;            /* said when there's nothing */
  /* beside it: the picked one (or the marked ones) */
  const u16 *picture;           /* 120x80, or NULL */
  const char *pic_empty;
  char head[96];
  char fact_names[2][16], facts[2][32];
  const char *const *actions;   /* key, what it does, ...; NULL-ended */
  const char *status;           /* what the last key did, or "" */
  int warn;                     /* the status asks something (delete?) */
  int percent;                  /* work under way, 0-100, or -1 */
};

void ui_manager_draw(const struct ui_manager_view *v);

/* ---- The speed test's results ---- */

struct ui_speed_part
{
  const char *name;
  int fast_forward;
  /* tenths: frames a second, and ms a frame */
  unsigned game_fps, drawn_fps, cpu_ms, gfx_ms, out_ms;
};

struct ui_speed_view
{
  unsigned mhz;
  int stopped;
  struct ui_speed_part parts[4];
  int count;
};

void ui_speed_draw(const struct ui_speed_view *v);

/* ---- Menus ---- */

enum ui_row_kind
{
  UI_ROW_ACTION,      /* does something */
  UI_ROW_SUBMENU,     /* opens a page */
  UI_ROW_PICK,        /* a value picked with left/right */
  UI_ROW_SWITCH,      /* off or on */
  UI_ROW_KEYS         /* two keys for a button (Controls) */
};

struct ui_row
{
  char label[48];
  char value[40];         /* PICK; for KEYS, the first key */
  char value2[24];        /* KEYS: the second key */
  u8 kind;
  u8 on;                  /* SWITCH */
  u8 gap_before;          /* a divider above it (main menu) */
  s8 key_column;          /* KEYS: which key is picked when the row is */
};

#define UI_MAX_ROWS 32

struct ui_menu_view
{
  int page;                     /* -1: the main menu, else a settings tab */
  const char *const *tabs;
  int tab_count;
  const char *title;            /* the game (main menu) */
  struct ui_row rows[UI_MAX_ROWS];
  int count, picked;
  const char *help;             /* the picked line's ('\n' is a space) */
  const char *status;           /* what the last action did, or "" */
  int capturing;                /* waiting for a key (Controls) */
  int about;                    /* the About page: credits, no rows */
  /* main menu: the picture beside it, and what it is */
  int no_game;
  const u16 *picture;           /* STATE_PIC_W x STATE_PIC_H, or NULL */
  const char *pic_empty;        /* said instead when there's none */
  char pic_head[48];
  char pic_sub[64];
  int version;                  /* changes with what picture points at */
};

void ui_menu_draw(const struct ui_menu_view *v);

#endif
