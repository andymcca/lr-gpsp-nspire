/* Gamepak ROM page-cache allocation (total megabytes). Override before including
 * this header or pass -DROM_BUFFER_SIZE=N on the compiler command line. */
#ifndef ROM_BUFFER_SIZE
#define ROM_BUFFER_SIZE 8
#endif

#define getcwd(path, size) \
	NU_Current_Dir("A:", (char*)(path))
	
#define GetRTC() (*(volatile unsigned*)0x90090000)

#define WOP_D_CLEAN 0
#define WOP_I_INVALIDATE 1

/* RGB565 full LCD back buffer (matches extra_screen_*); D-clean range before display swap. */
#define NSPIRE_LCD_FB_SIZE_BYTES ((unsigned long)(320 * 240 * 2))

/* nspire_screen is the buffer the next frame is drawn into, and
 * nspire_flip() shows it; the buffers take turns (nspire.c). */
extern void* nspire_screen;
extern void* nspire_displayed_screen;
extern u16* os_screen;
/* A landscape 320x240 scratch frame: menus, and the blended scalers on a
 * portrait LCD, draw here before it is turned into nspire_screen. */
extern u16 nspire_landscape[320 * 240];
/* Nonzero on a portrait LCD (CX II): frames go into the display buffers
 * turned, 240 pixels per line, 320 lines, 5:6:5 (see nspire_turn.c). */
extern int nspire_lcd_portrait;
void nspire_init();
void nspire_restore();
void set_display_buffer(void* buffer);
void nspire_flip(void);
/* Which of the screen buffers nspire_screen is (0, 1 or 2). */
int nspire_screen_index(void);
/* Whether the LCD has started showing the frame nspire_flip gave it last. */
int nspire_flip_taken(void);
/* About how long (timer ticks) until the LCD's next refresh starts. */
u32 nspire_lcd_ticks_to_refresh(void);
/* Waits for the LCD's next refresh (at most 1/20 s); menus run at its pace. */
void update_at_vblank();

/* A free-running 32768 Hz tick counter (the first SP804 timer). */
#define NSPIRE_TICK_HZ 32768u
u32 nspire_ticks(void);
void nspire_wait_until(u32 tick);

/* Coherent LCD framebuffer before triple-buffer swap (range vs full D-cache clean). */
void nspire_lcd_fb_cache_clean_for_display(void);

int warm_cache_op_range(int op, void *addr, unsigned long size);
void clean_dcache(void);
void nspire_jit_cache_sync(void *base, void *end);
/* Makes code written as data anywhere runnable (the menu's trampolines). */
void nspire_sync_written_code(void);

struct tm* localtime(const time_t*);

void upscale_aspect(u16 *dst, u16 *src);
void upscale_aspect_fast(u16 *dst, u16 *src);
void upscale_aspect_raw(u16 *dst, u16 *src);
/* Fused libretro RGB565 repack + raw upscale (no nspire_upscale_src pass). */
void upscale_aspect_raw_from_gba(u16 *dst, const u16 *gba);

/* nspire_turn.c. dst is a 240x320 portrait display buffer; landscape pixel
 * (x, y) goes to position 239 - y of line x. */
void nspire_turn_raw(u16 *dst, const u16 *src, u32 src_pitch);
void nspire_turn_rect_555(u16 *dst, const u16 *src, u32 src_pitch,
                          u32 x0, u32 y0, u32 w, u32 h);

void enableAlignmentExceptions();
u32 menu_wrapper(u16*);
u32 save_backup_wrapper(char*);
void save_config_file_wrapper();
void load_state_wrapper(u8*);
void save_state_wrapper(u8*, u16*);
void quit_wrapper();

u32 key_scan();
