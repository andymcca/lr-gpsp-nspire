#!/bin/sh
# Builds and runs the PC tests: the core and the frontend's parts
# (core_tests.c, frontend_tests.c), then whole scenarios on a simulated
# calculator (host_tests.py, host_hw.c). Needs gcc, g++, zlib and python3,
# and jsmolka's GBA test ROMs (github.com/jsmolka/gba-tests) in $GBA_TESTS
# (default: ~/nspire/build/testroms/gba/gba-tests).
set -e
cd "$(dirname "$0")"
GBA_TESTS=${GBA_TESTS:-$HOME/nspire/build/testroms/gba/gba-tests}
WORK=build/work
make -s
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$GBA_TESTS/arm/arm.gba" "$WORK/arm.gba.tns"
cp "$GBA_TESTS/save/sram.gba" "$WORK/sram.gba.tns"

# An 8 MB ROM: the ARM test, then text-like, random and padding bytes, with
# an odd size so the last page is partly past the end.
python3 -I - "$WORK" <<'EOF'
import random, sys
random.seed(7)
work = sys.argv[1]
rom = bytearray(open(work + "/arm.gba.tns", "rb").read())
while len(rom) < 3 * 1024 * 1024:
    rom += bytes(random.choice(b"ABCDEFGH ") for _ in range(4096))
rom += random.randbytes(2 * 1024 * 1024)
rom += b"\xff" * (8 * 1024 * 1024 - len(rom) - 1000)
open(work + "/big.gba.tns", "wb").write(rom)
EOF
python3 -I ../../tools/gbz.py pack -o "$WORK" "$WORK/arm.gba.tns" "$WORK/big.gba.tns" > /dev/null

# Zipped ROMs as zip tools write them (nspire_zip.c): deflated with a text
# file before the ROM in a folder, stored, without a .gba (the biggest file
# counts), with no ROM at all, damaged, and the 8 MB one.
python3 -I - "$WORK" <<'EOF'
import sys, zipfile
work = sys.argv[1]
arm = open(work + "/arm.gba.tns", "rb").read()
big = open(work + "/big.gba.tns", "rb").read()
D, S = zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED
def make(name, files, method=D):
    with zipfile.ZipFile(work + "/" + name, "w", method) as z:
        for inner, data in files:
            z.writestr(inner, data)
make("arm.zip.tns", [("readme.txt", b"read me\n" * 40), ("roms/arm.gba", arm)])
make("stored.zip.tns", [("arm.gba", arm)], S)
make("nogba.zip.tns", [("notes.txt", b"x" * 500), ("game.bin", arm)])
make("text.zip.tns", [("a.txt", b"only text")])
make("big.zip.tns", [("big.gba", big)])
data = bytearray(open(work + "/arm.zip.tns", "rb").read())
i = data.index(b"roms/arm.gba") + len("roms/arm.gba") + 2000   # inside the ROM's deflated data
data[i] ^= 0xFF
open(work + "/bad.zip.tns", "wb").write(bytes(data))
EOF
./build/core_tests "$WORK"
./build/frontend_tests "$WORK"
python3 -I host_tests.py
