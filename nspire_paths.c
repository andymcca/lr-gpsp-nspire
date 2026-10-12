/* Where gpSP's files go: see nspire_paths.h. */

#include "nspire_paths.h"

#include <os.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

char nspire_program_path[512];

/* gpSP's own files that earlier builds kept next to the program, and the
 * BIOS. gpSP's settings files move in too (is_settings_file), so the
 * program's folder has only the program and the games. */
static const char *const program_files[] =
{
  "gpsp_romdir.tns", "gpsp_clock.tns", "gpsp_perf.txt.tns", "gpsp_results.txt.tns", "gba_bios.bin.tns"
};

/* A game's files are its base name and one of these, or its base name, a
 * slot number and ".svs.tns" (a state). */
static const char *const game_suffixes[] = { ".sav.tns", ".sav.gz.tns", ".settings.tns" };
#define NUM_GAME_SUFFIXES (sizeof(game_suffixes) / sizeof(game_suffixes[0]))
#define OLD_ORDER_FILE "gpsp_order.txt.tns"

void nspire_save_file(char *out, size_t size, const char *name)
{
  const char *docs = get_documents_dir();
  size_t len = strlen(docs);

  snprintf(out, size, "%s%s%s%s%s", docs, len && docs[len - 1] == '/' ? "" : "/", SAVE_FOLDER,
           name[0] ? "/" : "", name);
}

void nspire_save_dir_make(void)
{
  char dir[600];
  struct stat st;

  nspire_save_file(dir, sizeof(dir), "");
  if (stat(dir, &st) != 0)
    mkdir(dir, 0755);
}

void nspire_normalize_path(char *out, size_t size, const char *path)
{
  size_t i, len = 0;

  if (!size)
    return;
  for (i = 0; path[i] && len + 1 < size; i++)
  {
    char c = path[i] == '\\' ? '/' : path[i];
    if (c == '/' && len && out[len - 1] == '/')
      continue;
    out[len++] = c;
  }
  while (len > 1 && out[len - 1] == '/')
    len--;
  out[len] = 0;
}

void nspire_order_file(char *out, size_t size, const char *dir)
{
  char path[512], docs[512], name[600];
  const char *rel = NULL;
  size_t docs_len, i;

  nspire_normalize_path(path, sizeof(path), dir);
  nspire_normalize_path(docs, sizeof(docs), get_documents_dir());
  docs_len = strlen(docs);
  if (!strncasecmp(path, docs, docs_len) && (path[docs_len] == 0 || path[docs_len] == '/'))
    rel = path + docs_len;
  else
  {
    /* A path the OS gave in another form ("A:/documents/gba"). */
    const char *p = strstr(path, "/documents");
    if (p && (p[10] == 0 || p[10] == '/'))
      rel = p + 10;
    else
      rel = path;
  }
  while (*rel == '/')
    rel++;
  if (!*rel)
  {
    nspire_save_file(out, size, OLD_ORDER_FILE);
    return;
  }
  snprintf(name, sizeof(name), "gpsp_order_%s.txt.tns", rel);
  for (i = 0; name[i]; i++)
    if (name[i] == '/')
      name[i] = '_';
  nspire_save_file(out, size, name);
}

/* Moves a file unless the new name is taken. */
static void move_file(const char *from, const char *to)
{
  struct stat st;

  if (stat(from, &st) != 0 || stat(to, &st) == 0)
    return;
  rename(from, to);
}

/* The slot part of a state's name after its base ("12.svs.tns"): its
 * length, or 0 if it isn't one. */
static size_t state_tail(const char *tail)
{
  size_t digits = 0;

  while (isdigit((unsigned char)tail[digits]) && digits < 4)
    digits++;
  return digits && !strcasecmp(tail + digits, ".svs.tns") ? digits + 8 : 0;
}

/* The names in a folder that pass the test, each malloc'd, in a malloc'd
 * array; *count gets how many. Gathered before anything moves, so the
 * folder isn't changed while it's read. */
static char **folder_names(const char *dir, int (*wanted)(const char *name, const void *arg),
                           const void *arg, unsigned *count)
{
  char **names = NULL, **grown;
  unsigned capacity = 0;
  struct dirent *entry;
  DIR *d = opendir(dir);

  *count = 0;
  if (!d)
    return NULL;
  while ((entry = readdir(d)) != NULL)
  {
    if (!wanted(entry->d_name, arg))
      continue;
    if (*count == capacity)
    {
      capacity = capacity ? capacity * 2 : 16;
      grown = (char **)realloc(names, capacity * sizeof(*names));
      if (!grown)
        break;
      names = grown;
    }
    names[*count] = strdup(entry->d_name);
    if (names[*count])
      (*count)++;
  }
  closedir(d);
  return names;
}

static void free_names(char **names, unsigned count)
{
  unsigned i;
  for (i = 0; i < count; i++)
    free(names[i]);
  free(names);
}

/* Splits "dir/name" into its folder ("." for none) and name. */
static const char *split_path(const char *path, char *dir, size_t size)
{
  const char *slash = strrchr(path, '/');

  if (!slash)
  {
    snprintf(dir, size, ".");
    return path;
  }
  snprintf(dir, size, "%.*s", slash == path ? 1 : (int)(slash - path), path);
  return slash + 1;
}

static int is_state_of(const char *name, const void *arg)
{
  const char *base = (const char *)arg;
  size_t len = strlen(base);
  return !strncasecmp(name, base, len) && state_tail(name + len);
}

void nspire_move_game_files(const char *from_base, const char *to_base)
{
  char from[700], to[700], dir[600], clean_from[600], clean_to[600];
  const char *name;
  char **states;
  unsigned count, i;

  nspire_normalize_path(clean_from, sizeof(clean_from), from_base);
  nspire_normalize_path(clean_to, sizeof(clean_to), to_base);
  from_base = clean_from;
  to_base = clean_to;
  if (!strcmp(from_base, to_base))
    return;
  for (i = 0; i < NUM_GAME_SUFFIXES; i++)
  {
    snprintf(from, sizeof(from), "%s%s", from_base, game_suffixes[i]);
    snprintf(to, sizeof(to), "%s%s", to_base, game_suffixes[i]);
    move_file(from, to);
  }
  name = split_path(from_base, dir, sizeof(dir));
  states = folder_names(dir, is_state_of, name, &count);
  for (i = 0; i < count; i++)
  {
    snprintf(from, sizeof(from), "%s/%s", dir, states[i]);
    snprintf(to, sizeof(to), "%s%s", to_base, states[i] + strlen(name));
    move_file(from, to);
  }
  free_names(states, count);
}

/* A file name an earlier build gave a GBA game's save, state or settings:
 * its base name (the ROM's name) ends in .gba, .agb or .bin, so other
 * emulators' saves (Game.gbc.sav.tns) stay. */
static int is_old_game_file(const char *name)
{
  size_t len = strlen(name), base_len = 0, i;

  for (i = 0; i < NUM_GAME_SUFFIXES && !base_len; i++)
  {
    size_t n = strlen(game_suffixes[i]);
    if (len > n && !strcasecmp(name + len - n, game_suffixes[i]))
      base_len = len - n;
  }
  if (!base_len && len > 8 && !strcasecmp(name + len - 8, ".svs.tns"))
  {
    base_len = len - 8;
    while (base_len && isdigit((unsigned char)name[base_len - 1]) && len - 8 - base_len < 4)
      base_len--;
    if (base_len == len - 8)
      base_len = 0;
  }
  return base_len > 4 && (!strncasecmp(name + base_len - 4, ".gba", 4) ||
                          !strncasecmp(name + base_len - 4, ".agb", 4) ||
                          !strncasecmp(name + base_len - 4, ".bin", 4));
}

/* A settings file of gpSP's in its folder: this build's before
 * (gpsp_qol.cfg.tns), older builds' (gpsp.cfg.tns, "..cfg.tns" with no game
 * open, and a game's: "Game.gba.cfg.tns", or "Game.cfg.tns" for a game in
 * the folder). Another program's (ndless.cfg.tns) isn't. */
static int is_settings_file(const char *dir, const char *name)
{
  static const char *const rom_exts[] = { ".gba.tns", ".zip.tns", ".gbz.tns" };
  size_t len = strlen(name), base_len;
  char path[700];
  struct stat st;
  unsigned i;

  if (len <= 8 || strcasecmp(name + len - 8, ".cfg.tns"))
    return 0;
  base_len = len - 8;
  if (!strcmp(name, "..cfg.tns") || !strncasecmp(name, "gpsp", 4))
    return 1;
  if (base_len > 4 && (!strncasecmp(name + base_len - 4, ".gba", 4) || !strncasecmp(name + base_len - 4, ".agb", 4)))
    return 1;
  for (i = 0; i < sizeof(rom_exts) / sizeof(rom_exts[0]); i++)
  {
    snprintf(path, sizeof(path), "%s/%.*s%s", dir, (int)base_len, name, rom_exts[i]);
    if (stat(path, &st) == 0)
      return 1;
  }
  return 0;
}

static int is_program_file(const char *dir, const char *name)
{
  unsigned i;
  for (i = 0; i < sizeof(program_files) / sizeof(program_files[0]); i++)
    if (!strcasecmp(name, program_files[i]))
      return 1;
  return is_settings_file(dir, name);
}

struct sweep
{
  const char *dir;
  int with_program_files;
};

static int is_old_file(const char *name, const void *arg)
{
  const struct sweep *sweep = (const struct sweep *)arg;
  return is_old_game_file(name) || !strcmp(name, OLD_ORDER_FILE) ||
         (sweep->with_program_files && is_program_file(sweep->dir, name));
}

void nspire_move_old_files(const char *folder, int with_program_files)
{
  char from[1100], to[1100], dir[600];
  struct sweep sweep;
  char **names;
  unsigned count, i;

  nspire_normalize_path(dir, sizeof(dir), folder);
  sweep.dir = dir;
  sweep.with_program_files = with_program_files;
  names = folder_names(dir, is_old_file, &sweep, &count);
  if (count)
    nspire_save_dir_make();
  for (i = 0; i < count; i++)
  {
    snprintf(from, sizeof(from), "%s%s%s", dir, !strcmp(dir, "/") ? "" : "/", names[i]);
    if (!strcmp(names[i], OLD_ORDER_FILE))
      nspire_order_file(to, sizeof(to), dir);
    else
      nspire_save_file(to, sizeof(to), names[i]);
    move_file(from, to);
  }
  free_names(names, count);
}

static int is_any_file(const char *name, const void *arg)
{
  (void)arg;
  return strcmp(name, ".") && strcmp(name, "..");
}

static int is_documents_settings_file(const char *name, const void *arg)
{
  return is_settings_file((const char *)arg, name);
}

/* Each name in from_dir moved to the same name in the saves folder. */
static void move_names_in(const char *from_dir, char **names, unsigned count)
{
  char from[1100], to[1100];
  unsigned i;

  if (count)
    nspire_save_dir_make();
  for (i = 0; i < count; i++)
  {
    snprintf(from, sizeof(from), "%s%s%s", from_dir, !strcmp(from_dir, "/") ? "" : "/", names[i]);
    nspire_save_file(to, sizeof(to), names[i]);
    move_file(from, to);
  }
  free_names(names, count);
}

void nspire_move_old_documents_files(void)
{
  char docs[600], old_dir[700];
  char **names;
  unsigned count;
  struct stat st;

  nspire_normalize_path(docs, sizeof(docs), get_documents_dir());
  snprintf(old_dir, sizeof(old_dir), "%s%s%s", docs, !strcmp(docs, "/") ? "" : "/", OLD_SAVE_FOLDER);
  if (stat(old_dir, &st) == 0)
  {
    names = folder_names(old_dir, is_any_file, NULL, &count);
    move_names_in(old_dir, names, count);
    rmdir(old_dir);   /* only goes if everything moved */
  }
  names = folder_names(docs, is_documents_settings_file, docs, &count);
  move_names_in(docs, names, count);
}
