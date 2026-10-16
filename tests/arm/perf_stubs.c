/* What gpSP's core and picture output need from the calculator frontend,
 * for the speed harness (perf.c): core_stubs.c's stand-ins, plus screen
 * buffers that take turns as nspire/nspire.c's do. */

#include "common.h"
#include "nspire.h"

#include <sys/stat.h>

void nspire_jit_cache_sync(void *base, void *end)
{
  (void)base;
  (void)end;
}

void nspire_fps_overlay_reset(void) {}
void nspire_backup_mark_dirty(void) {}
u32 nspire_ticks(void) { return 0; }
u32 nspire_render_ticks;
u32 nspire_perf_on;

int mkdir(const char *path, mode_t mode)
{
  (void)path;
  (void)mode;
  return -1;
}

static u32 old_key;

bool input_check_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  return (p && bson_contains_key(p, "prevkey", BSON_TYPE_INT32));
}

bool input_read_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  if (p)
    return bson_read_int32(p, "prevkey", &old_key);
  return false;
}

unsigned input_write_savestate(u8 *dst)
{
  u8 *wbptr1, *startp = dst;
  bson_start_document(dst, "input", wbptr1);
  bson_write_int32(dst, "prevkey", old_key);
  bson_finish_document(dst, wbptr1);
  return (unsigned int)(dst - startp);
}

/* The screen buffers, as on the calculator: never read by the CPU, so
 * never in its data cache. */
#define SCREEN_PIXELS (320 * 240)
#define SCREENS 3
static u16 screens[SCREENS][SCREEN_PIXELS] __attribute__((aligned(32)));
static int drawing_screen = 1;

void *nspire_screen = screens[1];
void *nspire_displayed_screen = screens[0];
u16 nspire_landscape[SCREEN_PIXELS] __attribute__((aligned(32)));
int nspire_lcd_portrait = 1;

void nspire_flip(void)
{
  nspire_displayed_screen = nspire_screen;
  drawing_screen = (drawing_screen + 1) % SCREENS;
  nspire_screen = screens[drawing_screen];
}

int nspire_screen_index(void)
{
  return drawing_screen;
}

void update_at_vblank(void) {}
