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
  A complete, built Ndless SDK with these tools on PATH:
    nspire-gcc, nspire-g++, nspire-tools, genzehn, make-prg
  The ARM toolchain executables arm-none-eabi-gcc and arm-none-eabi-g++ must be
  installed under the path returned by `nspire-tools _toolchainpath`. The SDK's
  zlib third-party dependency must be built/installed so include/zlib.h and
  lib/libz.a exist in the SDK root or $HOME/.ndless.

Build
-----
  Check prerequisites first:
    make check-sdk

  Then do a clean build:
    make clean
    make -j2

  The output is gpsp_libretro.tns. The target creates gpsp_libretro.elf, uses
  genzehn to create gpsp_libretro.tns.zehn, and calls make-prg to produce the
  final .tns file.

  Docker is also supported when you have an Ndless SDK image tagged
  ndless-dev:latest (or set NSPIRE_BUILD_IMAGE):
    docker compose run --rm ndless-dev make check-sdk
    docker compose run --rm ndless-dev sh -c 'make clean && make -j2'

Touchpad swipe controls
-----------------------
  Configure input includes Touchpad swipes, Swipe dead zone, and Swipe
  sensitivity. Inputs are based on movement from the start of a finger contact,
  not the location where the finger first touches. Swipe right triggers
  SW-RIGHT and swipe up triggers SW-UP. Diagonal swipes activate X and Y
  components separately and also expose SW-UPLEFT, SW-UPRIGHT, SW-DNLEFT, and
  SW-DNRIGHT. The X and Y components are normalized against the reported
  touchpad width and height, respecting the CX II's 4:3 ratio. The dead zone
  filters small movement and sensitivity scales it before checking the zone.
  You can map a swipe together with a physical calculator key, and reuse a
  swipe or physical key in multiple GBA action rows to activate several GBA
  buttons at once. Turn swipes off to restore the original edge-arrow controls.

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
