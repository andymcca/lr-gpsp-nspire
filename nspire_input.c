/* Ndless keypad input for libretro gpSP core (replaces libretro/input.c):
 * the keypad, key bindings (nspire_keys.h), the GBA buttons and hotkeys of a
 * frame, and menu input with key repeat. */

#include "common.h"
#include "input.h"
#include "serial.h"
#include "gba_memory.h"
#include "gui.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_keys.h"
#include "nspire_hw.h"
#include "nspire_brightness.h"

#include <os.h>

bool libretro_supports_bitmasks    = false;
bool libretro_supports_ff_override = false;
bool libretro_ff_enabled           = false;
bool libretro_ff_enabled_prev      = false;

unsigned turbo_period      = TURBO_PERIOD_MIN;
unsigned turbo_pulse_width = TURBO_PULSE_WIDTH_MIN;
unsigned turbo_a_counter   = 0;
unsigned turbo_b_counter   = 0;

u32 global_enable_analog = 0;
u32 analog_sensitivity_level = 4;

static u32 old_key = 0;

void retro_set_input_state(retro_input_state_t cb) { (void)cb; }

void set_fastforward_override(bool fastforward) { (void)fastforward; }

static void trigger_key(u32 key)
{
  u32 p1_cnt = read_ioreg(REG_P1CNT);

  if ((p1_cnt >> 14) & 0x01)
  {
    u32 key_intersection = (p1_cnt & key) & 0x3FF;

    if (p1_cnt >> 15)
    {
      if (key_intersection == (p1_cnt & 0x3FF))
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
    else
    {
      if (key_intersection)
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
  }
}

/* ---- The keypad ---- */

#define TOUCHPAD_KEYS_MASK 0xFF00FFFD

static u32 current_keys[NUM_KEY_WORDS];
static u32 new_keys[NUM_KEY_WORDS];
static int swipe_contact;
static unsigned touchpad_width = 320;
static unsigned touchpad_height = 240;
static int touchpad_size_known;

static void touchpad_size(void)
{
  touchpad_info_t *info;

  if (touchpad_size_known)
    return;
  info = touchpad_getinfo();
  if (info && info->width && info->height)
  {
    touchpad_width = info->width;
    touchpad_height = info->height;
  }
  touchpad_size_known = 1;
}

/* Bits of current_keys[4] for the four swipe keys. The first poll of a
 * contact only records where the finger landed. */
static u32 swipe_key_bits(const touchpad_report_t *report)
{
  static const unsigned deadzones[SWIPE_DEADZONE_CHOICES] = { 3, 5, 8, 10, 12, 15, 20 };
  static const unsigned sensitivities[SWIPE_SENSITIVITY_CHOICES] =
    { 50, 75, 100, 125, 150, 175, 200 };
  static const unsigned swipe_keys[NUM_SWIPE_DIRS] =
    { KEY_SWIPE_UP, KEY_SWIPE_DOWN, KEY_SWIPE_LEFT, KEY_SWIPE_RIGHT };
  static unsigned short origin_x, origin_y;
  static int have_origin;
  unsigned dirs, deadzone, sensitivity, i;
  int dx, dy;
  u32 bits = 0;

  if (!nspire_touchpad_swipe || !report->contact)
  {
    swipe_contact = 0;
    have_origin = 0;
    return 0;
  }

  swipe_contact = 1;
  if (!have_origin)
  {
    origin_x = report->x;
    origin_y = report->y;
    have_origin = 1;
    return 0;
  }

  touchpad_size();
  dx = (int)report->x - (int)origin_x;
  dy = (int)report->y - (int)origin_y;
  deadzone = deadzones[nspire_touchpad_deadzone % SWIPE_DEADZONE_CHOICES];
  sensitivity = sensitivities[nspire_touchpad_sensitivity % SWIPE_SENSITIVITY_CHOICES];
  touchpad_swipe_directions(dx, dy, touchpad_width, touchpad_height,
                            deadzone, sensitivity, &dirs);
  for (i = 0; i < NUM_SWIPE_DIRS; i++)
    if (dirs & (1u << i))
      bits |= 1u << ((swipe_keys[i] - 1) & 31);
  return bits;
}

static u32 touchpad_arrow_bits(const touchpad_report_t *report)
{
  switch ((tpad_arrow_t)report->arrow)
  {
  case TPAD_ARROW_UP:        return 0x00010000;
  case TPAD_ARROW_UPRIGHT:   return 0x00050000;
  case TPAD_ARROW_RIGHT:     return 0x00040000;
  case TPAD_ARROW_RIGHTDOWN: return 0x00140000;
  case TPAD_ARROW_DOWN:      return 0x00100000;
  case TPAD_ARROW_DOWNLEFT:  return 0x00500000;
  case TPAD_ARROW_LEFT:      return 0x00400000;
  case TPAD_ARROW_LEFTUP:    return 0x00410000;
  case TPAD_ARROW_CLICK:     return 0x00000002;
  default:                   return 0;
  }
}

void keys_poll(void)
{
  touchpad_report_t report;
  u32 touchpad_pressed;
  unsigned i;

  memset(&report, 0, sizeof(report));
  touchpad_scan(&report);
  if (nspire_touchpad_swipe)
  {
    /* Edge arrows are not directions in swipe mode. The center click stays. */
    touchpad_pressed = report.arrow == TPAD_ARROW_CLICK ? 0x00000002 : 0;
  }
  else
  {
    swipe_contact = 0;
    touchpad_pressed = touchpad_arrow_bits(&report);
  }

  for (i = 0; i < NUM_KEY_WORDS; i++)
    new_keys[i] = ~current_keys[i];

  current_keys[0] = HW_READ(HW_KEYPAD) | (on_key_pressed() ? 0x200 : 0);
  current_keys[1] = HW_READ(HW_KEYPAD + 4);
  current_keys[2] = HW_READ(HW_KEYPAD + 8);
  current_keys[3] = (HW_READ(HW_KEYPAD + 12) & TOUCHPAD_KEYS_MASK) | touchpad_pressed;
  current_keys[4] = swipe_key_bits(&report);

  for (i = 0; i < NUM_KEY_WORDS; i++)
    new_keys[i] &= current_keys[i];
}

int key_down(unsigned key)
{
  return key && key <= NUM_KEYS && (current_keys[(key - 1) >> 5] >> ((key - 1) & 0x1F)) & 1;
}

int key_pressed(unsigned key)
{
  return key && key <= NUM_KEYS && (new_keys[(key - 1) >> 5] >> ((key - 1) & 0x1F)) & 1;
}

int any_key_down(void)
{
  unsigned i;
  u32 bits = 0;

  for (i = 0; i < NUM_KEY_WORDS; i++)
    bits |= current_keys[i];
  return bits || swipe_contact;
}

void keys_wait_release(void)
{
  do
  {
    keys_poll();
    nspire_wait_until(nspire_ticks() + NSPIRE_TICK_HZ / 100);
  } while (any_key_down());
}

int binding_held(key_binding b)
{
  return b.key && key_down(b.key) && (!b.with || key_down(b.with));
}

static int binding_pressed(key_binding b)
{
  return binding_held(b) && (key_pressed(b.key) || (b.with && key_pressed(b.with)));
}

/* A swipe list or a held combo replaces the ordinary binding of its trigger
 * for this poll, so the same input is not applied twice. */
static int binding_replaced(key_binding b)
{
  int i;

  if (!b.key)
    return 0;
  if (b.key >= KEY_SWIPE_UP && b.key <= KEY_SWIPE_RIGHT &&
      swipe_action_masks[b.key - KEY_SWIPE_UP] && key_down(b.key))
    return 1;
  if (b.with >= KEY_SWIPE_UP && b.with <= KEY_SWIPE_RIGHT &&
      swipe_action_masks[b.with - KEY_SWIPE_UP] && key_down(b.with))
    return 1;
  for (i = 0; i < NUM_COMBO_SLOTS; i++)
  {
    combo_binding c = combo_bindings[i];
    if (!c.action_mask || !binding_held(c.trigger))
      continue;
    if (c.trigger.key == b.key || c.trigger.key == b.with ||
        (c.trigger.with && (c.trigger.with == b.key || c.trigger.with == b.with)))
      return 1;
  }
  return 0;
}

static unsigned short held_gba_mask(void)
{
  unsigned short mask = 0;
  int i;

  for (i = 0; i < NUM_SWIPE_DIRS; i++)
    if (swipe_action_masks[i] && key_down(KEY_SWIPE_UP + (unsigned)i))
      mask |= swipe_action_masks[i];
  for (i = 0; i < NUM_COMBO_SLOTS; i++)
    if (combo_bindings[i].action_mask && binding_held(combo_bindings[i].trigger))
      mask |= combo_bindings[i].action_mask;
  return mask & (unsigned short)((1u << NUM_GBA_ACTIONS) - 1u);
}

static int action_held(int action)
{
  int slot;

  for (slot = 0; slot < KEY_SLOTS; slot++)
    if (!binding_replaced(key_bindings[action][slot]) &&
        binding_held(key_bindings[action][slot]))
      return 1;
  return 0;
}

static int key_is_bound(unsigned key)
{
  int i, slot;
  for (i = 0; i < NUM_ACTIONS; i++)
    for (slot = 0; slot < KEY_SLOTS; slot++)
      if (key_bindings[i][slot].key == key || key_bindings[i][slot].with == key)
        return 1;
  return 0;
}

static int key_in_held_combo(unsigned key)
{
  int i, slot;
  for (i = FIRST_HOTKEY; i < NUM_ACTIONS; i++)
    for (slot = 0; slot < KEY_SLOTS; slot++)
    {
      key_binding b = key_bindings[i][slot];
      if (b.with && (b.key == key || b.with == key) && binding_held(b))
        return 1;
    }
  return 0;
}

int hotkey_pressed(int action)
{
  int slot;
  for (slot = 0; slot < KEY_SLOTS; slot++)
  {
    key_binding b = key_bindings[action][slot];
    if (binding_replaced(b))
      continue;
    if (binding_pressed(b) && (b.with || !key_in_held_combo(b.key)))
      return 1;
  }
  return 0;
}

key_binding key_capture(void)
{
  key_binding b = { 0, 0 };
  unsigned first = 0, key;

  keys_wait_release();
  /* The first key pressed, then either another one while it's held (a
   * combination) or its release (a single key). An arrow or the touchpad
   * click gives up and returns no key. */
  while (!first)
  {
    keys_poll();
    for (key = 1; key <= NUM_KEYS && !first; key++)
      if (key_pressed(key))
      {
        if (key_is_reserved(key))
        {
          keys_wait_release();
          return b;
        }
        if (key_is_bindable(key))
          first = key;
      }
  }
  for (;;)
  {
    keys_poll();
    if (!key_down(first))
    {
      b.key = (unsigned char)first;
      break;
    }
    for (key = 1; key <= NUM_KEYS; key++)
      if (key != first && key_pressed(key) && key_is_bindable(key))
      {
        b.with = (unsigned char)first;
        b.key = (unsigned char)key;
        break;
      }
    if (b.key)
      break;
  }
  keys_wait_release();
  return b;
}

/* ---- A frame of the game ---- */

/* Reads the keys for a frame: sets the GBA's buttons and returns the hotkeys
 * just pressed, one bit per action (1 << ACTION_MENU, ...). */
u32 nspire_input_frame(void)
{
  u32 gba_buttons = 0, hotkeys = 0;
  unsigned short multi;
  int up, down, left, right, i;

  keys_poll();
  multi = held_gba_mask();

  up = key_down(KEY_UP) || action_held(ACTION_UP) || (multi & (1u << ACTION_UP));
  down = key_down(KEY_DOWN) || action_held(ACTION_DOWN) || (multi & (1u << ACTION_DOWN));
  left = key_down(KEY_LEFT) || action_held(ACTION_LEFT) || (multi & (1u << ACTION_LEFT));
  right = key_down(KEY_RIGHT) || action_held(ACTION_RIGHT) || (multi & (1u << ACTION_RIGHT));
  /* The number pad's corners are diagonals, unless bound to something. */
  if (key_down(KEY_7) && !key_is_bound(KEY_7)) up = left = 1;
  if (key_down(KEY_9) && !key_is_bound(KEY_9)) up = right = 1;
  if (key_down(KEY_1) && !key_is_bound(KEY_1)) down = left = 1;
  if (key_down(KEY_3) && !key_is_bound(KEY_3)) down = right = 1;
  if (up && down)
    up = down = 0;
  if (left && right)
    left = right = 0;

  if (up)    gba_buttons |= BUTTON_UP;
  if (down)  gba_buttons |= BUTTON_DOWN;
  if (left)  gba_buttons |= BUTTON_LEFT;
  if (right) gba_buttons |= BUTTON_RIGHT;
  if (action_held(ACTION_A) || (multi & (1u << ACTION_A)))           gba_buttons |= BUTTON_A;
  if (action_held(ACTION_B) || (multi & (1u << ACTION_B)))           gba_buttons |= BUTTON_B;
  if (action_held(ACTION_L) || (multi & (1u << ACTION_L)))           gba_buttons |= BUTTON_L;
  if (action_held(ACTION_R) || (multi & (1u << ACTION_R)))           gba_buttons |= BUTTON_R;
  if (action_held(ACTION_START) || (multi & (1u << ACTION_START)))   gba_buttons |= BUTTON_START;
  if (action_held(ACTION_SELECT) || (multi & (1u << ACTION_SELECT))) gba_buttons |= BUTTON_SELECT;

  if (serial_mode == SERIAL_MODE_GBP)
  {
    if (frame_counter > 20 && frame_counter < 100)
      gba_buttons = (frame_counter % 3) ? 0x3FF : 0x30F;
  }

  if ((gba_buttons | old_key) != old_key)
    trigger_key(gba_buttons);
  old_key = gba_buttons;
  write_ioreg(REG_P1, (~old_key) & 0x3FF);

  for (i = FIRST_HOTKEY; i < NUM_ACTIONS; i++)
    if (hotkey_pressed(i))
      hotkeys |= 1u << i;
  /* Without a key for the menu, the menu key opens it, so it can't get lost. */
  if (!key_bindings[ACTION_MENU][0].key && !key_bindings[ACTION_MENU][1].key &&
      key_pressed(KEY_MENU))
    hotkeys |= 1u << ACTION_MENU;
  return hotkeys;
}

/* The buttons the game sees while it's paused (menus, loading). */
void nspire_input_release_all(void)
{
  old_key = 0;
  write_ioreg(REG_P1, 0x3FF);
}

void init_input(void) {}

bool input_check_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  return (p && bson_contains_key(p, "prevkey", BSON_TYPE_INT32));
}

bool input_read_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  if (p)
    return bson_read_int32(p, "prevkey", &old_key);
  return false;
}

unsigned input_write_savestate(u8 *dst)
{
  u8 *wbptr1, *startp = dst;
  bson_start_document(dst, "input", wbptr1);
  bson_write_int32(dst, "prevkey", old_key);
  bson_finish_document(dst, wbptr1);
  return (unsigned int)(dst - startp);
}

/* ---- Menus ---- */

static gui_action_type held_direction = CURSOR_NONE;
static u32 repeat_at;
static unsigned repeats;

int nspire_gui_digits_move = 1;

int nspire_digit_pressed(void)
{
  static const unsigned char digits[10] =
    { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0 };
  int i;

  for (i = 0; i < 10; i++)
    if (key_pressed(digits[i]))
      return i;
  return -1;
}

unsigned gui_repeat_count(void)
{
  return repeats;
}

/* Arrows (or 8 2 5 4 6, except in the file list) move and repeat while
 * held: after 0.4 s, 15 times a second. The brightness keys work here too. */
gui_action_type get_gui_input(void)
{
  gui_action_type direction = CURSOR_NONE;
  u32 now;

  keys_poll();

  if (hotkey_pressed(ACTION_BRIGHTER))
    nspire_brightness_step(1);
  if (hotkey_pressed(ACTION_DIMMER))
    nspire_brightness_step(-1);

  if (key_pressed(KEY_CLICK) || key_pressed(KEY_ENTER))
    return CURSOR_SELECT;
  if (key_pressed(KEY_ESC))
    return CURSOR_BACK;
  if (key_pressed(KEY_MENU) || hotkey_pressed(ACTION_MENU))
    return CURSOR_EXIT;
  if (key_pressed(KEY_DEL))
    return CURSOR_CLEAR;

  if (key_down(KEY_UP) || (nspire_gui_digits_move && key_down(KEY_8)))
    direction = CURSOR_UP;
  else if (key_down(KEY_DOWN) || (nspire_gui_digits_move && (key_down(KEY_5) || key_down(KEY_2))))
    direction = CURSOR_DOWN;
  else if (key_down(KEY_RIGHT) || (nspire_gui_digits_move && key_down(KEY_6)))
    direction = CURSOR_RIGHT;
  else if (key_down(KEY_LEFT) || (nspire_gui_digits_move && key_down(KEY_4)))
    direction = CURSOR_LEFT;

  now = nspire_ticks();
  if (direction != held_direction)
  {
    held_direction = direction;
    repeat_at = now + NSPIRE_TICK_HZ * 2 / 5;
    repeats = 0;
    return direction;
  }
  if (direction != CURSOR_NONE && (s32)(now - repeat_at) >= 0)
  {
    repeat_at = now + NSPIRE_TICK_HZ / 15;
    repeats++;
    return direction;
  }
  return CURSOR_NONE;
}
