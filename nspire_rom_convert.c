/* Compressing and uncompressing ROMs: see nspire_rom_convert.h. */

#include "common.h"
#include "nspire_rom_convert.h"
#include "nspire_gbz.h"
#include "nspire_paths.h"
#include "nspire_states.h"
#include "nspire_zip.h"
#include "streams/file_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define CHUNK      GBZ_PAGE_SIZE   /* a .gbz page, so a read is one page */
#define ZIP_LEVEL  6               /* as the states: nearly 9's size, faster */
#define ZIP_DATE   0x0021          /* 1980-01-01, the zip format's first day */

static u8 in_buf[CHUNK], out_buf[CHUNK];

static void put16(u8 *p, u32 v)
{
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
  p[0] = (u8)v;
  p[1] = (u8)(v >> 8);
  p[2] = (u8)(v >> 16);
  p[3] = (u8)(v >> 24);
}

static int ends_with(const char *s, const char *end)
{
  size_t a = strlen(s), b = strlen(end);
  return a >= b && !strcasecmp(s + a - b, end);
}

int rom_kind(const char *path)
{
  u8 head[4];
  size_t n;
  FILE *f = fopen(path, "rb");

  if (!f)
    return ROM_OTHER;
  n = fread(head, 1, 4, f);
  fclose(f);
  if (n == 4 && !memcmp(head, "PK\3\4", 4))
    return ROM_ZIP;
  if (n == 4 && !memcmp(head, "GBZ1", 4))
    return ROM_GBZ;
  return ends_with(path, ".gba.tns") || ends_with(path, ".gba") ? ROM_PLAIN : ROM_OTHER;
}

/* ---- Reading a ROM whatever it's in ---- */

struct source
{
  int kind;
  FILE *f;           /* plain */
  RFILE *rfile;      /* .gbz */
  gbz_rom *gbz;
  u32 gbz_crc;
  zip_rom *zip;
  u32 size, done;
};

static int source_open(struct source *s, const char *path)
{
  memset(s, 0, sizeof(*s));
  s->kind = rom_kind(path);
  if (s->kind == ROM_ZIP)
  {
    s->zip = zip_open(path);
    if (!s->zip)
      return 0;
    s->size = zip_size(s->zip);
    return 1;
  }
  if (s->kind == ROM_GBZ)
  {
    u8 head[24];
    FILE *f = fopen(path, "rb");
    if (!f)
      return 0;
    if (fread(head, 1, sizeof(head), f) == sizeof(head))
      s->gbz_crc = head[20] | (head[21] << 8) | (head[22] << 16) | ((u32)head[23] << 24);
    fclose(f);
    s->rfile = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!s->rfile || !(s->gbz = gbz_open(s->rfile)))
      return 0;
    s->size = gbz_rom_size(s->gbz);
    return 1;
  }
  s->f = fopen(path, "rb");
  if (!s->f)
    return 0;
  fseek(s->f, 0, SEEK_END);
  s->size = (u32)ftell(s->f);
  fseek(s->f, 0, SEEK_SET);
  return 1;
}

/* The next CHUNK bytes (fewer at the end) into in_buf: how many, 0 at the
 * end, -1 if they couldn't be read. */
static long source_read(struct source *s)
{
  u32 n = s->size - s->done < CHUNK ? s->size - s->done : CHUNK;

  if (!n)
    return 0;
  if (s->zip)
  {
    if (zip_read(s->zip, in_buf, n))
      return -1;
  }
  else if (s->gbz)
  {
    if (gbz_read_page(s->gbz, s->done / CHUNK, in_buf))
      return -1;
  }
  else if (fread(in_buf, 1, n, s->f) != n)
    return -1;
  s->done += n;
  return (long)n;
}

/* Closes it: 0 if everything read was right (a zip's CRC, a .gbz's). */
static int source_close(struct source *s, u32 crc)
{
  int ok = 1;
  if (s->zip)
  {
    ok = zip_check(s->zip) == 0;
    zip_close(s->zip);
  }
  if (s->gbz)
  {
    ok = !s->gbz_crc || s->gbz_crc == crc;
    gbz_close(s->gbz);
  }
  if (s->rfile)
    filestream_close(s->rfile);
  if (s->f)
    fclose(s->f);
  memset(s, 0, sizeof(*s));
  return ok ? 0 : -1;
}

/* ---- Names ---- */

/* "dir/" of a path ("" for none) into dir. */
static void folder_of(const char *path, char *dir, size_t size)
{
  const char *slash = strrchr(path, '/');
  snprintf(dir, size, "%.*s", slash ? (int)(slash - path) + 1 : 0, path);
}

/* A ROM's name without its extension: "Game" for "Game.gba". */
static void strip_extension(char *name)
{
  char *dot = strrchr(name, '.');
  if (dot && dot != name)
    *dot = 0;
}

static int file_exists(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

/* ---- Writing ---- */

static void report(rom_progress_fn progress, unsigned long done, unsigned long total)
{
  if (progress)
    progress(done, total);
}

/* Writes what's left in out_buf after deflating: 0, or -1 if it couldn't. */
static int write_out(FILE *f, z_stream *z)
{
  size_t n = CHUNK - z->avail_out;
  if (n && fwrite(out_buf, 1, n, f) != n)
    return -1;
  z->next_out = out_buf;
  z->avail_out = CHUNK;
  return 0;
}

/* The ROM at path deflated into a zip at zip_path with the name inner. */
static const char *write_zip(const char *path, const char *zip_path, const char *inner,
                             rom_progress_fn progress, unsigned long *total)
{
  struct source s;
  z_stream z;
  u8 header[46];
  u32 crc = crc32(0, NULL, 0), name_len = (u32)strlen(inner), packed, central, rom_size;
  const char *why = NULL;
  long n;
  int status = Z_OK;
  FILE *f;

  if (!source_open(&s, path))
  {
    source_close(&s, 0);
    return "Couldn't read it.";
  }
  rom_size = s.size;
  *total = 2ul * rom_size;
  f = fopen(zip_path, "wb");
  if (!f)
  {
    source_close(&s, 0);
    return "Couldn't make the new file.";
  }
  memset(&z, 0, sizeof(z));
  if (deflateInit2(&z, ZIP_LEVEL, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
  {
    source_close(&s, 0);
    fclose(f);
    return "Not enough memory.";
  }

  /* The file's header; its CRC and sizes are filled in at the end. */
  memset(header, 0, 30);
  put32(header, 0x04034B50);
  put16(header + 4, 20);
  put16(header + 8, 8);
  put16(header + 12, ZIP_DATE);
  put16(header + 26, name_len);
  if (fwrite(header, 1, 30, f) != 30 || fwrite(inner, 1, name_len, f) != name_len)
    why = "Not enough space for it.";

  z.next_out = out_buf;
  z.avail_out = CHUNK;
  while (!why && status != Z_STREAM_END)
  {
    n = source_read(&s);
    if (n < 0)
    {
      why = "Couldn't read it.";
      break;
    }
    crc = crc32(crc, in_buf, (uInt)n);
    z.next_in = in_buf;
    z.avail_in = (uInt)n;
    do
    {
      status = deflate(&z, n ? Z_NO_FLUSH : Z_FINISH);
      if (status == Z_STREAM_ERROR)
        why = "Couldn't compress it.";
      else if (z.avail_out == 0 && write_out(f, &z))
        why = "Not enough space for it.";
    } while (!why && (z.avail_in || (!n && status == Z_OK)));
    report(progress, s.done, *total);
  }
  if (!why && write_out(f, &z))
    why = "Not enough space for it.";
  packed = (u32)z.total_out;
  deflateEnd(&z);
  if (source_close(&s, crc) && !why)
    why = "Couldn't read it.";

  /* The central directory: one file, then its end. */
  central = 30 + name_len + packed;
  memset(header, 0, sizeof(header));
  put32(header, 0x02014B50);
  put16(header + 4, 20);
  put16(header + 6, 20);
  put16(header + 10, 8);
  put16(header + 14, ZIP_DATE);
  put32(header + 16, crc);
  put32(header + 20, packed);
  put32(header + 24, rom_size);
  put16(header + 28, name_len);
  if (!why && (fwrite(header, 1, 46, f) != 46 || fwrite(inner, 1, name_len, f) != name_len))
    why = "Not enough space for it.";
  memset(header, 0, 22);
  put32(header, 0x06054B50);
  put16(header + 8, 1);
  put16(header + 10, 1);
  put32(header + 12, 46 + name_len);
  put32(header + 16, central);
  if (!why && fwrite(header, 1, 22, f) != 22)
    why = "Not enough space for it.";
  /* The file header's CRC and sizes. */
  put32(header, crc);
  put32(header + 4, packed);
  put32(header + 8, rom_size);
  if (!why && (fseek(f, 14, SEEK_SET) != 0 || fwrite(header, 1, 12, f) != 12))
    why = "Not enough space for it.";
  if (fclose(f) != 0 && !why)
    why = "Not enough space for it.";
  return why;
}

/* The ROM at path as it is into out_path. */
static const char *write_plain(const char *path, const char *out_path, rom_progress_fn progress,
                               unsigned long *total, u32 *crc)
{
  struct source s;
  const char *why = NULL;
  long n;
  FILE *f;

  *crc = crc32(0, NULL, 0);
  if (!source_open(&s, path))
  {
    source_close(&s, 0);
    return "Couldn't read it.";
  }
  *total = 2ul * s.size;
  f = fopen(out_path, "wb");
  if (!f)
  {
    source_close(&s, 0);
    return "Couldn't make the new file.";
  }
  while ((n = source_read(&s)) > 0)
  {
    *crc = crc32(*crc, in_buf, (uInt)n);
    if (fwrite(in_buf, 1, (size_t)n, f) != (size_t)n)
    {
      why = "Not enough space for it.";
      break;
    }
    report(progress, s.done, *total);
  }
  if (!why && n < 0)
    why = "Couldn't read it.";
  if (source_close(&s, *crc) && !why)
    why = "It's damaged: it doesn't read back right.";
  if (fclose(f) != 0 && !why)
    why = "Not enough space for it.";
  return why;
}

/* Reads the new file back: a zip unpacks to the right CRC, a plain ROM has
 * it. */
static int check_new_file(const char *path, u32 crc, unsigned long half, rom_progress_fn progress)
{
  struct source s;
  u32 got = crc32(0, NULL, 0);
  long n;
  int ok, zip;

  if (!source_open(&s, path))
  {
    source_close(&s, 0);
    return 0;
  }
  zip = s.kind == ROM_ZIP;
  while ((n = source_read(&s)) > 0)
  {
    got = crc32(got, in_buf, (uInt)n);
    report(progress, half + s.done, 2 * half);
  }
  ok = n == 0 && s.done == half;
  if (source_close(&s, got))
    ok = 0;
  /* A zip's CRC was checked as it unpacked; a plain file's is checked here. */
  return ok && (zip || got == crc);
}

/* The last step of both: the checked file takes its name, the old one
 * goes. */
static const char *finish(const char *path, const char *temp, const char *new_path)
{
  if (rename(temp, new_path) != 0)
  {
    remove(temp);
    return "Couldn't name the new file.";
  }
  remove(path);
  return NULL;
}

const char *rom_compress(const char *path, char *new_path, size_t size, rom_progress_fn progress)
{
  char dir[512], inner[300], name[300], temp[700];
  unsigned long total = 0;
  const char *why;
  int kind = rom_kind(path);

  if (kind == ROM_ZIP)
    return "It's a zip already.";
  if (kind == ROM_OTHER)
    return "It isn't a GBA ROM.";
  folder_of(path, dir, sizeof(dir));
  nspire_game_name(path, inner, sizeof(inner));
  snprintf(name, sizeof(name), "%s", inner);
  strip_extension(name);
  snprintf(new_path, size, "%s%s.zip.tns", dir, name);
  if (file_exists(new_path))
    return "Its .zip is there already.";
  snprintf(temp, sizeof(temp), "%s.tmp", new_path);

  why = write_zip(path, temp, inner, progress, &total);
  if (!why && !check_new_file(temp, 0, total / 2, progress))
    why = "The new file didn't check out.";
  if (why)
  {
    remove(temp);
    return why;
  }
  return finish(path, temp, new_path);
}

const char *rom_uncompress(const char *path, char *new_path, size_t size, rom_progress_fn progress)
{
  char dir[512], inner[300], name[300], temp[700];
  unsigned long total = 0;
  const char *why;
  u32 crc;
  int kind = rom_kind(path), renamed;

  if (kind == ROM_PLAIN || kind == ROM_OTHER)
    return kind == ROM_PLAIN ? "It isn't compressed." : "It isn't a GBA ROM.";
  folder_of(path, dir, sizeof(dir));
  nspire_game_name(path, inner, sizeof(inner));
  /* A ROM named otherwise than .gba in a zip becomes a .gba: the file list
   * shows only those. */
  renamed = !ends_with(inner, ".gba");
  snprintf(name, sizeof(name), "%s", inner);
  if (renamed)
  {
    strip_extension(name);
    snprintf(new_path, size, "%s%s.gba.tns", dir, name);
  }
  else
    snprintf(new_path, size, "%s%s.tns", dir, name);
  if (file_exists(new_path))
    return "Its .gba is there already.";
  snprintf(temp, sizeof(temp), "%s.tmp", new_path);

  why = write_plain(path, temp, progress, &total, &crc);
  if (!why && !check_new_file(temp, crc, total / 2, progress))
    why = "The new file didn't check out.";
  if (why)
  {
    remove(temp);
    return why;
  }
  if (renamed)
  {
    /* Its saves go by the new name. */
    char from[600], to[600];
    nspire_game_base(path, from, sizeof(from));
    why = finish(path, temp, new_path);
    nspire_game_base(new_path, to, sizeof(to));
    if (!why)
      nspire_move_game_files(from, to);
    return why;
  }
  return finish(path, temp, new_path);
}
