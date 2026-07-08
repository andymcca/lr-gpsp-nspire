# lr-gpsp-nspire

Standalone [libretro gpsp](https://github.com/libretro/gpsp) for TI-Nspire calculators (CX, CX CAS, CX II, etc.).

**Current tree: Alpha v0.4.1** — stable v0.4 base with gamepak LRU reset on ROM load and integer fixed-point sound setup under `NSPIRE_NO_AUDIO`.

The GUI and host code come from legacy gpSP; the emulation core is libretro-gpsp, without a libretro frontend.

## Build

Requires the [Ndless SDK](https://ndless.me/) (`nspire-gcc`, `nspire-g++`, `genzehn`).

**Recommended (Docker):**

```bash
docker compose run --rm ndless-dev make
```

Tag your Ndless SDK image as `ndless-dev:latest` (e.g. from [stepney141/ndless-docker](https://github.com/stepney141/ndless-docker)), or set `NSPIRE_BUILD_IMAGE`.

**Host build:**

```bash
make
```

Output: `gpsp_libretro.tns` (install on the calculator like the legacy gpSP launcher).

See [README.txt](README.txt) and [DOCKER_BUILD.md](DOCKER_BUILD.md) for device file layout, RAM flags, and Windows/Docker notes.

## Device files

- `gba_bios.bin.tns`
- ROMs as `.gba.tns` (ZIP loading disabled)
- `gpsp.cfg.tns` (written by the GUI)

## Releases

Older releases (including v0.5) remain on the [Releases](https://github.com/andymcca/lr-gpsp-nspire/releases) page. This commit restores the tested v0.4.1 sources on `main`.
