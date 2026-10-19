# Building gpSP for TI-Nspire with the Ndless SDK

This ZIP is intended to be extracted as one project folder. Its `docker-compose.yml`
mounts that folder at `/work` and runs `make` from `/work`; it does not rely on a
particular project directory name or a sibling checkout.

## Prerequisites

- Docker with Compose v2, or a native host with a complete Ndless SDK.
- An SDK image tagged `ndless-dev:latest`, or set `NSPIRE_BUILD_IMAGE` to your image.
- `nspire-gcc`, `nspire-g++`, `nspire-tools`, `genzehn`, `make-prg`, and the
  SDK's ARM C/C++ compilers on `PATH`/in the SDK toolchain directory.
- Ndless third-party zlib installed (`include/zlib.h` and `lib/libz.a`, either
  in the SDK root or `$HOME/.ndless`).

The uploaded Ndless source-only ZIP is not by itself a built SDK; build its
cross-compiler and third-party libraries before compiling gpSP.

## Clean build with Docker

Run these commands from the extracted project root:

```bash
docker compose run --rm ndless-dev make check-sdk
docker compose run --rm ndless-dev sh -c 'make clean && make -j2'
```

For a native host with the SDK installed:

```bash
make check-sdk
make clean
make -j2
```

The output is `gpsp_libretro.tns`. The build links the ARM executable
`gpsp_libretro.elf`, runs `genzehn` to create `gpsp_libretro.tns.zehn` with the
Ndless and touchpad metadata, and invokes `make-prg` to create the final `.tns`.
The Makefile's `check-sdk` target reports missing compiler executables or zlib
files before compiling objects. Avoid running `make -j clean all`; the clean and
build goals can race. Use separate `make clean` and `make -j2` commands.

## Target details

The executable targets the ARM926EJ-S / ARMv5TE CPU in ARM mode with software
floating point. It does not enable VFP/NEON. The Makefile uses header dependency
files so a changed shared header triggers the relevant incremental rebuilds.
The final `.tns` metadata currently sets the minimum Ndless version/revision in
`ZEHNFLAGS`.

## Touchpad swipe controls

Configure input offers swipe enablement, dead zone and sensitivity settings.
Swipe direction is derived from relative touch coordinates; X and Y are scaled
against the touchpad's measured width and height so the CX II's 4:3 aspect ratio
is respected. Diagonal movement raises two independent component inputs and a
separate 8-way swipe input. A swipe or calculator key can be bound to multiple
GBA action rows for multi-button input, and a swipe can be combined with a
physical calculator key for a gated input.

## Host validation

The PC frontend suite needs only gcc/g++, zlib and Python:

```bash
make -C tests/host clean
make -C tests/host -j2 all
mkdir -p /tmp/gpsp-frontend-tests
tests/host/build/frontend_tests /tmp/gpsp-frontend-tests
```

This checks the shared input polling path, normalized swipe calculations,
config persistence, and multi-button mappings. A successful host test is not a
substitute for a target compiler build or a test on the physical calculator.
