/* What the Nspire frontend's files share: messages and counters over the
 * picture (nspire_video_present.c), frame timing (nspire_host_main.c), and
 * the settings the QoL features added (gui.c). */
#ifndef NSPIRE_FRONTEND_H
#define NSPIRE_FRONTEND_H

#include <stddef.h>

/* ---- Text over the picture ---- */

enum { OSD_WHITE, OSD_GOOD, OSD_WARN, OSD_BAD };

/* A message at the bottom of the screen for two seconds. */
void nspire_osd_message(const char *format, ...);
void nspire_osd_clear(void);
u16 nspire_osd_color(int which);
/* The FPS counter ("58/60") and its color, and the detailed line that
 * replaces it; empty strings show nothing. Set by the main loop. */
extern char nspire_fps_text[64];
extern u16 nspire_fps_color;
extern char nspire_perf_text[80];

/* Text in gpSP's 6x10 font (font.h) into any 16-bit buffer, without the
 * menu's clipping; it stops before max_x. Returns the x after the text. */
#define OSD_FONT_W 6
#define OSD_FONT_H 10
u32 nspire_draw_text(u16 *dst, u32 pitch, u32 x, u32 y, const char *text, u16 fg, u16 bg,
                     u32 max_x);

void nspire_present_frame(void);
void nspire_video_mix_reset(void);
/* Something other than a game frame went into the screen buffers (a menu):
 * the text bands are drawn again. */
void nspire_present_forget_bands(void);

/* nspire_blend.c: the blended pictures, in one pass, turned (portrait LCDs).
 * src is 240x160, GBA_SCREEN_PITCH pixels a line; with prev, frames are
 * blended with the previous one (interframe blending), which prev holds. */
void nspire_blend_turn_aspect(u16 *dst, const u16 *src, u16 *prev);
void nspire_blend_turn_full(u16 *dst, const u16 *src, u16 *prev);

/* nspire_turn.c, besides the ones in nspire.h */
void nspire_turn_1x(u16 *dst, const u16 *src, u32 src_pitch);
void nspire_turn_rect_565(u16 *dst, const u16 *src, u32 src_pitch,
                          u32 x0, u32 y0, u32 w, u32 h);

/* ---- Timing ---- */

/* Ticks (nspire_ticks) spent drawing scanlines, counted while
 * nspire_perf_on is set (nspire_video_renderer_dispatch.c). */
extern u32 nspire_render_ticks;
extern int nspire_perf_on;

/* Starts frame timing over (after the menu, loading, saving...). */
void nspire_reset_timing(void);

/* ---- Settings added for the QoL features (gui.c) ---- */

extern u32 nspire_auto_increment;   /* saving goes to a new slot */
extern u32 nspire_save_on_exit;     /* leaving a game saves a state first */
extern u32 nspire_load_on_start;    /* starting a game loads its newest state */
extern u32 nspire_cpu_speed;        /* 0 normal, then the raised speeds */
extern u32 nspire_compress_saves;   /* new states and in-game saves are compressed */

/* The loading screen (nspire_loading.c): the game's name and a bar for its
 * ROM (gba_memory.c reports through nspire_rom_progress), a line for what
 * comes after, or why it didn't load (waits for a key). */
void nspire_loading_start(const char *rom_path);
void nspire_loading_note(const char *text);
void nspire_loading_end(void);
void nspire_loading_failed(const char *why);
/* Why the last ROM didn't load, when gba_memory.c can say (else NULL). */
extern const char *nspire_rom_load_error;

/* The game's saves, their sizes, compressing and deleting them
 * (nspire_save_manager.c; main menu > Save manager). */
void nspire_save_manager(void);

/* Set when the file browser started a game with shift+enter: its newest
 * state isn't loaded that time. */
extern int nspire_skip_state_load;

/* The game's own settings and keys (nspire_config.c): opened before its ROM
 * loads, written with the settings and when it closes. */
void nspire_config_open_game(const char *rom_path);
void nspire_config_close_game(void);
s32 nspire_config_load(void);
s32 nspire_config_save(void);
/* The menu's "Settings for" / "Keys for" (0 all games, 1 this game), and
 * what to do when they change. */
extern u32 nspire_settings_for;
extern u32 nspire_keys_for;
void nspire_config_settings_for_changed(void);
void nspire_config_keys_for_changed(void);

/* Loads and starts a game: its own settings, the ROM, the in-game save and
 * its newest state if wanted. Returns 0 if the ROM couldn't be loaded. */
int nspire_start_game(const char *rom_path);
/* Before leaving a game (another game, exit): saves a state if wanted,
 * writes the in-game save and the game's settings. */
void nspire_leave_game(void);
/* Leaves the game, writes the settings and exits gpSP. */
void nspire_quit_game(void);

/* Save state helpers used by the menu and the hotkeys; they show messages. */
int nspire_save_state_now(void);
int nspire_load_state_slot(int slot);
/* The slot the next save goes to, or -1 if every slot is used. */
int nspire_save_target(void);

/* The speed test (Graphics options). */
void nspire_speed_test(void);

#endif
