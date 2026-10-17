/* What gpSP's core needs from the calculator frontend, for the core run
 * under qemu-arm (dynarec_test.c, replay.c). */

#include "common.h"

#include <sys/stat.h>

/* qemu-arm notices code written as data by itself. */
void nspire_jit_cache_sync(void *base, void *end)
{
  (void)base;
  (void)end;
}

void nspire_video_mix_reset(void) {}
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
