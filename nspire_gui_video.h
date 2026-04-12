/* Menu / file browser UI declarations for NSPIRE_LIBRETRO (legacy gpSP video API). */
#ifndef NSPIRE_GUI_VIDEO_H
#define NSPIRE_GUI_VIDEO_H

void update_screen(void);
void init_video(void);
void video_resolution_large(void);
void video_resolution_small(void);
void print_string(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y);
void print_string_pad(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y, u32 pad);
void print_string_ext(const char *str, u16 fg_color, u16 bg_color, u32 x, u32 y,
                      void *_dest_ptr, u32 pitch, u32 pad, u32 h_offset, u32 height);
void clear_screen(u16 color);
void blit_to_screen(u16 *src, u32 w, u32 h, u32 x, u32 y);
u16 *copy_screen(void);
void flip_screen(void);
void video_write_mem_savestate(file_tag_type savestate_file);
void video_read_savestate(file_tag_type savestate_file);

void debug_screen_clear(void);
void debug_screen_start(void);
void debug_screen_end(void);
void debug_screen_printf(const char *format, ...);
void debug_screen_printl(const char *format, ...);
void debug_screen_newline(u32 count);
void debug_screen_update(void);

extern u32 frame_speed;

extern s32 affine_reference_x[2];
extern s32 affine_reference_y[2];

typedef void (*tile_render_function)(u32 layer_number, u32 start, u32 end, void *dest_ptr);
typedef void (*bitmap_render_function)(u32 start, u32 end, void *dest_ptr);

typedef struct
{
  tile_render_function normal_render_base;
  tile_render_function normal_render_transparent;
  tile_render_function alpha_render_base;
  tile_render_function alpha_render_transparent;
  tile_render_function color16_render_base;
  tile_render_function color16_render_transparent;
  tile_render_function color32_render_base;
  tile_render_function color32_render_transparent;
} tile_layer_render_struct;

typedef struct
{
  bitmap_render_function normal_render;
} bitmap_layer_render_struct;

typedef enum
{
  unscaled,
  scaled_aspect,
  fullscreen,
  scaled_raw
} video_scale_type;

typedef enum
{
  filter_nearest,
  filter_bilinear
} video_filter_type;

extern u32 screen_scale;
extern u32 current_scale;
extern u32 screen_filter;

void set_gba_resolution(video_scale_type scale);

#endif
