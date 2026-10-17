/*
 * A game that jumps where no code can run (rom/bad_jump.S; an Advance Wars
 * from a bad dump does it as it starts) stops gpSP's dynarec there, with
 * nspire_bad_jump set (cpu.h). It used to jump to the lookup's "no code"
 * marker: the calculator reset, and qemu-arm stops with SIGSEGV.
 *
 *   badjump_test ROM PC      PC: where the ROM jumps (hex)
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern u32 nspire_rom_buffer_size_choice;
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

int main(int argc, char **argv)
{
  char line[120];
  u32 expected, ran;
  int frame;

  if (argc < 3)
  {
    fprintf(stderr, "usage: badjump_test ROM PC\n");
    return 2;
  }
  expected = (u32)strtoul(argv[2], NULL, 16);
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
  for (frame = 0; frame < 5 && !nspire_bad_jump; frame++)
    execute_arm_translate(execute_cycles);

  memcpy(&ran, &ewram[0x3FF00], 4);
  check(ran == 1, "the ROM ran");
  check(nspire_bad_jump, "the dynarec stopped at the jump");
  snprintf(line, sizeof(line), "it stopped at %08X, expected %08X", (unsigned)nspire_bad_jump_pc,
           (unsigned)expected);
  check(nspire_bad_jump_pc == expected, line);

  printf("%s: a jump to %08X: %d of %d checks passed\n", failures ? "FAIL" : "PASS",
         (unsigned)expected, checks - failures, checks);
  return failures != 0;
}
