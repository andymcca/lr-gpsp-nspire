Self-contained Nspire build (libretro-gpsp core + legacy GUI)
==============================================================

  Most sources live under this directory. The Makefile may pull libretro-common
  and arm/ dynarec files from ../libretro-gpsp when the vendored copy is
  incomplete. Host files are copied from nspire-libretro; includes use
  -Ilibretro-gpsp -Inspire -I.

Output
------
  gpsp_libretro.tns   — install on the calculator like the legacy gpSP launcher.

  Prerequisites
-------------
  The usual way to build is inside the Ndless SDK Docker image (see Build below),
  which already provides nspire-gcc, nspire-g++, and genzehn on PATH.

  Optional: install the Ndless SDK on the host instead and run make locally.

Build
-----
  Recommended: Ndless SDK in Docker (docker-compose.yml in this directory).

  The compose file mounts the parent folder (gpsp-master) as /work and sets the
  working directory to this project (/work/nspire-libretro-standalone). Tag your
  Ndless SDK image as ndless-dev:latest (for example after building from
  stepney141/ndless-docker), or set NSPIRE_BUILD_IMAGE to another tag.

    cd nspire-libretro-standalone
    docker compose run --rm ndless-dev make

  On Windows Docker Desktop the bind mount resolves the host path to the parent
  of this folder automatically (the .. volume in compose).

  Manual equivalent without compose:

    docker run --rm -v "/path/to/gpsp-master:/work" -w /work/nspire-libretro-standalone ndless-dev:latest make

  Both produce gpsp_libretro.elf then gpsp_libretro.tns via genzehn.

  Optional (inspect ARM926EJ-S assembly for video.cc):

    docker compose run --rm ndless-dev make video-cc-asm

  Writes video.cc.asm in this directory (no LTO so the listing is readable).

  Alternative — host with Ndless SDK on PATH:

    cd nspire-libretro-standalone
    make

Ndless metadata (genzehn)
-------------------------
  ZEHNFLAGS in the Makefile set:
    --ndless-min 31 --ndless-rev-min 2001
  Adjust if you target a different Ndless version.

Files on device
---------------
  Same conventions as the legacy port:
    gba_bios.bin.tns
    ROMs as .gba.tns (ZIP loading disabled in this tree)
    gpsp.cfg.tns (written by the GUI)
    game_config.txt.tns per-game (from libretro game-db path)

RAM tuning
----------
  Makefile defines -DSMALL_TRANSLATION_CACHE and -DROM_BUFFER_SIZE=2
  for lower RAM use on CX-class devices. Override ROM_BUFFER_SIZE if needed.

  -DNSPIRE_NO_AUDIO skips GBA audio synthesis (no hardware audio on Nspire).
  Remove it from BASE_FLAGS in the Makefile if you need to re-enable mixing
  for testing.

  Frameskip settings are stored in gpsp.cfg.tns (alongside other global
  options). Per-game *.cfg.tns still overrides them when present.

Notes
-----
  This target does not use libretro/libretro.c; it links a small Ndless host
  (nspire_host_main.c) that drives the same core entry points as RetroArch
  (execute_arm_translate, reset_gba, etc.).

  Core video is libretro-gpsp/video.cc (240-pitch GBA buffer). Before calling
  nspire/upscale_aspect.S, nspire_video_present.c copies each row into a 320-pitch
  scratch buffer (the asm scalers match legacy video.c layout). nspire_video_ui.c
  handles the menu framebuffer; nspire/video_blend.S is used for expand_blend.
