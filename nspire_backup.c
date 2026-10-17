/* Cartridge save (SRAM / Flash / EEPROM) persistence for NSPIRE_LIBRETRO.
 *
 * An in-game save is the cartridge's memory as it is (the size says the
 * type), in the saves folder (nspire_paths.h): "<ROM name>.sav.tns", what
 * other gpSP builds and PC emulators read,
 * or with Compress saves on, gzipped: "<ROM name>.sav.gz.tns" (a PC unpacks
 * it like any .gz), as PocketSNES does. Writing one form removes the other;
 * loading takes the newer of the two. A compressed ROM (.gbz.tns) or a zip
 * uses the same files as the plain ROM. */

#include "common.h"
#include "gba_memory.h"
#include "streams/file_stream.h"
#include "nspire_states.h"
#include "nspire_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

char backup_filename[512];

static void backup_path(char *out)
{
  char base[500];   /* room for ".sav.tns" in the 512 of backup_filename */
  nspire_game_base(gamepak_filename, base, sizeof(base));
  snprintf(out, 512, "%s.sav.tns", base);
}

static void packed_backup_path(char *out)
{
  char base[496];
  nspire_game_base(gamepak_filename, base, sizeof(base));
  snprintf(out, 512, "%s.sav.gz.tns", base);
}

/* gzip (windowBits 31) or gunzip (47: gzip or zlib) from one buffer into
 * another; the bytes out, or 0. */
static u32 gzip_buffer(int pack, const u8 *in, u32 in_size, u8 *out, u32 out_size)
{
  z_stream z;
  int ok;

  memset(&z, 0, sizeof(z));
  ok = (pack ? deflateInit2(&z, 6, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY)
             : inflateInit2(&z, 47)) == Z_OK;
  if (!ok)
    return 0;
  z.next_in = (Bytef *)in;
  z.avail_in = in_size;
  z.next_out = out;
  z.avail_out = out_size;
  ok = (pack ? deflate(&z, Z_FINISH) : inflate(&z, Z_FINISH)) == Z_STREAM_END;
  if (pack)
    deflateEnd(&z);
  else
    inflateEnd(&z);
  return ok ? (u32)z.total_out : 0;
}

static int file_time(const char *path, u32 *time)
{
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  /* st_mtim: Ndless's os.h undefines the st_mtime shorthand. */
  *time = (u32)st.st_mtim.tv_sec;
  return 1;
}

#define WRITE_BACKUP_DELAY 10

static u32 backup_update_count = WRITE_BACKUP_DELAY + 1;
/* The save manager deleted the save: it isn't written back from memory
 * when the game is left, only once the game saves again. */
static int backup_deleted;

void nspire_backup_mark_dirty(void)
{
  backup_update_count = WRITE_BACKUP_DELAY;
  backup_deleted = 0;
}

/* Sets the type from a save's size, as load_backup does; 0 for a size no
 * cartridge has. */
static int backup_type_from_size(u32 sz);

/* A gzipped save into the cartridge memory. */
static u32 load_packed_backup(const char *name)
{
  FILE *f = fopen(name, "rb");
  long size;
  u8 *packed;
  u32 sz = 0;

  if (!f)
    return 0;
  if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && size < 0x40000 &&
      fseek(f, 0, SEEK_SET) == 0 && (packed = (u8 *)malloc((size_t)size)) != NULL)
  {
    if (fread(packed, 1, (size_t)size, f) == (size_t)size)
      sz = gzip_buffer(0, packed, (u32)size, gamepak_backup, sizeof(gamepak_backup));
    free(packed);
  }
  fclose(f);
  if (!sz || !backup_type_from_size(sz))
  {
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }
  return 1;
}

void nspire_load_cartridge_backup(void)
{
  char packed[512];
  u32 plain_time = 0, packed_time = 0;
  int has_plain, has_packed;

  if (!gamepak_filename[0])
    return;
  backup_deleted = 0;
  backup_path(backup_filename);
  packed_backup_path(packed);
  has_plain = file_time(backup_filename, &plain_time);
  has_packed = file_time(packed, &packed_time);
  if (has_packed && (!has_plain || packed_time >= plain_time))
  {
    if (!load_packed_backup(packed) && has_plain)
      load_backup(backup_filename);
  }
  else
    load_backup(backup_filename);
  backup_update_count = WRITE_BACKUP_DELAY + 1;
}

static u32 nspire_backup_file_size(void)
{
  switch (backup_type)
  {
    case BACKUP_SRAM:
      return 0x8000;

    case BACKUP_FLASH:
      if (flash_bank_cnt == FLASH_SIZE_128KB)
        return 0x20000;
      return 0x10000;

    case BACKUP_EEPROM:
      if (eeprom_size == EEPROM_512_BYTE)
        return 0x200;
      return 0x2000;

    default:
      break;
  }
  return 0;
}

u32 load_backup(char *name)
{
  RFILE *fd;
  int64_t sz64;
  u32 sz;

  fd = filestream_open(name, RETRO_VFS_FILE_ACCESS_READ,
                       RETRO_VFS_FILE_ACCESS_HINT_NONE);
  if (!fd)
  {
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }

  sz64 = filestream_get_size(fd);
  if (sz64 <= 0 || sz64 > (int64_t)sizeof(gamepak_backup))
  {
    filestream_close(fd);
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }

  sz = (u32)sz64;
  if (filestream_read(fd, gamepak_backup, sz) != (int64_t)sz)
  {
    filestream_close(fd);
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }
  filestream_close(fd);

  if (!backup_type_from_size(sz))
  {
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }
  return 1;
}

static int backup_type_from_size(u32 sz)
{
  switch (sz)
  {
    case 0x200:
      backup_type = BACKUP_EEPROM;
      eeprom_size = EEPROM_512_BYTE;
      break;

    case 0x2000:
      backup_type = BACKUP_EEPROM;
      eeprom_size = EEPROM_8_KBYTE;
      break;

    case 0x8000:
      backup_type = BACKUP_SRAM;
      break;

    case 0x10000:
      backup_type = BACKUP_FLASH;
      flash_bank_cnt = FLASH_SIZE_64KB;
      flash_device_id = FLASH_DEVICE_MACRONIX_64KB;
      break;

    case 0x20000:
      backup_type = BACKUP_FLASH;
      flash_bank_cnt = FLASH_SIZE_128KB;
      flash_device_id = FLASH_DEVICE_MACRONIX_128KB;
      break;

    default:
      return 0;
  }

  backup_type_reset = backup_type;
  flash_mode = FLASH_BASE_MODE;
  flash_command_position = 0;
  flash_bank_num = 0;
  eeprom_mode = EEPROM_BASE_MODE;
  eeprom_address = 0;
  eeprom_counter = 0;
  return 1;
}

/* The save gzipped into name. */
static u32 save_packed_backup(const char *name)
{
  u32 size = nspire_backup_file_size(), bound, packed_size;
  u8 *packed;
  FILE *f;
  int ok;

  if (!size || (backup_type != BACKUP_SRAM && backup_type != BACKUP_FLASH &&
                backup_type != BACKUP_EEPROM))
    return 0;
  bound = size + size / 8 + 64;
  packed = (u8 *)malloc(bound);
  if (!packed)
    return 0;
  packed_size = gzip_buffer(1, gamepak_backup, size, packed, bound);
  f = packed_size ? fopen(name, "wb") : NULL;
  ok = f && fwrite(packed, 1, packed_size, f) == packed_size;
  if (f && fclose(f) != 0)
    ok = 0;
  free(packed);
  return ok ? 1 : 0;
}

/* The game's save in the form Compress saves asks for (or compressed as
 * given), the other form removed once it's written. */
static u32 write_backup_as(int compressed)
{
  char plain[512], packed[512];
  u32 ok;

  backup_path(plain);
  packed_backup_path(packed);
  ok = compressed ? save_packed_backup(packed) : save_backup(plain);
  if (ok)
    remove(compressed ? plain : packed);
  return ok;
}

u32 save_backup(char *name)
{
  RFILE *fd;
  u32 backup_size;
  int64_t w;

  if (backup_type != BACKUP_SRAM && backup_type != BACKUP_FLASH &&
      backup_type != BACKUP_EEPROM)
    return 0;

  backup_size = nspire_backup_file_size();
  if (!backup_size)
    return 0;

  fd = filestream_open(name, RETRO_VFS_FILE_ACCESS_WRITE,
                       RETRO_VFS_FILE_ACCESS_HINT_NONE);
  if (!fd)
    return 0;

  w = filestream_write(fd, gamepak_backup, (int64_t)backup_size);
  filestream_close(fd);
  return (w == (int64_t)backup_size) ? 1 : 0;
}

void update_backup(void)
{
  if (!update_backup_flag || !gamepak_filename[0])
    return;

  if (backup_update_count != WRITE_BACKUP_DELAY + 1)
    backup_update_count--;

  if (backup_update_count == 0)
  {
    write_backup_as(nspire_compress_saves);
    backup_update_count = WRITE_BACKUP_DELAY + 1;
  }
}

void update_backup_force(void)
{
  if (!gamepak_filename[0] || backup_deleted)
    return;
  write_backup_as(nspire_compress_saves);
  backup_update_count = WRITE_BACKUP_DELAY + 1;
}

/* ---- For the save manager ---- */

int nspire_backup_info(long *bytes, int *compressed)
{
  char plain[512], packed[512];
  u32 plain_time = 0, packed_time = 0;
  int has_plain, has_packed;
  struct stat st;

  backup_path(plain);
  packed_backup_path(packed);
  has_plain = file_time(plain, &plain_time);
  has_packed = file_time(packed, &packed_time);
  if (!has_plain && !has_packed)
    return 0;
  *compressed = has_packed && (!has_plain || packed_time >= plain_time);
  *bytes = stat(*compressed ? packed : plain, &st) == 0 ? (long)st.st_size : 0;
  return 1;
}

int nspire_backup_set_compressed(int compressed)
{
  long bytes;
  int now;

  if (!nspire_backup_info(&bytes, &now))
    return 0;
  if (now == compressed)
    return 1;
  /* The game's memory has the newest save (it was loaded, and written to
   * since): written out in the other form. */
  return write_backup_as(compressed) != 0;
}

int nspire_backup_delete(void)
{
  char plain[512], packed[512];
  int removed = 0;

  backup_path(plain);
  packed_backup_path(packed);
  removed |= remove(plain) == 0;
  removed |= remove(packed) == 0;
  backup_deleted = 1;
  backup_update_count = WRITE_BACKUP_DELAY + 1;
  return removed;
}
