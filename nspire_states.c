/* Save state slots: see nspire_states.h. */

#include "common.h"
#include "nspire_states.h"
#include "nspire_zip.h"
#include "nspire_frontend.h"
#include "nspire_paths.h"
#include "nspire_ui.h"

#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define STATE_MAGIC   "GPSZ"
#define STATE_VERSION 1
/* In the header's flags: written by a build that finds the newest state by
 * its counter (this one). The first QoL build's states don't have it. */
#define STATE_FLAG_COUNTED 1u
/* The state follows the header as it is, not packed (Compress saves off). */
#define STATE_FLAG_STORED  2u
/* A picture of the game follows the state (reserved[0] is its size). After
 * the state, so builds that don't know it still load the state. */
#define STATE_FLAG_PICTURE 4u
#define PICTURE_MAX   (8 + STATE_PIC_W * STATE_PIC_H * 2 + 1024)
/* zlib's fastest: a picture packs about as small, in a fraction of the time
 * (it's made at every save; tests/arm/ui_bench.sh) */
#define PICTURE_LEVEL 1
#define STATE_LEVEL   6       /* zlib level: 6 packs nearly as small as 9, faster */
#define CHUNK         16384   /* file reads and writes go through this */

struct state_header
{
  char magic[4];
  u32 version;
  u32 counter;       /* saves are numbered; the newest has the highest */
  u32 raw_size;      /* GBA_STATE_MEM_SIZE */
  u32 packed_size;   /* zlib bytes after the header (raw_size if stored) */
  u32 flags;
  u32 reserved[2];   /* [0]: the picture's size with STATE_FLAG_PICTURE */
};

/* The picture: width and height (u16 each), the zlib size (u32), then the
 * 5:6:5 pixels packed. */
struct picture_header
{
  u16 w, h;
  u32 packed_size;
};

static char base_path[512];   /* "<saves folder>/Pokemon.gba" */
static unsigned char used[STATE_SLOTS];
static unsigned char counted[STATE_SLOTS];   /* has STATE_FLAG_COUNTED */
static u32 counters[STATE_SLOTS];
static u32 mtimes[STATE_SLOTS];             /* the file's modified time */
static u8 io_buffer[CHUNK];

/* A zip's saves are named after the ROM in it, so the zipped and unzipped
 * game share them. The name is looked up once per zip. */
static int zip_name(const char *rom_path, char *out, size_t size)
{
  static char last_zip[512], last_inner[256];
  size_t len = strlen(rom_path);

  if (!((len > 8 && !strcasecmp(rom_path + len - 8, ".zip.tns")) ||
        (len > 4 && !strcasecmp(rom_path + len - 4, ".zip"))))
    return 0;
  if (strcmp(last_zip, rom_path))
  {
    if (!zip_rom_name(rom_path, last_inner, sizeof(last_inner)))
      return 0;
    snprintf(last_zip, sizeof(last_zip), "%s", rom_path);
  }
  snprintf(out, size, "%s", last_inner);
  return 1;
}

void nspire_game_name(const char *rom_path, char *out, size_t size)
{
  const char *slash = strrchr(rom_path, '/');
  const char *file = slash ? slash + 1 : rom_path;
  const char *dot = strrchr(file, '.');
  size_t len = dot && dot != file ? (size_t)(dot - file) : strlen(file);

  if (zip_name(rom_path, out, size))
    return;
  if (len >= size)
    len = size - 1;
  memcpy(out, file, len);
  out[len] = 0;
  if (len >= 4 && !strcasecmp(out + len - 4, ".gbz"))
    memcpy(out + len - 4, ".gba", 4);
}

void nspire_game_base(const char *rom_path, char *out, size_t size)
{
  char name[300];
  nspire_game_name(rom_path, name, sizeof(name));
  nspire_save_file(out, size, name);
}

void nspire_game_old_base(const char *rom_path, char *out, size_t size)
{
  char name[300];
  const char *slash = strrchr(rom_path, '/');
  nspire_game_name(rom_path, name, sizeof(name));
  snprintf(out, size, "%.*s%s", slash ? (int)(slash - rom_path) + 1 : 0, rom_path, name);
}

void nspire_game_move_old_files(const char *rom_path)
{
  char dir[512], old_base[600], base[600];
  const char *slash = strrchr(rom_path, '/');

  snprintf(dir, sizeof(dir), "%.*s", slash ? (slash == rom_path ? 1 : (int)(slash - rom_path)) : 1,
           slash ? rom_path : ".");
  nspire_move_old_files(dir, 0);
  /* A game whose name the folder sweep doesn't know (a zip's ROM named
   * otherwise than .gba). */
  nspire_game_old_base(rom_path, old_base, sizeof(old_base));
  nspire_game_base(rom_path, base, sizeof(base));
  nspire_save_dir_make();
  nspire_move_game_files(old_base, base);
}

void states_file_name(int slot, char *out, size_t size)
{
  snprintf(out, size, "%s%d.svs.tns", base_path, slot);
}

/* The slot of a file name like "Pokemon.gba12.svs.tns", or -1. */
static int slot_from_name(const char *name, const char *prefix, size_t prefix_len)
{
  const char *p = name + prefix_len;
  int slot = 0, digits = 0;

  if (strncasecmp(name, prefix, prefix_len) != 0)
    return -1;
  while (isdigit((unsigned char)*p) && digits < 4)
  {
    slot = slot * 10 + (*p++ - '0');
    digits++;
  }
  if (!digits || slot >= STATE_SLOTS || (digits > 1 && p[-digits] == '0'))
    return -1;
  return strcasecmp(p, ".svs.tns") == 0 ? slot : -1;
}

/* The header's counter (0 for an old uncompressed state), its flag, and the
 * file's modified time. */
static void read_slot_info(int slot)
{
  char path[600];
  struct state_header header;
  struct stat st;
  FILE *f;

  counters[slot] = mtimes[slot] = 0;
  counted[slot] = 0;
  states_file_name(slot, path, sizeof(path));
  /* st_mtim: Ndless's os.h undefines the st_mtime shorthand. */
  if (stat(path, &st) == 0)
    mtimes[slot] = (u32)st.st_mtim.tv_sec;
  f = fopen(path, "rb");
  if (!f)
    return;
  if (fread(&header, sizeof(header), 1, f) == 1 && !memcmp(header.magic, STATE_MAGIC, 4))
  {
    counters[slot] = header.counter;
    counted[slot] = (header.flags & STATE_FLAG_COUNTED) != 0;
  }
  fclose(f);
}

void states_open_game(const char *rom_path)
{
  char dir_path[512];
  const char *prefix;
  size_t prefix_len;
  char *slash;
  DIR *dir;
  struct dirent *entry;
  int slot;

  nspire_game_base(rom_path, base_path, sizeof(base_path));
  memset(used, 0, sizeof(used));
  memset(counted, 0, sizeof(counted));
  memset(counters, 0, sizeof(counters));
  memset(mtimes, 0, sizeof(mtimes));

  snprintf(dir_path, sizeof(dir_path), "%s", base_path);
  slash = strrchr(dir_path, '/');
  if (slash)
  {
    prefix = base_path + (slash - dir_path) + 1;
    if (slash == dir_path)
      slash[1] = 0;
    else
      *slash = 0;
  }
  else
  {
    prefix = base_path;
    strcpy(dir_path, ".");
  }
  prefix_len = strlen(prefix);

  dir = opendir(dir_path);
  if (!dir)
    return;
  while ((entry = readdir(dir)) != NULL)
  {
    slot = slot_from_name(entry->d_name, prefix, prefix_len);
    if (slot >= 0)
      used[slot] = 1;
  }
  closedir(dir);

  for (slot = 0; slot < STATE_SLOTS; slot++)
    if (used[slot])
      read_slot_info(slot);
}

int states_exists(int slot)
{
  return slot >= 0 && slot < STATE_SLOTS && used[slot];
}

int states_count(void)
{
  int slot, count = 0;
  for (slot = 0; slot < STATE_SLOTS; slot++)
    count += used[slot];
  return count;
}

/* This build's states count their saves, which still orders them after the
 * calculator's clock is reset (as a reset of the calculator does). States
 * from older builds don't: while a game has none of this build's, the most
 * recently modified file is the newest. */
int states_newest(void)
{
  int slot, newest = -1, any_counted = 0;

  for (slot = 0; slot < STATE_SLOTS; slot++)
    any_counted |= used[slot] && counted[slot];
  for (slot = 0; slot < STATE_SLOTS; slot++)
  {
    if (!used[slot] || (any_counted && !counted[slot]))
      continue;
    if (newest < 0 ||
        (any_counted ? counters[slot] >= counters[newest] :
                       mtimes[slot] > mtimes[newest] ||
                       (mtimes[slot] == mtimes[newest] && counters[slot] >= counters[newest])))
      newest = slot;
  }
  return newest;
}

int states_next_new_slot(void)
{
  int slot, highest = -1;
  for (slot = 0; slot < STATE_SLOTS; slot++)
    if (used[slot])
      highest = slot;
  if (highest < STATE_SLOTS - 1)
    return highest + 1;
  for (slot = 0; slot < STATE_SLOTS; slot++)
    if (!used[slot])
      return slot;
  return -1;
}

/* The state in memory. The buffer starts zeroed so the state's unused room
 * (its "zpadding" field) packs down to almost nothing. */
static u8 *state_to_memory(void)
{
  u8 *raw = (u8 *)calloc(1, GBA_STATE_MEM_SIZE);
  if (raw)
    gba_save_state(raw);
  return raw;
}

/* Packs raw into f after its header (which is filled in). */
static int write_packed(FILE *f, const u8 *raw, struct state_header *header)
{
  z_stream z;
  int status, ok = 1;

  memset(&z, 0, sizeof(z));
  if (deflateInit(&z, STATE_LEVEL) != Z_OK)
    return 0;
  z.next_in = (Bytef *)raw;
  z.avail_in = GBA_STATE_MEM_SIZE;
  header->packed_size = 0;
  do
  {
    z.next_out = io_buffer;
    z.avail_out = CHUNK;
    status = deflate(&z, Z_FINISH);
    if (status == Z_STREAM_ERROR)
    {
      ok = 0;
      break;
    }
    if (fwrite(io_buffer, 1, CHUNK - z.avail_out, f) != CHUNK - z.avail_out)
      ok = 0;
    header->packed_size += CHUNK - z.avail_out;
  } while (ok && status != Z_STREAM_END);
  deflateEnd(&z);
  return ok && status == Z_STREAM_END;
}

/* The state after the header: packed, or as it is when the header says
 * stored. */
static int write_payload(FILE *f, const u8 *raw, struct state_header *header)
{
  if (header->flags & STATE_FLAG_STORED)
  {
    header->packed_size = GBA_STATE_MEM_SIZE;
    return fwrite(raw, 1, GBA_STATE_MEM_SIZE, f) == GBA_STATE_MEM_SIZE;
  }
  return write_packed(f, raw, header);
}

/* The game's last frame at a fifth of its size... at 3/5: 144x96. */
static int make_picture(u16 *pic)
{
  if (!gba_screen_pixels)
    return 0;
  ui_scale(pic, STATE_PIC_W, STATE_PIC_H, gba_screen_pixels, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT,
           GBA_SCREEN_PITCH);
  return 1;
}

/* Packs the picture into block (PICTURE_MAX bytes); returns its size, 0 if
 * it didn't pack. */
static u32 pack_picture(const u16 *pic, u8 *block)
{
  struct picture_header ph;
  uLongf packed = PICTURE_MAX - sizeof(ph);
  if (compress2(block + sizeof(ph), &packed, (const Bytef *)pic, STATE_PIC_W * STATE_PIC_H * 2, PICTURE_LEVEL)
      != Z_OK)
    return 0;
  ph.w = STATE_PIC_W;
  ph.h = STATE_PIC_H;
  ph.packed_size = (u32)packed;
  memcpy(block, &ph, sizeof(ph));
  return (u32)(sizeof(ph) + packed);
}

/* The picture block after a state, or 0. */
static u32 read_picture_block(FILE *f, const struct state_header *header, u8 *block)
{
  u32 size = header->reserved[0];
  if (!(header->flags & STATE_FLAG_PICTURE) || size < sizeof(struct picture_header) || size > PICTURE_MAX ||
      fseek(f, (long)(sizeof(*header) + header->packed_size), SEEK_SET) != 0)
    return 0;
  return fread(block, 1, size, f) == size ? size : 0;
}

static int unpack_picture(const u8 *block, u32 size, u16 *pic)
{
  struct picture_header ph;
  uLongf raw = STATE_PIC_W * STATE_PIC_H * 2;
  memcpy(&ph, block, sizeof(ph));
  if (ph.w != STATE_PIC_W || ph.h != STATE_PIC_H || ph.packed_size + sizeof(ph) > size)
    return 0;
  return uncompress((Bytef *)pic, &raw, block + sizeof(ph), ph.packed_size) == Z_OK &&
         raw == STATE_PIC_W * STATE_PIC_H * 2;
}

/* The picture of the state in a file: 1 if it has one. */
static int file_picture(const char *path, u16 *pic)
{
  static u8 block[PICTURE_MAX];
  struct state_header header;
  int ok = 0;
  FILE *f = fopen(path, "rb");
  u32 size;

  if (!f)
    return 0;
  if (fread(&header, sizeof(header), 1, f) == 1 && !memcmp(header.magic, STATE_MAGIC, 4) &&
      (size = read_picture_block(f, &header, block)) != 0)
    ok = unpack_picture(block, size, pic);
  fclose(f);
  return ok;
}

int states_picture(int slot, u16 *pic)
{
  char path[600];
  if (!states_exists(slot))
    return 0;
  states_file_name(slot, path, sizeof(path));
  return file_picture(path, pic);
}

int states_peek(const char *rom_path, int *count, int *newest, u16 *pic)
{
  /* the open game's slots stay as they are */
  static char keep_base[sizeof(base_path)];
  static unsigned char keep_used[STATE_SLOTS], keep_counted[STATE_SLOTS];
  static u32 keep_counters[STATE_SLOTS], keep_mtimes[STATE_SLOTS];
  int has_picture;

  memcpy(keep_base, base_path, sizeof(base_path));
  memcpy(keep_used, used, sizeof(used));
  memcpy(keep_counted, counted, sizeof(counted));
  memcpy(keep_counters, counters, sizeof(counters));
  memcpy(keep_mtimes, mtimes, sizeof(mtimes));

  states_open_game(rom_path);
  *count = states_count();
  *newest = states_newest();
  has_picture = *newest >= 0 && pic && states_picture(*newest, pic);

  memcpy(base_path, keep_base, sizeof(base_path));
  memcpy(used, keep_used, sizeof(used));
  memcpy(counted, keep_counted, sizeof(counted));
  memcpy(counters, keep_counters, sizeof(counters));
  memcpy(mtimes, keep_mtimes, sizeof(mtimes));
  return has_picture;
}

int states_save(int slot)
{
  static u16 pic[STATE_PIC_W * STATE_PIC_H];
  static u8 block[PICTURE_MAX];
  u32 block_size = 0;
  char path[600], temp_path[620];
  struct state_header header;
  u8 *raw;
  FILE *f;
  int i, ok;

  if (slot < 0 || slot >= STATE_SLOTS)
    return 0;
#ifdef HAVE_DYNAREC
  if (dynarec_enable)
    flush_dynarec_caches();
#endif
  raw = state_to_memory();
  if (!raw)
    return 0;

  memset(&header, 0, sizeof(header));
  memcpy(header.magic, STATE_MAGIC, 4);
  header.version = STATE_VERSION;
  header.raw_size = GBA_STATE_MEM_SIZE;
  header.flags = STATE_FLAG_COUNTED | (nspire_compress_saves ? 0 : STATE_FLAG_STORED);
  if (make_picture(pic) && (block_size = pack_picture(pic, block)) != 0)
  {
    header.flags |= STATE_FLAG_PICTURE;
    header.reserved[0] = block_size;
  }
  header.counter = 1;
  for (i = 0; i < STATE_SLOTS; i++)
    if (used[i] && counters[i] >= header.counter)
      header.counter = counters[i] + 1;

  /* A temporary file first, so running out of space can't destroy the
   * state already in the slot. */
  states_file_name(slot, path, sizeof(path));
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
  f = fopen(temp_path, "wb");
  ok = f != NULL;
  if (ok)
  {
    ok = fwrite(&header, sizeof(header), 1, f) == 1 && write_payload(f, raw, &header) &&
         (!block_size || fwrite(block, 1, block_size, f) == block_size) &&
         fseek(f, 0, SEEK_SET) == 0 && fwrite(&header, sizeof(header), 1, f) == 1;
    if (fclose(f) != 0)
      ok = 0;
    if (ok)
    {
      remove(path);
      ok = rename(temp_path, path) == 0;
    }
    if (!ok)
      remove(temp_path);
  }
  free(raw);
  if (!ok)
    return 0;
  used[slot] = 1;
  counted[slot] = 1;
  counters[slot] = header.counter;
  return 1;
}

static int read_packed(FILE *f, u8 *raw, const struct state_header *header)
{
  z_stream z;
  int status = Z_OK;
  u32 left = header->packed_size;

  memset(&z, 0, sizeof(z));
  if (inflateInit(&z) != Z_OK)
    return 0;
  z.next_out = raw;
  z.avail_out = GBA_STATE_MEM_SIZE;
  while (left && status == Z_OK)
  {
    u32 want = left < CHUNK ? left : CHUNK;
    if (fread(io_buffer, 1, want, f) != want)
      break;
    left -= want;
    z.next_in = io_buffer;
    z.avail_in = want;
    status = inflate(&z, Z_NO_FLUSH);
  }
  inflateEnd(&z);
  return status == Z_STREAM_END && z.total_out == GBA_STATE_MEM_SIZE;
}

int states_load_file(const char *path)
{
  struct state_header header;
  u8 *raw;
  FILE *f;
  int ok = 0;
  long size;

  f = fopen(path, "rb");
  if (!f)
    return 0;
  raw = (u8 *)malloc(GBA_STATE_MEM_SIZE);
  if (raw && fread(&header, sizeof(header), 1, f) == 1)
  {
    if (!memcmp(header.magic, STATE_MAGIC, 4))
      ok = header.version == STATE_VERSION && header.raw_size == GBA_STATE_MEM_SIZE &&
           ((header.flags & STATE_FLAG_STORED) ?
            fread(raw, 1, GBA_STATE_MEM_SIZE, f) == GBA_STATE_MEM_SIZE :
            read_packed(f, raw, &header));
    else if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) == GBA_STATE_MEM_SIZE &&
             fseek(f, 0, SEEK_SET) == 0)
      /* gpSP's older states: the state as it is in memory. */
      ok = fread(raw, 1, GBA_STATE_MEM_SIZE, f) == GBA_STATE_MEM_SIZE;
  }
  fclose(f);
  if (ok)
    ok = gba_load_state(raw);
  free(raw);
  return ok ? 1 : -1;
}

int states_load(int slot)
{
  char path[600];
  int result;

  if (!states_exists(slot))
    return 0;
  states_file_name(slot, path, sizeof(path));
  result = states_load_file(path);
  if (result == 0)
    used[slot] = 0;   /* the file went away */
  return result;
}

void *states_snapshot(void)
{
#ifdef HAVE_DYNAREC
  if (dynarec_enable)
    flush_dynarec_caches();
#endif
  return state_to_memory();
}

int states_restore(const void *snapshot)
{
  return snapshot && gba_load_state(snapshot);
}

/* ---- For the save manager ---- */

int states_file_info(int slot, long *bytes, int *compressed)
{
  char path[600];
  struct state_header header;
  struct stat st;
  FILE *f;

  if (!states_exists(slot))
    return 0;
  states_file_name(slot, path, sizeof(path));
  if (stat(path, &st) != 0)
    return 0;
  *bytes = (long)st.st_size;
  *compressed = 0;
  f = fopen(path, "rb");
  if (f)
  {
    *compressed = fread(&header, sizeof(header), 1, f) == 1 && !memcmp(header.magic, STATE_MAGIC, 4) &&
                  !(header.flags & STATE_FLAG_STORED);
    fclose(f);
  }
  return 1;
}

/* Rewrites a state packed or stored, keeping its counter (so which one is
 * the newest doesn't change). A state from an older build gets a header
 * (it's then this build's, counted as older than the others). */
int states_set_compressed(int slot, int compress)
{
  static u8 block[PICTURE_MAX];
  u32 block_size = 0;
  char path[600], temp_path[620];
  struct state_header header;
  u8 *raw;
  FILE *f;
  long size = 0;
  int ok = 0, had_header = 0;

  if (!states_exists(slot))
    return 0;
  states_file_name(slot, path, sizeof(path));
  raw = (u8 *)malloc(GBA_STATE_MEM_SIZE);
  f = fopen(path, "rb");
  if (raw && f && fread(&header, sizeof(header), 1, f) == 1)
  {
    if (!memcmp(header.magic, STATE_MAGIC, 4))
    {
      had_header = 1;
      if (!(header.flags & STATE_FLAG_STORED) == !!compress)
      {
        fclose(f);
        free(raw);
        return 1;   /* already that way */
      }
      ok = header.version == STATE_VERSION && header.raw_size == GBA_STATE_MEM_SIZE &&
           ((header.flags & STATE_FLAG_STORED) ? fread(raw, 1, GBA_STATE_MEM_SIZE, f) == GBA_STATE_MEM_SIZE
                                               : read_packed(f, raw, &header));
      /* the picture stays with it */
      if (ok)
        block_size = read_picture_block(f, &header, block);
      if (!block_size)
      {
        header.flags &= ~STATE_FLAG_PICTURE;
        header.reserved[0] = 0;
      }
    }
    else if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) == GBA_STATE_MEM_SIZE &&
             fseek(f, 0, SEEK_SET) == 0)
    {
      if (!compress)
      {
        fclose(f);
        free(raw);
        return 1;   /* an older build's state is stored as it is already */
      }
      ok = fread(raw, 1, GBA_STATE_MEM_SIZE, f) == GBA_STATE_MEM_SIZE;
    }
  }
  if (f)
    fclose(f);
  if (!ok)
  {
    free(raw);
    return 0;
  }

  if (!had_header)
  {
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, STATE_MAGIC, 4);
    header.version = STATE_VERSION;
    header.raw_size = GBA_STATE_MEM_SIZE;
    header.counter = counters[slot];
    header.flags = counted[slot] ? STATE_FLAG_COUNTED : 0;
  }
  header.flags = compress ? header.flags & ~STATE_FLAG_STORED : header.flags | STATE_FLAG_STORED;

  snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
  f = fopen(temp_path, "wb");
  ok = f != NULL;
  if (ok)
  {
    ok = fwrite(&header, sizeof(header), 1, f) == 1 && write_payload(f, raw, &header) &&
         (!block_size || fwrite(block, 1, block_size, f) == block_size) &&
         fseek(f, 0, SEEK_SET) == 0 && fwrite(&header, sizeof(header), 1, f) == 1;
    if (fclose(f) != 0)
      ok = 0;
    if (ok)
    {
      remove(path);
      ok = rename(temp_path, path) == 0;
    }
    if (!ok)
      remove(temp_path);
  }
  free(raw);
  if (ok)
    read_slot_info(slot);
  return ok;
}

int states_delete(int slot)
{
  char path[600];

  if (!states_exists(slot))
    return 0;
  states_file_name(slot, path, sizeof(path));
  if (remove(path) != 0)
    return 0;
  used[slot] = counted[slot] = 0;
  counters[slot] = mtimes[slot] = 0;
  return 1;
}
