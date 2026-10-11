/* The file list's order: see nspire_game_list.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nspire_game_list.h"

static int compare_names(const void *a, const void *b)
{
  const char *x = *(char *const *)a, *y = *(char *const *)b;
  int x_up = !strcmp(x, ".."), y_up = !strcmp(y, "..");

  if (x_up || y_up)
    return y_up - x_up;
  return strcasecmp(x, y);
}

void game_list_sort(char **names, unsigned count)
{
  if (count > 1)
    qsort(names, count, sizeof(char *), compare_names);
}

void game_list_move(char **names, unsigned count, unsigned from, unsigned to)
{
  char *name;
  unsigned i;

  /* (The bound on count lets GCC see the copies stay small.) */
  if (count > 65536 || from >= count || to >= count || from == to)
    return;
  name = names[from];
  for (i = from; i < to; i++)
    names[i] = names[i + 1];
  for (i = from; i > to; i--)
    names[i] = names[i - 1];
  names[to] = name;
}

static void order_path(char *path, size_t size, const char *dir)
{
  size_t len = strlen(dir);
  snprintf(path, size, "%s%s%s", dir, len && dir[len - 1] == '/' ? "" : "/", GAME_ORDER_FILE);
}

int game_list_apply_order(char **names, unsigned count, const char *dir)
{
  char path[1100], line[512];
  unsigned placed = 0, i;
  FILE *f;

  order_path(path, sizeof(path), dir);
  f = fopen(path, "r");
  if (!f)
    return 0;
  while (placed < count && fgets(line, sizeof(line), f))
  {
    line[strcspn(line, "\r\n")] = 0;
    for (i = placed; i < count; i++)
      if (!strcmp(names[i], line))
      {
        game_list_move(names, count, i, placed++);
        break;
      }
  }
  fclose(f);
  return 1;
}

int game_list_save_order(char **names, unsigned count, const char *dir)
{
  char path[1100];
  unsigned i;
  int ok;
  FILE *f;

  order_path(path, sizeof(path), dir);
  f = fopen(path, "w");
  if (!f)
    return 0;
  ok = 1;
  for (i = 0; i < count; i++)
    if (fprintf(f, "%s\n", names[i]) < 0)
      ok = 0;
  if (fclose(f) != 0)
    ok = 0;
  return ok;
}

void game_list_forget_order(const char *dir)
{
  char path[1100];
  order_path(path, sizeof(path), dir);
  remove(path);
}
