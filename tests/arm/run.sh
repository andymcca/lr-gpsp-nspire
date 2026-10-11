#!/bin/bash
# Builds and runs the ARM tests (tests/arm/Makefile) under qemu-arm.
# Needs the Ndless toolchain (. ~/nspire/build/env.sh) and qemu-arm (QEMU=,
# by default the one unpacked in ~/nspire/build/tools/qemu).
set -u
cd "$(dirname "$0")"
QEMU=${QEMU:-$HOME/nspire/build/tools/qemu/root/usr/bin/qemu-arm}
[ -x "$QEMU" ] || { echo "no qemu-arm at $QEMU"; exit 1; }
command -v nspire-gcc >/dev/null || . ~/nspire/build/env.sh
make -s -j"$(nproc)" >/dev/null || { echo "build failed"; exit 1; }
"$QEMU" -cpu arm926 build/blend_test
