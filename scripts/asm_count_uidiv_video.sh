#!/bin/sh
set -e
cd "$(dirname "$0")/.."
COMM_DIR=../libretro-gpsp/libretro/libretro-common
BASE_FLAGS="-DNSPIRE_BUILD -DNSPIRE_LIBRETRO -DNSPIRE_NO_AUDIO -DARM_ARCH -DHAVE_DYNAREC=1 -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=2"
INC="-Ilibretro-gpsp -Ilibretro-gpsp/libretro -I${COMM_DIR}/include -I../libretro-gpsp -I. -Inspire"
CXXFLAGS="-O3 -std=gnu++11 -msoft-float -funsigned-char -fno-common ${BASE_FLAGS} ${INC} -fno-exceptions -fno-rtti -Wall -fno-lto -S -fverbose-asm"
mkdir -p asm_inspect
nspire-g++ ${CXXFLAGS} -o asm_inspect/video.s libretro-gpsp/video.cc
echo -n "__aeabi_uidivmod count in video.s: "
grep -c __aeabi_uidivmod asm_inspect/video.s || echo 0
