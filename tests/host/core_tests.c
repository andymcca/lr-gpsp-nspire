/* PC tests for the parts of the Nspire port that work on the emulator's
 * state and files: compressed ROMs (nspire_gbz.c and the paging in
 * gba_memory.c), zipped ROMs (nspire_zip.c), save state slots, packed or
 * stored (nspire_states.c), and in-game saves, plain or gzipped
 * (nspire_backup.c), with what the save manager does to them. run.sh makes
 * the test files and runs these.
 *
 *   core_tests DIR   DIR holds arm.gba.tns, arm.gbz.tns, big.gba.tns,
 *                    big.gbz.tns, sram.gba.tns and the zips, made by run.sh;
 *                    states and saves go in there too. ROMs are named as on
 *                    the calculator. */

#include "common.h"
#include "nspire_states.h"
#include "nspire_zip.h"
#include "nspire_frontend.h"
#include "nspire_paths.h"
#include "nspire_rom_convert.h"

#include <utime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

extern u32 gamepak_buffer_count;
extern u8 *gamepak_buffers[32];

static int failures, checks;
static char dir[512];

#define CHECK(cond, ...)                                      \
  do                                                          \
  {                                                           \
    checks++;                                                 \
    if (!(cond))                                              \
    {                                                         \
      failures++;                                             \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                    \
      printf("\n");                                           \
    }                                                         \
  } while (0)

extern char unit_documents_dir[600];

static void path_in_dir(char *out, const char *name)
{
  snprintf(out, 600, "%s/%s", dir, name);
}

/* A file in the saves folder (DIR/zgpSP_saves: DIR is the documents here). */
static void path_in_saves(char *out, const char *name)
{
  nspire_save_file(out, 600, name);
}

static u8 *read_file(const char *path, long *size)
{
  FILE *f = fopen(path, "rb");
  u8 *data;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  data = (u8 *)malloc(*size + 1);
  if (fread(data, 1, *size, f) != (size_t)*size)
  {
    free(data);
    data = NULL;
  }
  fclose(f);
  return data;
}

static long file_size(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static int dir_exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int load_rom(const char *name)
{
  char path[600];
  path_in_dir(path, name);
  if (load_gamepak(NULL, path, FEAT_AUTODETECT, FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0)
    return 0;
  reset_gba();
  return 1;
}

static void run_frames(int frames)
{
  while (frames-- > 0)
    execute_arm(execute_cycles);
}

/* A fingerprint of the machine: registers, RAM, VRAM and the picture. */
static u32 machine_hash(void)
{
  u32 h = crc32(0L, (const Bytef *)reg, sizeof(u32) * 18);
  h = crc32(h, ewram, 256 * 1024);
  h = crc32(h, iwram + 0x8000, 32 * 1024);
  h = crc32(h, vram, sizeof(vram));
  h = crc32(h, (const Bytef *)gba_screen_pixels, GBA_SCREEN_PITCH * GBA_SCREEN_HEIGHT * 2);
  return h;
}

/* Every page of the compressed ROM reads the same as the plain one, with a
 * 2 MB ROM buffer so most of the 8 MB ROM is paged in from the file. */
static void test_gbz_pages(void)
{
  char raw_path[600];
  long raw_size, page;
  u8 *raw;
  u32 plain_size;
  int pass;

  path_in_dir(raw_path, "big.gba.tns");
  raw = read_file(raw_path, &raw_size);
  CHECK(raw != NULL, "big.gba.tns missing");
  if (!raw)
    return;

  for (pass = 0; pass < 2; pass++)
  {
    const char *name = pass ? "big.gbz.tns" : "big.gba.tns";
    int bad = 0;
    CHECK(load_rom(name), "%s doesn't load", name);
    if (pass == 0)
      plain_size = gamepak_size;
    else
      CHECK(gamepak_size == plain_size, "gbz size %u, plain %u", gamepak_size, plain_size);
    CHECK(gamepak_must_swap(), "%s: ROM should be paged (buffer %u MB)", name, gamepak_buffer_count);

    /* In and out of order, so pages get evicted and read back. */
    for (page = 0; page < (raw_size + 0x7FFF) / 0x8000; page++)
    {
      long p = (page * 37) % ((raw_size + 0x7FFF) / 0x8000);
      long bytes = raw_size - p * 0x8000 < 0x8000 ? raw_size - p * 0x8000 : 0x8000;
      u8 *data = load_gamepak_page((u32)p);
      if (memcmp(data, raw + p * 0x8000, bytes) != 0)
        bad++;
      else if (pass == 1 && bytes < 0x8000)
      {
        long i;
        for (i = bytes; i < 0x8000; i++)
          if (data[i])
          {
            bad++;
            break;
          }
      }
    }
    CHECK(bad == 0, "%s: %d pages differ", name, bad);
  }
  free(raw);
}

/* The test ROM runs the same from the compressed ROM, and passes. */
static void test_gbz_runs(void)
{
  u32 hash_plain, hash_gbz, result_plain;

  CHECK(load_rom("arm.gba.tns"), "arm.gba.tns doesn't load");
  run_frames(120);
  hash_plain = machine_hash();
  result_plain = reg[12];
  CHECK(load_rom("arm.gbz.tns"), "arm.gbz.tns doesn't load");
  run_frames(120);
  hash_gbz = machine_hash();
  CHECK(hash_plain == hash_gbz, "plain %08x, gbz %08x", hash_plain, hash_gbz);
  /* The core's own accuracy, not the port's: just reported. */
  printf("  jsmolka arm test: %s %u\n", result_plain ? "fails at test" : "passes", result_plain);
  CHECK(strcmp(gamepak_code, "") != 0, "game code empty");
}

static void test_game_base(void)
{
  char out[512], want[600];
  nspire_game_name("/documents/gba/Pokemon.gba.tns", out, sizeof(out));
  CHECK(!strcmp(out, "Pokemon.gba"), "%s", out);
  nspire_game_name("/documents/gba/Pokemon.gbz.tns", out, sizeof(out));
  CHECK(!strcmp(out, "Pokemon.gba"), "%s", out);
  nspire_game_name("Zelda.GBZ.tns", out, sizeof(out));
  CHECK(!strcmp(out, "Zelda.gba"), "%s", out);
  nspire_game_name("/a.b/game", out, sizeof(out));
  CHECK(!strcmp(out, "game"), "%s", out);
  /* The files go in the saves folder; earlier builds had them by the ROM. */
  nspire_game_base("/documents/gba/Pokemon.gbz.tns", out, sizeof(out));
  snprintf(want, sizeof(want), "%s/zgpSP_saves/Pokemon.gba", dir);
  CHECK(!strcmp(out, want), "%s", out);
  nspire_game_old_base("/documents/gba/Pokemon.gbz.tns", out, sizeof(out));
  CHECK(!strcmp(out, "/documents/gba/Pokemon.gba"), "%s", out);
}

static void remove_states(void)
{
  char path[600];
  int slot;
  for (slot = 0; slot < STATE_SLOTS; slot++)
  {
    states_file_name(slot, path, sizeof(path));
    remove(path);
  }
}

static void test_states(void)
{
  char rom[600], path[600];
  u32 hash_saved, hash_after;
  long size;
  void *snapshot;

  CHECK(load_rom("arm.gba.tns"), "arm.gba.tns doesn't load");
  path_in_dir(rom, "arm.gba.tns");
  states_open_game(rom);
  remove_states();
  states_open_game(rom);
  CHECK(states_count() == 0, "count %d", states_count());
  CHECK(states_newest() == -1, "newest %d", states_newest());
  CHECK(states_next_new_slot() == 0, "next %d", states_next_new_slot());
  CHECK(states_load(3) == 0, "empty slot should load as 0");

  run_frames(30);
  CHECK(states_save(0) == 1, "save 0");
  states_file_name(0, path, sizeof(path));
  size = file_size(path);
  CHECK(size > 0 && size < 100 * 1024, "slot 0 is %ld bytes", size);
  printf("  a state is %ld bytes (416 KB raw)\n", size);
  run_frames(1);
  hash_saved = machine_hash();
  run_frames(40);
  CHECK(states_load(0) == 1, "load 0");
  run_frames(1);
  hash_after = machine_hash();
  CHECK(hash_saved == hash_after, "after loading: %08x, saved %08x", hash_after, hash_saved);

  /* Order and auto-increment. */
  CHECK(states_save(5) == 1, "save 5");
  CHECK(states_save(2) == 1, "save 2");
  CHECK(states_newest() == 2, "newest %d", states_newest());
  CHECK(states_next_new_slot() == 6, "next %d", states_next_new_slot());
  states_open_game(rom);   /* the order survives a rescan */
  CHECK(states_count() == 3, "count %d", states_count());
  CHECK(states_newest() == 2, "newest after rescan %d", states_newest());
  CHECK(states_exists(5) && !states_exists(4), "exists");

  /* A full set wraps to the first free slot. */
  CHECK(states_save(999) == 1, "save 999");
  CHECK(states_next_new_slot() == 1, "next after 999: %d", states_next_new_slot());

  /* gpSP's older states (the raw 416 KB) still load. */
  {
    u8 *raw = (u8 *)calloc(1, GBA_STATE_MEM_SIZE);
    FILE *f;
    run_frames(10);
    gba_save_state(raw);
    run_frames(1);
    hash_saved = machine_hash();
    states_file_name(7, path, sizeof(path));
    f = fopen(path, "wb");
    fwrite(raw, 1, GBA_STATE_MEM_SIZE, f);
    fclose(f);
    free(raw);
    states_open_game(rom);
    CHECK(states_exists(7), "legacy state not found");
    CHECK(states_newest() == 999, "a legacy state counts as old: newest %d", states_newest());
    run_frames(25);
    CHECK(states_load(7) == 1, "legacy load");
    run_frames(1);
    CHECK(machine_hash() == hash_saved, "legacy state differs after load");
  }

  /* A damaged file fails cleanly. */
  {
    FILE *f;
    states_file_name(8, path, sizeof(path));
    f = fopen(path, "wb");
    fputs("GPSZ garbage", f);
    fclose(f);
    states_open_game(rom);
    CHECK(states_load(8) == -1, "damaged state should fail");
    f = fopen(path, "wb");
    fputs("not a state", f);
    fclose(f);
    CHECK(states_load(8) == -1, "foreign file should fail");
  }

  /* Names: unpadded numbers only, like gpSP's. */
  {
    FILE *f;
    char name[600];
    snprintf(name, sizeof(name), "%s/arm.gba012.svs.tns", dir);
    f = fopen(name, "wb");
    fclose(f);
    snprintf(name, sizeof(name), "%s/arm.gba1000.svs.tns", dir);
    f = fopen(name, "wb");
    fclose(f);
    states_open_game(rom);
    CHECK(!states_exists(12), "012 shouldn't be a slot");
    CHECK(states_count() == 6, "count %d", states_count());
  }

  /* The compressed ROM shares the plain one's states. */
  path_in_dir(rom, "arm.gbz.tns");
  states_open_game(rom);
  CHECK(states_exists(0) && states_exists(999), "gbz ROM sees the gba's states");

  snapshot = states_snapshot();
  run_frames(1);
  hash_saved = machine_hash();
  run_frames(20);
  CHECK(states_restore(snapshot), "restore snapshot");
  run_frames(1);
  CHECK(machine_hash() == hash_saved, "snapshot differs");
  free(snapshot);

  /* Compress saves off: states stored as they are, behind the header. */
  path_in_dir(rom, "arm.gba.tns");
  states_open_game(rom);
  {
    long bytes;
    int compressed, newest;

    nspire_compress_saves = 0;
    run_frames(5);
    CHECK(states_save(3) == 1, "save 3 stored");
    nspire_compress_saves = 1;
    states_file_name(3, path, sizeof(path));
    /* the state, then its picture (packed, a few KB) */
    CHECK(file_size(path) > 32 + GBA_STATE_MEM_SIZE && file_size(path) < 32 + GBA_STATE_MEM_SIZE + 32 * 1024,
          "a stored state is %ld bytes", file_size(path));
    {
      static u16 pic[STATE_PIC_W * STATE_PIC_H];
      CHECK(states_picture(3, pic), "the stored state has a picture");
    }
    CHECK(states_file_info(3, &bytes, &compressed) && bytes == file_size(path) && !compressed,
          "slot 3: %ld bytes, compressed %d", bytes, compressed);
    run_frames(1);
    hash_saved = machine_hash();
    run_frames(30);
    CHECK(states_load(3) == 1, "load the stored state");
    run_frames(1);
    CHECK(machine_hash() == hash_saved, "stored state differs after load");
    newest = states_newest();
    CHECK(newest == 3, "the stored state is the newest: %d", newest);

    /* The save manager packs and unpacks them, keeping the order. */
    CHECK(states_set_compressed(3, 1) == 1, "compress slot 3");
    CHECK(states_file_info(3, &bytes, &compressed) && compressed && bytes < 100 * 1024,
          "slot 3 packed: %ld bytes, compressed %d", bytes, compressed);
    CHECK(states_newest() == 3, "still the newest: %d", states_newest());
    run_frames(30);
    CHECK(states_load(3) == 1, "load it packed");
    run_frames(1);
    CHECK(machine_hash() == hash_saved, "packed state differs after load");
    CHECK(states_set_compressed(3, 0) == 1 && file_size(path) > 32 + GBA_STATE_MEM_SIZE, "uncompress slot 3");
    {
      static u16 pic[STATE_PIC_W * STATE_PIC_H];
      CHECK(states_picture(3, pic), "the picture survives packing and unpacking");
    }
    CHECK(states_set_compressed(3, 0) == 1, "uncompressing it again is fine");
    states_open_game(rom);
    CHECK(states_newest() == 3, "the order survives: newest %d", states_newest());

    /* An older build's state gets the header, and stays older. */
    CHECK(states_set_compressed(7, 0) == 1, "an older state is stored already");
    CHECK(states_set_compressed(7, 1) == 1, "compress the older state");
    CHECK(states_file_info(7, &bytes, &compressed) && compressed, "the older state packed");
    states_open_game(rom);
    CHECK(states_newest() == 3, "the older state stays older: newest %d", states_newest());
    run_frames(10);
    CHECK(states_load(7) == 1, "the older state loads packed");

    CHECK(states_delete(3) == 1 && !states_exists(3) && file_size(path) < 0, "delete slot 3");
    CHECK(states_delete(3) == 0, "deleting an empty slot");
    CHECK(states_newest() != 3, "a deleted slot isn't the newest");
  }

  remove_states();
}

static void test_backup(void)
{
  char path[600], plain[600];
  long size;
  u8 *saved = (u8 *)malloc(sizeof(gamepak_backup));
  FILE *f;

  CHECK(load_rom("sram.gba.tns"), "sram.gba.tns doesn't load");
  run_frames(60);
  CHECK(backup_type == BACKUP_SRAM, "backup type %d", backup_type);
  memcpy(saved, gamepak_backup, sizeof(gamepak_backup));

  /* In-game saves stay plain .sav files. */
  path_in_dir(path, "sram.sav.tns");
  CHECK(save_backup(path) == 1, "save_backup");
  size = file_size(path);
  CHECK(size == 0x8000, "the save is %ld bytes", size);
  memset(gamepak_backup, 0x5A, sizeof(gamepak_backup));
  backup_type = BACKUP_UNKN;
  CHECK(load_backup(path) == 1, "load_backup");
  CHECK(backup_type == BACKUP_SRAM, "type after load %d", backup_type);
  CHECK(memcmp(saved, gamepak_backup, 0x8000) == 0, "SRAM differs after load");

  /* A file of no save size is refused, and leaves a blank save. */
  path_in_dir(plain, "odd.sav.tns");
  f = fopen(plain, "wb");
  fwrite(saved, 1, 1000, f);
  fclose(f);
  CHECK(load_backup(plain) == 0, "odd size should fail");
  CHECK(gamepak_backup[0] == 0xFF, "blank after a bad save");

  /* The game's own save files: gzipped with Compress saves on, plain with
   * it off, the other form removed; loading takes the newer. */
  {
    char gz[600];
    long bytes;
    int compressed;
    u8 *data;
    struct utimbuf times;

    memcpy(gamepak_backup, saved, 0x8000);
    backup_type = BACKUP_SRAM;
    path_in_saves(plain, "sram.gba.sav.tns");
    path_in_saves(gz, "sram.gba.sav.gz.tns");
    remove(plain);
    remove(gz);
    nspire_compress_saves = 1;
    update_backup_force();
    CHECK(file_size(gz) > 0 && file_size(gz) < 0x8000 && file_size(plain) < 0,
          "gzipped: %ld bytes, plain %ld", file_size(gz), file_size(plain));
    data = read_file(gz, &bytes);
    CHECK(data && bytes > 2 && data[0] == 0x1F && data[1] == 0x8B, "a gzip file");
    free(data);
    memset(gamepak_backup, 0x5A, sizeof(gamepak_backup));
    nspire_load_cartridge_backup();
    CHECK(!memcmp(gamepak_backup, saved, 0x8000) && backup_type == BACKUP_SRAM, "the gzipped save loads");
    CHECK(nspire_backup_info(&bytes, &compressed) && compressed && bytes == file_size(gz), "info: gzipped");

    nspire_compress_saves = 0;
    update_backup_force();
    CHECK(file_size(plain) == 0x8000 && file_size(gz) < 0, "plain again: %ld, gz %ld", file_size(plain),
          file_size(gz));
    nspire_compress_saves = 1;

    /* Both there: the newer one counts. */
    CHECK(nspire_backup_set_compressed(1) == 1, "save manager: compress");
    f = fopen(plain, "wb");
    memset(gamepak_backup, 0x11, 0x8000);
    fwrite(gamepak_backup, 1, 0x8000, f);
    fclose(f);
    times.actime = times.modtime = 1000000;
    utime(gz, &times);
    times.actime = times.modtime = 2000000;
    utime(plain, &times);
    nspire_load_cartridge_backup();
    CHECK(gamepak_backup[0] == 0x11, "the newer plain save loads");
    utime(gz, &times);
    times.actime = times.modtime = 1000000;
    utime(plain, &times);
    nspire_load_cartridge_backup();
    CHECK(!memcmp(gamepak_backup, saved, 0x8000), "the newer gzipped save loads");

    /* A damaged gzip falls back to the plain one. */
    f = fopen(gz, "wb");
    fputs("not gzip", f);
    fclose(f);
    nspire_load_cartridge_backup();
    CHECK(gamepak_backup[0] == 0x11, "damaged gzip: the plain save loads");

    /* The save manager: uncompress, delete (not written back when leaving,
     * until the game saves again). */
    memcpy(gamepak_backup, saved, 0x8000);
    remove(plain);
    remove(gz);
    update_backup_force();
    CHECK(nspire_backup_set_compressed(0) == 1 && file_size(plain) == 0x8000 && file_size(gz) < 0,
          "save manager: uncompress");
    CHECK(nspire_backup_delete() == 1 && file_size(plain) < 0, "save manager: delete");
    CHECK(!nspire_backup_info(&bytes, &compressed), "no save after deleting");
    update_backup_force();
    CHECK(file_size(plain) < 0 && file_size(gz) < 0, "a deleted save isn't written back when leaving");
    nspire_backup_mark_dirty();
    update_backup_force();
    CHECK(file_size(gz) > 0, "the game saved again: it's back");
    remove(gz);
  }
  free(saved);
}

/* Zipped ROMs: which file is the ROM, that it runs like the plain one,
 * damaged ones, sharing saves, and a big one with a small ROM buffer. */
static void test_zip(void)
{
  char path[600], name[256], rom[600];
  u32 hash_plain;
  long raw_size, page;
  u8 *raw;
  int bad = 0;

  path_in_dir(path, "arm.zip.tns");
  CHECK(zip_is_zip(path), "arm.zip.tns is a zip");
  path_in_dir(path, "arm.gba.tns");
  CHECK(!zip_is_zip(path), "arm.gba.tns isn't");
  path_in_dir(path, "arm.zip.tns");
  CHECK(zip_rom_name(path, name, sizeof(name)) && !strcmp(name, "arm.gba"), "the ROM in arm.zip: %s", name);
  path_in_dir(path, "nogba.zip.tns");
  CHECK(zip_rom_name(path, name, sizeof(name)) && !strcmp(name, "game.bin"), "without .gba, the biggest: %s",
        name);
  path_in_dir(path, "text.zip.tns");
  CHECK(!zip_rom_name(path, name, sizeof(name)), "a zip of text has no ROM");

  CHECK(load_rom("arm.gba.tns"), "arm.gba.tns doesn't load");
  run_frames(120);
  hash_plain = machine_hash();
  CHECK(load_rom("arm.zip.tns"), "arm.zip.tns doesn't load: %s", nspire_rom_load_error);
  run_frames(120);
  CHECK(machine_hash() == hash_plain, "the zipped ROM runs differently");
  CHECK(load_rom("stored.zip.tns"), "stored.zip.tns doesn't load");
  run_frames(120);
  CHECK(machine_hash() == hash_plain, "the stored zip runs differently");
  CHECK(load_rom("nogba.zip.tns"), "nogba.zip.tns doesn't load");

  CHECK(!load_rom("bad.zip.tns") && nspire_rom_load_error, "a damaged zip is refused: %s",
        nspire_rom_load_error ? nspire_rom_load_error : "(no reason)");
  CHECK(!load_rom("text.zip.tns") && nspire_rom_load_error, "a zip without a ROM is refused");
  CHECK(load_rom("arm.zip.tns"), "a good zip loads after a bad one");

  /* Its saves are the unzipped ROM's. */
  path_in_dir(path, "arm.zip.tns");
  nspire_game_base(path, name, sizeof(name));
  path_in_saves(rom, "arm.gba");
  CHECK(!strcmp(name, rom), "zip base name %s", name);
  path_in_dir(rom, "arm.gba.tns");
  states_open_game(rom);
  remove_states();
  CHECK(states_save(4) == 1, "save 4 from the zip");
  states_open_game(path);
  CHECK(states_exists(4), "the zip sees the gba's state");
  remove_states();

  /* 8 MB zipped with a 2 MB ROM buffer: the buffer grows, nothing pages. */
  path_in_dir(path, "big.gba.tns");
  raw = read_file(path, &raw_size);
  CHECK(gamepak_buffer_count == 2, "the buffer is %u MB", gamepak_buffer_count);
  CHECK(load_rom("big.zip.tns"), "big.zip.tns doesn't load: %s", nspire_rom_load_error);
  CHECK(gamepak_buffer_count >= 8 && !gamepak_must_swap(), "buffer %u MB, swaps %d", gamepak_buffer_count,
        gamepak_must_swap());
  for (page = 0; raw && page < (raw_size + 0x7FFF) / 0x8000; page++)
  {
    long bytes = raw_size - page * 0x8000 < 0x8000 ? raw_size - page * 0x8000 : 0x8000;
    if (memcmp(gamepak_buffers[page / 32] + (page % 32) * 0x8000, raw + page * 0x8000, bytes))
      bad++;
  }
  CHECK(raw && bad == 0, "%d pages of the big zip differ", bad);
  free(raw);
}

/* ---- Where files go (nspire_paths.c) ---- */

static void write_text(const char *path, const char *text)
{
  FILE *f = fopen(path, "wb");
  if (f)
  {
    fputs(text, f);
    fclose(f);
  }
}

static int file_is(const char *path, const char *text)
{
  long size;
  u8 *data = read_file(path, &size);
  int same = data && size == (long)strlen(text) && !memcmp(data, text, size);
  free(data);
  return same;
}

static void copy_file(const char *from, const char *to)
{
  long size;
  u8 *data = read_file(from, &size);
  FILE *f = fopen(to, "wb");
  if (data && f)
    fwrite(data, 1, size, f);
  if (f)
    fclose(f);
  free(data);
}

static void test_paths(void)
{
  char path[1300], want[1300], old[600], saved[1300];
  unsigned i;

  /* The OS's form of the current folder, joined with a name as gpSP does. */
  nspire_normalize_path(path, sizeof(path), "\\documents\\gba\\/Pokemon.zip.tns");
  CHECK(!strcmp(path, "/documents/gba/Pokemon.zip.tns"), "normalized: %s", path);
  nspire_normalize_path(path, sizeof(path), "\\documents\\gba\\");
  CHECK(!strcmp(path, "/documents/gba"), "normalized folder: %s", path);
  nspire_normalize_path(path, sizeof(path), "/");
  CHECK(!strcmp(path, "/"), "normalized root: %s", path);

  /* Game orders: one file per ROM folder, named after the folder. */
  snprintf(old, sizeof(old), "%s/gba", dir);
  nspire_order_file(path, sizeof(path), old);
  path_in_saves(want, "gpsp_order_gba.txt.tns");
  CHECK(!strcmp(path, want), "order file %s", path);
  snprintf(old, sizeof(old), "%s/gba/rpg/", dir);
  nspire_order_file(path, sizeof(path), old);
  path_in_saves(want, "gpsp_order_gba_rpg.txt.tns");
  CHECK(!strcmp(path, want), "order file %s", path);
  nspire_order_file(path, sizeof(path), dir);
  path_in_saves(want, "gpsp_order.txt.tns");
  CHECK(!strcmp(path, want), "documents' order file %s", path);
  nspire_order_file(path, sizeof(path), "\\documents\\gba\\");
  path_in_saves(want, "gpsp_order_gba.txt.tns");
  CHECK(!strcmp(path, want), "the OS's form of the folder: %s", path);

  /* What earlier builds left in a ROM folder moves in; other emulators'
   * saves, ROMs and other programs' files stay. From gpSP's own folder,
   * its settings files (older builds' too), last folder and BIOS move in. */
  {
    static const char *const moved[] = { "Game.gba.sav.tns", "Game.gba.sav.gz.tns", "Game.gba3.svs.tns",
                                         "Game.gba.settings.tns", "Zip.agb12.svs.tns" };
    static const char *const program[] = { "gpsp_qol.cfg.tns", "gpsp.cfg.tns", "Game.gba.cfg.tns",
                                           "Game.cfg.tns", "..cfg.tns", "gba_bios.bin.tns", "gpsp_romdir.tns" };
    static const char *const stay[] = { "Other.gbc.sav.tns", "Game.gba.tns", "notes.txt.tns", "ndless.cfg.tns",
                                        "Other.cfg.tns" };
    static const char *const *const lists[] = { moved, program, stay };
    static const unsigned sizes[] = { 5, 7, 5 };
    unsigned l;

    snprintf(old, sizeof(old), "%s/oldgba", dir);
    mkdir(old, 0755);
    for (l = 0; l < 3; l++)
      for (i = 0; i < sizes[l]; i++)
      {
        snprintf(path, sizeof(path), "%s/%s", old, lists[l][i]);
        write_text(path, lists[l][i]);
      }
    snprintf(path, sizeof(path), "%s/gpsp_order.txt.tns", old);
    write_text(path, "B.gba.tns\nA.gba.tns\n");
    /* One the saves folder has already wins. */
    snprintf(path, sizeof(path), "%s/Keep.gba.sav.tns", old);
    write_text(path, "old");
    path_in_saves(saved, "Keep.gba.sav.tns");
    write_text(saved, "new");

    nspire_move_old_files(old, 0);
    for (i = 0; i < sizes[0]; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old, moved[i]);
      path_in_saves(saved, moved[i]);
      CHECK(file_size(path) < 0 && file_is(saved, moved[i]), "%s moved in", moved[i]);
      remove(saved);
    }
    for (i = 0; i < sizes[1]; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old, program[i]);
      CHECK(file_is(path, program[i]), "%s moves only from gpSP's folder", program[i]);
    }
    path_in_saves(saved, "gpsp_order_oldgba.txt.tns");
    CHECK(file_is(saved, "B.gba.tns\nA.gba.tns\n"), "the folder's game order moved in");
    remove(saved);
    snprintf(path, sizeof(path), "%s/Keep.gba.sav.tns", old);
    path_in_saves(saved, "Keep.gba.sav.tns");
    CHECK(file_is(path, "old") && file_is(saved, "new"), "a save the folder has already isn't replaced");
    remove(path);
    remove(saved);

    nspire_move_old_files(old, 1);
    for (i = 0; i < sizes[1]; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old, program[i]);
      path_in_saves(saved, program[i]);
      CHECK(file_size(path) < 0 && file_is(saved, program[i]), "%s moved in from gpSP's folder", program[i]);
      remove(saved);
    }
    for (i = 0; i < sizes[2]; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old, stay[i]);
      CHECK(file_is(path, stay[i]), "%s stays", stay[i]);
      remove(path);
    }
    rmdir(old);
  }

  /* The saves folder is My Documents/zgpSP_saves (the z lists it below the
   * game folders). The one before it, gpSP_saves, moves in whole and goes;
   * gpSP's settings files in My Documents itself move in, others' stay. */
  {
    static const char *const moved[] = { "Game.gba.sav.gz.tns", "Game.gba14.svs.tns", "gpsp_qol.cfg.tns",
                                         "gpsp_order_gba.txt.tns", "gpsp_romdir.tns", "gba_bios.bin.tns" };
    static const char *const from_docs[] = { "..cfg.tns", "gpsp.cfg.tns", "Root.gba.cfg.tns" };
    static const char *const stay[] = { "notes.txt.tns", "Other.cfg.tns", "Game.gbc.sav.tns" };
    char old_dir[700];

    path_in_saves(path, "");
    snprintf(want, sizeof(want), "%s/zgpSP_saves", dir);
    CHECK(!strcmp(path, want), "the saves folder: %s", path);

    snprintf(old_dir, sizeof(old_dir), "%s/gpSP_saves", dir);
    mkdir(old_dir, 0755);
    for (i = 0; i < 6; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old_dir, moved[i]);
      write_text(path, moved[i]);
    }
    snprintf(path, sizeof(path), "%s/Keep.gba.sav.tns", old_dir);
    write_text(path, "old");
    path_in_saves(saved, "Keep.gba.sav.tns");
    write_text(saved, "new");
    for (i = 0; i < 3; i++)
    {
      path_in_dir(path, from_docs[i]);
      write_text(path, from_docs[i]);
      path_in_dir(path, stay[i]);
      write_text(path, stay[i]);
    }

    nspire_move_old_documents_files();
    for (i = 0; i < 6; i++)
    {
      snprintf(path, sizeof(path), "%s/%s", old_dir, moved[i]);
      path_in_saves(saved, moved[i]);
      CHECK(file_size(path) < 0 && file_is(saved, moved[i]), "%s moved in from gpSP_saves", moved[i]);
      remove(saved);
    }
    snprintf(path, sizeof(path), "%s/Keep.gba.sav.tns", old_dir);
    path_in_saves(saved, "Keep.gba.sav.tns");
    CHECK(file_is(path, "old") && file_is(saved, "new"), "gpSP_saves: one the saves folder has already stays");
    CHECK(dir_exists(old_dir), "gpSP_saves stays while a file is left in it");
    for (i = 0; i < 3; i++)
    {
      path_in_dir(path, from_docs[i]);
      path_in_saves(saved, from_docs[i]);
      CHECK(file_size(path) < 0 && file_is(saved, from_docs[i]), "%s moved in from My Documents", from_docs[i]);
      remove(saved);
      path_in_dir(path, stay[i]);
      CHECK(file_is(path, stay[i]), "%s stays in My Documents", stay[i]);
      remove(path);
    }

    snprintf(path, sizeof(path), "%s/Keep.gba.sav.tns", old_dir);
    remove(path);
    remove(saved);
    nspire_move_old_documents_files();
    CHECK(!dir_exists(old_dir), "gpSP_saves goes when it's empty");
    path_in_saves(path, "");
    CHECK(dir_exists(path), "the saves folder is there");
    nspire_move_old_documents_files();   /* nothing to do: nothing changes */
    CHECK(!dir_exists(old_dir) && dir_exists(path), "a second start changes nothing");
  }

  /* A game's files under another name (a zip's ROM named game.bin). */
  path_in_saves(path, "game.bin.sav.tns");
  write_text(path, "sram");
  path_in_saves(path, "game.bin7.svs.tns");
  write_text(path, "state");
  path_in_saves(old, "game.bin");
  path_in_saves(want, "game.gba");
  nspire_move_game_files(old, want);
  path_in_saves(path, "game.gba.sav.tns");
  path_in_saves(saved, "game.gba7.svs.tns");
  CHECK(file_is(path, "sram") && file_is(saved, "state"), "a game's files renamed");
  remove(path);
  remove(saved);
}

/* ---- Compressing and uncompressing ROMs (nspire_rom_convert.c) ---- */

static unsigned long progress_last, progress_total;
static int progress_backwards;

static void note_progress(unsigned long done, unsigned long total)
{
  if (done < progress_last)
    progress_backwards = 1;
  progress_last = done;
  progress_total = total;
}

/* Whether path unpacks (zip) or reads (plain) to the bytes of want. */
static int rom_is(const char *path, const char *want)
{
  long size, want_size;
  u8 *expected = read_file(want, &want_size), *got = NULL;
  int same = 0;

  if (zip_is_zip(path))
  {
    zip_rom *z = zip_open(path);
    if (z && expected && zip_size(z) == (u32)want_size)
    {
      got = (u8 *)malloc(want_size);
      same = got && !zip_read(z, got, (u32)want_size) && !zip_check(z) && !memcmp(got, expected, want_size);
    }
    zip_close(z);
  }
  else
  {
    got = read_file(path, &size);
    same = got && expected && size == want_size && !memcmp(got, expected, size);
  }
  free(expected);
  free(got);
  return same;
}

static void test_rom_convert(void)
{
  char conv[600], arm[600], gbz[600], path[1300], out[1300], name[256], saved[1300];
  const char *why;

  path_in_dir(arm, "arm.gba.tns");
  path_in_dir(gbz, "arm.gbz.tns");
  snprintf(conv, sizeof(conv), "%s/conv", dir);
  mkdir(conv, 0755);

  /* .gba -> .zip -> .gba */
  snprintf(path, sizeof(path), "%s/Arm.gba.tns", conv);
  copy_file(arm, path);
  CHECK(rom_kind(path) == ROM_PLAIN, "a plain ROM");
  progress_last = progress_total = 0;
  progress_backwards = 0;
  why = rom_compress(path, out, sizeof(out), note_progress);
  CHECK(!why, "compress: %s", why);
  snprintf(saved, sizeof(saved), "%s/Arm.zip.tns", conv);
  CHECK(!strcmp(out, saved) && file_size(path) < 0, "compress: %s made, the .gba gone", out);
  CHECK(rom_kind(out) == ROM_ZIP && zip_rom_name(out, name, sizeof(name)) && !strcmp(name, "Arm.gba"),
        "compress: Arm.gba in it: %s", name);
  CHECK(rom_is(out, arm), "compress: the zip unpacks to the ROM");
  CHECK(file_size(out) < file_size(arm), "compress: smaller (%ld of %ld)", file_size(out), file_size(arm));
  CHECK(!progress_backwards && progress_total == 2ul * file_size(arm) && progress_last == progress_total,
        "compress: progress to the end (%lu of %lu)", progress_last, progress_total);
  CHECK(rom_compress(out, path, sizeof(path), NULL) != NULL, "compress: a zip isn't compressed again");

  why = rom_uncompress(saved, out, sizeof(out), NULL);
  snprintf(path, sizeof(path), "%s/Arm.gba.tns", conv);
  CHECK(!why && !strcmp(out, path) && file_size(saved) < 0, "uncompress: %s", why ? why : out);
  CHECK(rom_is(path, arm), "uncompress: the ROM as it was");
  CHECK(rom_uncompress(path, out, sizeof(out), NULL) != NULL, "uncompress: a .gba isn't");
  remove(path);

  /* .gbz both ways */
  snprintf(path, sizeof(path), "%s/Packed.gbz.tns", conv);
  copy_file(gbz, path);
  why = rom_uncompress(path, out, sizeof(out), NULL);
  snprintf(saved, sizeof(saved), "%s/Packed.gba.tns", conv);
  CHECK(!why && !strcmp(out, saved) && rom_is(out, arm) && file_size(path) < 0, "gbz -> gba: %s", why);
  remove(saved);
  copy_file(gbz, path);
  why = rom_compress(path, out, sizeof(out), NULL);
  snprintf(saved, sizeof(saved), "%s/Packed.zip.tns", conv);
  CHECK(!why && !strcmp(out, saved) && rom_is(out, arm) && file_size(path) < 0, "gbz -> zip: %s", why);
  remove(saved);

  /* A file with the new name already there: nothing changes. */
  snprintf(path, sizeof(path), "%s/Dup.gba.tns", conv);
  copy_file(arm, path);
  snprintf(saved, sizeof(saved), "%s/Dup.zip.tns", conv);
  write_text(saved, "mine");
  CHECK(rom_compress(path, out, sizeof(out), NULL) != NULL && rom_is(path, arm) && file_is(saved, "mine"),
        "the .zip there already: nothing changed");
  remove(path);
  remove(saved);

  /* A zip whose ROM isn't a .gba: the .gba gets the zip's ROM's name, and
   * its saves follow. */
  snprintf(path, sizeof(path), "%s/NoGba.zip.tns", conv);
  path_in_dir(saved, "nogba.zip.tns");
  copy_file(saved, path);
  path_in_saves(saved, "game.bin.sav.tns");
  write_text(saved, "sram");
  why = rom_uncompress(path, out, sizeof(out), NULL);
  snprintf(saved, sizeof(saved), "%s/game.gba.tns", conv);
  CHECK(!why && !strcmp(out, saved) && rom_is(out, arm), "a zip's game.bin: %s", why ? why : out);
  path_in_saves(saved, "game.gba.sav.tns");
  CHECK(file_is(saved, "sram"), "a zip's game.bin: its save follows");
  remove(saved);
  remove(out);

  /* A damaged zip: refused, nothing left behind. */
  snprintf(path, sizeof(path), "%s/Bad.zip.tns", conv);
  path_in_dir(saved, "bad.zip.tns");
  copy_file(saved, path);
  why = rom_uncompress(path, out, sizeof(out), NULL);
  snprintf(saved, sizeof(saved), "%s/arm.gba.tns", conv);
  CHECK(why && file_size(path) > 0 && file_size(saved) < 0, "a damaged zip: %s", why ? why : "uncompressed!");
  snprintf(saved, sizeof(saved), "%s/arm.gba.tns.tmp", conv);
  CHECK(file_size(saved) < 0, "a damaged zip: no temporary file left");
  remove(path);
  rmdir(conv);
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: core_tests DIR\n");
    return 2;
  }
  snprintf(dir, sizeof(dir), "%s", argv[1]);
  snprintf(unit_documents_dir, sizeof(unit_documents_dir), "%s/", dir);
  nspire_save_dir_make();

  nspire_rom_buffer_size_choice = 2;
  init_gamepak_buffer();
  init_sound();
  memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
  gba_screen_pixels = (u16 *)calloc(1, GBA_SCREEN_BUFFER_SIZE);

  printf("game base names\n");
  test_game_base();
  printf("compressed ROM pages\n");
  test_gbz_pages();
  printf("compressed ROM runs\n");
  test_gbz_runs();
  printf("save states\n");
  test_states();
  printf("in-game saves\n");
  test_backup();
  printf("where files go\n");
  test_paths();
  printf("compressing ROMs\n");
  test_rom_convert();
  printf("zipped ROMs\n");
  test_zip();   /* last: the big zip grows the ROM buffer */

  memory_term();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
