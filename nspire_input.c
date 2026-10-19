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
#define TOUCHPAD_DEFAULT_WIDTH 320u
#define TOUCHPAD_DEFAULT_HEIGHT 240u

static u32 current_keys[NUM_KEY_WORDS];
static u32 new_keys[NUM_KEY_WORDS];
static unsigned touchpad_width = TOUCHPAD_DEFAULT_WIDTH;
static unsigned touchpad_height = TOUCHPAD_DEFAULT_HEIGHT;
static int touchpad_size_checked;
static int swipe_contact_active;
static unsigned short swipe_origin_x, swipe_origin_y;
/* Raw (un-pulsed) swipe directions currently above the deadzone, used during
 * key_capture() so capturing a swipe never drops out mid-duty-cycle, and
 * per-axis weights (0..100) for proportional diagonal duty-cycling in-game. */
static unsigned swipe_raw_directions;
static unsigned swipe_x_weight, swipe_y_weight;
static unsigned swipe_x_accum, swipe_y_accum;
/* Preserve edge-arrow navigation in menus while reserving those zones from
 * GBA gameplay input when swipe mode is enabled. */
static unsigned current_touchpad_arrow;

static void touchpad_get_size(void)
{
  if (!touchpad_size_checked)
  {
    touchpad_info_t *info = touchpad_getinfo();
    if (info && info->width && info->height)
    {
      touchpad_width = info->width;
      touchpad_height = info->height;
    }
    /* The fallback is deliberately 4:3, matching the CX II CAS touchpad. */
    touchpad_size_checked = 1;
  }
}

static void set_virtual_key(u32 *mask, unsigned key)
{
  if (key > 128 && key <= NUM_KEYS)
    *mask |= 1u << ((key - 1) & 31);
}

static u32 touchpad_swipe_keys(const touchpad_report_t *report)
{
  static const unsigned deadzones[SWIPE_DEADZONE_CHOICES] = { 3, 5, 8, 10, 12, 15, 20 };
  static const unsigned sensitivities[SWIPE_SENSITIVITY_CHOICES] = { 50, 75, 100, 125, 175, 150, 200 };
  static const unsigned sens_table[SWIPE_SENSITIVITY_CHOICES] = { 50, 75, 100, 125, 150, 175, 200 };
  unsigned directions, deadzone, sensitivity;
  unsigned x_weight = 0, y_weight = 0;
  int dx, dy;
  u32 mask = 0;

  (void)sensitivities;
  if (!nspire_touchpad_swipe || !report->contact)
  {
    swipe_contact_active = 0;
    swipe_raw_directions = 0;
    swipe_x_weight = 0;
    swipe_y_weight = 0;
    swipe_x_accum = 0;
    swipe_y_accum = 0;
    return 0;
  }

  touchpad_get_size();
  if (!swipe_contact_active)
  {
    swipe_origin_x = report->x;
    swipe_origin_y = report->y;
    swipe_contact_active = 1;
    swipe_raw_directions = 0;
    swipe_x_weight = 0;
    swipe_y_weight = 0;
    swipe_x_accum = 100;
    swipe_y_accum = 100;
    return 0;
  }

  /* CX II coordinates follow its arrow convention: X grows right, Y grows up.
   * Keep the start point for the entire contact, so a swipe is a held gesture
   * until lift-off rather than a one-poll velocity spike. */
  dx = (int)report->x - (int)swipe_origin_x;
  dy = (int)report->y - (int)swipe_origin_y;
  deadzone = deadzones[nspire_touchpad_deadzone % SWIPE_DEADZONE_CHOICES];
  sensitivity = sens_table[nspire_touchpad_sensitivity % SWIPE_SENSITIVITY_CHOICES];

  touchpad_swipe_axis_weights(dx, dy, touchpad_width, touchpad_height,
                              deadzone, sensitivity,
                              &directions, &x_weight, &y_weight);
  swipe_raw_directions = directions;
  swipe_x_weight = x_weight;
  swipe_y_weight = y_weight;

  /* Split any diagonal movement into separate horizontal (LEFT/RIGHT) and
   * vertical (UP/DOWN) inputs, weighted proportionally to how the diagonal
   * movement was made on the 4:3 touchpad via a Bresenham duty-cycle accumulator.
   * If an axis has weight 100 (dominant axis or single-axis swipe), it is active
   * on 100% of frames. If it is the minor axis of a shallow/steep diagonal
   * (e.g. weight 50), it pulses on 50% of frames so the GBA receives a weighted
   * horizontal + vertical vector. */
  if (directions & (SWIPE_DIR_LEFT | SWIPE_DIR_RIGHT))
  {
    int x_active = 1;
    if (x_weight < 100u)
    {
      swipe_x_accum += x_weight;
      if (swipe_x_accum >= 100u)
        swipe_x_accum -= 100u;
      else
        x_active = 0;
    }
    if (x_active)
    {
      if (directions & SWIPE_DIR_LEFT)  set_virtual_key(&mask, KEY_SWIPE_LEFT);
      if (directions & SWIPE_DIR_RIGHT) set_virtual_key(&mask, KEY_SWIPE_RIGHT);
    }
  }
  else
  {
    swipe_x_accum = 100;
  }

  if (directions & (SWIPE_DIR_UP | SWIPE_DIR_DOWN))
  {
    int y_active = 1;
    if (y_weight < 100u)
    {
      swipe_y_accum += y_weight;
      if (swipe_y_accum >= 100u)
        swipe_y_accum -= 100u;
      else
        y_active = 0;
    }
    if (y_active)
    {
      if (directions & SWIPE_DIR_UP)   set_virtual_key(&mask, KEY_SWIPE_UP);
      if (directions & SWIPE_DIR_DOWN) set_virtual_key(&mask, KEY_SWIPE_DOWN);
    }
  }
  else
  {
    swipe_y_accum = 100;
  }

  return mask;
}

void keys_poll(void)
{
  touchpad_report_t report;
  u32 touchpad_pressed = 0;
  unsigned i;

  memset(&report, 0, sizeof(report));
  touchpad_scan(&report);
  current_touchpad_arrow = report.arrow;
  if (nspire_touchpad_swipe)
  {
    /* In swipe mode, touching/pressing an edge is not a directional input.
     * Only relative contact movement creates SW-* keys. Retain the old center
     * click behavior; turning swipe mode off restores the original edge arrows. */
    if (report.arrow == TPAD_ARROW_CLICK)
      touchpad_pressed = 0x00000002;
  }
  else
  {
    switch ((tpad_arrow_t)report.arrow)
    {
    case TPAD_ARROW_UP:         touchpad_pressed = 0x00010000; break;
    case TPAD_ARROW_UPRIGHT:    touchpad_pressed = 0x00050000; break;
    case TPAD_ARROW_RIGHT:      touchpad_pressed = 0x00040000; break;
    case TPAD_ARROW_RIGHTDOWN:  touchpad_pressed = 0x00140000; break;
    case TPAD_ARROW_DOWN:       touchpad_pressed = 0x00100000; break;
    case TPAD_ARROW_DOWNLEFT:   touchpad_pressed = 0x00500000; break;
    case TPAD_ARROW_LEFT:       touchpad_pressed = 0x00400000; break;
    case TPAD_ARROW_LEFTUP:     touchpad_pressed = 0x00410000; break;
    case TPAD_ARROW_CLICK:      touchpad_pressed = 0x00000002; break;
    default:                    touchpad_pressed = 0x00000000; break;
    }
  }

  for (i = 0; i < NUM_KEY_WORDS; i++)
    new_keys[i] = ~current_keys[i];

  current_keys[0] = HW_READ(HW_KEYPAD) | (on_key_pressed() ? 0x200 : 0);
  current_keys[1] = HW_READ(HW_KEYPAD + 4);
  current_keys[2] = HW_READ(HW_KEYPAD + 8);
  current_keys[3] = (HW_READ(HW_KEYPAD + 12) & TOUCHPAD_KEYS_MASK) | touchpad_pressed;
  current_keys[4] = touchpad_swipe_keys(&report);

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
  for (i = 0; i < NUM_KEY_WORDS; i++)
    if (current_keys[i])
      return 1;
  return swipe_contact_active;
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

static int action_held(int action)
{
  return binding_held(key_bindings[action][0]) || binding_held(key_bindings[action][1]);
}

static int key_is_bound(unsigned key)
{
  int i, slot;
  for (i = 0; i < NUM_ACTIONS; i++)
    for (slot = 0; slot < KEY_SLOTS; slot++)
      if (key_bindings[i][slot].key == key || key_bindings[i][slot].with == key)
        return 1;
  for (i = 0; i < NUM_COMBO_SLOTS; i++)
    if (combo_bindings[i].action_mask &&
        (combo_bindings[i].trigger.key == key || combo_bindings[i].trigger.with == key))
      return 1;
  return 0;
}

/* Only suppresses single-key hotkeys when a two-key hotkey combo is held.
 * Gameplay controls and multi-control combos NEVER block each other so you
 * can freely hold multiple calculator keys and swipe at the same time! */
static int key_in_held_hotkey_combo(unsigned key)
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
    if (binding_pressed(b) && (b.with || !key_in_held_hotkey_combo(b.key)))
      return 1;
  }
  return 0;
}

static int key_is_swipe(unsigned key)
{
  return key >= KEY_SWIPE_UP && key <= KEY_SWIPE_RIGHT;
}

/* Returns the dominant single-axis swipe key during key_capture() based on
 * the 4:3-normalized weights so capturing a swipe in the menu always picks
 * the primary direction cleanly. */
static unsigned dominant_swipe_key(void)
{
  if (!swipe_raw_directions)
    return 0;
  if (swipe_x_weight > swipe_y_weight)
  {
    if (swipe_raw_directions & SWIPE_DIR_LEFT)  return KEY_SWIPE_LEFT;
    if (swipe_raw_directions & SWIPE_DIR_RIGHT) return KEY_SWIPE_RIGHT;
  }
  if (swipe_raw_directions & SWIPE_DIR_UP)    return KEY_SWIPE_UP;
  if (swipe_raw_directions & SWIPE_DIR_DOWN)  return KEY_SWIPE_DOWN;
  if (swipe_raw_directions & SWIPE_DIR_LEFT)  return KEY_SWIPE_LEFT;
  if (swipe_raw_directions & SWIPE_DIR_RIGHT) return KEY_SWIPE_RIGHT;
  return 0;
}

static unsigned first_pressed_input(void)
{
  unsigned sw = dominant_swipe_key();
  unsigned key;
  if (sw)
    return sw;
  for (key = 1; key <= NUM_KEYS; key++)
    if (key_pressed(key))
      return key;
  return 0;
}

key_binding key_capture(void)
{
  key_binding b = { 0, 0 };
  unsigned first = 0, key, sw;

  keys_wait_release();
  /* The first key pressed, then either another one while it's held (a
   * combination) or its release (a single key). An arrow or the touchpad
   * click gives up and returns no key. */
  while (!first)
  {
    keys_poll();
    key = first_pressed_input();
    if (key)
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
    sw = dominant_swipe_key();

    if (key_is_swipe(first))
    {
      if (!swipe_contact_active)
      {
        b.key = (unsigned char)first;
        break;
      }
      if (sw)
        first = sw;
    }
    else if (!key_down(first))
    {
      b.key = (unsigned char)first;
      break;
    }

    for (key = 1; key <= NUM_KEYS; key++)
      if (key != first && key_pressed(key) && key_is_bindable(key))
      {
        if (key_is_swipe(first) && key_is_swipe(key))
        {
          if (sw)
            first = sw;
          continue;
        }
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

/* Computes the combined bitmask of GBA actions (bits 0..9 = ACTION_UP..SELECT)
 * activated by the 4 orthogonal touchpad swipe multi-combos and 8 calculator
 * combo slots. Because a diagonal swipe activates both a horizontal SW-* key
 * and a vertical SW-* key (weighted according to the 4:3 diagonal angle),
 * swiping diagonally automatically combines the horizontal swipe's actions
 * and the vertical swipe's actions simultaneously! */
static unsigned short active_multi_action_mask(void)
{
  unsigned short mask = 0;
  int i;

  /* 1. Direct touchpad swipe multi-control mappings (Up, Down, Left, Right).
   * Both horizontal and vertical components can be active at once on a
   * diagonal swipe, weighted by touchpad_swipe_keys(). */
  for (i = 0; i < NUM_SWIPE_DIRS; i++)
    if (key_down(KEY_SWIPE_UP + (unsigned)i))
      mask |= swipe_action_masks[i];

  /* 2. Custom calculator key / key+with / swipe multi-control combo slots.
   * Multiple combo slots and physical keys can be held simultaneously. */
  for (i = 0; i < NUM_COMBO_SLOTS; i++)
  {
    combo_binding c = combo_bindings[i];
    if (c.action_mask && binding_held(c.trigger))
      mask |= c.action_mask;
  }

  return mask;
}

/* Reads the keys for a frame: sets the GBA's buttons and returns the hotkeys
 * just pressed, one bit per action (1 << ACTION_MENU, ...). */
u32 nspire_input_frame(void)
{
  u32 gba_buttons = 0, hotkeys = 0;
  unsigned short multi_mask;
  int up, down, left, right, i;

  keys_poll();
  multi_mask = active_multi_action_mask();

  up    = key_down(KEY_UP)    || action_held(ACTION_UP)    || ((multi_mask & (1u << ACTION_UP)) != 0);
  down  = key_down(KEY_DOWN)  || action_held(ACTION_DOWN)  || ((multi_mask & (1u << ACTION_DOWN)) != 0);
  left  = key_down(KEY_LEFT)  || action_held(ACTION_LEFT)  || ((multi_mask & (1u << ACTION_LEFT)) != 0);
  right = key_down(KEY_RIGHT) || action_held(ACTION_RIGHT) || ((multi_mask & (1u << ACTION_RIGHT)) != 0);
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
  if (action_held(ACTION_A)      || (multi_mask & (1u << ACTION_A)))      gba_buttons |= BUTTON_A;
  if (action_held(ACTION_B)      || (multi_mask & (1u << ACTION_B)))      gba_buttons |= BUTTON_B;
  if (action_held(ACTION_L)      || (multi_mask & (1u << ACTION_L)))      gba_buttons |= BUTTON_L;
  if (action_held(ACTION_R)      || (multi_mask & (1u << ACTION_R)))      gba_buttons |= BUTTON_R;
  if (action_held(ACTION_START)  || (multi_mask & (1u << ACTION_START)))  gba_buttons |= BUTTON_START;
  if (action_held(ACTION_SELECT) || (multi_mask & (1u << ACTION_SELECT))) gba_buttons |= BUTTON_SELECT;

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

static int touchpad_menu_key_down(unsigned key)
{
  if (!nspire_touchpad_swipe)
    return 0;

  switch (key)
  {
  case KEY_UP:
    return current_touchpad_arrow == TPAD_ARROW_UP ||
           current_touchpad_arrow == TPAD_ARROW_UPRIGHT ||
           current_touchpad_arrow == TPAD_ARROW_LEFTUP;
  case KEY_DOWN:
    return current_touchpad_arrow == TPAD_ARROW_DOWN ||
           current_touchpad_arrow == TPAD_ARROW_RIGHTDOWN ||
           current_touchpad_arrow == TPAD_ARROW_DOWNLEFT;
  case KEY_LEFT:
    return current_touchpad_arrow == TPAD_ARROW_LEFT ||
           current_touchpad_arrow == TPAD_ARROW_LEFTUP ||
           current_touchpad_arrow == TPAD_ARROW_DOWNLEFT;
  case KEY_RIGHT:
    return current_touchpad_arrow == TPAD_ARROW_RIGHT ||
           current_touchpad_arrow == TPAD_ARROW_UPRIGHT ||
           current_touchpad_arrow == TPAD_ARROW_RIGHTDOWN;
  default:
    return 0;
  }
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

  if (key_down(KEY_UP) || touchpad_menu_key_down(KEY_UP) ||
      (nspire_gui_digits_move && key_down(KEY_8)))
    direction = CURSOR_UP;
  else if (key_down(KEY_DOWN) || touchpad_menu_key_down(KEY_DOWN) ||
           (nspire_gui_digits_move && (key_down(KEY_5) || key_down(KEY_2))))
    direction = CURSOR_DOWN;
  else if (key_down(KEY_RIGHT) || touchpad_menu_key_down(KEY_RIGHT) ||
           (nspire_gui_digits_move && key_down(KEY_6)))
    direction = CURSOR_RIGHT;
  else if (key_down(KEY_LEFT) || touchpad_menu_key_down(KEY_LEFT) ||
           (nspire_gui_digits_move && key_down(KEY_4)))
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
