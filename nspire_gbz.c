/* Compressed ROMs (.gbz.tns): see nspire_gbz.h. */

#include "common.h"
#include "nspire_gbz.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define GBZ_HEADER_FIXED 32u

struct gbz_rom
{
  RFILE *file;
  u32 rom_size;
  u32 pages;
  u32 *offsets;   /* pages + 1 */
};

/* A compressed page is never longer than a page (the tool stores such pages
 * as they are), so one page of room holds any of them. */
static u8 packed[GBZ_PAGE_SIZE];

static u32 read_le32(const u8 *p)
{
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

int gbz_is_gbz(RFILE *file)
{
  u8 magic[4];
  int is_gbz;

  if (filestream_seek(file, 0, RETRO_VFS_SEEK_POSITION_START) != 0)
    return 0;
  is_gbz = filestream_read(file, magic, 4) == 4 && memcmp(magic, "GBZ1", 4) == 0;
  filestream_seek(file, 0, RETRO_VFS_SEEK_POSITION_START);
  return is_gbz;
}

gbz_rom *gbz_open(RFILE *file)
{
  u8 head[GBZ_HEADER_FIXED];
  u8 *table = NULL;
  gbz_rom *rom = NULL;
  int64_t file_size = filestream_get_size(file);
  u32 header_size, rom_size, page_size, pages, i;

  if (filestream_seek(file, 0, RETRO_VFS_SEEK_POSITION_START) != 0 ||
      filestream_read(file, head, sizeof(head)) != (int64_t)sizeof(head) ||
      memcmp(head, "GBZ1", 4) != 0)
    return NULL;

  header_size = read_le32(head + 4);
  rom_size = read_le32(head + 8);
  page_size = read_le32(head + 12);
  pages = read_le32(head + 16);
  /* gpSP maps at most 32 MB of ROM. */
  if (page_size != GBZ_PAGE_SIZE || rom_size == 0 || rom_size > 32u * 1024 * 1024 ||
      pages != (rom_size + GBZ_PAGE_SIZE - 1) / GBZ_PAGE_SIZE ||
      header_size != GBZ_HEADER_FIXED + 4 * (pages + 1) || file_size < (int64_t)header_size)
    return NULL;

  rom = (gbz_rom *)calloc(1, sizeof(*rom));
  table = (u8 *)malloc(4 * (pages + 1));
  if (!rom || !table)
    goto fail;
  rom->offsets = (u32 *)malloc(4 * (pages + 1));
  if (!rom->offsets ||
      filestream_read(file, table, 4 * (pages + 1)) != (int64_t)(4 * (pages + 1)))
    goto fail;

  for (i = 0; i <= pages; i++)
  {
    rom->offsets[i] = read_le32(table + 4 * i);
    if (rom->offsets[i] < header_size || (int64_t)rom->offsets[i] > file_size)
      goto fail;
    if (i && (rom->offsets[i] < rom->offsets[i - 1] ||
              rom->offsets[i] - rom->offsets[i - 1] > GBZ_PAGE_SIZE ||
              rom->offsets[i] == rom->offsets[i - 1]))
      goto fail;
  }

  free(table);
  rom->file = file;
  rom->rom_size = rom_size;
  rom->pages = pages;
  return rom;

fail:
  free(table);
  if (rom)
    free(rom->offsets);
  free(rom);
  return NULL;
}

void gbz_close(gbz_rom *rom)
{
  if (!rom)
    return;
  free(rom->offsets);
  free(rom);
}

u32 gbz_rom_size(const gbz_rom *rom)
{
  return rom->rom_size;
}

int gbz_read_page(gbz_rom *rom, u32 page, u8 *dst)
{
  u32 length;
  uLongf out_size = GBZ_PAGE_SIZE;

  if (page >= rom->pages)
    goto fail;
  length = rom->offsets[page + 1] - rom->offsets[page];
  if (filestream_seek(rom->file, rom->offsets[page], RETRO_VFS_SEEK_POSITION_START) != 0)
    goto fail;

  if (length == GBZ_PAGE_SIZE)
  {
    if (filestream_read(rom->file, dst, GBZ_PAGE_SIZE) != GBZ_PAGE_SIZE)
      goto fail;
    return 0;
  }

  if (filestream_read(rom->file, packed, length) != (int64_t)length ||
      uncompress(dst, &out_size, packed, length) != Z_OK || out_size != GBZ_PAGE_SIZE)
    goto fail;
  return 0;

fail:
  memset(dst, 0xFF, GBZ_PAGE_SIZE);
  return -1;
}
