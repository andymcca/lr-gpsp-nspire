/* The save manager (main menu > Save manager), as in PocketSNES: the game's
 * in-game save and states with their sizes, compressed or not. enter marks
 * files (A: all); C, U and del compress, uncompress or delete the marked
 * ones, or else the one picked. Deleting asks first. */

#include "common.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"
#include "nspire_states.h"

#include <stdio.h>
#include <string.h>

/* gpSP's menu colors (gui.c's color16) */
#define MENU_COLOR(r, g, b) ((u16)((((g) & 1) << 15) | ((b) << 10) | (((g) >> 1) << 5) | (r)))
#define COLOR_BACK     MENU_COLOR(2, 8, 10)
#define COLOR_ACTIVE   MENU_COLOR(31, 63, 31)
#define COLOR_INACTIVE MENU_COLOR(13, 40, 18)
#define COLOR_HELP     MENU_COLOR(16, 40, 24)
#define COLOR_MARKED   MENU_COLOR(31, 50, 4)

#define ROWS 19          /* lines 20 to 200 */
#define IN_GAME_SAVE -1  /* the in-game save's "slot" */

struct save_file
{
  int slot;
  long bytes;
  int compressed;
};

static struct save_file files[STATE_SLOTS + 1];
static unsigned char marked[STATE_SLOTS + 1];
static int file_count, selected, top;
static char status[100];

static void refresh(void)
{
  int slot;

  file_count = 0;
  if (nspire_backup_info(&files[0].bytes, &files[0].compressed))
    files[file_count++].slot = IN_GAME_SAVE;
  for (slot = 0; slot < STATE_SLOTS; slot++)
    if (states_file_info(slot, &files[file_count].bytes, &files[file_count].compressed))
      files[file_count++].slot = slot;
  memset(marked, 0, sizeof(marked));
  if (selected >= file_count)
    selected = file_count ? file_count - 1 : 0;
}

static int marked_count(void)
{
  int i, n = 0;
  for (i = 0; i < file_count; i++)
    n += marked[i];
  return n;
}

static void size_text(long bytes, char *text, size_t size)
{
  if (bytes < 1024)
    snprintf(text, size, "%ld B", bytes);
  else
    snprintf(text, size, "%ld KB", (bytes + 1023) / 1024);
}

static void file_label(const struct save_file *f, char *text, size_t size)
{
  if (f->slot == IN_GAME_SAVE)
    snprintf(text, size, "In-game save");
  else
    snprintf(text, size, "Slot %03d", f->slot);
}

static void draw(void)
{
  char line[100], label[32], bytes[24];
  long total = 0;
  int i, row;

  for (i = 0; i < file_count; i++)
    total += files[i].bytes;
  size_text(total, bytes, sizeof(bytes));
  clear_screen(COLOR_BACK);
  snprintf(line, sizeof(line), "Save manager: %d %s, %s", file_count, file_count == 1 ? "file" : "files",
           bytes);
  print_string(line, COLOR_ACTIVE, COLOR_BACK, 0, 0);

  if (!file_count)
    print_string("No saves for this game yet.", COLOR_INACTIVE, COLOR_BACK, 10, 20);
  for (row = 0; row < ROWS && top + row < file_count; row++)
  {
    const struct save_file *f = &files[top + row];
    u16 color = top + row == selected ? COLOR_ACTIVE : marked[top + row] ? COLOR_MARKED : COLOR_INACTIVE;
    file_label(f, label, sizeof(label));
    size_text(f->bytes, bytes, sizeof(bytes));
    snprintf(line, sizeof(line), "%c %-14s %7s  %s", marked[top + row] ? '*' : ' ', label, bytes,
             f->compressed ? "compressed" : "not compressed");
    print_string(line, color, COLOR_BACK, 10, 20 + row * 10);
  }

  if (status[0])
    print_string(status, COLOR_HELP, COLOR_BACK, 10, 220);
  else
    print_string("enter: mark  A: all  C: compress  U: uncompress", COLOR_HELP, COLOR_BACK, 10, 220);
  print_string("del: delete  esc: back", COLOR_HELP, COLOR_BACK, 10, 230);
  flip_screen();
}

/* The marked files, or the one picked, are the ones acted on. */
static int is_target(int i, int targets)
{
  return targets ? marked[i] : i == selected;
}

/* Deleting asks first: del again deletes, any other key doesn't. */
static int confirm_delete(int targets)
{
  char label[32];
  int i;

  if (targets > 1)
    snprintf(status, sizeof(status), "Delete these %d? del: yes, other keys: no", targets);
  else
  {
    for (i = 0; !is_target(i, targets); i++)
      ;
    file_label(&files[i], label, sizeof(label));
    snprintf(status, sizeof(status), "Delete %s? del: yes, other keys: no", label);
  }
  keys_wait_release();
  for (;;)
  {
    gui_action_type action;
    draw();
    update_at_vblank();
    action = get_gui_input();
    if (action == CURSOR_CLEAR)
    {
      status[0] = 0;
      return 1;
    }
    if (action != CURSOR_NONE || (any_key_down() && !key_down(KEY_DEL)))
    {
      snprintf(status, sizeof(status), "Nothing deleted.");
      keys_wait_release();
      return 0;
    }
  }
}

static void act(int what)   /* 'c' compress, 'u' uncompress, 'd' delete */
{
  static int slots[STATE_SLOTS + 1];
  int targets = marked_count(), count = 0, done = 0, failed = 0, i, k;
  long before = 0, after = 0;
  char was[24], now[24];
  const char *verb;

  if (!file_count || (what == 'd' && !confirm_delete(targets)))
    return;
  for (i = 0; i < file_count; i++)
  {
    int ok, slot = files[i].slot;
    if (!is_target(i, targets))
      continue;
    before += files[i].bytes;
    if (what == 'd')
      ok = slot == IN_GAME_SAVE ? nspire_backup_delete() : states_delete(slot);
    else
      ok = slot == IN_GAME_SAVE ? nspire_backup_set_compressed(what == 'c')
                                : states_set_compressed(slot, what == 'c');
    if (ok)
      done++;
    else
      failed++;
    slots[count++] = slot;
  }
  refresh();
  for (i = 0; i < file_count; i++)
    for (k = 0; k < count; k++)
      if (files[i].slot == slots[k])
        after += files[i].bytes;

  size_text(before, was, sizeof(was));
  size_text(after, now, sizeof(now));
  verb = what == 'c' ? "Compressed" : what == 'u' ? "Uncompressed" : "Deleted";
  if (failed)
    snprintf(status, sizeof(status), "%s %d; %d couldn't be (space?)", verb, done, failed);
  else if (what == 'd')
    snprintf(status, sizeof(status), "Deleted %d (%s).", done, was);
  else
    snprintf(status, sizeof(status), "%s %d: %s before, %s now.", verb, done, was, now);
}

void nspire_save_manager(void)
{
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
    if (action == CURSOR_UP && file_count)
      selected = (selected + file_count - 1) % file_count;
    else if (action == CURSOR_DOWN && file_count)
      selected = (selected + 1) % file_count;
    else if (action == CURSOR_SELECT && file_count)
    {
      marked[selected] = !marked[selected];
      if (selected + 1 < file_count)
        selected++;
    }
    else if (action == CURSOR_CLEAR)
      act('d');
    else if (key_pressed(KEY_C))
      act('c');
    else if (key_pressed(KEY_U))
      act('u');
    else if (key_pressed(KEY_A) && file_count)
    {
      int all = marked_count() == file_count;
      memset(marked, all ? 0 : 1, sizeof(marked));
      status[0] = 0;
    }
  }
  keys_wait_release();
}
