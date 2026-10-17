#!/bin/bash
# Builds and runs the ARM tests (tests/arm/Makefile) under qemu-arm.
# Needs the Ndless toolchain (. ~/nspire/build/env.sh) and qemu-arm (QEMU=,
# by default the one unpacked in ~/nspire/build/tools/qemu).
#
# The Pokemon Emerald replay needs files that can't be in the repository,
# in $GBA_GAMES (default ~/nspire/build/testroms/gba/games):
#   Pokemon - Emerald Version (USA, Europe).gba   the game (.zip: also tried)
#   gba_bios.bin                                  the GBA BIOS
#   emerald_route_grass.svs.tns                   a gpSP save state on a
#                                                 route next to tall grass
#   Advance Wars (USA) (Rev 1) (damaged).gba      the owner's bad dump (CRC32
#                                                 418740D0, not No-Intro's)
# Without them those are skipped (said so).
set -u
cd "$(dirname "$0")"
QEMU=${QEMU:-$HOME/nspire/build/tools/qemu/root/usr/bin/qemu-arm}
GBA_GAMES=${GBA_GAMES:-$HOME/nspire/build/testroms/gba/games}
[ -x "$QEMU" ] || { echo "no qemu-arm at $QEMU"; exit 1; }
command -v nspire-gcc >/dev/null || . ~/nspire/build/env.sh
make -s -j"$(nproc)" >/dev/null || { echo "build failed"; exit 1; }

failed=0

# A test program; a crash fails too (qemu stops at an unaligned stmia as the
# calculator does).
run() {
  local name=$1 status
  shift
  "$QEMU" -cpu arm926 "$@"
  status=$?
  if [ $status -gt 128 ]; then
    echo "FAIL: $name crashed (signal $((status - 128)))"
  fi
  [ $status -eq 0 ] || failed=$((failed + 1))
}

run blend_test build/blend_test
run fill_test build/fill_test
run "dynarec_test, Partial SMC" build/dynarec_test build/ram_code.gba 0
run "dynarec_test, Classic SMC" build/dynarec_test build/ram_code.gba 1
run "badjump_test, Thumb" build/badjump_test build/bad_jump_thumb.gba 910803DE
run "badjump_test, ARM" build/badjump_test build/bad_jump_arm.gba 0E000000

# Pokemon Emerald from a save state: into the tall grass until a wild
# Pokemon appears (its battle wipe draws window edges on odd pixels; the
# battle text is up by frame 1300). video.cc crashed in the wipe and Partial
# SMC froze at once. Every setting has to get there and draw the same
# frames, and the last one has to be the battle's.
EMERALD_LAST="frame 1900 e0a09c4a"
emerald="$GBA_GAMES/Pokemon - Emerald Version (USA, Europe).gba"
if [ -f "$emerald" ] && [ -f "$GBA_GAMES/gba_bios.bin" ] && [ -f "$GBA_GAMES/emerald_route_grass.svs.tns" ]; then
  # Semihosting splits the command line at spaces: links without them.
  mkdir -p build/games build/replays
  ln -sf "$emerald" build/games/emerald.gba
  ln -sf "$GBA_GAMES/gba_bios.bin" build/games/gba_bios.bin
  ln -sf "$GBA_GAMES/emerald_route_grass.svs.tns" build/games/emerald_route_grass.svs
  roms="emerald.gba"
  if [ -f "${emerald%.gba}.zip" ]; then
    ln -sf "${emerald%.gba}.zip" build/games/emerald.zip
    roms="$roms emerald.zip"
  fi
  first="" replays=0 replay_failed=0
  for rom in $roms; do
    for renderer in 0 1; do
      for policy in 0 1; do
        for rombuf in 8 32; do
          [ "$rom" = emerald.zip ] && [ $rombuf = 32 ] && continue   # zips are unpacked whole anyway
          name="$rom, $([ $renderer = 0 ] && echo video.cc || echo legacy) renderer, $([ $policy = 0 ] && echo Partial || echo Classic) SMC, ${rombuf} MB ROM buffer"
          out=build/replays/$rom-r$renderer-p$policy-b$rombuf.txt
          "$QEMU" -cpu arm926 build/replay build/games/$rom build/games/gba_bios.bin \
            build/games/emerald_route_grass.svs $rombuf $policy $renderer 1900 replay/emerald_grass.keys > "$out" 2>&1
          status=$?
          replays=$((replays + 1))
          if [ $status -ne 0 ]; then
            echo "FAIL  Emerald replay, $name: $([ $status -gt 128 ] && echo "crashed (signal $((status - 128)))" || echo "exit $status") at $(grep frame "$out" | tail -1 | cut -d' ' -f2 || echo 0)"
            replay_failed=$((replay_failed + 1))
          elif [ "$(tail -1 "$out")" != "$EMERALD_LAST" ]; then
            echo "FAIL  Emerald replay, $name: last frame $(tail -1 "$out"), expected $EMERALD_LAST (the battle)"
            replay_failed=$((replay_failed + 1))
          elif [ -z "$first" ]; then
            first=$out
          elif ! cmp -s "$first" "$out"; then
            echo "FAIL  Emerald replay, $name: frames differ from the first setting's, from $(diff "$first" "$out" | sed -n 's/^> frame \([0-9]*\).*/\1/p' | head -1)"
            replay_failed=$((replay_failed + 1))
          fi
        done
      done
    done
  done
  echo "$([ $replay_failed = 0 ] && echo PASS || echo FAIL): Emerald replay: $((replays - replay_failed)) of $replays settings reached the battle with the same frames"
  [ $replay_failed = 0 ] || failed=$((failed + 1))
else
  echo "skipped: Emerald replay (no game, BIOS or state in $GBA_GAMES)"
fi

# The owner's Advance Wars, a bad dump: a function pointer of its reads as
# 0x910803DF, and the game jumps there as it starts (the calculator reset).
# gpSP has to stop there instead.
aw="$GBA_GAMES/Advance Wars (USA) (Rev 1) (damaged).gba"
if [ -f "$aw" ] && [ -f "$GBA_GAMES/gba_bios.bin" ]; then
  mkdir -p build/games build/replays
  ln -sf "$aw" build/games/aw_damaged.gba
  ln -sf "$GBA_GAMES/gba_bios.bin" build/games/gba_bios.bin
  : > build/replays/no_keys
  "$QEMU" -cpu arm926 build/replay build/games/aw_damaged.gba build/games/gba_bios.bin - 32 0 0 300 \
    build/replays/no_keys > build/replays/aw_damaged.txt 2>&1
  status=$?
  if [ $status = 3 ] && grep -q "stopped at 910803DE" build/replays/aw_damaged.txt; then
    echo "PASS: the damaged Advance Wars stops at its jump to 910803DE ($(cut -d' ' -f1-2 build/replays/aw_damaged.txt))"
  else
    echo "FAIL  the damaged Advance Wars: $([ $status -gt 128 ] && echo "crashed (signal $((status - 128)))" || echo "exit $status: $(tail -1 build/replays/aw_damaged.txt)")"
    failed=$((failed + 1))
  fi
else
  echo "skipped: the damaged Advance Wars (not in $GBA_GAMES)"
fi

[ $failed = 0 ] && echo "ARM tests: all passed" || echo "ARM tests: $failed failed"
[ $failed = 0 ]
