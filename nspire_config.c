/* gpSP's settings: the ones for all games, kept inside the program file
 * (nspire_selfstore.h) with a copy, gpsp_qol.cfg.tns, in the saves folder
 * (nspire_paths.h), and a game's own settings and keys, "<ROM
 * name>.settings.tns" in the saves folder.
 *
 * Words 0-71 match the SWP1 layout (word 71 stores diagonal weighting mode);
 * words 72-84 store "CMB1" followed by the 4 orthogonal touchpad swipe
 * multi-action masks (packed two per word in 73-74) and the 8 custom
 * calculator/swipe multi-control combo slots (words 77-84: low 16 bits =
 * binding_pack(trigger), high 16 bits = action_mask). */

#include "common.h"
#include "gui.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"
#include "nspire_states.h"
#include "nspire_clock.h"
#include "nspire_brightness.h"
#include "nspire_paths.h"
#include "nspire_selfstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_WORDS 85
#define LEGACY_SWIPE_WORDS 72
#define QOL_MAGIC  0x334C4F51u   /* "QOL3" */
#define QOL2_MAGIC 0x324C4F51u   /* "QOL2", before word 66 and the slot keys */
#define QOL1_MAGIC 0x314C4F51u   /* "QOL1", the first QoL build's */
#define GAME_FILE_VERSION 7
#define SWIPE_CONFIG_MAGIC 0x31505753u /* "SWP1" */
#define COMBO_CONFIG_MAGIC 0x31424D43u /* "CMB1" */
/* Actions before brightness up/down, in words 38-57. */
#define FIRST_ACTIONS 20
/* Top byte of word 26: gpSP's newer frameskip encoding (auto/manual/off). */
#define FS_V2_MARK 0xFF000000u

u32 nspire_auto_increment = 1;
u32 nspire_save_on_exit = 1;
u32 nspire_load_on_start = 1;
u32 nspire_cpu_speed = 0;
u32 nspire_compress_saves = 1;
/* The menu's "Settings for" and "Keys for": 0 all games, 1 this game. */
u32 nspire_settings_for;
u32 nspire_keys_for;

extern u32 update_backup_flag;
extern u32 global_enable_analog;
extern u32 analog_sensitivity_level;

/* The settings a game can have its own of. */
static const struct
{
  const char *name;
  u32 *value;
  u32 count;
} settings[] =
{
  { "screen_scale", &screen_scale, 4 },
  { "frameskip_type", &current_frameskip_type, 3 },
  { "frameskip_value", &frameskip_value, 100 },
  { "frameskip_random", &random_skip, 2 },
  { "show_fps", &nspire_fps_overlay, 3 },
  { "frame_mixing", &nspire_frame_mix_choice, 2 },
  { "gba_blending_off", &nspire_gba_blend_off, 2 },
  { "renderer", &nspire_video_renderer_choice, 2 },
  { "no_sprite_limit", &nspire_sprlim_choice, 2 },
  { "rtc", &nspire_rtc_choice, 3 },
  { "bios", &nspire_bios_choice, 2 },
  { "dynarec_smc", &nspire_dynarec_ram_policy, 2 },
  { "write_saves", &update_backup_flag, 2 },
  { "auto_increment", &nspire_auto_increment, 2 },
  { "save_on_exit", &nspire_save_on_exit, 2 },
  { "load_on_start", &nspire_load_on_start, 2 },
  { "compress_saves", &nspire_compress_saves, 2 },
  { "cpu_speed", &nspire_cpu_speed, CPU_SPEEDS },
  { "touchpad_swipe", &nspire_touchpad_swipe, 2 },
  { "swipe_deadzone", &nspire_touchpad_deadzone, SWIPE_DEADZONE_CHOICES },
  { "swipe_sensitivity", &nspire_touchpad_sensitivity, SWIPE_SENSITIVITY_CHOICES },
  { "swipe_diag_weight", &nspire_touchpad_diagonal_weight, 2 },
};

#define NUM_SETTINGS (int)(sizeof(settings) / sizeof(settings[0]))

static const char *const action_ids[NUM_ACTIONS] =
{
  "up", "down", "left", "right", "a", "b", "l", "r", "start", "select",
  "menu", "fast_forward", "load_state", "save_state", "save_exit",
  "load_newest", "next_slot", "previous_slot", "show_fps", "restart",
  "brighter", "dimmer"
};

static const char *const swipe_mask_ids[NUM_SWIPE_DIRS] =
{
  "up", "down", "left", "right"
};

/* The first QoL build's CPU speeds (normal, 432, 456, 480 MHz) in today's
 * list (nspire_clock.h). */
static u32 cpu_speed_from_qol1(u32 old)
{
  static const u32 now[4] = { 0, 3, 5, 7 };
  return now[old % 4];
}

/* The values for all games, while a game with its own is open. */
static u32 global_values[NUM_SETTINGS];
static key_binding global_keys[NUM_ACTIONS][KEY_SLOTS];
static unsigned short global_swipe_masks[NUM_SWIPE_DIRS];
static combo_binding global_combos[NUM_COMBO_SLOTS];
static int game_open, own_settings, own_keys;
/* The open game's file, kept from when it opened: by the time it closes,
 * gamepak_filename may already name the next game. */
static char game_file[600];

static void capture(u32 *values)
{
  int i;
  for (i = 0; i < NUM_SETTINGS; i++)
    values[i] = *settings[i].value;
}

static void apply(const u32 *values)
{
  int i;
  for (i = 0; i < NUM_SETTINGS; i++)
    *settings[i].value = values[i] % settings[i].count;
}

#define CONFIG_FILE     "gpsp_qol.cfg.tns"
#define OLD_CONFIG_FILE "gpsp.cfg.tns"   /* older gpSP builds' */

/* A settings file next to gpSP (builds before the saves folder). */
static void program_folder_path(char *path, const char *name)
{
  sprintf(path, "%s/%s", main_path, name);
}

/* A whole settings file into o (at most CONFIG_WORDS words): its size, or
 * -1 if it isn't there or is bigger than any settings file. */
static long read_settings_file(const char *path, u32 *o)
{
  long size;
  FILE *f = fopen(path, "rb");

  if (!f)
    return -1;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0 || size > CONFIG_WORDS * 4 || fread(o, 1, (size_t)size, f) != (size_t)size)
    size = -1;
  fclose(f);
  return size;
}

/* The settings came from the program file (not from a copy or an older
 * file). */
static int settings_in_program;

int nspire_config_in_program(void)
{
  return settings_in_program;
}

/* The settings' bytes, from the first place that has them (see the top of
 * this file). */
static long read_settings(u32 *o)
{
  char path[600];
  long size = selfstore_read(nspire_program_path, o, CONFIG_WORDS * 4);

  settings_in_program = size >= 0;
  if (size >= 0)
    return size;
  nspire_save_file(path, sizeof(path), CONFIG_FILE);
  if ((size = read_settings_file(path, o)) >= 0)
    return size;
  program_folder_path(path, CONFIG_FILE);
  if ((size = read_settings_file(path, o)) >= 0)
    return size;
  /* Older builds' file, moved into the saves folder at start. */
  nspire_save_file(path, sizeof(path), OLD_CONFIG_FILE);
  if ((size = read_settings_file(path, o)) >= 0)
    return size;
  program_folder_path(path, OLD_CONFIG_FILE);
  return read_settings_file(path, o);
}

static void clamp_rom_buffer_size(void)
{
  if (nspire_rom_buffer_size_choice < 2)
    nspire_rom_buffer_size_choice = 2;
  else if (nspire_rom_buffer_size_choice > 32)
    nspire_rom_buffer_size_choice = 32;
}

s32 nspire_config_load(void)
{
  u32 o[CONFIG_WORDS];
  long size;
  u32 words, i;
  int qol = 0;   /* 0: older, 1: QOL1, 2: QOL2, 3: QOL3 layout */
  int swipe_config_present;
  int combo_config_present;

  memset(o, 0, sizeof(o));
  size = read_settings(o);
  /* Sizes gpSP's builds wrote (92 = keys, 96 = +BIOS, 112 = +frameskip,
   * 124 = +emulator options, 128 = +ROM buffer, 132 = +LCD cache, 136 = +FPS,
   * 140 = +blending, 144 = +dynarec SMC, 148 = +renderer, 152 = older
   * renderer, 288 = SWP1, 340 = CMB1). */
  if (!(size == 92 || (size >= 96 && size <= 152 && size % 4 == 0) ||
        size == LEGACY_SWIPE_WORDS * 4 || size == CONFIG_WORDS * 4))
    return -1;
  words = (u32)size / 4;
  swipe_config_present = words >= 71 && o[67] == SWIPE_CONFIG_MAGIC;
  if (swipe_config_present)
  {
    nspire_touchpad_swipe = o[68] % 2;
    nspire_touchpad_deadzone = o[69] % SWIPE_DEADZONE_CHOICES;
    nspire_touchpad_sensitivity = o[70] % SWIPE_SENSITIVITY_CHOICES;
    /* Word 71 bit 0 stores diagonal weighting (0x100 marks initialized). */
    if (words >= 72 && (o[71] & 0x100u))
      nspire_touchpad_diagonal_weight = o[71] & 1u;
    else
      nspire_touchpad_diagonal_weight = 1;
  }
  else
  {
    nspire_touchpad_swipe = 1;
    nspire_touchpad_deadzone = 2;      /* 8% */
    nspire_touchpad_sensitivity = 2;  /* 100% */
    nspire_touchpad_diagonal_weight = 1;
  }

  screen_scale = o[0] % 4;
  screen_filter = o[1] % 2;
  update_backup_flag = o[4] % 2;
  global_enable_analog = o[5] % 2;
  analog_sensitivity_level = o[6] % 8;

  /* gpSP's single key per action, for the first 15. */
  keys_set_defaults(key_bindings);
  for (i = 0; i < 15; i++)
  {
    key_binding b = binding_unpack(o[7 + i] & 0xFF);
    key_bindings[i][0] = b;
  }

  nspire_bios_choice = words >= 24 ? o[23] % 2 : 1;
  if (words >= 28)
  {
    if ((o[26] & FS_V2_MARK) == FS_V2_MARK)
      current_frameskip_type = o[24] % 3;
    else
    {
      static const u32 old_types[] = { no_frameskip, auto_frameskip, auto_frameskip, manual_frameskip };
      current_frameskip_type = old_types[o[24] % 4];
    }
    frameskip_value = o[25] > 99 ? 99 : o[25];
    if (frameskip_value < 1 && current_frameskip_type == auto_frameskip)
      frameskip_value = 4;
    random_skip = o[27] % 2;
  }
  if (words >= 31)
  {
    nspire_sprlim_choice = o[28] % 2;
    nspire_rtc_choice = o[29] % 3;
    nspire_frame_mix_choice = o[30] % 2;
  }
  nspire_rom_buffer_size_choice = words >= 32 ? o[31] : 8;
  nspire_lcd_cache_clean_full = words >= 33 ? o[32] % 2 : 0;
  nspire_fps_overlay = words >= 34 ? o[33] % 2 : 0;
  nspire_gba_blend_off = words >= 35 ? o[34] % 2 : 0;
  nspire_dynarec_ram_policy = words >= 36 ? o[35] % 2 : 0;
  nspire_video_renderer_choice = words >= 37 ? o[36] % 2 : 0;

  if (words >= 63 && (o[37] == QOL_MAGIC || o[37] == QOL2_MAGIC || o[37] == QOL1_MAGIC))
  {
    qol = o[37] == QOL1_MAGIC || words < 67 ? 1 : o[37] == QOL2_MAGIC ? 2 : 3;
    for (i = 0; i < FIRST_ACTIONS; i++)
    {
      key_bindings[i][0] = binding_unpack(o[38 + i] & 0xFFFF);
      key_bindings[i][1] = binding_unpack(o[38 + i] >> 16);
    }
    nspire_auto_increment = o[58] % 2;
    nspire_save_on_exit = o[59] % 2;
    nspire_load_on_start = o[60] % 2;
    nspire_cpu_speed = qol >= 2 ? o[61] % CPU_SPEEDS : cpu_speed_from_qol1(o[61]);
    nspire_fps_overlay = o[62] % 3;
  }
  nspire_compress_saves = qol == 3 ? o[66] % 2 : 1;
  if (qol >= 2)
  {
    nspire_brightness = o[63] % (BRIGHTNESS_LEVELS + 1);
    for (i = FIRST_ACTIONS; i < NUM_ACTIONS; i++)
    {
      key_bindings[i][0] = binding_unpack(o[64 + i - FIRST_ACTIONS] & 0xFFFF);
      key_bindings[i][1] = binding_unpack(o[64 + i - FIRST_ACTIONS] >> 16);
    }
  }
  else
  {
    /* See the top of this file. */
    screen_scale = scaled_raw;
    keys_drop_clashing_defaults(key_bindings, qol ? FIRST_ACTIONS : 15);
  }
  if (qol < 3)
  {
    keys_add_default_if_free(key_bindings, ACTION_NEXT_SLOT);
    keys_add_default_if_free(key_bindings, ACTION_PREVIOUS_SLOT);
  }
  /* Old releases had no swipe virtual inputs. Add them only in empty secondary
   * slots; a populated custom slot remains exactly as the user saved it. */
  if (!swipe_config_present)
    keys_add_swipe_defaults_if_free(key_bindings);

  combo_config_present = words >= CONFIG_WORDS && o[72] == COMBO_CONFIG_MAGIC;
  if (combo_config_present)
  {
    for (i = 0; i < NUM_SWIPE_DIRS / 2; i++)
    {
      swipe_action_masks[i * 2]     = (unsigned short)(o[73 + i] & 0x3FFu);
      swipe_action_masks[i * 2 + 1] = (unsigned short)((o[73 + i] >> 16) & 0x3FFu);
    }
    for (i = 0; i < NUM_COMBO_SLOTS; i++)
    {
      combo_bindings[i].trigger     = binding_unpack(o[77 + i] & 0xFFFFu);
      combo_bindings[i].action_mask = (unsigned short)((o[77 + i] >> 16) & 0x3FFu);
    }
  }
  else
  {
    combos_set_defaults(swipe_action_masks, combo_bindings);
  }

  clamp_rom_buffer_size();
  nspire_emulator_options_apply();
  return 0;
}

static int write_global_file(void)
{
  char path[600];
  u32 o[CONFIG_WORDS];
  u32 i;
  FILE *f;
  int ok;

  memset(o, 0, sizeof(o));
  o[0] = screen_scale;
  o[1] = screen_filter;
  o[4] = update_backup_flag;
  o[5] = global_enable_analog;
  o[6] = analog_sensitivity_level;
  for (i = 0; i < 15; i++)
    o[7 + i] = key_bindings[i][0].with ? 0 : key_bindings[i][0].key;
  clamp_rom_buffer_size();
  o[23] = nspire_bios_choice % 2;
  o[24] = current_frameskip_type % 3;
  o[25] = frameskip_value;
  o[26] = FS_V2_MARK;
  o[27] = random_skip % 2;
  o[28] = nspire_sprlim_choice % 2;
  o[29] = nspire_rtc_choice % 3;
  o[30] = nspire_frame_mix_choice % 2;
  o[31] = nspire_rom_buffer_size_choice;
  o[32] = nspire_lcd_cache_clean_full % 2;
  o[33] = nspire_fps_overlay ? 1 : 0;
  o[34] = nspire_gba_blend_off % 2;
  o[35] = nspire_dynarec_ram_policy % 2;
  o[36] = nspire_video_renderer_choice % 2;
  o[37] = QOL_MAGIC;
  for (i = 0; i < NUM_ACTIONS; i++)
    o[i < FIRST_ACTIONS ? 38 + i : 64 + i - FIRST_ACTIONS] =
        binding_pack(key_bindings[i][0]) | (binding_pack(key_bindings[i][1]) << 16);
  o[58] = nspire_auto_increment;
  o[59] = nspire_save_on_exit;
  o[60] = nspire_load_on_start;
  o[61] = nspire_cpu_speed;
  o[62] = nspire_fps_overlay;
  o[63] = nspire_brightness;
  o[66] = nspire_compress_saves % 2;
  o[67] = SWIPE_CONFIG_MAGIC;
  o[68] = nspire_touchpad_swipe % 2;
  o[69] = nspire_touchpad_deadzone % SWIPE_DEADZONE_CHOICES;
  o[70] = nspire_touchpad_sensitivity % SWIPE_SENSITIVITY_CHOICES;
  o[71] = 0x100u | (nspire_touchpad_diagonal_weight % 2);

  o[72] = COMBO_CONFIG_MAGIC;
  for (i = 0; i < NUM_SWIPE_DIRS / 2; i++)
    o[73 + i] = ((u32)(swipe_action_masks[i * 2] & 0x3FFu)) |
                (((u32)(swipe_action_masks[i * 2 + 1] & 0x3FFu)) << 16);
  for (i = 0; i < NUM_COMBO_SLOTS; i++)
    o[77 + i] = (binding_pack(combo_bindings[i].trigger) & 0xFFFFu) |
                (((u32)(combo_bindings[i].action_mask & 0x3FFu)) << 16);

  /* The copy in the saves folder, then the program file. */
  nspire_save_dir_make();
  nspire_save_file(path, sizeof(path), CONFIG_FILE);
  f = fopen(path, "wb");
  ok = 0;
  if (f)
  {
    ok = fwrite(o, 4, CONFIG_WORDS, f) == CONFIG_WORDS;
    if (fclose(f) != 0)
      ok = 0;
  }
  if (selfstore_write(nspire_program_path, o, sizeof(o)))
    ok = 1;
  return ok ? 0 : -1;
}

/* ---- A game's own settings and keys ---- */

static void game_file_path(const char *rom_path, char *path, size_t size)
{
  char base[512];
  nspire_game_base(rom_path, base, sizeof(base));
  snprintf(path, size, "%s.settings.tns", base);
}

static void write_game_file(void)
{
  FILE *f;
  int i, slot;

  if (!game_open)
    return;
  if (!own_settings && !own_keys)
  {
    remove(game_file);
    return;
  }
  f = fopen(game_file, "w");
  if (!f)
    return;
  fprintf(f, "version=%d\n", GAME_FILE_VERSION);
  if (own_settings)
  {
    fprintf(f, "own_settings=1\n");
    for (i = 0; i < NUM_SETTINGS; i++)
      fprintf(f, "%s=%u\n", settings[i].name, (unsigned)*settings[i].value);
  }
  if (own_keys)
  {
    fprintf(f, "own_keys=1\n");
    for (i = 0; i < NUM_ACTIONS; i++)
      for (slot = 0; slot < KEY_SLOTS; slot++)
        fprintf(f, "key_%s%s=%u\n", action_ids[i], slot ? "_2" : "",
                binding_pack(key_bindings[i][slot]));
    for (i = 0; i < NUM_SWIPE_DIRS; i++)
      fprintf(f, "swipe_mask_%s=%u\n", swipe_mask_ids[i],
              (unsigned)(swipe_action_masks[i] & 0x3FFu));
    for (i = 0; i < NUM_COMBO_SLOTS; i++)
    {
      fprintf(f, "combo_key_%d=%u\n", i + 1,
              binding_pack(combo_bindings[i].trigger));
      fprintf(f, "combo_mask_%d=%u\n", i + 1,
              (unsigned)(combo_bindings[i].action_mask & 0x3FFu));
    }
  }
  fclose(f);
}

void nspire_config_open_game(const char *rom_path)
{
  char line[200];
  u32 values[NUM_SETTINGS];
  unsigned char in_file[NUM_SETTINGS];
  key_binding keys[NUM_ACTIONS][KEY_SLOTS];
  unsigned short swipes[NUM_SWIPE_DIRS];
  combo_binding combos[NUM_COMBO_SLOTS];
  int has_settings = 0, has_keys = 0, version = 1, i;
  FILE *f;

  nspire_config_close_game();
  capture(global_values);
  memcpy(global_keys, key_bindings, sizeof(global_keys));
  memcpy(global_swipe_masks, swipe_action_masks, sizeof(global_swipe_masks));
  memcpy(global_combos, combo_bindings, sizeof(global_combos));
  memcpy(values, global_values, sizeof(values));
  memcpy(keys, key_bindings, sizeof(keys));
  memcpy(swipes, swipe_action_masks, sizeof(swipes));
  memcpy(combos, combo_bindings, sizeof(combos));
  memset(in_file, 0, sizeof(in_file));
  game_open = 1;
  own_settings = own_keys = 0;

  game_file_path(rom_path, game_file, sizeof(game_file));
  f = fopen(game_file, "r");
  if (f)
  {
    while (fgets(line, sizeof(line), f))
    {
      char *value = strchr(line, '=');
      unsigned long number;
      if (!value)
        continue;
      *value++ = 0;
      number = strtoul(value, NULL, 10);
      if (!strcmp(line, "version"))
        version = (int)number;
      else if (!strcmp(line, "own_settings"))
        has_settings = number != 0;
      else if (!strcmp(line, "own_keys"))
        has_keys = number != 0;
      else if (!strncmp(line, "swipe_mask_", 11))
      {
        for (i = 0; i < NUM_SWIPE_DIRS; i++)
          if (!strcmp(line + 11, swipe_mask_ids[i]))
            swipes[i] = (unsigned short)(number & 0x3FFu);
      }
      else if (!strncmp(line, "combo_key_", 10))
      {
        int idx = atoi(line + 10) - 1;
        if (idx >= 0 && idx < NUM_COMBO_SLOTS)
          combos[idx].trigger = binding_unpack((unsigned)number);
      }
      else if (!strncmp(line, "combo_mask_", 11))
      {
        int idx = atoi(line + 11) - 1;
        if (idx >= 0 && idx < NUM_COMBO_SLOTS)
          combos[idx].action_mask = (unsigned short)(number & 0x3FFu);
      }
      else if (!strncmp(line, "key_", 4))
      {
        size_t len = strlen(line + 4);
        int second = len > 2 && !strcmp(line + 4 + len - 2, "_2");
        for (i = 0; i < NUM_ACTIONS; i++)
          if (strlen(action_ids[i]) == len - (second ? 2 : 0) &&
              !strncmp(line + 4, action_ids[i], strlen(action_ids[i])))
            keys[i][second] = binding_unpack((unsigned)number);
      }
      else
        for (i = 0; i < NUM_SETTINGS; i++)
          if (!strcmp(line, settings[i].name))
          {
            values[i] = (u32)number % settings[i].count;
            in_file[i] = 1;
          }
    }
    fclose(f);
  }

  if (has_settings && version < 3)
  {
    /* Files predating version 3 use the old scale and CPU-speed values. */
    for (i = 0; i < NUM_SETTINGS; i++)
      if (settings[i].value == &screen_scale)
        values[i] = scaled_raw;
      else if (settings[i].value == &nspire_cpu_speed && in_file[i])
        values[i] = cpu_speed_from_qol1(values[i]);
  }
  if (has_settings)
  {
    own_settings = 1;
    apply(values);
  }
  if (has_keys)
  {
    own_keys = 1;
    if (version < 3)
    {
      keys_add_default_if_free(keys, ACTION_NEXT_SLOT);
      keys_add_default_if_free(keys, ACTION_PREVIOUS_SLOT);
    }
    /* Profiles saved before swipe inputs existed inherit safe swipe defaults
     * in still-empty second slots without overwriting a user's older bindings. */
    if (version < 5)
      keys_add_swipe_defaults_if_free(keys);
    memcpy(key_bindings, keys, sizeof(keys));
    memcpy(swipe_action_masks, swipes, sizeof(swipes));
    memcpy(combo_bindings, combos, sizeof(combos));
  }
  /* A raised CPU speed froze this game last time (nspire_clock.c). */
  if (nspire_clock_crashed_rom[0] && !strcmp(nspire_clock_crashed_rom, rom_path))
  {
    nspire_cpu_speed = 0;
    nspire_clock_crashed_rom[0] = 0;
    write_game_file();
  }
  nspire_settings_for = (u32)own_settings;
  nspire_keys_for = (u32)own_keys;
  clamp_rom_buffer_size();
  nspire_emulator_options_apply();
}

void nspire_config_close_game(void)
{
  if (!game_open)
    return;
  write_game_file();
  if (own_settings)
    apply(global_values);
  else
    capture(global_values);
  if (own_keys)
  {
    memcpy(key_bindings, global_keys, sizeof(global_keys));
    memcpy(swipe_action_masks, global_swipe_masks, sizeof(global_swipe_masks));
    memcpy(combo_bindings, global_combos, sizeof(global_combos));
  }
  else
  {
    memcpy(global_keys, key_bindings, sizeof(global_keys));
    memcpy(global_swipe_masks, swipe_action_masks, sizeof(global_swipe_masks));
    memcpy(global_combos, combo_bindings, sizeof(global_combos));
  }
  game_open = own_settings = own_keys = 0;
  nspire_settings_for = nspire_keys_for = 0;
  nspire_emulator_options_apply();
}

/* The menu changed "Settings for": this game's own starts from the values in
 * use, and going back to all games drops them. */
void nspire_config_settings_for_changed(void)
{
  if (!game_open)
  {
    nspire_settings_for = 0;
    return;
  }
  if (nspire_settings_for && !own_settings)
  {
    capture(global_values);
    own_settings = 1;
  }
  else if (!nspire_settings_for && own_settings)
  {
    own_settings = 0;
    apply(global_values);
    nspire_emulator_options_apply();
  }
}

void nspire_config_keys_for_changed(void)
{
  if (!game_open)
  {
    nspire_keys_for = 0;
    return;
  }
  if (nspire_keys_for && !own_keys)
  {
    memcpy(global_keys, key_bindings, sizeof(global_keys));
    memcpy(global_swipe_masks, swipe_action_masks, sizeof(global_swipe_masks));
    memcpy(global_combos, combo_bindings, sizeof(global_combos));
    own_keys = 1;
  }
  else if (!nspire_keys_for && own_keys)
  {
    own_keys = 0;
    memcpy(key_bindings, global_keys, sizeof(global_keys));
    memcpy(swipe_action_masks, global_swipe_masks, sizeof(global_swipe_masks));
    memcpy(combo_bindings, global_combos, sizeof(global_combos));
  }
}

/* Writes the settings for all games, and the open game's own. */
s32 nspire_config_save(void)
{
  u32 live[NUM_SETTINGS];
  key_binding live_keys[NUM_ACTIONS][KEY_SLOTS];
  unsigned short live_swipes[NUM_SWIPE_DIRS];
  combo_binding live_combos[NUM_COMBO_SLOTS];
  s32 result;

  /* What isn't the game's own belongs to all games. */
  if (!(game_open && own_settings))
    capture(global_values);
  if (!(game_open && own_keys))
  {
    memcpy(global_keys, key_bindings, sizeof(global_keys));
    memcpy(global_swipe_masks, swipe_action_masks, sizeof(global_swipe_masks));
    memcpy(global_combos, combo_bindings, sizeof(global_combos));
  }

  capture(live);
  memcpy(live_keys, key_bindings, sizeof(live_keys));
  memcpy(live_swipes, swipe_action_masks, sizeof(live_swipes));
  memcpy(live_combos, combo_bindings, sizeof(live_combos));
  apply(global_values);
  memcpy(key_bindings, global_keys, sizeof(global_keys));
  memcpy(swipe_action_masks, global_swipe_masks, sizeof(global_swipe_masks));
  memcpy(combo_bindings, global_combos, sizeof(global_combos));
  result = write_global_file();
  apply(live);
  memcpy(key_bindings, live_keys, sizeof(live_keys));
  memcpy(swipe_action_masks, live_swipes, sizeof(live_swipes));
  memcpy(combo_bindings, live_combos, sizeof(live_combos));

  write_game_file();
  return result;
}
