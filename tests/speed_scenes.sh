#!/bin/bash
# The PC speed estimate (tests/calc_speed.sh) for every test scene at once,
# side by side: the owner's Advance Wars name entry (an interrupt every
# line), Emerald into the tall grass (the battle wipe), and the Fire
# Emblem, Golden Sun and Minish Cap intros (frames 1700-2400 from power-on,
# tests/arm/replay/boot.keys). One line each: ms per frame running the game,
# drawing and making the picture, and game frames a second drawing every one.
#   tests/speed_scenes.sh [--model ARGS] [SETTING=VALUE...]
# (--model: for the ARM926 model, e.g. repl=random; settings as in
# calc_speed.sh --set, e.g. batch=0). Needs the games in $GBA_GAMES (see
# tests/arm/run.sh); a scene without its game is left out.
cd "$(dirname "$0")/.." || exit 1
G=${GBA_GAMES:-$HOME/nspire/build/testroms/gba/games}
extra=()
while [ $# -gt 0 ]; do
  case $1 in
    --model) extra+=(--model "$2"); shift 2 ;;
    *) extra+=(--set "$1"); shift ;;
  esac
done
tag=scene$$
run() {   # name game state keys frames skip
  local name=$1 game="$G/$2" state=$3 keys=$4 out
  [ -f "$game" ] || return
  [ "$state" = - ] || state="$G/$state"
  [ "$state" = - ] || [ -f "$state" ] || return
  out=$(tests/calc_speed.sh "$state" --rom "$game" --keys "$keys" --frames "$5" --skip "$6" \
        --tag "$tag-$name" "${extra[@]}" 2>&1)
  printf '%-9s game %5s  drawing %5s  picture %5s  %s\n' "$name" \
    "$(echo "$out" | awk '$1 == "game" {print $2; exit}')" \
    "$(echo "$out" | awk '$1 == "drawing" {print $2; exit}')" \
    "$(echo "$out" | awk '$1 == "picture" {print $2; exit}')" \
    "$(echo "$out" | sed -n 's/^  all .*average: \([0-9.]*\).*/\1 fps/p')"
}
# Build first (the runs share the build).
tests/calc_speed.sh --help >/dev/null 2>&1
command -v arm-none-eabi-gcc >/dev/null || . ~/nspire/build/env.sh
make -s -C tests/arm build/perf >/dev/null || exit 1
{
  run aw "Advance Wars (USA) (Rev 1).gba" aw_latest.svs.tns - 600 30 &
  run emerald "Pokemon - Emerald Version (USA, Europe).gba" emerald_route_grass.svs.tns tests/arm/replay/emerald_grass.keys 1900 30 &
  run fe "Fire Emblem - The Sacred Stones (USA, Australia).gba" - tests/arm/replay/boot.keys 2400 1700 &
  run gs "Golden Sun (USA, Europe).gba" - tests/arm/replay/boot.keys 2400 1700 &
  run mc "Legend of Zelda, The - The Minish Cap (USA).gba" - tests/arm/replay/boot.keys 2400 1700 &
  wait
} | sort
rm -f tests/arm/build/speed/$tag-*
