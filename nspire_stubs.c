/* Globals and small stubs normally provided by libretro/libretro.c */

#include "common.h"
#include "main.h"
#include "cpu.h"

#include <stddef.h>
#include <stdint.h>

int dynarec_enable = 1;
/* Default 1 = official file (legacy standalone behavior before menu option). */
u32 nspire_bios_choice = 1;
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 0;
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

u32 save_backup(char *name)
{
  (void)name;
  return 0;
}

