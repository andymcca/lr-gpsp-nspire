/*
 * gpSP's speed harness: plays a game from a save state the way the
 * calculator does a frame (the core with its ARM dynarec, the renderer, and
 * the picture turned into a screen buffer by the same code the calculator
 * runs), built with the calculator's flags. tests/calc_speed.sh runs it
 * under qemu-arm with the ARM926 model (tools/calc_speed/arm926.c), which
 * counts the cycles of each frame; perf_frame_mark marks the frames.
 *
 *   perf ROM BIOS STATE|- FRAMES KEYS|- DRAW [SETTING=VALUE...]
 *
 * DRAW: 1 draws every frame, 0 none, N one frame in N (fast forward
 * drawing fewer). Settings (default: the owner's, 2026-10-14): rombuf=16
 * policy=0 (Partial SMC) renderer=0 (video.cc) scale=1 (scaled/blended 3:2)
 * mix=1 (interframe blending) sprlim=1 (no sprite limit); batch=0 draws
 * each line at its HBlank instead of in batches (video.h). KEYS as in
 * replay.c. Prints a hash of every 50th frame (every=N: every Nth) and of
 * the last one, so a change that should draw the same can be checked with
 * the same run;
 * ppm=DIR also writes the screen as shown at those frames (DIR/sNNNNN.ppm,
 * turned back to landscape); dump=DIR writes the dynarec's code at the end;
 * flush=N empties the dynarec's caches at the start of frame N (as loading
 * a state does), to see what translating everything again costs.
 */

#include "common.h"
#include "nspire.h"
#include "nspire_gui_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

extern u32 nspire_rom_buffer_size_choice, nspire_dynarec_ram_policy, nspire_video_renderer_choice;
extern u32 nspire_frame_mix_choice, nspire_sprlim_choice, screen_scale;
extern u32 resolution_width, resolution_height;
extern u16 *gba_screen_pixels;
extern u32 execute_cycles;
extern int sprite_limit;
extern char nspire_fps_text[];
extern u16 nspire_fps_color;
void init_gamepak_buffer(void);
void nspire_present_frame(void);
void nspire_video_mix_reset(void);
void nspire_video_frame_start(void);

/* Called at the start of every frame; the model splits its count here. */
void __attribute__((noinline)) perf_frame_mark(void)
{
  __asm__ volatile ("" ::: "memory");
}

struct key_span { int from, to; u32 keys; };
static struct key_span spans[1024];
static int nspans;

static int read_keys(const char *path)
{
  static const char *names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L" };
  char line[256];
  FILE *f;

  if (!strcmp(path, "-"))
    return 1;
  f = fopen(path, "r");
  if (!f)
    return 0;
  while (fgets(line, sizeof(line), f) && nspans < 1024)
  {
    char *tok, *save;
    int from, to;
    u32 keys = 0;
    unsigned i;

    if (line[0] == '#' || sscanf(line, "%d %d", &from, &to) != 2)
      continue;
    strtok_r(line, " \t\r\n", &save);
    strtok_r(NULL, " \t\r\n", &save);
    while ((tok = strtok_r(NULL, " \t\r\n", &save)))
    {
      for (i = 0; i < 10 && strcmp(tok, names[i]); i++)
        ;
      if (i == 10)
      {
        fprintf(stderr, "unknown key %s\n", tok);
        fclose(f);
        return 0;
      }
      keys |= 1u << i;
    }
    spans[nspans].from = from;
    spans[nspans].to = to;
    spans[nspans].keys = keys;
    nspans++;
  }
  fclose(f);
  return 1;
}

static u32 keys_at(int frame)
{
  u32 keys = 0;
  int i;
  for (i = 0; i < nspans; i++)
    if (frame >= spans[i].from && frame < spans[i].to)
      keys |= spans[i].keys;
  return keys;
}

/* A state as gpSP writes it (see replay.c). */
static int load_state_file(const char *path)
{
  u8 *file, *raw;
  long size;
  int ok = 0;
  FILE *f = fopen(path, "rb");

  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  file = malloc(size);
  raw = malloc(GBA_STATE_MEM_SIZE);
  if (file && raw && fread(file, 1, size, f) == (size_t)size)
  {
    if (size > 32 && !memcmp(file, "GPSZ", 4))
    {
      u32 packed_size, flags;
      uLongf out = GBA_STATE_MEM_SIZE;
      memcpy(&packed_size, file + 16, 4);
      memcpy(&flags, file + 20, 4);
      if (flags & 2)
        ok = packed_size == GBA_STATE_MEM_SIZE && size >= 32 + GBA_STATE_MEM_SIZE &&
             (memcpy(raw, file + 32, GBA_STATE_MEM_SIZE), 1);
      else
        ok = uncompress(raw, &out, file + 32, packed_size) == Z_OK && out == GBA_STATE_MEM_SIZE;
    }
    else if (size == GBA_STATE_MEM_SIZE)
      ok = (memcpy(raw, file, GBA_STATE_MEM_SIZE), 1);
  }
  fclose(f);
  if (ok)
    ok = gba_load_state(raw);
  free(file);
  free(raw);
  return ok;
}

static u32 frame_hash(void)
{
  u32 h = 2166136261u;
  const u8 *p = (const u8 *)gba_screen_pixels;
  u32 i;
  for (i = 0; i < 240 * 160 * 2; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

/* The screen buffer last shown, turned back: LCD line x, position 239 - y
 * holds landscape pixel (x, y) (nspire_turn.c). */
static void write_screen_ppm(const char *dir, int frame)
{
  const u16 *lcd = (const u16 *)nspire_displayed_screen;
  char path[600];
  FILE *f;
  int x, y;

  snprintf(path, sizeof(path), "%s/s%05d.ppm", dir, frame);
  f = fopen(path, "wb");
  if (!f)
    return;
  fprintf(f, "P6\n320 240\n255\n");
  for (y = 0; y < 240; y++)
    for (x = 0; x < 320; x++)
    {
      u16 c = lcd[x * 240 + 239 - y];
      unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                               (unsigned char)((c & 31) << 3) };
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static const char *text_setting(int argc, char **argv, const char *name)
{
  size_t n = strlen(name);
  int i;
  for (i = 7; i < argc; i++)
    if (!strncmp(argv[i], name, n) && argv[i][n] == '=')
      return argv[i] + n + 1;
  return NULL;
}

static int setting(int argc, char **argv, const char *name, int value)
{
  size_t n = strlen(name);
  int i;
  for (i = 7; i < argc; i++)
    if (!strncmp(argv[i], name, n) && argv[i][n] == '=')
      value = atoi(argv[i] + n + 1);
  return value;
}

int main(int argc, char **argv)
{
  int frames, frame, draw_every, hash_every, flush_at;
  const char *ppm_dir;

  if (argc < 7)
  {
    fprintf(stderr, "usage: perf ROM BIOS STATE|- FRAMES KEYS|- DRAW [SETTING=VALUE...]\n");
    return 2;
  }
  frames = atoi(argv[4]);
  draw_every = atoi(argv[6]);
  nspire_rom_buffer_size_choice = setting(argc, argv, "rombuf", 16);
  nspire_dynarec_ram_policy = setting(argc, argv, "policy", 0);
  nspire_video_renderer_choice = setting(argc, argv, "renderer", 0);
  screen_scale = setting(argc, argv, "scale", scaled_aspect);
  nspire_frame_mix_choice = setting(argc, argv, "mix", 1);
  nspire_sprlim_choice = setting(argc, argv, "sprlim", 1);
  sprite_limit = nspire_sprlim_choice ? 0 : 1;
  ppm_dir = text_setting(argc, argv, "ppm");
  hash_every = setting(argc, argv, "every", 50);
  flush_at = setting(argc, argv, "flush", 0);
  nspire_batch_lines = setting(argc, argv, "batch", 1);
  if (!read_keys(argv[5]))
  {
    fprintf(stderr, "can't read keys %s\n", argv[5]);
    return 2;
  }

  init_gamepak_buffer();
  init_sound();
  gba_screen_pixels = calloc(1, GBA_SCREEN_BUFFER_SIZE);
  if (load_bios((char *)argv[2]) != 0)
  {
    fprintf(stderr, "can't read the BIOS %s\n", argv[2]);
    return 2;
  }
  if (load_gamepak(NULL, argv[1], FEAT_AUTODETECT, FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0)
  {
    fprintf(stderr, "can't load %s\n", argv[1]);
    return 2;
  }
  reset_gba();
  memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
  if (strcmp(argv[3], "-") && !load_state_file(argv[3]))
  {
    fprintf(stderr, "can't load the state %s\n", argv[3]);
    return 2;
  }

  /* The picture as the calculator shows it in a game, with the counter. */
  nspire_lcd_portrait = 1;
  resolution_width = 240;
  resolution_height = 160;
  nspire_video_mix_reset();
  strcpy(nspire_fps_text, "60/60");
  nspire_fps_color = 0x07E0;

  for (frame = 1; frame <= frames; frame++)
  {
    perf_frame_mark();
    if (frame == flush_at)
      flush_dynarec_caches();
    skip_next_frame = draw_every == 0 || (frame % draw_every) != 0;
    if (!skip_next_frame)
      nspire_video_frame_start();
    write_ioreg(REG_P1, (~keys_at(frame)) & 0x3FF);
    execute_arm_translate(execute_cycles);
    if (nspire_bad_jump)
    {
      printf("frame %d stopped at %08X\n", frame, (unsigned)nspire_bad_jump_pc);
      return 3;
    }
    if (!skip_next_frame)
      nspire_present_frame();
    if (frame % hash_every == 0 || frame == frames)
    {
      printf("frame %d %08x\n", frame, (unsigned)frame_hash());
      if (ppm_dir)
        write_screen_ppm(ppm_dir, frame);
    }
  }
  perf_frame_mark();

  /* dump=DIR: the dynarec's code, to read with objdump (-b binary -m arm
   * --adjust-vma=ADDR, the address printed). */
  if (text_setting(argc, argv, "dump"))
  {
    char path[600];
    FILE *f;
    snprintf(path, sizeof(path), "%s/rom_cache.bin", text_setting(argc, argv, "dump"));
    if ((f = fopen(path, "wb")))
    {
      fwrite(rom_translation_cache, 1, rom_translation_ptr - rom_translation_cache, f);
      fclose(f);
      printf("rom cache at %08x, %u bytes\n", (unsigned)(uintptr_t)rom_translation_cache,
             (unsigned)(rom_translation_ptr - rom_translation_cache));
    }
    snprintf(path, sizeof(path), "%s/ram_cache.bin", text_setting(argc, argv, "dump"));
    if ((f = fopen(path, "wb")))
    {
      fwrite(ram_translation_cache, 1, ram_translation_ptr - ram_translation_cache, f);
      fclose(f);
      printf("ram cache at %08x, %u bytes\n", (unsigned)(uintptr_t)ram_translation_cache,
             (unsigned)(ram_translation_ptr - ram_translation_cache));
    }
  }
  return 0;
}
