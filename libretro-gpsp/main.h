/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>

#define TIMER_INACTIVE                0
#define TIMER_PRESCALE                1
#define TIMER_CASCADE                 2

#define TIMER_NO_IRQ                  0
#define TIMER_TRIGGER_IRQ             1

#define TIMER_DS_CHANNEL_NONE         0
#define TIMER_DS_CHANNEL_A            1
#define TIMER_DS_CHANNEL_B            2
#define TIMER_DS_CHANNEL_BOTH         3

typedef struct
{
  s32 count;
  u32 reload;
  u32 prescale;
  fixed8_24 frequency_step;
  u32 direct_sound_channels;
  u32 irq;
  u32 status;
} timer_type;

typedef enum
{
#ifdef NSPIRE_LIBRETRO
  auto_frameskip = 0,
  manual_frameskip,
  no_frameskip
#else
  no_frameskip = 0,
  auto_frameskip,
  auto_threshold_frameskip,
  fixed_interval_frameskip
#endif
} frameskip_type;

typedef enum
{
  auto_detect = 0,
  builtin_bios,
  official_bios
} bios_type;

typedef enum
{
  boot_game = 0,
  boot_bios
} boot_mode;

extern u32 gbc_update_count;

extern u32 frame_counter;
extern u32 cpu_ticks;
extern u32 execute_cycles;
extern u32 skip_next_frame;

extern u32 flush_ram_count;

extern char main_path[512];

u16 rand_gen();
void rand_seed(u32 data);

#define cycles_to_run(c) ((c) & 0x7FFF)
#define completed_frame(c) ((c) & 0x80000000)
u32 function_cc update_gba(int remaining_cycles);
void reset_gba(void);

void init_main(void);

void game_name_ext(char *src, char *buffer, char *extension);

#ifdef NSPIRE_LIBRETRO
extern u32 clock_speed;
extern frameskip_type current_frameskip_type;
extern u32 frameskip_value;
extern u32 random_skip;
/* 0 = built-in open BIOS, 1 = gba_bios.bin.tns next to executable */
extern u32 nspire_bios_choice;
/* Emulator options (see libretro core options of the same names). */
extern u32 nspire_sprlim_choice;
extern u32 nspire_rtc_choice;
extern u32 nspire_frame_mix_choice;
extern u32 nspire_lcd_cache_clean_full;
extern u32 nspire_fps_overlay;
/* 1 = skip BLDCNT alpha / fade / semi-transparent combine in video.cc (faster). */
extern u32 nspire_gba_blend_off;
/* 0 = libretro video.cc renderer; 1 = legacy old_video/video.c renderer. */
extern u32 nspire_video_renderer_choice;
/* 0 = partial SMC + conservative RAM branch emits (default). 1 = classic:
 * flush_dynarec_caches() on SMC and full PC-relative block linking from RAM
 * code (same as ROM emits). */
extern u32 nspire_dynarec_ram_policy;
extern u32 nspire_rom_buffer_size_choice;
void nspire_emulator_options_apply(void);
void nspire_dynarec_ram_policy_menu_hook(void);
void get_ticks_us(u64 *ticks_return);
void nspire_fps_overlay_reset(void);
void nspire_video_mix_reset(void);
int nspire_rtc_force_value(void);
int nspire_apply_bios(void);
void nspire_bios_error_wait(const char *msg);
void change_ext(u8 *src, u8 *buffer, u8 *extension);
u32 file_length(u8 *dummy, FILE *fp);
#endif

bool main_check_savestate(const u8 *src);
unsigned main_write_savestate(u8* ptr);
bool main_read_savestate(const u8 *src);

extern u32 num_skipped_frames;
extern int dynarec_enable;
extern boot_mode selected_boot_mode;
extern int sprite_limit;
extern u32 netplay_num_clients, netplay_client_id;

#ifdef TRACE_REGISTERS
void print_regs(void);
#endif

#ifdef TRACE_EVENTS
  #define trace_update_gba(remcyc)   \
    printf("update_gba: %d remaining cycles\n", (remcyc));
#else  /* TRACE_EVENTS */
  #define trace_update_gba(x)
#endif

#endif


