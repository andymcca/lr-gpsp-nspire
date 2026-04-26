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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(NSPIRE_LIBRETRO)
static void nspire_sync_bios_menu_from_disk(void)
{
  char path[512];
  FILE *fp;
  long sz;
  unsigned char b;

  sprintf(path, "%s/gba_bios.bin.tns", main_path);

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
extern void nspire_present_frame(u32 skip_next_frame);

extern uint32_t not_crt0_savedsp;
extern u32 quick_save_slot;

frameskip_type current_frameskip_type = manual_frameskip;
u32 frameskip_value = 2;
u32 random_skip = 0;

u8 *file_ext[] = {(u8 *)".gba.tns", NULL};

u32 update_backup_flag = 1;
u32 synchronize_flag = 1;
s32 relative_frame_count = 0;
u32 clock_speed = 132;

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

static void switch_to_romdir(void)
{
  char buff[256];
  int r;
  file_open(romdir_file, "gpsp_romdir.tns", read);

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

  sprintf(buff, "%s/gpsp_romdir.tns", main_path);
  file_open(romdir_file, buff, write);

  if (file_check_valid(romdir_file))
  {
    if (getcwd(buff, sizeof(buff)) && buff[0])
      file_write(romdir_file, buff, strlen(buff));
    file_close(romdir_file);
  }
}

int load_state(u8 *filename)
{
  FILE *f;
  u8 *buf;

  if (!filename)
    return 0;
  f = fopen((char *)filename, "rb");
  if (!f)
    return 0;
  buf = (u8 *)malloc(GBA_STATE_MEM_SIZE);
  if (!buf)
  {
    fclose(f);
    return 0;
  }
  if (fread(buf, 1, GBA_STATE_MEM_SIZE, f) != GBA_STATE_MEM_SIZE)
  {
    free(buf);
    fclose(f);
    return 0;
  }
  fclose(f);
  if (gba_load_state(buf))
  {
    instruction_count = 0;
    reg[OAM_UPDATED] = 1;
    free(buf);
    return 1;
  }
  free(buf);
  return 0;
}

void save_state(u8 *filename, u16 *screen_capture)
{
  u8 *buf;
  FILE *f;

  (void)screen_capture;
  if (!filename)
    return;
#ifdef HAVE_DYNAREC
  if (dynarec_enable)
    flush_dynarec_caches();
#endif
  buf = (u8 *)malloc(GBA_STATE_MEM_SIZE);
  if (!buf)
    return;
  gba_save_state(buf);
  f = fopen((char *)filename, "wb");
  if (f)
  {
    fwrite(buf, 1, GBA_STATE_MEM_SIZE, f);
    fclose(f);
  }
  free(buf);
}

void quit(void)
{
  save_romdir();
  update_backup_force();
  memory_term();
  nspire_restore();
  exit(0);
}

void delay_us(u32 us_count) { msleep(us_count / 1000); }

void get_ticks_us(u64 *ticks_return) { *ticks_return = (u64)GetRTC() * 1000000ULL; }

static void capture_initial_sp(void)
{
  uintptr_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  not_crt0_savedsp = (uint32_t)sp;
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
    char path[512];

    sprintf(path, "%s/gba_bios.bin.tns", main_path);
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

static void emulation_loop(void)
{
  while (1)
  {
    if (update_input())
      continue;

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

    process_cheats();
    nspire_present_frame(skip_next_frame);
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
}

int main(int argc, char *argv[])
{
  u8 load_filename[512];

  /* Ndless: is_classic is (hwtype() < 1) from os.h — skip on classic models. */
  if (is_classic)
    return 0;

  capture_initial_sp();

  ChangeWorkingDirectory(argv[0]);
  getcwd(main_path, sizeof(main_path));
  load_config_file();
#if defined(NSPIRE_LIBRETRO)
  nspire_sync_bios_menu_from_disk();
#endif

  gamepak_filename[0] = 0;

  nspire_init();
  core_bootstrap();
  init_video();
  video_resolution_large();

  nspire_apply_bios();

  init_input();

  if (argc > 1)
  {
    if (load_gamepak(NULL, argv[1], nspire_rtc_force_value(), FEAT_AUTODETECT,
                     SERIAL_MODE_AUTO) != 0)
      quit();
    reset_gba();
    nspire_frameskip_reset();
    nspire_load_cartridge_backup();
    set_gba_resolution((video_scale_type)screen_scale);
    video_resolution_small();
  }
  else
  {
    switch_to_romdir();
    if (load_file(file_ext, load_filename) == -1)
      menu(copy_screen());
    else
    {
      if (load_gamepak(NULL, (char *)load_filename, nspire_rtc_force_value(),
                       FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0)
        quit();
      set_clock_speed();
      set_gba_resolution((video_scale_type)screen_scale);
      video_resolution_small();
      reset_gba();
      nspire_frameskip_reset();
      nspire_load_cartridge_backup();
    }
  }

  if (quick_save_slot)
  {
    u8 current_savestate_filename[512];
    get_savestate_filename_noshot(quick_save_slot - 1, current_savestate_filename);
    if (load_state(current_savestate_filename))
      quick_save_slot = 0;
  }

  emulation_loop();
  return 0;
}
