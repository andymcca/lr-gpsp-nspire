/* Key names and bindings (nspire_keys.h); the keypad itself is read in
 * nspire_input.c. */

#include <stdio.h>
#include <string.h>

#include "nspire_keys.h"

/* gpSP's names, by key number (1 + the bit in the keypad registers). The
 * arrows (113-120) have none: they always move. */
char *nspire_key_strings[NUM_KEYS + 1] =
{
    "NONE",
	"return", "enter", NULL, "(-)", "space", "Z", "Y", "0", "?!", "home", NULL, NULL, NULL, NULL, NULL, NULL,
    "X", "W", "V", "3", "U", "T", "S", "1", "pi", "trig", "10^x", NULL, NULL, NULL, NULL, NULL,
    "R", "Q", "P", "6", "O", "N", "M", "4", "EE", "x^2", NULL, NULL, NULL, NULL, NULL, NULL,
    "L", "K", "J", "9", "I", "H", "G", "7", "/", "e^x", NULL, NULL, NULL, NULL, NULL, NULL,
    "F", "E", "D", NULL, "C", "B", "A", "=", "*", "^", NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, "var", "-", ")", ".", "(", "5", "cat", "frac", "del", "scratch", NULL, NULL, NULL, NULL, NULL,
    "flag", "click", "+", "doc", "2", "menu", "8", "esc", NULL, "tab", NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "shift", "ctrl", ",", NULL, NULL, NULL, NULL, NULL,
    [KEY_SWIPE_UP] = "SW-UP",
    [KEY_SWIPE_DOWN] = "SW-DOWN",
    [KEY_SWIPE_LEFT] = "SW-LEFT",
    [KEY_SWIPE_RIGHT] = "SW-RIGHT",
    [KEY_SWIPE_UP_LEFT] = "SW-UPLEFT",
    [KEY_SWIPE_UP_RIGHT] = "SW-UPRIGHT",
    [KEY_SWIPE_DOWN_LEFT] = "SW-DNLEFT",
    [KEY_SWIPE_DOWN_RIGHT] = "SW-DNRIGHT"
};

/* ---- Bindings ---- */

key_binding key_bindings[NUM_ACTIONS][KEY_SLOTS];
unsigned short swipe_action_masks[NUM_SWIPE_DIRS];
combo_binding combo_bindings[NUM_COMBO_SLOTS];

/* Touchpad swipe is enabled by default. An 8% dead zone, 100% sensitivity,
 * and weighted 4:3 diagonal splitting are enabled by default. */
unsigned nspire_touchpad_swipe = 1;
unsigned nspire_touchpad_deadzone = 2;
unsigned nspire_touchpad_sensitivity = 2;
unsigned nspire_touchpad_diagonal_weight = 1;

const char *const action_names[NUM_ACTIONS] =
{
  "D-pad up", "D-pad down", "D-pad left", "D-pad right",
  "A", "B", "L", "R", "Start", "Select",
  "Menu", "Fast forward", "Load state", "Save state", "Save+Exit",
  "Load newest state", "Next slot", "Previous slot", "FPS counter", "Restart game",
  "Brightness up", "Brightness down"
};

const char *const swipe_dir_names[NUM_SWIPE_DIRS] =
{
  "Swipe up",
  "Swipe down",
  "Swipe left",
  "Swipe right"
};

static const char *const short_gba_names[NUM_GBA_ACTIONS] =
{
  "Up", "Dn", "Lt", "Rt", "A", "B", "L", "R", "St", "Sl"
};

/* gpSP's layout, plus the new hotkeys, which start without a key except
 * the slots, ) next and ( previous as in PocketSNES, and brightness, + and
 * - (as the OS has them with ctrl). */
static const unsigned char default_keys[NUM_ACTIONS] =
{
  KEY_8, KEY_5, KEY_4, KEY_6, KEY_DEL, KEY_VAR, KEY_SCRATCH, KEY_DOC, KEY_SHIFT, KEY_CTRL,
  KEY_MENU, KEY_F, KEY_L, KEY_S, KEY_Q,
  0, KEY_RPAREN, KEY_LPAREN, 0, 0,
  KEY_PLUS, KEY_MINUS
};

void combos_set_defaults(unsigned short swipes[NUM_SWIPE_DIRS],
                         combo_binding combos[NUM_COMBO_SLOTS])
{
  if (swipes)
  {
    memset(swipes, 0, sizeof(unsigned short) * NUM_SWIPE_DIRS);
    swipes[0] = (1u << ACTION_UP);
    swipes[1] = (1u << ACTION_DOWN);
    swipes[2] = (1u << ACTION_LEFT);
    swipes[3] = (1u << ACTION_RIGHT);
  }
  if (combos)
    memset(combos, 0, sizeof(combo_binding) * NUM_COMBO_SLOTS);
}

void keys_set_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS])
{
  int i;
  memset(bindings, 0, sizeof(key_binding) * NUM_ACTIONS * KEY_SLOTS);
  for (i = 0; i < NUM_ACTIONS; i++)
    bindings[i][0].key = default_keys[i];

  /* Normal calculator keys stay in slot 0; swipe components are in slot 1. */
  bindings[ACTION_UP][1].key = KEY_SWIPE_UP;
  bindings[ACTION_DOWN][1].key = KEY_SWIPE_DOWN;
  bindings[ACTION_LEFT][1].key = KEY_SWIPE_LEFT;
  bindings[ACTION_RIGHT][1].key = KEY_SWIPE_RIGHT;

  if (bindings == key_bindings)
    combos_set_defaults(swipe_action_masks, combo_bindings);
}

void keys_drop_clashing_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS], int first)
{
  int i, j, slot;

  for (i = first; i < NUM_ACTIONS; i++)
    for (j = 0; j < first; j++)
      for (slot = 0; slot < KEY_SLOTS; slot++)
        if (default_keys[i] && (bindings[j][slot].key == default_keys[i] ||
                                bindings[j][slot].with == default_keys[i]))
          memset(bindings[i], 0, sizeof(bindings[i]));
}

void keys_add_default_if_free(key_binding bindings[NUM_ACTIONS][KEY_SLOTS], int action)
{
  int i, slot;
  unsigned key = default_keys[action];

  if (!key)
    return;
  for (i = 0; i < NUM_ACTIONS; i++)
    for (slot = 0; slot < KEY_SLOTS; slot++)
      if (bindings[i][slot].key == key || bindings[i][slot].with == key ||
          (i == action && bindings[i][slot].key))
        return;
  bindings[action][0].key = (unsigned char)key;
  bindings[action][0].with = 0;
}

void keys_add_swipe_defaults_if_free(key_binding bindings[NUM_ACTIONS][KEY_SLOTS])
{
  static const unsigned actions[] = { ACTION_UP, ACTION_DOWN, ACTION_LEFT, ACTION_RIGHT };
  static const unsigned keys[] = { KEY_SWIPE_UP, KEY_SWIPE_DOWN, KEY_SWIPE_LEFT, KEY_SWIPE_RIGHT };
  unsigned i;

  for (i = 0; i < sizeof(actions) / sizeof(actions[0]); i++)
    if (!bindings[actions[i]][1].key && !bindings[actions[i]][1].with)
      bindings[actions[i]][1].key = (unsigned char)keys[i];
}

/* Computes both the active direction flags (SWIPE_DIR_*) and the relative
 * horizontal/vertical weights (0..100) for a swipe after normalizing X and Y
 * against the 4:3 touchpad dimensions (width vs. height).
 *
 * How the 4:3 weighting works:
 * 1. Raw dx is divided by `width` (e.g. 320) and raw dy is divided by `height`
 *    (e.g. 240) using high-precision integer math (0..10000 scale) so that
 *    physical fraction of touchpad travel is compared on equal footing.
 * 2. Any axis below `deadzone_percent` has weight 0.
 * 3. When only one axis exceeds the deadzone, that axis gets weight 100.
 * 4. When both axes exceed the deadzone (a diagonal swipe), the swipe splits
 *    into separate horizontal and vertical inputs weighted by the movement:
 *    - If `nspire_touchpad_diagonal_weight` is enabled (Proportional 4:3):
 *      the dominant axis gets weight 100 (held every frame), and the minor
 *      axis gets weight = (minor_travel * 100) / dominant_travel (25..100),
 *      unless both axes are within 20% of each other (a true ~45-degree swipe),
 *      in which case both get 100.
 *    - If `nspire_touchpad_diagonal_weight` is off (Equal 100/100):
 *      both axes get weight 100 whenever both are above the deadzone. */
void touchpad_swipe_axis_weights(int dx, int dy, unsigned width,
                                 unsigned height, unsigned deadzone_percent,
                                 unsigned sensitivity_percent,
                                 unsigned *out_dirs,
                                 unsigned *out_x_weight,
                                 unsigned *out_y_weight)
{
  unsigned long ax, ay, x_norm, y_norm, dz_norm;
  unsigned dirs = 0, x_w = 0, y_w = 0;

  if (out_dirs) *out_dirs = 0;
  if (out_x_weight) *out_x_weight = 0;
  if (out_y_weight) *out_y_weight = 0;

  if (!width || !height)
    return;
  if (deadzone_percent > 100)
    deadzone_percent = 100;
  if (!sensitivity_percent)
    sensitivity_percent = 1;

  ax = (unsigned long)(dx < 0 ? -dx : dx);
  ay = (unsigned long)(dy < 0 ? -dy : dy);

  /* 10000 = 100.00% of full pad width/height, scaled by sensitivity_percent. */
  x_norm = (ax * 100u * sensitivity_percent) / width;
  y_norm = (ay * 100u * sensitivity_percent) / height;
  dz_norm = (unsigned long)deadzone_percent * 100u;

  if (x_norm >= dz_norm)
  {
    dirs |= (dx < 0) ? SWIPE_DIR_LEFT : SWIPE_DIR_RIGHT;
    x_w = 100u;
  }
  /* The Ndless CX II report maps increasing Y to the Up edge. */
  if (y_norm >= dz_norm)
  {
    dirs |= (dy > 0) ? SWIPE_DIR_UP : SWIPE_DIR_DOWN;
    y_w = 100u;
  }

  /* When swiping diagonally (both X and Y active), weight the horizontal and
   * vertical components according to their 4:3-normalized travel ratio. */
  if (x_w && y_w && nspire_touchpad_diagonal_weight)
  {
    if (x_norm > y_norm)
    {
      unsigned ratio = (unsigned)((y_norm * 100u) / x_norm);
      /* Within 80..100% of the dominant axis is a true ~45-deg diagonal (both 100%). */
      y_w = (ratio >= 80u) ? 100u : (ratio < 20u ? 20u : ratio);
    }
    else if (y_norm > x_norm)
    {
      unsigned ratio = (unsigned)((x_norm * 100u) / y_norm);
      x_w = (ratio >= 80u) ? 100u : (ratio < 20u ? 20u : ratio);
    }
  }

  if (out_dirs) *out_dirs = dirs;
  if (out_x_weight) *out_x_weight = x_w;
  if (out_y_weight) *out_y_weight = y_w;
}

unsigned touchpad_swipe_direction_mask(int dx, int dy, unsigned width,
                                       unsigned height, unsigned deadzone_percent,
                                       unsigned sensitivity_percent)
{
  unsigned dirs = 0;
  touchpad_swipe_axis_weights(dx, dy, width, height, deadzone_percent,
                              sensitivity_percent, &dirs, NULL, NULL);
  return dirs;
}

const char *key_name(unsigned key)
{
  return key <= NUM_KEYS ? nspire_key_strings[key] : NULL;
}

int key_is_reserved(unsigned key)
{
  return (key >= KEY_UP && key <= KEY_LEFTUP) || key == KEY_CLICK;
}

int key_is_bindable(unsigned key)
{
  /* Only the 4 orthogonal swipe components (SW-UP, SW-DOWN, SW-LEFT, SW-RIGHT)
   * are used as swipe triggers so diagonals always split into H + V components. */
  if (key >= KEY_SWIPE_UP_LEFT && key <= KEY_SWIPE_DOWN_RIGHT)
    return 0;
  return key > 0 && key <= NUM_KEYS && key_name(key) && !key_is_reserved(key);
}

const char *binding_name(key_binding b, char *text, size_t size)
{
  const char *key = key_name(b.key), *with = key_name(b.with);

  if (!b.key || !key)
    snprintf(text, size, "none");
  else if (b.with && with)
    snprintf(text, size, "%s+%s", with, key);
  else
    snprintf(text, size, "%s", key);
  return text;
}

const char *action_mask_name(unsigned short mask, char *text, size_t size)
{
  int i;
  size_t used = 0;

  if (!text || !size)
    return "";
  text[0] = '\0';
  mask &= (unsigned short)((1u << NUM_GBA_ACTIONS) - 1u);
  if (!mask)
  {
    snprintf(text, size, "none");
    return text;
  }

  for (i = 0; i < NUM_GBA_ACTIONS; i++)
  {
    if (mask & (1u << i))
    {
      int n = snprintf(text + used, size - used, "%s%s",
                       used ? "+" : "", short_gba_names[i]);
      if (n < 0 || (size_t)n >= size - used)
        break;
      used += (size_t)n;
    }
  }
  return text;
}

unsigned binding_pack(key_binding b)
{
  return b.key | ((unsigned)b.with << 8);
}

key_binding binding_unpack(unsigned packed)
{
  key_binding b = { 0, 0 };
  unsigned key = packed & 0xFF, with = (packed >> 8) & 0xFF;

  if (key_is_bindable(key))
  {
    b.key = (unsigned char)key;
    if (with != key && key_is_bindable(with))
      b.with = (unsigned char)with;
  }
  return b;
}
