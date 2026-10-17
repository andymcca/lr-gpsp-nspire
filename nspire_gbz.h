/* Compressed ROMs (.gbz.tns), made from .gba files by tools/gbz.py.
 *
 * gpSP keeps only part of a big ROM in memory and reads 32 KB pages from the
 * file as the game needs them (gba_memory.c), so the ROM is compressed page by
 * page: each 32 KB page is its own zlib stream and a table says where each one
 * starts. Reading a page means reading its compressed bytes and inflating them.
 *
 * File layout, all numbers 32-bit little-endian:
 *   0  "GBZ1"
 *   4  header size: 32 + 4 * (pages + 1)
 *   8  ROM size in bytes
 *  12  page size (32768)
 *  16  pages: ROM size / 32768, rounded up
 *  20  CRC-32 of the whole ROM (for tools; not checked here)
 *  24  flags (0)
 *  28  reserved (0)
 *  32  pages + 1 file offsets: page i is the bytes from offset i up to
 *      offset i + 1. A page of exactly 32768 bytes is stored as is, any other
 *      length is a zlib stream. Every page inflates to 32768 bytes: the last
 *      one is padded with zeros past the end of the ROM.
 */
#ifndef NSPIRE_GBZ_H
#define NSPIRE_GBZ_H

#include "streams/file_stream.h"

#define GBZ_PAGE_SIZE 32768u

typedef struct gbz_rom gbz_rom;

/* The file starts with "GBZ1". Leaves the file position at 0. */
int gbz_is_gbz(RFILE *file);
/* Reads the header and page table; NULL if the file is damaged or memory is
 * short. The file stays owned by the caller and must stay open. */
gbz_rom *gbz_open(RFILE *file);
void gbz_close(gbz_rom *rom);
u32 gbz_rom_size(const gbz_rom *rom);
/* Fills dst (GBZ_PAGE_SIZE bytes) with page 'page'. Returns 0 on success; on
 * failure the page reads as 0xFF bytes and -1 is returned. */
int gbz_read_page(gbz_rom *rom, u32 page, u8 *dst);

#endif
