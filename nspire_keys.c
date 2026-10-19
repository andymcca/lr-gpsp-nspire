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
    [KEY_SWIPE_RIGHT] = "SW-RIGHT"
};

/* ---- Bindings ---- */

key_binding key_bindings[NUM_ACTIONS][KEY_SLOTS];
unsigned short swipe_action_masks[NUM_SWIPE_DIRS];
combo_binding combo_bindings[NUM_COMBO_SLOTS];

/* Arrows, until Configure input says otherwise. 8% dead zone, 100% sensitivity. */
unsigned nspire_touchpad_swipe;
unsigned nspire_touchpad_deadzone = 2;
unsigned nspire_touchpad_sensitivity = 2;

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
  "Swipe up", "Swipe down", "Swipe left", "Swipe right"
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
    memset(swipes, 0, sizeof(unsigned short) * NUM_SWIPE_DIRS);
  if (combos)
    memset(combos, 0, sizeof(combo_binding) * NUM_COMBO_SLOTS);
}

void keys_set_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS])
{
  int i;
  memset(bindings, 0, sizeof(key_binding) * NUM_ACTIONS * KEY_SLOTS);
  for (i = 0; i < NUM_ACTIONS; i++)
    bindings[i][0].key = default_keys[i];
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
  static const unsigned actions[NUM_SWIPE_DIRS] =
    { ACTION_UP, ACTION_DOWN, ACTION_LEFT, ACTION_RIGHT };
  static const unsigned keys[NUM_SWIPE_DIRS] =
    { KEY_SWIPE_UP, KEY_SWIPE_DOWN, KEY_SWIPE_LEFT, KEY_SWIPE_RIGHT };
  unsigned i;

  for (i = 0; i < NUM_SWIPE_DIRS; i++)
    if (!bindings[actions[i]][1].key && !bindings[actions[i]][1].with)
      bindings[actions[i]][1].key = (unsigned char)keys[i];
}

void touchpad_swipe_directions(int dx, int dy, unsigned width, unsigned height,
                               unsigned deadzone_percent, unsigned sensitivity_percent,
                               unsigned *out_dirs)
{
  unsigned long long x_norm, y_norm, dz;
  unsigned long ax, ay;
  unsigned dirs = 0;

  if (out_dirs)
    *out_dirs = 0;
  if (!width)
    width = 4;
  if (!height)
    height = 3;
  if (deadzone_percent > 100)
    deadzone_percent = 100;
  if (!sensitivity_percent)
    sensitivity_percent = 100;

  ax = (unsigned long)(dx < 0 ? -dx : dx);
  ay = (unsigned long)(dy < 0 ? -dy : dy);
  /* Hundredths of a percent of that axis, then the sensitivity. */
  x_norm = (unsigned long long)ax * 10000ull * sensitivity_percent / width / 100ull;
  y_norm = (unsigned long long)ay * 10000ull * sensitivity_percent / height / 100ull;
  dz = (unsigned long long)deadzone_percent * 100ull;

  if (dx != 0 && x_norm >= dz)
    dirs |= dx > 0 ? SWIPE_DIR_RIGHT : SWIPE_DIR_LEFT;
  if (dy != 0 && y_norm >= dz)
    dirs |= dy > 0 ? SWIPE_DIR_UP : SWIPE_DIR_DOWN;
  if (out_dirs)
    *out_dirs = dirs;
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
    int wrote;
    if (!(mask & (1u << i)))
      continue;
    wrote = snprintf(text + used, size - used, "%s%s", used ? "+" : "", short_gba_names[i]);
    if (wrote < 0 || (size_t)wrote >= size - used)
      break;
    used += (size_t)wrote;
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

