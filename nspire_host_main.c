/* Ndless entry: libretro gpSP core + legacy menu/GUI. */

#include "common.h"
#include "gba_memory.h"
#include "savestate.h"
#include "serial.h"
#include "cheats.h"
#include "gui.h"
#include "cpu.h"
#include "video.h"
#include "nspire.h"
#include "nspire_gui_video.h"
#include "nspire_frameskip.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"
#include "nspire_states.h"
#include "nspire_clock.h"
#include "nspire_brightness.h"
#include "nspire_paths.h"

#include <os.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The official BIOS file: in the saves folder, where gpSP moves it from
 * its own folder at start (nspire_paths.h), else still next to gpSP. */
static void bios_path(char *path, size_t size)
{
  FILE *f;
  nspire_save_file(path, size, "gba_bios.bin.tns");
  if ((f = fopen(path, "rb")) != NULL)
  {
    fclose(f);
    return;
  }
  snprintf(path, size, "%s/gba_bios.bin.tns", main_path);
}

#if defined(NSPIRE_LIBRETRO)
static void nspire_sync_bios_menu_from_disk(void)
{
  char path[600];
  FILE *fp;
  long sz;
  unsigned char b;

  bios_path(path, sizeof(path));

  fp = fopen(path, "rb");
  if (!fp)
  {
    nspire_bios_choice = 0;
    return;
  }
  if (fread(&b, 1, 1, fp) != 1)
  {
    fclose(fp);
    nspire_bios_choice = 0;
    return;
  }
  if (b != 0x18U)
  {
    fclose(fp);
    nspire_bios_choice = 0;
    return;
  }
  fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  fclose(fp);
  if (sz < 0x4000L)
  {
    nspire_bios_choice = 0;
    return;
  }
  nspire_bios_choice = 1;
}
#endif

extern void init_gamepak_buffer(void);
extern u32 nspire_input_frame(void);
extern void nspire_input_release_all(void);

extern uint32_t not_crt0_savedsp;
extern u32 quick_save_slot;

/* Automatic frameskip works now (nspire_frameskip.c), so it's the default:
 * every frame is drawn unless the game falls behind. */
u32 current_frameskip_type = auto_frameskip;
u32 frameskip_value = 4;
u32 random_skip = 0;

u8 *file_ext[] = {(u8 *)".gba.tns", (u8 *)".gbz.tns", (u8 *)".zip.tns", NULL};

u32 update_backup_flag = 1;
u32 synchronize_flag = 1;
s32 relative_frame_count = 0;
u32 clock_speed = 132;

int nspire_skip_state_load;

void set_clock_speed(void) {}

void change_ext(u8 *src, u8 *buffer, u8 *extension)
{
  char *dot_position;
  strcpy((char *)buffer, (char *)src);
  dot_position = strrchr((char *)buffer, '.');

  if (dot_position)
    strcpy(dot_position, (char *)extension);
  else
  {
    strcat((char *)buffer, ".");
    strcat((char *)buffer, (char *)extension);
  }
}

u32 file_length(u8 *dummy, FILE *fp)
{
  u32 length;
  (void)dummy;
  fseek(fp, 0, SEEK_END);
  length = (u32)ftell(fp);
  fseek(fp, 0, SEEK_SET);
  return length;
}

static void ChangeWorkingDirectory(char *exe)
{
  char *s = strrchr(exe, '/');
  if (s != NULL)
  {
    *s = '\0';
    chdir(exe);
    *s = '/';
  }
}

/* The file list's last folder, kept in the saves folder. */
static void switch_to_romdir(void)
{
  char buff[600];
  int r;
  nspire_save_file(buff, sizeof(buff), "gpsp_romdir.tns");
  file_open(romdir_file, buff, read);

  if (file_check_valid(romdir_file))
  {
    r = file_read(romdir_file, buff, sizeof(buff) - 1);
    if (r > 0)
    {
      buff[r] = 0;
      while (r > 0 && isspace((unsigned char)buff[r - 1]))
        buff[--r] = 0;
      chdir(buff);
    }
    file_close(romdir_file);
  }
}

static void save_romdir(void)
{
  char buff[512];

  nspire_save_file(buff, sizeof(buff), "gpsp_romdir.tns");
  file_open(romdir_file, buff, write);

  if (file_check_valid(romdir_file))
  {
    /* getcwd is the OS's NU_Current_Dir here, which returns 0 when it
     * works: what it wrote is checked instead. */
    buff[0] = 0;
    getcwd(buff, sizeof(buff));
    if (buff[0])
      file_write(romdir_file, buff, strlen(buff));
    file_close(romdir_file);
  }
}

/* A state file by name (the menu's "load savestate from file"). */
int load_state(u8 *filename)
{
  if (!filename)
    return 0;
  if (states_load_file((const char *)filename) != 1)
    return 0;
  instruction_count = 0;
  reg[OAM_UPDATED] = 1;
  return 1;
}

/* ---- Save states ---- */

int nspire_save_target(void)
{
  return nspire_auto_increment ? states_next_new_slot() : (int)savestate_slot;
}

int nspire_save_state_now(void)
{
  int slot = nspire_save_target(), saved = 0;

  nspire_clock_normal();
  if (slot < 0)
    nspire_osd_message("All 1000 slots are used");
  else if (!states_save(slot))
    nspire_osd_message("Couldn't save slot %03d (out of space?)", slot);
  else
  {
    savestate_slot = (u32)slot;
    nspire_osd_message("Saved slot %03d", slot);
    saved = 1;
  }
  nspire_clock_game();
  return saved;
}

int nspire_load_state_slot(int slot)
{
  int result = states_load(slot);

  if (result == 1)
  {
    savestate_slot = (u32)slot;
    nspire_osd_message("Loaded slot %03d", slot);
  }
  else if (result == 0)
    nspire_osd_message("Slot %03d is empty", slot);
  else
    nspire_osd_message("Couldn't load slot %03d", slot);
  return result;
}

void save_state(u8 *filename, u16 *screen_capture)
{
  (void)filename;
  (void)screen_capture;
  nspire_save_state_now();
}

/* ---- Starting and leaving games ---- */

int nspire_start_game(const char *rom_path)
{
  char full_path[512];
  int newest;

  /* The file browser gives a name in the folder it's in. The game's saves,
   * states and settings are named after the ROM's path, and browsing for
   * another game changes folders, so the path is made absolute. */
  if (rom_path[0] != '/')
  {
    char cwd[512];
    size_t len;
    cwd[0] = 0;
    getcwd(cwd, sizeof(cwd));
    len = strlen(cwd);
    snprintf(full_path, sizeof(full_path), "%s%s%s", cwd, len && cwd[len - 1] == '/' ? "" : "/",
             rom_path);
    rom_path = full_path;
  }

  nspire_loading_start(rom_path);
  nspire_game_move_old_files(rom_path);
  nspire_config_open_game(rom_path);
  nspire_apply_bios();
  if (load_gamepak(NULL, rom_path, nspire_rtc_force_value(), FEAT_AUTODETECT,
                   SERIAL_MODE_AUTO) != 0)
  {
    nspire_config_close_game();
    nspire_loading_failed(nspire_rom_load_error ? nspire_rom_load_error :
                          "Couldn't read this game. Is it a GBA ROM?");
    return 0;
  }
  reset_gba();
  instruction_count = 0;
  reg[OAM_UPDATED] = 1;
  nspire_frameskip_reset();
  nspire_load_cartridge_backup();
  states_open_game(gamepak_filename);
  newest = states_newest();
  savestate_slot = newest >= 0 ? (u32)newest : 0;
  nspire_osd_clear();
  synchronize_flag = 1;

  if (nspire_load_on_start && !nspire_skip_state_load && newest >= 0)
  {
    char line[40];
    snprintf(line, sizeof(line), "Loading its state (slot %03d)", newest);
    nspire_loading_note(line);
    if (states_load(newest) == 1)
      nspire_osd_message("Resumed from slot %03d", newest);
    else
      nspire_osd_message("Couldn't resume from slot %03d", newest);
  }
  nspire_skip_state_load = 0;
  nspire_loading_end();
  nspire_reset_timing();
  return 1;
}

void nspire_leave_game(void)
{
  if (!gamepak_filename[0])
    return;
  nspire_clock_normal();
  nspire_perf_log_flush();
  if (nspire_save_on_exit)
  {
    int slot = nspire_save_target();
    if (slot >= 0 && states_save(slot))
      savestate_slot = (u32)slot;
  }
  update_backup_force();
  nspire_config_close_game();
}

void quit(void)
{
  nspire_clock_normal();
  nspire_clock_left_game();
  nspire_perf_log_flush();
  save_romdir();
  update_backup_force();
  memory_term();
  nspire_brightness_restore();
  nspire_restore();
  exit(0);
}

/* Leaves the game the way Save+Exit and "Exit gpSP" do. */
void nspire_quit_game(void)
{
  nspire_leave_game();
  save_config_file();
  quit();
}

void delay_us(u32 us_count)
{
  nspire_wait_until(nspire_ticks() + (u32)(((u64)us_count * NSPIRE_TICK_HZ) / 1000000));
}

void get_ticks_us(u64 *ticks_return)
{
  *ticks_return = (u64)nspire_ticks() * 1000000ULL / NSPIRE_TICK_HZ;
}

static void capture_initial_sp(void)
{
#if defined(__arm__)
  uintptr_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  not_crt0_savedsp = (uint32_t)sp;
#endif
}

void nspire_bios_error_wait(const char *msg)
{
  gui_action_type gui_action = CURSOR_NONE;

  debug_screen_start();
  debug_screen_printl(msg);
  debug_screen_printl("Press any key.");
  debug_screen_update();
  while (gui_action == CURSOR_NONE)
  {
    gui_action = get_gui_input();
    delay_us(15000);
  }
  debug_screen_end();
}

int nspire_apply_bios(void)
{
  if (nspire_bios_choice == 0)
  {
    memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
    return 0;
  }

  {
    char path[600];

    bios_path(path, sizeof(path));
    if (load_bios(path) != 0 || bios_rom[0] != 0x18U)
    {
      memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
      nspire_bios_choice = 0;
    }
    return 0;
  }
}

static void core_bootstrap(void)
{
  init_gamepak_buffer();
  init_sound();
  if (!gba_screen_pixels)
  {
    gba_screen_pixels = (u16 *)malloc(GBA_SCREEN_BUFFER_SIZE);
    if (!gba_screen_pixels)
      quit();
    memset(gba_screen_pixels, 0, GBA_SCREEN_BUFFER_SIZE);
  }
  selected_boot_mode = boot_game;
}

/* ---- Playing ---- */

static int in_speed_test;

/* The game jumped where no code can run (nspire_bad_jump, cpu.h): a damaged
 * ROM (an Advance Wars from a bad dump does it as it starts) or a game gone
 * wrong. The core stopped there instead of crashing the calculator. This
 * says so and closes the game without saving a state of it (it's in
 * pieces); then the file list comes up again, as after a game that doesn't
 * load (leaving it opens the menu, without a game). */
static void game_stopped(void)
{
  char why[96];
  u8 load_filename[512];
  int started = 0;

  nspire_clock_normal();
  nspire_perf_log_flush();
  nspire_input_release_all();
  video_resolution_large();
  snprintf(why, sizeof(why), "It jumped to %08X, where nothing\ncan run: its ROM is probably damaged.",
           (unsigned)nspire_bad_jump_pc);
  nspire_bad_jump = 0;
  nspire_game_stopped(why);
  update_backup_force();
  nspire_config_close_game();
  gamepak_filename[0] = 0;

  switch_to_romdir();
  while (!started)
  {
    if (load_file(file_ext, load_filename) == -1)
    {
      menu(copy_screen());
      break;
    }
    started = nspire_start_game((char *)load_filename);
  }
  if (started)
  {
    set_gba_resolution((video_scale_type)screen_scale);
    video_resolution_small();
  }
  nspire_clock_game();
  nspire_reset_timing();
}

static void run_frame(void)
{
  u32 output_ticks = 0;

  rumble_frame_reset();
  nspire_frameskip_begin_frame();

#ifdef HAVE_DYNAREC
  if (dynarec_enable)
    execute_arm_translate(execute_cycles);
  else
#endif
  {
    clear_gamepak_stickybits();
    execute_arm(execute_cycles);
  }
  if (nspire_bad_jump)
  {
    /* The speed test stops and puts the game back as it was. */
    if (!in_speed_test)
      game_stopped();
    return;
  }

  process_cheats();
  if (!skip_next_frame)
  {
    u32 start = nspire_ticks();
    nspire_present_frame();
    output_ticks = nspire_ticks() - start;
  }
  nspire_frameskip_end_frame(output_ticks);
  update_backup();

  switch (serial_mode)
  {
  case SERIAL_MODE_RFU:
    rfu_frame_update();
    break;
  case SERIAL_MODE_SERIAL_POKE:
    serialpoke_frame_update();
    break;
  default:
    break;
  }
}

static void open_menu(void)
{
  nspire_clock_normal();
  nspire_perf_log_flush();
  nspire_input_release_all();
  /* Called directly, on this stack: the frame has ended, so the dynarec
   * isn't using it. */
  menu(copy_screen());
  keys_wait_release();
  nspire_clock_game();
  nspire_reset_timing();
}

static void change_slot(int delta)
{
  savestate_slot = (savestate_slot + STATE_SLOTS + delta) % STATE_SLOTS;
  nspire_osd_message(states_exists((int)savestate_slot) ? "Slot %03d (saved)" : "Slot %03d (empty)",
                     (int)savestate_slot);
}

#define HOTKEY(action) (hotkeys & (1u << (action)))

/* The hotkeys pressed this frame (nspire_input_frame). */
static void do_hotkeys(u32 hotkeys)
{
  if (HOTKEY(ACTION_MENU))
  {
    open_menu();
    return;
  }
  if (HOTKEY(ACTION_QUIT))
    nspire_quit_game();
  if (HOTKEY(ACTION_SAVE_STATE))
  {
    nspire_save_state_now();
    nspire_reset_timing();
  }
  if (HOTKEY(ACTION_LOAD_STATE))
  {
    nspire_load_state_slot((int)savestate_slot);
    nspire_reset_timing();
  }
  if (HOTKEY(ACTION_LOAD_NEWEST))
  {
    if (states_newest() >= 0)
      nspire_load_state_slot(states_newest());
    else
      nspire_osd_message("No saved states yet");
    nspire_reset_timing();
  }
  if (HOTKEY(ACTION_NEXT_SLOT))
    change_slot(1);
  if (HOTKEY(ACTION_PREVIOUS_SLOT))
    change_slot(-1);
  if (HOTKEY(ACTION_SHOW_FPS))
  {
    nspire_fps_overlay = nspire_fps_overlay ? 0 : 1;
    nspire_osd_message(nspire_fps_overlay ? "FPS counter on" : "FPS counter off");
  }
  if (HOTKEY(ACTION_FAST_FORWARD))
  {
    synchronize_flag ^= 1;
    nspire_osd_message(synchronize_flag ? "Fast forward off" : "Fast forward on");
    nspire_reset_timing();
  }
  if (HOTKEY(ACTION_RESTART))
  {
    nspire_apply_bios();
    reset_gba();
    nspire_osd_message("Game restarted");
    nspire_reset_timing();
  }
  if (HOTKEY(ACTION_BRIGHTER))
    nspire_brightness_step(1);
  if (HOTKEY(ACTION_DIMMER))
    nspire_brightness_step(-1);
}

static void emulation_loop(void)
{
  nspire_reset_timing();
  while (1)
  {
    u32 hotkeys = nspire_input_frame();
    if (hotkeys)
      do_hotkeys(hotkeys);
    run_frame();
  }
}

/* ---- Speed test ---- */

struct speed_test_part
{
  const char *name;
  enum nspire_pace_mode pace;
  u32 seconds;
};

static const struct speed_test_part speed_test_parts[] =
{
  { "Normal play, frameskip off", PACE_NORMAL, 10 },
  { "No speed limit, every frame drawn", PACE_UNCAPPED_DRAW_ALL, 5 },
  { "No speed limit, nothing drawn", PACE_UNCAPPED_DRAW_NONE, 5 },
};

#define SPEED_TEST_PARTS (int)(sizeof(speed_test_parts) / sizeof(speed_test_parts[0]))
#define TENTHS(v) (unsigned)((v) / 10), (unsigned)((v) % 10)

/* Runs the game from where it is in three ways and reports frames per
 * second and where each frame's time goes; the game is put back as it was.
 * Called from the menu. */
void nspire_speed_test(void)
{
  struct nspire_perf_stats stats[SPEED_TEST_PARTS];
  void *snapshot;
  u32 saved_type = current_frameskip_type;
  u32 saved_value = frameskip_value, saved_fps = nspire_fps_overlay, saved_sync = synchronize_flag;
  u32 mhz;
  int done = 0, stopped = 0, i;
  char path[600], line[300];
  FILE *f;

  if (!gamepak_filename[0])
    return;
  snapshot = states_snapshot();
  if (!snapshot)
  {
    nspire_osd_message("Not enough memory for the speed test");
    return;
  }

  video_resolution_small();
  nspire_clock_game();
  mhz = nspire_cpu_mhz();
  keys_wait_release();
  synchronize_flag = 1;
  in_speed_test = 1;
  for (i = 0; i < SPEED_TEST_PARTS && !stopped; i++)
  {
    const struct speed_test_part *part = &speed_test_parts[i];
    struct nspire_perf base;
    u32 length = part->seconds * NSPIRE_TICK_HZ;

    states_restore(snapshot);
    current_frameskip_type = no_frameskip;
    nspire_fps_overlay = 2;
    nspire_pace = part->pace;
    nspire_osd_message("Speed test %d/%d (esc stops it)", i + 1, SPEED_TEST_PARTS);
    nspire_reset_timing();
    nspire_perf_on = 1;
    base = nspire_perf_totals;
    while (nspire_ticks() - base.start < length &&
           nspire_perf_totals.frames - base.frames < part->seconds * 400)
    {
      nspire_input_release_all();
      keys_poll();
      if (key_pressed(KEY_ESC))
      {
        stopped = 1;
        break;
      }
      run_frame();
      if (nspire_bad_jump)
      {
        stopped = 1;
        break;
      }
    }
    stats[i] = nspire_perf_since(&base, nspire_ticks());
    done = i + 1;
  }

  in_speed_test = 0;
  nspire_bad_jump = 0;
  nspire_pace = PACE_NORMAL;
  current_frameskip_type = saved_type;
  frameskip_value = saved_value;
  nspire_fps_overlay = saved_fps;
  synchronize_flag = saved_sync;
  states_restore(snapshot);
  free(snapshot);
  nspire_osd_clear();
  nspire_clock_normal();
  video_resolution_large();

  /* A new file each run: appending has garbled files on the calculator. */
  nspire_save_file(path, sizeof(path), "gpsp_results.txt.tns");
  f = fopen(path, "w");
  if (f)
  {
    fprintf(f, "== %s (%s), cpu %u MHz, %s LCD, renderer %s, scaling %u\n", gamepak_title, gamepak_code,
            (unsigned)mhz, nspire_lcd_portrait ? "portrait" : "landscape",
            nspire_video_renderer_choice ? "legacy" : "video.cc", (unsigned)screen_scale);
    nspire_clock_report(line, sizeof(line));
    fprintf(f, "%s\n", line);
    nspire_brightness_report(line, sizeof(line));
    fprintf(f, "%s\n", line);
    for (i = 0; i < done; i++)
      fprintf(f, "%s:\n  game %u.%u fps, drawn %u.%u; per frame: cpu %u.%u, gfx %u.%u, out %u.%u, idle %u.%u ms\n",
              speed_test_parts[i].name, TENTHS(stats[i].game_fps), TENTHS(stats[i].drawn_fps),
              TENTHS(stats[i].cpu_ms), TENTHS(stats[i].render_ms), TENTHS(stats[i].output_ms),
              TENTHS(stats[i].idle_ms));
    fclose(f);
  }

  /* Results, until a key is pressed. */
  keys_wait_release();
  do
  {
    u16 white = 0xFFFF, dim = 0x5294;
    clear_screen(0);
    snprintf(line, sizeof(line), "Speed test%s, CPU %u MHz", stopped ? " (stopped)" : "", (unsigned)mhz);
    print_string(line, white, 0, 10, 10);
    print_string("ms per frame:  cpu = running the game,", dim, 0, 10, 30);
    print_string("gfx = drawing it, out = showing it", dim, 0, 10, 40);
    for (i = 0; i < done; i++)
    {
      print_string(speed_test_parts[i].name, white, 0, 10, 60 + i * 34);
      snprintf(line, sizeof(line), "%u.%u fps  cpu %u.%u  gfx %u.%u  out %u.%u", TENTHS(stats[i].game_fps),
               TENTHS(stats[i].cpu_ms), TENTHS(stats[i].render_ms), TENTHS(stats[i].output_ms));
      print_string(line, dim, 0, 20, 72 + i * 34);
    }
    print_string("Saved to gpsp_results.txt. Press a key.", dim, 0, 10, 220);
    flip_screen();
    update_at_vblank();
    keys_poll();
  } while (!any_key_down());
  keys_wait_release();
  nspire_reset_timing();
}

/* ---- Start ---- */

/* ROMs opened from the calculator's documents start this gpSP: Ndless keeps
 * file associations in ndless.cfg.tns as "ext.gba=<program name without
 * .tns>" and finds the program by that name, so the name gpSP was started
 * as is registered. An association naming another program is left alone
 * (Ndless's rule); one naming a gpSP like this one (a name starting with
 * gpsp_ or _gpsp, such as gpsp_qol, this build's name before _gpSP) is
 * pointed at this one, so renaming the program keeps ROMs opening. */
static const char *const rom_types[] = { "gba", "gbz" };
#define NUM_ROM_TYPES (sizeof(rom_types) / sizeof(rom_types[0]))

static int gpsp_program_name(const char *value, size_t len)
{
  return len >= 5 && (!strncasecmp(value, "gpsp_", 5) || !strncasecmp(value, "_gpsp", 5));
}

/* The file is short: read it whole, change the lines, write it back. */
static void retarget_rom_types(const char *name)
{
  static char text[8192], out[8192 + 256];
  char path[600];
  size_t len, out_len = 0, name_len = strlen(name), i;
  int changed = 0;
  char *line;
  FILE *f;

  snprintf(path, sizeof(path), "%sndless/ndless.cfg.tns", get_documents_dir());
  f = fopen(path, "rb");
  if (!f)
    return;
  len = fread(text, 1, sizeof(text) - 1, f);
  if (!feof(f))   /* longer than any ndless.cfg.tns: leave it */
  {
    fclose(f);
    return;
  }
  fclose(f);
  text[len] = 0;

  for (line = text; *line; )
  {
    char *end = strchr(line, '\n');
    size_t line_len = end ? (size_t)(end - line + 1) : strlen(line);
    const char *value = NULL;
    size_t value_len;

    for (i = 0; i < NUM_ROM_TYPES; i++)
    {
      char key[16];
      snprintf(key, sizeof(key), "ext.%s=", rom_types[i]);
      if (!strncmp(line, key, strlen(key)))
        value = line + strlen(key);
    }
    value_len = value ? strcspn(value, "\r\n") : 0;
    if (value && gpsp_program_name(value, value_len) &&
        !(value_len == name_len && !strncmp(value, name, name_len)) &&
        out_len + (size_t)(value - line) + name_len + 1 < sizeof(out))
    {
      memcpy(out + out_len, line, value - line);
      out_len += value - line;
      memcpy(out + out_len, name, name_len);
      out_len += name_len;
      out[out_len++] = '\n';
      changed = 1;
    }
    else if (out_len + line_len < sizeof(out))
    {
      memcpy(out + out_len, line, line_len);
      out_len += line_len;
    }
    line += line_len;
  }

  if (changed && (f = fopen(path, "wb")) != NULL)
  {
    fwrite(out, 1, out_len, f);
    fclose(f);
  }
}

static void register_rom_types(const char *exe_path)
{
  const char *slash = strrchr(exe_path, '/');
  char name[64];
  size_t len, i;

  snprintf(name, sizeof(name), "%s", slash ? slash + 1 : exe_path);
  len = strlen(name);
  if (len > 4 && !strcasecmp(name + len - 4, ".tns"))
    name[len - 4] = 0;
  if (!name[0])
    return;
  retarget_rom_types(name);
  /* The types that have no program yet. */
  for (i = 0; i < NUM_ROM_TYPES; i++)
    cfg_register_fileext(rom_types[i], name);
}

int main(int argc, char *argv[])
{
  u8 load_filename[512];
  int started;

  /* Ndless: is_classic is (hwtype() < 1) from os.h — skip on classic models. */
  if (is_classic)
    return 0;

  capture_initial_sp();

  ChangeWorkingDirectory(argv[0]);
  getcwd(main_path, sizeof(main_path));
  /* The saves folder, made on the first start (also on a new calculator,
   * wherever gpSP is), and what earlier builds left in My Documents
   * (gpSP_saves) and next to gpSP moved in, before the settings are read
   * (nspire_paths.h). */
  snprintf(nspire_program_path, sizeof(nspire_program_path), "%s", argv[0]);
  nspire_save_dir_make();
  nspire_move_old_documents_files();
  nspire_move_old_files(main_path, 1);
  keys_set_defaults(key_bindings);
  /* Settings found elsewhere (the saves folder's copy, an older build's
   * file) go into the program file now, not at the next save. */
  if (load_config_file() != 0)
    nspire_tear_free = is_cx2 ? 1 : 0;
  else if (!nspire_config_in_program())
    save_config_file();
#if defined(NSPIRE_LIBRETRO)
  nspire_sync_bios_menu_from_disk();
#endif

  gamepak_filename[0] = 0;

  nspire_init();
  nspire_brightness_init();
  nspire_brightness_apply();
  nspire_clock_check_crash();
  register_rom_types(argv[0]);
  core_bootstrap();
  init_video();
  video_resolution_large();

  nspire_apply_bios();

  init_input();

  /* A game that doesn't load says why, then the file list comes up again
   * (leaving it opens the menu, without a game). */
  started = argc > 1 && nspire_start_game(argv[1]);
  if (!started)
    switch_to_romdir();
  while (!started)
  {
    if (load_file(file_ext, load_filename) == -1)
    {
      menu(copy_screen());
      break;
    }
    started = nspire_start_game((char *)load_filename);
  }
  if (started)
  {
    set_gba_resolution((video_scale_type)screen_scale);
    video_resolution_small();
  }

  nspire_clock_game();
  emulation_loop();
  return 0;
}
