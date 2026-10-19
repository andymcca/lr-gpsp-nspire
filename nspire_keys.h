/* Calculator keys and what they're bound to.
 *
 * Keys are numbered 1 + their bit in the keypad registers at 0x900E0010
 * (gpSP's numbering, shared with PocketSNES's settings files); 0 is no key.
 * Each action has two bindings, and a binding is a key or a combination
 * ('key' pressed while 'with' is held). One key may be in several actions'
 * bindings. */
#ifndef NSPIRE_KEYS_H
#define NSPIRE_KEYS_H

#include <stddef.h>

/* The GBA buttons, then the hotkeys, in the order "Configure input" lists
 * them. The first 15 are gpSP's old gamepad_config_map entries. */
enum nspire_action
{
  ACTION_UP,
  ACTION_DOWN,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_A,
  ACTION_B,
  ACTION_L,
  ACTION_R,
  ACTION_START,
  ACTION_SELECT,
  ACTION_MENU,
  ACTION_FAST_FORWARD,
  ACTION_LOAD_STATE,
  ACTION_SAVE_STATE,
  ACTION_QUIT,            /* "Save+Exit": saves a state if that's on, then leaves */
  ACTION_LOAD_NEWEST,
  ACTION_NEXT_SLOT,
  ACTION_PREVIOUS_SLOT,
  ACTION_SHOW_FPS,
  ACTION_RESTART,
  ACTION_BRIGHTER,        /* screen brightness (CX II) */
  ACTION_DIMMER,
  NUM_ACTIONS
};

#define FIRST_HOTKEY ACTION_MENU
#define KEY_SLOTS 2

#define KEY_ENTER   2
#define KEY_0       8
#define KEY_Q       34
#define KEY_S       23
#define KEY_U       21
#define KEY_A       71
#define KEY_C       69
#define KEY_L       49
#define KEY_F       65
#define KEY_1       24
#define KEY_3       20
#define KEY_4       40
#define KEY_5       87
#define KEY_6       36
#define KEY_7       56
#define KEY_9       52
#define KEY_2       101
#define KEY_8       103
#define KEY_VAR     82
#define KEY_RPAREN  84
#define KEY_LPAREN  86
#define KEY_MINUS   83
#define KEY_DEL     90
#define KEY_SCRATCH 91
#define KEY_CLICK   98
#define KEY_PLUS    99
#define KEY_DOC     100
#define KEY_MENU    102
#define KEY_ESC     104
#define KEY_TAB     106
#define KEY_UP      113
#define KEY_RIGHT   115
#define KEY_DOWN    117
#define KEY_LEFT    119
#define KEY_LEFTUP  120
#define KEY_SHIFT   121
#define KEY_CTRL    122

/* Virtual keys from a touchpad swipe. They are not keypad bits. A diagonal
 * holds the horizontal and vertical keys together for as long as both axes
 * stay past the dead zone. */
#define KEY_SWIPE_UP     129
#define KEY_SWIPE_DOWN   130
#define KEY_SWIPE_LEFT   131
#define KEY_SWIPE_RIGHT  132
#define NUM_KEYS         132
#define NUM_KEY_WORDS    ((NUM_KEYS + 31) / 32)

#define NUM_GBA_ACTIONS  10
#define NUM_SWIPE_DIRS   4
#define NUM_COMBO_SLOTS  8

#define SWIPE_DIR_UP     (1u << 0)
#define SWIPE_DIR_DOWN   (1u << 1)
#define SWIPE_DIR_LEFT   (1u << 2)
#define SWIPE_DIR_RIGHT  (1u << 3)

typedef struct
{
  unsigned char key;
  unsigned char with;
} key_binding;

/* One calculator key, or a key combination, or a swipe, holding several
 * GBA buttons at once. action_mask bits 0..9 are ACTION_UP..ACTION_SELECT. */
typedef struct
{
  key_binding trigger;
  unsigned short action_mask;
} combo_binding;

extern key_binding key_bindings[NUM_ACTIONS][KEY_SLOTS];
/* Empty means that swipe uses the key rows above. A non-empty list replaces
 * those rows for that swipe while it is held. */
extern unsigned short swipe_action_masks[NUM_SWIPE_DIRS];
extern combo_binding combo_bindings[NUM_COMBO_SLOTS];
extern const char *const action_names[NUM_ACTIONS];
extern const char *const swipe_dir_names[NUM_SWIPE_DIRS];

/* 0: a touch at the edge is a direction (the previous behavior). 1: swipes. */
extern unsigned nspire_touchpad_swipe;
extern unsigned nspire_touchpad_deadzone;
extern unsigned nspire_touchpad_sensitivity;
#define SWIPE_DEADZONE_CHOICES 7
#define SWIPE_SENSITIVITY_CHOICES 7

void keys_set_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS]);
void combos_set_defaults(unsigned short swipes[NUM_SWIPE_DIRS],
                         combo_binding combos[NUM_COMBO_SLOTS]);
/* For settings from builds that had only actions 0 to first - 1: the newer
 * actions keep their default keys only where no older one uses that key. */
void keys_drop_clashing_defaults(key_binding bindings[NUM_ACTIONS][KEY_SLOTS], int first);
/* For settings written before an action had a default key: gives it that
 * key if the action has none and no action uses the key. */
void keys_add_default_if_free(key_binding bindings[NUM_ACTIONS][KEY_SLOTS], int action);
/* Puts SW-UP/DOWN/LEFT/RIGHT on the four directions' second key, when that
 * slot is empty. Used when swipes are turned on, so arrows mode stays as it
 * was. */
void keys_add_swipe_defaults_if_free(key_binding bindings[NUM_ACTIONS][KEY_SLOTS]);
/* A key's name, or NULL for a key that doesn't exist. */
const char *key_name(unsigned key);
/* Arrows and the touchpad click can't be bound: they always move. */
int key_is_reserved(unsigned key);
int key_is_bindable(unsigned key);
/* "ctrl", "shift+S" or "none". */
const char *binding_name(key_binding b, char *text, size_t size);
/* "A+B", "Up+Rt" or "none". */
const char *action_mask_name(unsigned short mask, char *text, size_t size);
/* Directions past the dead zone. dx/dy are touchpad units from where the
 * finger landed; on the CX II, X grows right and Y grows up. Each axis is
 * scaled by that axis's length, so a 4:3 pad is not biased, then by
 * sensitivity_percent. Both axes are reported together when both qualify. */
void touchpad_swipe_directions(int dx, int dy, unsigned width, unsigned height,
                               unsigned deadzone_percent, unsigned sensitivity_percent,
                               unsigned *out_dirs);
/* A binding as one number for the settings files: key | with << 8. */
unsigned binding_pack(key_binding b);
key_binding binding_unpack(unsigned packed);

/* Reads the keypad (nspire_input.c). */
void keys_poll(void);
int key_down(unsigned key);
int key_pressed(unsigned key);   /* went down at the last poll */
int any_key_down(void);
int binding_held(key_binding b);
/* A hotkey fires on a binding that was just pressed. A single key that is
 * part of a combination held for another hotkey doesn't fire: with S on
 * "Save state" and shift+S on "Load state", shift+S only loads. */
int hotkey_pressed(int action);

/* Waits for a key or a combination for the key configuration: hold one key
 * and press another for a combination. Returns no key (key 0) if an arrow or
 * the touchpad click is pressed first, which cancels. */
key_binding key_capture(void);
/* Waits until no key is down. */
void keys_wait_release(void);

/* How many times the held direction has repeated in menus (get_gui_input). */
unsigned gui_repeat_count(void);
/* 0 while the file list is up: number keys don't move there, they start
 * the first ten games. */
extern int nspire_gui_digits_move;
/* After get_gui_input: 0 to 9 for 1, 2, ... 9, 0 just pressed, else -1. */
int nspire_digit_pressed(void);

#endif
