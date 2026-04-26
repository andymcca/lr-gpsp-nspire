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

#ifndef COMMON_H
#define COMMON_H

#define ror(dest, value, shift)                                               \
  dest = ((value) >> (shift)) | ((value) << (32 - (shift)))                   \

#define MAX(a,b)  ((a) > (b) ? (a) : (b))
#define MIN(a,b)  ((a) < (b) ? (a) : (b))

#if defined(_WIN32)
  #define PATH_SEPARATOR "\\"
  #define PATH_SEPARATOR_CHAR '\\'
#else
  #define PATH_SEPARATOR "/"
  #define PATH_SEPARATOR_CHAR '/'
#endif

/* On x86 we pass arguments via registers instead of stack */
#ifdef X86_ARCH
  #define function_cc __attribute__((regparm(2)))
#else
  #define function_cc
#endif

#ifdef ARM_ARCH

#define _BSD_SOURCE // sync
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

#ifdef NSPIRE_LIBRETRO
#include <os.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#endif

#endif /* ARM_ARCH */

// Huge thanks to pollux for the heads up on using native file I/O
// functions on PSP for vastly improved memstick performance.

#ifdef PSP
  #include <pspkernel.h>
  #include <pspdebug.h>
  #include <pspctrl.h>
  #include <pspgu.h>
  #include <pspaudio.h>
  #include <pspaudiolib.h>
  #include <psprtc.h>
  #include <time.h>
#else
  typedef unsigned char u8;
  typedef signed char s8;
  typedef unsigned short int u16;
  typedef signed short int s16;
  typedef unsigned int u32;
  typedef signed int s32;
  typedef unsigned long long int u64;
  typedef signed long long int s64;
#endif

/* Same reorder as stock libretro so video.cc blend math (BLND_MSK) matches palette_ram_converted.
 * Ndless present path maps framebuffer pixels back to GBA wire order for the LCD (nspire_video_present). */
#if defined(USE_XBGR1555_FORMAT)
  #define convert_palette(value)  \
    (value & 0x7FFF)
#else
  #define convert_palette(value) \
    (((value & 0x1F) << 11) | ((value & 0x03E0) << 1) | ((value >> 10) & 0x1F))
#endif

#ifdef NSPIRE_LIBRETRO
  #define NSPIRE_SCREEN_WIDTH 320
  #define NSPIRE_SCREEN_HEIGHT 240
  #define SDL_SCREEN_WIDTH NSPIRE_SCREEN_WIDTH
  #define SDL_SCREEN_HEIGHT NSPIRE_SCREEN_HEIGHT

  #define stdio_file_open_read  "rb"
  #define stdio_file_open_write "wb"
  #define file_open(filename_tag, filename, mode) \
    FILE *filename_tag = fopen(filename, stdio_file_open_##mode)
  #define file_check_valid(filename_tag) (filename_tag)
  #define file_close(filename_tag) fclose(filename_tag)
  #define file_read(filename_tag, buffer, size) fread(buffer, 1, size, filename_tag)
  #define file_write(filename_tag, buffer, size) fwrite(buffer, 1, size, filename_tag)
  #define file_seek(filename_tag, offset, type) fseek(filename_tag, offset, type)
  #define file_tag_type FILE *
  #define file_read_variable(filename_tag, variable) \
    file_read(filename_tag, &variable, sizeof(variable))
  #define file_write_variable(filename_tag, variable) \
    file_write(filename_tag, &variable, sizeof(variable))
  #define file_read_array(filename_tag, array) \
    file_read(filename_tag, array, sizeof(array))
  #define file_write_array(filename_tag, array) \
    file_write(filename_tag, array, sizeof(array))
#endif

#define GBA_SCREEN_WIDTH  (240)
#define GBA_SCREEN_HEIGHT (160)
#define GBA_SCREEN_PITCH  (240)

// The buffer is 16 bit color depth.
// We reserve extra memory at the end for extra effects (winobj rendering).
#define GBA_SCREEN_BUFFER_SIZE  \
  (GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1) * sizeof(uint16_t))

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  #define netorder32(value) (value)
#else
  #define netorder32(value) __builtin_bswap32(value)
#endif

typedef u32 fixed16_16;
typedef u32 fixed8_24;

#define float_to_fp16_16(value)                                               \
  (fixed16_16)((value) * 65536.0)                                             \

#define fp16_16_to_float(value)                                               \
  (float)((value) / 65536.0)                                                  \

#define u32_to_fp16_16(value)                                                 \
  ((value) << 16)                                                             \

#define fp16_16_to_u32(value)                                                 \
  ((value) >> 16)                                                             \

#define fp16_16_fractional_part(value)                                        \
  ((value) & 0xFFFF)                                                          \

#define float_to_fp8_24(value)                                                \
  (fixed8_24)((value) * 16777216.0)                                           \

#define fp8_24_fractional_part(value)                                         \
  ((value) & 0xFFFFFF)                                                        \

#define fixed_div(numerator, denominator, bits)                               \
  (((numerator * (1 << bits)) + (denominator / 2)) / denominator)             \

#define address8(base, offset)                                                \
  *((u8 *)((u8 *)base + (offset)))                                            \

#define address16(base, offset)                                               \
  *((u16 *)((u8 *)base + (offset)))                                           \

#define address32(base, offset)                                               \
  *((u32 *)((u8 *)base + (offset)))                                           \

#define eswap8(value)  (value)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  #define eswap16(value) __builtin_bswap16(value)
  #define eswap32(value) __builtin_bswap32(value)
#else
  #define eswap16(value) (value)
  #define eswap32(value) (value)
#endif

#define  readaddress8(base, offset) eswap8( address8( base, offset))
#define readaddress16(base, offset) eswap16(address16(base, offset))
#define readaddress32(base, offset) eswap32(address32(base, offset))

#define read_ioreg(regnum) (eswap16(io_registers[(regnum)]))
#define write_ioreg(regnum, val) io_registers[(regnum)] = eswap16(val)
#define read_ioreg32(regnum) (read_ioreg(regnum) | (read_ioreg((regnum)+1) << 16))

#define read_dmareg(regnum, dmachan) (eswap16(io_registers[(regnum) + (dmachan) * 6]))
#define write_dmareg(regnum, dmachan, val) io_registers[(regnum) + (dmachan) * 6] = eswap16(val)

#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cpu.h"
#include "gba_memory.h"
#include "savestate.h"
#if defined(NSPIRE_LIBRETRO) && defined(COMPILING_GUI_MODULE)
#include "../nspire-libretro/nspire_gui_video.h"
#else
#include "video.h"
#endif
#include "input.h"
#include "sound.h"
#include "main.h"
#include "cheats.h"
#include "serial.h"

#ifdef NSPIRE_LIBRETRO
#include "../nspire/nspire.h"
int load_state(u8 *filename);
void save_state(u8 *filename, u16 *screen_capture);
void quit(void);
void update_backup(void);
void update_backup_force(void);
void nspire_backup_mark_dirty(void);
void nspire_load_cartridge_backup(void);
u32 load_backup(char *name);
u32 save_backup(char *name);
void set_clock_speed(void);
extern u32 update_backup_flag;
extern char backup_filename[512];
extern u32 synchronize_flag;
extern u32 frame_speed;
extern s32 relative_frame_count;
extern u8 *file_ext[];
extern u32 quick_save_slot;
#endif

#endif
