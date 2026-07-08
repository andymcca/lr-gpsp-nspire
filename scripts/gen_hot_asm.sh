#!/bin/sh
# Emit verbose ARM assembly for hot translation units (same defines/includes as Makefile).
# Uses -fno-lto so -S produces readable .s (final link still uses -flto).
set -e
cd "$(dirname "$0")/.."
COMM_DIR=../libretro-gpsp/libretro/libretro-common
BASE_FLAGS="-DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH -DHAVE_DYNAREC=1 -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=2"
INC="-Ilibretro-gpsp -Ilibretro-gpsp/libretro -I${COMM_DIR}/include -I../libretro-gpsp -I. -Inspire"
ASM_EXTRA="-fno-lto -S -fverbose-asm"
CFLAGS="-O3 -std=c99 -msoft-float -funsigned-char -fno-common -D_GNU_SOURCE ${BASE_FLAGS} ${INC} -Wall ${ASM_EXTRA}"
CXXFLAGS="-O3 -std=gnu++11 -msoft-float -funsigned-char -fno-common ${BASE_FLAGS} ${INC} -fno-exceptions -fno-rtti -Wall ${ASM_EXTRA}"
OUT=asm_inspect
mkdir -p "$OUT"
nspire-g++ ${CXXFLAGS} -o "$OUT/video.s" libretro-gpsp/video.cc
nspire-g++ ${CXXFLAGS} -o "$OUT/cpu.s" libretro-gpsp/cpu.cc
nspire-gcc ${CFLAGS} -o "$OUT/cpu_threaded.s" libretro-gpsp/cpu_threaded.c
nspire-gcc ${CFLAGS} -o "$OUT/gba_memory.s" libretro-gpsp/gba_memory.c
nspire-gcc ${CFLAGS} -o "$OUT/memmap.s" libretro-gpsp/memmap.c
nspire-gcc ${CFLAGS} -o "$OUT/nspire_video_present.s" nspire_video_present.c
nspire-gcc ${CFLAGS} -o "$OUT/nspire_upscale_from_gba.s" nspire_upscale_from_gba.c
ls -la "$OUT"
