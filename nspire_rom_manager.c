/* The ROM manager (main menu > ROM manager), next to the save manager: the
 * games in the folder of the game that's running (without one, the file
 * list's folder), their sizes, zipped or not. enter marks games (A: all); C
 * compresses the marked ones, or else the one picked, into .zip files, U
 * uncompresses them back into .gba files (nspire_rom_convert.h). Saves keep
 * working either way. The game that's running can be done too when its
 * whole ROM is in memory (else it still reads pieces of its file). */

#include "common.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"
#include "nspire_game_list.h"
#include "nspire_keys.h"
#include "nspire_paths.h"
#include "nspire_rom_convert.h"
#include "nspire_states.h"

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* gpSP's menu colors (gui.c's color16), as in the save manager */
#define MENU_COLOR(r, g, b) ((u16)((((g) & 1) << 15) | ((b) << 10) | (((g) >> 1) << 5) | (r)))
#define COLOR_BACK     MENU_COLOR(2, 8, 10)
#define COLOR_ACTIVE   MENU_COLOR(31, 63, 31)
#define COLOR_INACTIVE MENU_COLOR(13, 40, 18)
#define COLOR_HELP     MENU_COLOR(16, 40, 24)
#define COLOR_MARKED   MENU_COLOR(31, 50, 4)

#define ROWS 19          /* lines 20 to 200 */
#define MAX_ROMS 256

struct rom_entry
{
  char name[256];   /* the file's name, "Game.zip.tns" */
  long bytes;
  int kind;
};

static struct rom_entry *roms;
static unsigned char marked[MAX_ROMS];
static int rom_count, selected, top;
static char folder[512], status[100];
static int last_percent;

static int is_rom_name(const char *name)
{
  static const char *const exts[] = { ".gba.tns", ".gbz.tns", ".zip.tns" };
  size_t len = strlen(name), i;
  for (i = 0; i < 3; i++)
    if (len > 8 && !strcasecmp(name + len - 8, exts[i]))
      return 1;
  return 0;
}

static void rom_path(const char *name, char *path, size_t size)
{
  size_t len = strlen(folder);
  snprintf(path, size, "%s%s%s", folder, len && folder[len - 1] == '/' ? "" : "/", name);
}

/* The folder's games, in the file list's order. */
static void refresh(void)
{
  static char *names[MAX_ROMS];
  struct rom_entry *sorted;
  struct dirent *entry;
  DIR *dir;
  int i, j;

  rom_count = 0;
  dir = opendir(folder);
  while (dir && (entry = readdir(dir)) != NULL && rom_count < MAX_ROMS)
    if (is_rom_name(entry->d_name))
      snprintf(roms[rom_count++].name, sizeof(roms[0].name), "%s", entry->d_name);
  if (dir)
    closedir(dir);

  for (i = 0; i < rom_count; i++)
    names[i] = roms[i].name;
  game_list_sort(names, (unsigned)rom_count);
  game_list_apply_order(names, (unsigned)rom_count, folder);
  sorted = (struct rom_entry *)malloc(sizeof(*sorted) * (rom_count ? rom_count : 1));
  if (sorted)
  {
    for (i = 0; i < rom_count; i++)
      sorted[i] = *(struct rom_entry *)(names[i] - offsetof(struct rom_entry, name));
    memcpy(roms, sorted, sizeof(*sorted) * rom_count);
    free(sorted);
  }

  for (i = 0; i < rom_count; i++)
  {
    char path[800];
    struct stat st;
    rom_path(roms[i].name, path, sizeof(path));
    roms[i].bytes = stat(path, &st) == 0 ? (long)st.st_size : 0;
    roms[i].kind = rom_kind(path);
  }
  for (j = 0; j < MAX_ROMS; j++)
    marked[j] = 0;
  if (selected >= rom_count)
    selected = rom_count ? rom_count - 1 : 0;
}

static void size_text(long bytes, char *text, size_t size)
{
  if (bytes < 1024 * 1024)
    snprintf(text, size, "%ld KB", (bytes + 1023) / 1024);
  else
  {
    long tenths = (long)(((long long)bytes * 10 + 524288) / 1048576);
    snprintf(text, size, "%ld.%ld MB", tenths / 10, tenths % 10);
  }
}

static const char *kind_text(int kind)
{
  return kind == ROM_ZIP ? "zip" : kind == ROM_GBZ ? "gbz" : kind == ROM_PLAIN ? "gba" : "?";
}

static void draw(void)
{
  char line[100], name[64], bytes[24];
  long total = 0;
  int i, row;

  for (i = 0; i < rom_count; i++)
    total += roms[i].bytes;
  size_text(total, bytes, sizeof(bytes));
  clear_screen(COLOR_BACK);
  snprintf(line, sizeof(line), "ROM manager: %d %s, %s", rom_count, rom_count == 1 ? "game" : "games", bytes);
  print_string(line, COLOR_ACTIVE, COLOR_BACK, 0, 0);

  if (!rom_count)
    print_string("No games in this folder.", COLOR_INACTIVE, COLOR_BACK, 10, 20);
  for (row = 0; row < ROWS && top + row < rom_count; row++)
  {
    const struct rom_entry *r = &roms[top + row];
    u16 color = top + row == selected ? COLOR_ACTIVE : marked[top + row] ? COLOR_MARKED : COLOR_INACTIVE;
    size_t len = strlen(r->name);
    /* The name without its ".zip.tns", ".gba.tns"... */
    snprintf(name, sizeof(name), "%.*s", (int)(len > 8 ? len - 8 : len), r->name);
    size_text(r->bytes, bytes, sizeof(bytes));
    snprintf(line, sizeof(line), "%c %-36.36s %8s %s", marked[top + row] ? '*' : ' ', name, bytes,
             kind_text(r->kind));
    print_string(line, color, COLOR_BACK, 10, 20 + row * 10);
  }

  if (status[0])
    print_string(status, COLOR_HELP, COLOR_BACK, 10, 220);
  else
    print_string("enter: mark  A: all  C: compress  U: uncompress", COLOR_HELP, COLOR_BACK, 10, 220);
  print_string("C makes .zip files, U .gba files. esc: back", COLOR_HELP, COLOR_BACK, 10, 230);
  flip_screen();
}

static int marked_count(void)
{
  int i, n = 0;
  for (i = 0; i < rom_count; i++)
    n += marked[i];
  return n;
}

/* The status line while a game is done: "Compressing 2/5: Name 37%". */
static const char *working_verb;
static int working_number, working_count;
static char working_name[40];

static void progress(unsigned long done, unsigned long total)
{
  int percent = total ? (int)((unsigned long long)done * 100 / total) : 0;
  if (percent == last_percent)
    return;
  last_percent = percent;
  snprintf(status, sizeof(status), "%s %d/%d: %s %d%%", working_verb, working_number, working_count,
           working_name, percent);
  draw();
}

static void act(int compress)
{
  static char names[MAX_ROMS][256];
  int targets = marked_count(), count = 0, done = 0, skipped = 0, failed = 0, i;
  long before = 0, after = 0;
  const char *first_why = NULL;
  char was[24], now[24], keep[256];

  if (!rom_count)
    return;
  /* The game picked stays picked, under its new name. */
  snprintf(keep, sizeof(keep), "%s", roms[selected].name);
  /* What's to do: the marked games (or the one picked) that aren't that
   * way already. */
  for (i = 0; i < rom_count; i++)
  {
    if (targets ? !marked[i] : i != selected)
      continue;
    if (compress ? roms[i].kind == ROM_ZIP : roms[i].kind == ROM_PLAIN)
      skipped++;
    else
      snprintf(names[count++], sizeof(names[0]), "%s", roms[i].name);
  }
  if (!count)
  {
    snprintf(status, sizeof(status), compress ? "Nothing to compress: %s already."
                                              : "Nothing to uncompress: %s already.",
             skipped == 1 ? (compress ? "it's a zip" : "it's a .gba") : (compress ? "they're zips" : "they're .gba"));
    return;
  }

  working_verb = compress ? "Compressing" : "Uncompressing";
  working_count = count;
  for (i = 0; i < count; i++)
  {
    char path[800], new_path[512];
    const char *why, *new_name;
    struct stat st;
    int running;

    rom_path(names[i], path, sizeof(path));
    working_number = i + 1;
    snprintf(working_name, sizeof(working_name), "%.24s", names[i]);
    last_percent = -1;
    progress(0, 1);

    /* The running game's file can go only if it isn't read any more. */
    {
      char game[600];
      nspire_normalize_path(game, sizeof(game), gamepak_filename);
      running = gamepak_filename[0] && !strcmp(path, game);
    }
    if (running && !nspire_rom_file_release())
      why = "It's running and still reads its file.";
    else
    {
      long bytes = stat(path, &st) == 0 ? (long)st.st_size : 0;
      why = compress ? rom_compress(path, new_path, sizeof(new_path), progress)
                     : rom_uncompress(path, new_path, sizeof(new_path), progress);
      if (!why)
      {
        before += bytes;
        after += stat(new_path, &st) == 0 ? (long)st.st_size : 0;
        new_name = strrchr(new_path, '/') ? strrchr(new_path, '/') + 1 : new_path;
        game_list_rename(folder, names[i], new_name);
        if (!strcmp(keep, names[i]))
          snprintf(keep, sizeof(keep), "%.255s", new_name);
        if (running)
        {
          snprintf(gamepak_filename, sizeof(gamepak_filename), "%s", new_path);
          states_open_game(gamepak_filename);
        }
      }
    }
    if (why)
    {
      failed++;
      if (!first_why)
        first_why = why;
    }
    else
      done++;
  }

  refresh();
  for (i = 0; i < rom_count; i++)
    if (!strcmp(roms[i].name, keep))
      selected = i;
  size_text(before, was, sizeof(was));
  size_text(after, now, sizeof(now));
  if (failed)
    snprintf(status, sizeof(status), "%s %d; %d not: %s", compress ? "Compressed" : "Uncompressed", done,
             failed, first_why);
  else
    snprintf(status, sizeof(status), "%s %d: %s before, %s now.", compress ? "Compressed" : "Uncompressed",
             done, was, now);
}

void nspire_rom_manager(void)
{
  const char *slash;

  roms = (struct rom_entry *)malloc(sizeof(*roms) * MAX_ROMS);
  if (!roms)
    return;
  /* The folder in the form "/documents/gba" (the OS gives "\documents\gba\"). */
  if (gamepak_filename[0])
  {
    char game[512];
    nspire_normalize_path(game, sizeof(game), gamepak_filename);
    slash = strrchr(game, '/');
    snprintf(folder, sizeof(folder), "%.*s", !slash ? 1 : slash == game ? 1 : (int)(slash - game),
             slash ? game : ".");
  }
  else
  {
    char cwd[512];
    cwd[0] = 0;
    getcwd(cwd, sizeof(cwd));
    nspire_normalize_path(folder, sizeof(folder), cwd[0] ? cwd : ".");
  }
  selected = top = 0;
  status[0] = 0;
  refresh();
  keys_wait_release();
  for (;;)
  {
    gui_action_type action;

    if (selected < top)
      top = selected;
    if (selected >= top + ROWS)
      top = selected - ROWS + 1;
    draw();
    update_at_vblank();

    action = get_gui_input();
    if (action == CURSOR_BACK || action == CURSOR_EXIT)
      break;
    if (action != CURSOR_NONE)
      status[0] = 0;
    if (action == CURSOR_UP && rom_count)
      selected = (selected + rom_count - 1) % rom_count;
    else if (action == CURSOR_DOWN && rom_count)
      selected = (selected + 1) % rom_count;
    else if (action == CURSOR_SELECT && rom_count)
    {
      marked[selected] = !marked[selected];
      if (selected + 1 < rom_count)
        selected++;
    }
    else if (key_pressed(KEY_C))
      act(1);
    else if (key_pressed(KEY_U))
      act(0);
    else if (key_pressed(KEY_A) && rom_count)
    {
      int all = marked_count() == rom_count;
      memset(marked, all ? 0 : 1, sizeof(marked));
      status[0] = 0;
    }
  }
  keys_wait_release();
  free(roms);
  roms = NULL;
}
