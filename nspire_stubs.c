/* Globals and small stubs normally provided by libretro/libretro.c */

#include "common.h"
#include "main.h"
#include "cpu.h"
#include "gba_memory.h"

#include <stddef.h>
#include <stdint.h>

int dynarec_enable = 1;
/* Default 1 = official file (legacy standalone behavior before menu option). */
u32 nspire_bios_choice = 1;
/* Libretro gpsp_sprlim default "disabled": 0=hardware cap, 1=no cap. */
u32 nspire_sprlim_choice = 0;
/* Libretro gpsp_rtc: 0=auto, 1=enabled, 2=disabled. */
u32 nspire_rtc_choice = 0;
/* Libretro gpsp_frame_mixing: 0=off, 1=on. */
u32 nspire_frame_mix_choice = 0;
/* 0 = range D-clean of LCD buffer; 1 = full clean_dcache before display swap. */
u32 nspire_lcd_cache_clean_full = 0;
/* 1 = draw FPS overlay on presented game frames. */
u32 nspire_fps_overlay = 0;
/* 1 = F-key cycles FF levels (default); 0 = F-key toggles FF1 vs normal. */
u32 nspire_ff_key_style = 1;
/* 1 = skip PPU BLDCNT blending / brightness passes (performance; inaccurate). */
u32 nspire_gba_blend_off = 0;
/* 0 = libretro video.cc renderer; 1 = legacy old_video/video.c renderer. */
u32 nspire_video_renderer_choice = 0;
/* 0 = partial SMC (default); 1 = classic — flush_dynarec_caches on SMC + full
 * PC-relative block linking from RAM code (same emits as ROM blocks). */
u32 nspire_dynarec_ram_policy = 0;
/* RAM dynarec block reuse (partial SMC only; see nspire_ram_reuse_menu_hook). */
u32 nspire_dynarec_block_reuse = 0;
/* Runtime ROM page-cache target in MiB (2..32). */
u32 nspire_rom_buffer_size_choice = 8;
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 1;
u32 num_skipped_frames = 0;
u32 skip_next_frame = 0;

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
  (void)client_id;
  (void)buf;
  (void)len;
}

void netpacket_poll_receive(void) {}

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;

u32 netplay_num_clients = 0;
u32 netplay_client_id = 0;

void game_name_ext(char *src, char *buffer, char *extension)
{
  (void)extension;
  if (buffer && src)
    strcpy(buffer, src);
}

u32 quick_save_slot = 0;

gpsp_gui_cheat_entry cheats[16];
u32 num_cheats = 0;

void nspire_emulator_options_apply(void)
{
  static u32 last_mix = (u32)-1;
  static u32 last_ram_policy;
  static u32 ram_policy_seen;
  static u32 last_block_reuse;
  static u32 block_reuse_seen;

  if (nspire_rom_buffer_size_choice < 2)
    nspire_rom_buffer_size_choice = 2;
  else if (nspire_rom_buffer_size_choice > 32)
    nspire_rom_buffer_size_choice = 32;
  nspire_video_renderer_choice %= 2;

  sprite_limit = (nspire_sprlim_choice == 0) ? 1 : 0;
  if (last_mix != nspire_frame_mix_choice)
  {
    last_mix = nspire_frame_mix_choice;
    nspire_video_mix_reset();
  }

  if (!ram_policy_seen)
  {
    ram_policy_seen = 1;
    last_ram_policy = nspire_dynarec_ram_policy;
  }
  else if (last_ram_policy != nspire_dynarec_ram_policy)
  {
    last_ram_policy = nspire_dynarec_ram_policy;
    if (nspire_dynarec_ram_policy)
      nspire_dynarec_block_reuse = 0;
#ifdef HAVE_DYNAREC
    /* Flushing before init_emitter / init_bios_hooks can crash the device. */
    if (bios_swi_entrypoint)
      flush_dynarec_caches();
#endif
  }

  if (!block_reuse_seen)
  {
    block_reuse_seen = 1;
    last_block_reuse = nspire_dynarec_block_reuse;
  }
  else if (last_block_reuse != nspire_dynarec_block_reuse)
  {
    last_block_reuse = nspire_dynarec_block_reuse;
#ifdef HAVE_DYNAREC
    if (bios_swi_entrypoint)
      flush_dynarec_caches();
#endif
  }
}

void nspire_dynarec_ram_policy_menu_hook(void)
{
  if (nspire_dynarec_ram_policy)
    nspire_dynarec_block_reuse = 0;
  nspire_emulator_options_apply();
}

void nspire_ram_reuse_menu_hook(void)
{
  if (nspire_dynarec_ram_policy)
    nspire_dynarec_block_reuse = 0;
  nspire_emulator_options_apply();
}

int nspire_rtc_force_value(void)
{
  static const int map[] = {
    FEAT_AUTODETECT,
    FEAT_ENABLE,
    FEAT_DISABLE
  };

  return map[nspire_rtc_choice % 3];
}

