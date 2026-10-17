/*
 * gpSP's dynarec, built for the calculator's CPU and run under qemu-arm
 * (tests/arm/run.sh), on rom/ram_code.S: code in IWRAM and EWRAM that
 * branches between its own blocks and is rewritten by DMA and by the CPU.
 *
 *   dynarec_test ROM POLICY      POLICY: 0 = Partial SMC, 1 = Classic
 *
 * Partial SMC used to leave such branches as a placeholder (B +0) that ran
 * the next host instructions instead: Pokemon Emerald froze or crashed at
 * once. A branch linked straight into RAM code would run stale code after a
 * DMA, so the results after each rewrite are checked too.
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern u32 nspire_dynarec_ram_policy, nspire_rom_buffer_size_choice;
extern u16 *gba_screen_pixels;
extern u32 execute_cycles;
void init_gamepak_buffer(void);

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

static u32 result(unsigned offset)
{
  u32 value;
  memcpy(&value, &ewram[0x3FF00 + offset], 4);
  return value;
}

int main(int argc, char **argv)
{
  static const struct { unsigned offset; u32 expected; const char *what; } cases[] = {
    { 4, 165, "ARM code in IWRAM: a call, a loop and a jump between its blocks" },
    { 8, 275, "the same after DMA rewrote it" },
    { 12, 385, "the same after a CPU store rewrote it" },
    { 16, 165, "Thumb code in IWRAM" },
    { 20, 275, "the same after a 16-bit DMA rewrote it" },
    { 24, 165, "ARM code in EWRAM" },
    { 28, 275, "the same after DMA rewrote it" },
  };
  char line[160];
  unsigned i;
  int frame;

  if (argc < 3)
  {
    fprintf(stderr, "usage: dynarec_test ROM POLICY\n");
    return 2;
  }
  nspire_dynarec_ram_policy = atoi(argv[2]);
  nspire_rom_buffer_size_choice = 2;
  init_gamepak_buffer();
  init_sound();
  gba_screen_pixels = calloc(1, GBA_SCREEN_BUFFER_SIZE);
  memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
  if (load_gamepak(NULL, argv[1], FEAT_AUTODETECT, FEAT_AUTODETECT, SERIAL_MODE_AUTO) != 0)
  {
    printf("FAIL  couldn't load %s\n", argv[1]);
    return 1;
  }
  reset_gba();
  memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
  for (frame = 0; frame < 10; frame++)
    execute_arm_translate(execute_cycles);

  snprintf(line, sizeof(line), "the ROM ran to its end (it was at step %u)", (unsigned)result(32));
  check(result(0) == 0x454E4F44, line);
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    snprintf(line, sizeof(line), "%s: sum %u, expected %u", cases[i].what,
             (unsigned)result(cases[i].offset), (unsigned)cases[i].expected);
    check(result(cases[i].offset) == cases[i].expected, line);
  }

  printf("%s: dynarec with %s SMC: %d of %d checks passed\n", failures ? "FAIL" : "PASS",
         nspire_dynarec_ram_policy ? "Classic" : "Partial", checks - failures, checks);
  return failures != 0;
}
