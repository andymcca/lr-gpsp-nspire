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
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "shift", "ctrl", ",", NULL, NULL, NULL, NULL, NULL
};

/* ---- Bindings ---- */

key_binding key_bindings[NUM_ACTIONS][KEY_SLOTS];

const char *const action_names[NUM_ACTIONS] =
{
  "D-pad up", "D-pad down", "D-pad left", "D-pad right",
  "A", "B", "L", "R", "Start", "Select",
  "Menu", "Fast forward", "Load state", "Save state", "Save+Exit",
  "Load newest state", "Next slot", "Previous slot", "FPS counter", "Restart game",
  "Brightness up", "Brightness down"
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

void keys_set_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS])
{
  int i;
  memset(bindings, 0, sizeof(key_binding) * NUM_ACTIONS * KEY_SLOTS);
  for (i = 0; i < NUM_ACTIONS; i++)
    bindings[i][0].key = default_keys[i];
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

