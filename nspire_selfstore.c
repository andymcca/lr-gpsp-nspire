/* Settings kept inside the program file: see nspire_selfstore.h. */

#include "nspire_selfstore.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOOTER_SIZE 16
static const char magic[8] = { 'G', 'P', 'S', 'P', 'S', 'E', 'T', 'S' };

static uint32_t get32(const unsigned char *p)
{
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

/* Where the area starts in the open file, or -1 without one; *length gets
 * the stored length, *file_size the file's size. */
static long find_area(FILE *f, long *file_size, uint32_t *length)
{
  unsigned char footer[FOOTER_SIZE];

  if (fseek(f, 0, SEEK_END) != 0)
    return -1;
  *file_size = ftell(f);
  if (*file_size < FOOTER_SIZE + SELFSTORE_SIZE ||
      fseek(f, *file_size - FOOTER_SIZE, SEEK_SET) != 0 ||
      fread(footer, 1, FOOTER_SIZE, f) != FOOTER_SIZE || memcmp(footer, magic, 8) != 0 ||
      get32(footer + 12) != SELFSTORE_SIZE || get32(footer + 8) > SELFSTORE_SIZE)
    return -1;
  *length = get32(footer + 8);
  return *file_size - FOOTER_SIZE - SELFSTORE_SIZE;
}

long selfstore_read(const char *program_path, void *data, size_t size)
{
  FILE *f;
  long file_size, start;
  uint32_t length;

  if (!program_path[0] || !(f = fopen(program_path, "rb")))
    return -1;
  start = find_area(f, &file_size, &length);
  if (start < 0 || length > size || fseek(f, start, SEEK_SET) != 0 ||
      fread(data, 1, length, f) != length)
    start = -1;
  fclose(f);
  return start < 0 ? -1 : (long)length;
}

int selfstore_write(const char *program_path, const void *data, size_t length)
{
  unsigned char *area;
  long file_size, start;
  uint32_t old_length;
  int ok = 0;
  FILE *f;

  if (length > SELFSTORE_SIZE || !program_path[0] || !(f = fopen(program_path, "r+b")))
    return 0;
  area = (unsigned char *)calloc(1, SELFSTORE_SIZE + FOOTER_SIZE);
  if (!area)
  {
    fclose(f);
    return 0;
  }
  start = find_area(f, &file_size, &old_length);
  if (start >= 0 && old_length == length && fseek(f, start, SEEK_SET) == 0 &&
      fread(area, 1, length, f) == length && !memcmp(area, data, length))
    ok = 1;   /* already there */
  else
  {
    if (start < 0)
      start = file_size;
    /* The whole area and its footer in one write. */
    memset(area, 0, SELFSTORE_SIZE + FOOTER_SIZE);
    memcpy(area, data, length);
    memcpy(area + SELFSTORE_SIZE, magic, 8);
    put32(area + SELFSTORE_SIZE + 8, (uint32_t)length);
    put32(area + SELFSTORE_SIZE + 12, SELFSTORE_SIZE);
    ok = start >= 0 && fseek(f, start, SEEK_SET) == 0 &&
         fwrite(area, 1, SELFSTORE_SIZE + FOOTER_SIZE, f) == SELFSTORE_SIZE + FOOTER_SIZE;
  }
  free(area);
  if (fclose(f) != 0)
    ok = 0;
  return ok;
}
