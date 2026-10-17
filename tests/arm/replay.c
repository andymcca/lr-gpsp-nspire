/*
 * Plays a real game with gpSP's core as the calculator runs it (the ARM
 * dynarec, both renderers and the ROM page cache, under qemu-arm), from a
 * save state or from the start, pressing keys from a script. It prints a
 * hash of every 50th frame and of the last one; tests/arm/run.sh compares
 * the settings with each other and with what a good run gave. A game that
 * jumps where no code can run (cpu.h) prints "stopped at PC" and exits 3.
 *
 *   replay ROM BIOS STATE|- ROMBUF_MB POLICY RENDERER FRAMES KEYS [PPMDIR]
 *
 * STATE: a gpSP save state (.svs.tns, packed or not). POLICY: 0 Partial SMC,
 * 1 Classic. RENDERER: 0 video.cc, 1 legacy. KEYS: lines "FROM TO KEY..."
 * (frames FROM to TO-1; A B SELECT START RIGHT LEFT UP DOWN R L). PPMDIR:
 * also writes those frames as pictures.
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

extern u32 nspire_rom_buffer_size_choice, nspire_dynarec_ram_policy, nspire_video_renderer_choice;
extern u16 *gba_screen_pixels;
extern u32 execute_cycles;
void init_gamepak_buffer(void);

struct key_span { int from, to; u32 keys; };
static struct key_span spans[1024];
static int nspans;

static int read_keys(const char *path)
{
  static const char *names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L" };
  char line[256];
  FILE *f = fopen(path, "r");

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

/* A state as gpSP writes it: the GPSZ header (nspire_states.c), then the
 * state deflated or as it is; or gpSP's older plain states. */
static int load_state_file(const char *path)
{
  u8 header[32], *file, *raw;
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
      memcpy(header, file, 32);
      memcpy(&packed_size, header + 16, 4);
      memcpy(&flags, header + 20, 4);
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

static void write_ppm(const char *dir, int frame)
{
  char path[600];
  FILE *f;
  int x, y;

  snprintf(path, sizeof(path), "%s/f%05d.ppm", dir, frame);
  f = fopen(path, "wb");
  if (!f)
    return;
  fprintf(f, "P6\n240 160\n255\n");
  for (y = 0; y < 160; y++)
    for (x = 0; x < 240; x++)
    {
      u16 c = gba_screen_pixels[y * GBA_SCREEN_PITCH + x];
      unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                               (unsigned char)((c & 31) << 3) };
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

int main(int argc, char **argv)
{
  const char *ppm_dir;
  int frames, frame;

  if (argc < 9)
  {
    fprintf(stderr, "usage: replay ROM BIOS STATE|- ROMBUF_MB POLICY RENDERER FRAMES KEYS [PPMDIR]\n");
    return 2;
  }
  nspire_rom_buffer_size_choice = atoi(argv[4]);
  nspire_dynarec_ram_policy = atoi(argv[5]);
  nspire_video_renderer_choice = atoi(argv[6]);
  frames = atoi(argv[7]);
  ppm_dir = argc > 9 ? argv[9] : NULL;
  if (!read_keys(argv[8]))
  {
    fprintf(stderr, "can't read keys %s\n", argv[8]);
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
  /* As the calculator does without a save file (nspire_backup.c). */
  memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
  if (strcmp(argv[3], "-") && !load_state_file(argv[3]))
  {
    fprintf(stderr, "can't load the state %s\n", argv[3]);
    return 2;
  }

  for (frame = 1; frame <= frames; frame++)
  {
    write_ioreg(REG_P1, (~keys_at(frame)) & 0x3FF);
    execute_arm_translate(execute_cycles);
    if (nspire_bad_jump)
    {
      printf("frame %d stopped at %08X\n", frame, (unsigned)nspire_bad_jump_pc);
      return 3;
    }
    if (frame % 50 == 0 || frame == frames)
    {
      printf("frame %d %08x\n", frame, (unsigned)frame_hash());
      if (ppm_dir)
        write_ppm(ppm_dir, frame);
    }
  }
  return 0;
}
