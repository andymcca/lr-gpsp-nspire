#!/bin/bash
# Estimates how fast gpSP runs a game on the calculator, from a PC: see
# tests/calc_speed.py (or run with --help).
#   tests/calc_speed.sh [STATE] [--frames N] [--draw N] [--top N] ...
# Needs the Ndless toolchain (. ~/nspire/build/env.sh, done here if needed)
# and a qemu-arm built with plugins (QEMU=, default the one in
# ~/nspire/build/tools/qemu-plugins; DEVNOTES.md says how to build it).
command -v arm-none-eabi-gcc >/dev/null || . ~/nspire/build/env.sh
exec python3 -I "$(dirname "$0")/calc_speed.py" "$@"
