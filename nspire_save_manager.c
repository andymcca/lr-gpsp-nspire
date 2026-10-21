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
#include "nspire_ui_screens.h"

#include <stdio.h>
#include <string.h>

#define ROWS UI_LIST_ROWS
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
static int deleting;   /* the status asks whether to delete */

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

/* The picked state's picture, looked up once it has stayed picked for a
 * sixth of a second. */
static u16 picture[120 * 80];
static int picture_for = -2;     /* the slot whose picture it is */
static int picture_has, picture_waiting = -2;
static u32 picked_at;

static void look_up_picture(int slot)
{
  static u16 big[STATE_PIC_W * STATE_PIC_H];
  if (slot == picture_for)
    return;
  if (slot != picture_waiting)
  {
    picture_waiting = slot;
    picked_at = nspire_ticks();
  }
  if (slot == IN_GAME_SAVE || nspire_ticks() - picked_at >= NSPIRE_TICK_HZ / 6)
  {
    picture_has = slot != IN_GAME_SAVE && states_picture(slot, big);
    if (picture_has)
      ui_scale(picture, 120, 80, big, STATE_PIC_W, STATE_PIC_H, STATE_PIC_W);
    picture_for = slot;
  }
}

static void draw(void)
{
  static const char *const actions[] = { "C", "Compress", "U", "Uncompress", "del", "Delete", NULL };
  static struct ui_manager_view v;
  char bytes[24];
  long total = 0;
  int i, row, marked_n = marked_count();

  memset(&v, 0, sizeof(v));
  v.title = "Save manager";
  v.name = "saves";
  for (i = 0; i < file_count; i++)
    total += files[i].bytes;
  size_text(total, bytes, sizeof(bytes));
  snprintf(v.subtitle, sizeof(v.subtitle), "%d %s, %s", file_count, file_count == 1 ? "file" : "files", bytes);
  v.count = file_count;
  v.first = top;
  v.picked = selected;
  v.marked = marked_n;
  v.empty = "No saves for this game yet.";
  for (row = 0; row < ROWS && top + row < file_count; row++)
  {
    file_label(&files[top + row], v.rows[row].label, sizeof(v.rows[row].label));
    size_text(files[top + row].bytes, v.rows[row].size, sizeof(v.rows[row].size));
    v.rows[row].marked = marked[top + row];
  }

  if (marked_n)
  {
    long sum = 0;
    for (i = 0; i < file_count; i++)
      if (marked[i])
        sum += files[i].bytes;
    v.pic_empty = "";
    snprintf(v.head, sizeof(v.head), "%d marked", marked_n);
    snprintf(v.fact_names[0], sizeof(v.fact_names[0]), "Size");
    size_text(sum, v.facts[0], sizeof(v.facts[0]));
  }
  else if (file_count)
  {
    const struct save_file *f = &files[selected];
    look_up_picture(f->slot);
    v.picture = picture_for == f->slot && picture_has ? picture : NULL;
    v.pic_empty = f->slot == IN_GAME_SAVE ? "The game's own save" : picture_for == f->slot ? "No picture" : "";
    file_label(f, v.head, sizeof(v.head));
    snprintf(v.fact_names[0], sizeof(v.fact_names[0]), "Size");
    size_text(f->bytes, v.facts[0], sizeof(v.facts[0]));
    snprintf(v.fact_names[1], sizeof(v.fact_names[1]), "Stored");
    snprintf(v.facts[1], sizeof(v.facts[1]), "%s", f->compressed ? "Compressed" : "As it is");
  }
  v.actions = actions;
  v.status = status;
  v.warn = deleting;
  v.percent = -1;
  ui_manager_draw(&v);
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
  deleting = 1;
  for (;;)
  {
    gui_action_type action;
    draw();
    update_at_vblank();
    action = get_gui_input();
    if (action == CURSOR_CLEAR)
    {
      status[0] = 0;
      deleting = 0;
      return 1;
    }
    if (action != CURSOR_NONE || (any_key_down() && !key_down(KEY_DEL)))
    {
      snprintf(status, sizeof(status), "Nothing deleted.");
      deleting = 0;
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
  picture_for = picture_waiting = -2;   /* they changed */
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
  picture_for = picture_waiting = -2;
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
