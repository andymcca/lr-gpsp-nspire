/* Nspire menu / file-browser framebuffer (legacy gpSP video API subset). */

#include "common.h"
#include "nspire_gui_video.h"
#include "font.h"
#include "nspire.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static u32 screen_offset;
static u16 *screen_pixels;
#define get_screen_pixels() (screen_pixels)
#define get_screen_pitch()  ((u32)320)

u32 screen_scale = scaled_raw;
u32 current_scale = scaled_raw;
u32 screen_filter = filter_bilinear;

u32 resolution_width = 320;
u32 resolution_height = 240;

u32 frame_speed = 15000;

static u32 debug_cursor_x;
static u32 debug_cursor_y;

void init_video(void)
{
  screen_pixels = (u16 *)nspire_screen;
}

void video_resolution_large(void)
{
  screen_offset = 0;
  resolution_width = 320;
  resolution_height = 240;
  flip_screen();
  clear_screen(0);
}

void video_resolution_small(void)
{
  switch (screen_scale)
  {
    case unscaled:
      screen_offset = 320 * 40 + 40;
      break;
    case scaled_aspect:
      screen_offset = 320 * (80 - 14) + 80;
      break;
    case fullscreen:
      screen_offset = 320 * 80 + 80;
      break;
    case scaled_raw:
      screen_offset = 320 * (80 - 14) + 80;
      break;
    default:
      break;
  }

  clear_screen(0);
  flip_screen();
  clear_screen(0);
  flip_screen();
  clear_screen(0);
  flip_screen();
  clear_screen(0);

  resolution_width = 240;
  resolution_height = 160;
}

void set_gba_resolution(video_scale_type scale)
{
  screen_scale = scale;
}

void clear_screen(u16 color)
{
  u32 col = ((u32)color << 16) | color;
  u32 *p = (u32 *)nspire_screen;
  int c = 320 * 240 / 2;
  while (c-- > 0)
    *p++ = col;
}

u16 *copy_screen(void)
{
  static u16 copy[240 * 160];
  u32 row;
  u16 *dst, *src;
  for (row = 0, dst = copy, src = get_screen_pixels(); row < 160;
       row++, dst += 240, src += get_screen_pitch())
    memcpy(dst, src, 240 * 2);
  return copy;
}

void blit_to_screen(u16 *src, u32 w, u32 h, u32 dest_x, u32 dest_y)
{
  u32 pitch = get_screen_pitch();
  u16 *dest_ptr = get_screen_pixels() + dest_x + (dest_y * pitch);
  u16 *src_ptr = src;
  u32 line_skip = pitch - w;
  u32 x, y;

  for (y = 0; y < h; y++)
  {
    for (x = 0; x < w; x++, src_ptr++, dest_ptr++)
      *dest_ptr = *src_ptr;
    dest_ptr += line_skip;
  }
}

void print_string_ext(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y,
                      void *_dest_ptr, u32 pitch, u32 pad, u32 h_offset, u32 height)
{
  u16 *dest_ptr = (u16 *)_dest_ptr + (y * pitch) + x;
  u8 current_char = str[0];
  u32 current_row;
  u32 glyph_offset;
  u32 i = 0, i2, i3, h;
  u32 str_index = 1;
  u32 current_x = x;

  if (y + height > resolution_height)
    return;

  while (current_char)
  {
    if (current_char == '\n')
    {
      y += FONT_HEIGHT;
      current_x = x;
      dest_ptr = get_screen_pixels() + (y * pitch) + x;
    }
    else
    {
      glyph_offset = _font_offset[current_char];
      current_x += FONT_WIDTH;
      glyph_offset += h_offset;
      for (i2 = h_offset, h = 0; i2 < FONT_HEIGHT && h < height; i2++, h++, glyph_offset++)
      {
        current_row = _font_bits[glyph_offset];
        for (i3 = 0; i3 < FONT_WIDTH; i3++)
        {
          if ((current_row >> (15 - i3)) & 0x01)
            *dest_ptr = fg_color;
          else
            *dest_ptr = bg_color;
          dest_ptr++;
        }
        dest_ptr += (pitch - FONT_WIDTH);
      }
      dest_ptr = dest_ptr - (pitch * h) + FONT_WIDTH;
    }

    i++;
    current_char = str[str_index];

    if ((i < pad) && (current_char == 0))
      current_char = ' ';
    else
      str_index++;

    if (current_x + FONT_WIDTH > resolution_width)
    {
      while (current_char && current_char != '\n')
        current_char = str[str_index++];
    }
  }
}

void print_string(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y)
{
  print_string_ext(str, fg_color, bg_color, x, y, get_screen_pixels(), get_screen_pitch(), 0, 0,
                   FONT_HEIGHT);
}

void print_string_pad(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y, u32 pad)
{
  print_string_ext(str, fg_color, bg_color, x, y, get_screen_pixels(), get_screen_pitch(), pad, 0,
                   FONT_HEIGHT);
}

void flip_screen(void)
{
  if ((resolution_width == 240) && (resolution_height == 160))
  {
    if (screen_scale == fullscreen)
      upscale_aspect_fast(nspire_screen, screen_pixels);
    else if (screen_scale == scaled_raw)
      upscale_aspect_raw(nspire_screen, screen_pixels);
    else
      upscale_aspect(nspire_screen, screen_pixels);
  }
  clean_dcache();
  {
    void *temp = nspire_displayed_screen;
    nspire_displayed_screen = nspire_screen;
    nspire_screen = nspire_screen_2;
    nspire_screen_2 = nspire_screen_3;
    nspire_screen_3 = temp;
    screen_pixels = (u16 *)nspire_screen + screen_offset;
  }
}

void update_screen(void) {}

void video_write_mem_savestate(file_tag_type savestate_file)
{
  (void)savestate_file;
}

void video_read_savestate(file_tag_type savestate_file)
{
  (void)savestate_file;
}

void debug_screen_clear(void)
{
  debug_cursor_x = 0;
  debug_cursor_y = 0;
  clear_screen(0x0000);
}

void debug_screen_start(void)
{
  video_resolution_large();
  debug_screen_clear();
}

void debug_screen_end(void)
{
  video_resolution_small();
}

void debug_screen_update(void)
{
  flip_screen();
  update_at_vblank();
}

void debug_screen_printf(const char *format, ...)
{
  char str_buffer[512];
  va_list ap;
  va_start(ap, format);
  vsprintf(str_buffer, format, ap);
  va_end(ap);
  print_string(str_buffer, 0xFFFF, 0x0000, debug_cursor_x, debug_cursor_y);
  debug_cursor_x += FONT_WIDTH * (u32)strlen(str_buffer);
}

void debug_screen_newline(u32 count)
{
  debug_cursor_x = 0;
  debug_cursor_y += FONT_HEIGHT * count;
}

void debug_screen_printl(const char *format, ...)
{
  char str_buffer[512];
  va_list ap;
  va_start(ap, format);
  vsprintf(str_buffer, format, ap);
  va_end(ap);
  print_string(str_buffer, 0xFFFF, 0x0000, debug_cursor_x, debug_cursor_y);
  debug_screen_newline(1);
}
