/* Ndless keypad input for libretro gpSP core (replaces libretro/input.c). */

#include "common.h"
#include "input.h"
#include "serial.h"
#include "gba_memory.h"
#include "gui.h"
#include "nspire.h"
#include "nspire_gui_video.h"

#include <os.h>

bool libretro_supports_bitmasks    = false;
bool libretro_supports_ff_override = false;
bool libretro_ff_enabled           = false;
bool libretro_ff_enabled_prev      = false;

unsigned turbo_period      = TURBO_PERIOD_MIN;
unsigned turbo_pulse_width = TURBO_PULSE_WIDTH_MIN;
unsigned turbo_a_counter   = 0;
unsigned turbo_b_counter   = 0;

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

#define TOUCHPAD_KEYS_MASK 0xFF00FFFD
#define KEY_ENTER 2
#define KEY_Z 6
#define KEY_X 17
#define KEY_3 20
#define KEY_S 23
#define KEY_1 24
#define KEY_Q 34
#define KEY_6 36
#define KEY_4 40
#define KEY_L 49
#define KEY_9 52
#define KEY_7 56
#define KEY_F 65
#define KEY_A 71
#define KEY_VAR 82
#define KEY_5 87
#define KEY_DEL 90
#define KEY_SCRATCH 91
#define KEY_CLICK 98
#define KEY_DOC 100
#define KEY_2 101
#define KEY_MENU 102
#define KEY_8 103
#define KEY_ESC 104
#define KEY_UP 113
#define KEY_RIGHT 115
#define KEY_DOWN 117
#define KEY_LEFT 119
#define KEY_SHIFT 121
#define KEY_CTRL 122

static u32 current_keys[4];
static u32 new_keys[4];

#define getkey(key) (key && (current_keys[(key - 1) >> 5] & (1 << (((key)-1) & 0x1F))))
#define getnewkey(key) (key && (new_keys[(key - 1) >> 5] & (1 << (((key)-1) & 0x1F))))

static void update_keys(void)
{
  touchpad_report_t report;
  touchpad_scan(&report);

  u32 touchpad_pressed;
  switch ((tpad_arrow_t)report.arrow)
  {
  case TPAD_ARROW_UP:
    touchpad_pressed = 0x00010000;
    break;
  case TPAD_ARROW_UPRIGHT:
    touchpad_pressed = 0x00050000;
    break;
  case TPAD_ARROW_RIGHT:
    touchpad_pressed = 0x00040000;
    break;
  case TPAD_ARROW_RIGHTDOWN:
    touchpad_pressed = 0x00140000;
    break;
  case TPAD_ARROW_DOWN:
    touchpad_pressed = 0x00100000;
    break;
  case TPAD_ARROW_DOWNLEFT:
    touchpad_pressed = 0x00500000;
    break;
  case TPAD_ARROW_LEFT:
    touchpad_pressed = 0x00400000;
    break;
  case TPAD_ARROW_LEFTUP:
    touchpad_pressed = 0x00410000;
    break;
  case TPAD_ARROW_CLICK:
    touchpad_pressed = 0x00000002;
    break;
  default:
    touchpad_pressed = 0x00000000;
    break;
  }

  new_keys[0] = ~current_keys[0];
  new_keys[1] = ~current_keys[1];
  new_keys[2] = ~current_keys[2];
  new_keys[3] = ~current_keys[3];

  new_keys[0] &= (current_keys[0] = *(unsigned volatile *)0x900E0010 | (on_key_pressed() ? 0x200 : 0));
  new_keys[1] &= (current_keys[1] = *(unsigned volatile *)0x900E0014);
  new_keys[2] &= (current_keys[2] = *(unsigned volatile *)0x900E0018);
  new_keys[3] &= (current_keys[3] =
                      (*(unsigned volatile *)0x900E001C & TOUCHPAD_KEYS_MASK) | touchpad_pressed);
}

#define KEY_MAP_UP 0
#define KEY_MAP_DOWN 1
#define KEY_MAP_LEFT 2
#define KEY_MAP_RIGHT 3
#define KEY_MAP_A 4
#define KEY_MAP_B 5
#define KEY_MAP_L 6
#define KEY_MAP_R 7
#define KEY_MAP_START 8
#define KEY_MAP_SELECT 9
#define KEY_MAP_MENU 10
#define KEY_MAP_FASTFORWARD 11
#define KEY_MAP_LOADSTATE 12
#define KEY_MAP_SAVESTATE 13
#define KEY_MAP_QUICKSAVE 14

u32 gamepad_config_map[16] = {
    KEY_8, KEY_5, KEY_4, KEY_6, KEY_DEL, KEY_VAR, KEY_SCRATCH, KEY_DOC,
    KEY_SHIFT, KEY_CTRL, KEY_MENU, KEY_F, KEY_L, KEY_S, KEY_Q, 0,
};

u32 global_enable_analog = 0;
u32 analog_sensitivity_level = 4;

extern u32 synchronize_flag;
extern u32 quick_save_slot;
extern u32 savestate_slot;
extern void get_savestate_filename_noshot(u32 slot, u8 *name_buffer);

u32 update_input(void)
{
  uint32_t new_key;
  u32 gba_buttons;

  update_keys();

  if (getnewkey(gamepad_config_map[KEY_MAP_MENU]))
    return menu_wrapper(copy_screen());

  gba_buttons = 0;
  if (getkey(KEY_UP) || getkey(gamepad_config_map[KEY_MAP_UP]))
    gba_buttons |= BUTTON_UP;
  if (getkey(KEY_DOWN) || getkey(gamepad_config_map[KEY_MAP_DOWN]))
    gba_buttons |= BUTTON_DOWN;
  if (getkey(KEY_RIGHT) || getkey(gamepad_config_map[KEY_MAP_RIGHT]))
    gba_buttons |= BUTTON_RIGHT;
  if (getkey(KEY_LEFT) || getkey(gamepad_config_map[KEY_MAP_LEFT]))
    gba_buttons |= BUTTON_LEFT;
  if (getkey(gamepad_config_map[KEY_MAP_START]))
    gba_buttons |= BUTTON_START;
  if (getkey(gamepad_config_map[KEY_MAP_A]))
    gba_buttons |= BUTTON_A;
  if (getkey(gamepad_config_map[KEY_MAP_B]))
    gba_buttons |= BUTTON_B;
  if (getkey(gamepad_config_map[KEY_MAP_L]))
    gba_buttons |= BUTTON_L;
  if (getkey(gamepad_config_map[KEY_MAP_R]))
    gba_buttons |= BUTTON_R;
  if (getkey(gamepad_config_map[KEY_MAP_SELECT]))
    gba_buttons |= BUTTON_SELECT;

  if ((gba_buttons & BUTTON_LEFT) && (gba_buttons & BUTTON_RIGHT))
    gba_buttons &= ~(BUTTON_LEFT | BUTTON_RIGHT);
  if ((gba_buttons & BUTTON_DOWN) && (gba_buttons & BUTTON_UP))
    gba_buttons &= ~(BUTTON_DOWN | BUTTON_UP);

  new_key = gba_buttons;

  if (serial_mode == SERIAL_MODE_GBP)
  {
    if (frame_counter > 20 && frame_counter < 100)
      new_key = (frame_counter % 3) ? 0x3FF : 0x30F;
  }

  if ((new_key | old_key) != old_key)
    trigger_key(new_key);

  old_key = new_key;
  write_ioreg(REG_P1, (~old_key) & 0x3FF);

  if (getnewkey(gamepad_config_map[KEY_MAP_LOADSTATE]))
  {
    u8 current_savestate_filename[512];

    if (!gamepak_filename[0])
      return 0;
    get_savestate_filename_noshot(savestate_slot, current_savestate_filename);
    /* Do not use load_state_wrapper: tiny not_crt0_savedsp stack overflows BSON path. */
    if (load_state(current_savestate_filename))
      return 1;
    return 0;
  }

  if (getnewkey(gamepad_config_map[KEY_MAP_SAVESTATE]))
  {
    u8 current_savestate_filename[512];
    u16 *current_screen = copy_screen();

    if (!gamepak_filename[0])
      return 0;
    get_savestate_filename_noshot(savestate_slot, current_savestate_filename);
    save_state(current_savestate_filename, current_screen);
    return 0;
  }

  if (getnewkey(gamepad_config_map[KEY_MAP_FASTFORWARD]))
  {
    synchronize_flag ^= 1;
    return 0;
  }

  if (getnewkey(gamepad_config_map[KEY_MAP_QUICKSAVE]))
  {
    u8 current_savestate_filename[512];
    u16 *current_screen = copy_screen();

    if (gamepak_filename[0])
    {
      get_savestate_filename_noshot(savestate_slot, current_savestate_filename);
      save_state(current_savestate_filename, current_screen);
      quick_save_slot = savestate_slot + 1;
      save_config_file();
    }
    quit();
  }

  return 0;
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

u32 key_scan(void)
{
  u32 i;

  update_keys();
  while (current_keys[0] || current_keys[1] || current_keys[2] || current_keys[3])
    update_keys();
  while (!current_keys[0] && !current_keys[1] && !current_keys[2] && !current_keys[3])
    update_keys();
  for (i = 1; i <= 128; i++)
  {
    if ((i < KEY_UP || i > KEY_LEFT) && i != KEY_CLICK && getkey(i))
      return i;
  }
  return 0;
}

gui_action_type get_gui_input(void)
{
  update_keys();

  if (getnewkey(KEY_UP) || getnewkey(KEY_8))
    return CURSOR_UP;
  if (getnewkey(KEY_DOWN) || getnewkey(KEY_5) || getnewkey(KEY_2))
    return CURSOR_DOWN;
  if (getnewkey(KEY_RIGHT) || getnewkey(KEY_6))
    return CURSOR_RIGHT;
  if (getnewkey(KEY_LEFT) || getnewkey(KEY_4))
    return CURSOR_LEFT;
  if (getnewkey(KEY_ESC))
    return CURSOR_BACK;
  if (getnewkey(KEY_CLICK) || getnewkey(KEY_ENTER))
    return CURSOR_SELECT;
  if (getnewkey(KEY_MENU) || getnewkey(gamepad_config_map[10]))
    return CURSOR_EXIT;
  return CURSOR_NONE;
}
