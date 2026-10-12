/*
 * video_fill_u16_strip_arm (nspire/scanline_fill16.S), video.cc's backdrop
 * fill, at every start pixel and length, run under qemu-arm like the other
 * ARM tests (tests/arm/run.sh).
 *
 * The fill used to store 16 bytes at a time from wherever it started. A
 * window edge on an odd pixel (Pokemon battle wipes, door transitions)
 * started it 2 bytes off a word, and the calculator checks alignment: it
 * crashed. qemu stops the same way (SIGBUS), so the old fill fails here.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;

void video_fill_u16_strip_arm(u16 *dst, u32 nh, u32 value);

#define SENTINEL 0x5A5A
#define LINE 240

static u16 buffer[LINE + 64] __attribute__((aligned(32)));
static int failures, checks;

static void check(int ok, const char *what)
{
  checks++;
  if (!ok)
  {
    failures++;
    printf("FAIL  %s\n", what);
  }
}

/* Fills [start, start + count) of the line, which begins 16 pixels into
 * the buffer, and checks every pixel of the buffer. */
static void fill_and_check(u32 start, u32 count, u32 value)
{
  u16 *line = buffer + 16;
  char what[96];
  u32 i, wrong = 0;

  for (i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++)
    buffer[i] = SENTINEL;
  video_fill_u16_strip_arm(line + start, count, value);
  for (i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++)
  {
    u32 pixel = i - 16;
    u16 want = (i >= 16 && pixel >= start && pixel < start + count) ? (u16)value : SENTINEL;
    if (buffer[i] != want)
      wrong++;
  }
  snprintf(what, sizeof(what), "fill of %u pixels from pixel %u: %u wrong", (unsigned)count,
           (unsigned)start, (unsigned)wrong);
  check(wrong == 0, what);
}

int main(void)
{
  u32 start, count;

  /* Every start pixel within a few words, every short length (the tail
   * loop and the 8-pixel stores), and whole lines. */
  for (start = 0; start < 8; start++)
    for (count = 0; count <= 40; count++)
      fill_and_check(start, count, 0xABCD1234u + start * 977u + count);
  for (start = 0; start < 8; start++)
    fill_and_check(start, LINE - start, 0x7FFFu);
  /* Window edges as a battle wipe has them, anywhere on the line. */
  for (start = 1; start < LINE; start += 7)
    fill_and_check(start, (LINE - start) / 3, 0x0000u);

  printf("%s: scanline fill: %d of %d checks passed\n", failures ? "FAIL" : "PASS",
         checks - failures, checks);
  return failures != 0;
}
