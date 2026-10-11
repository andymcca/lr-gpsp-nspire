gpSP for the TI-Nspire CX II: developer notes
=============================================

The working notes for the QoL work on this port (2026-10): where things
are, how to build, test and put a build on the calculator, the decisions
made and why, what has and hasn't been seen working on the calculator, and
what's next. [README.md](README.md) is the user guide and test overview.

"Seen" means it ran on the calculator; "PC only" means it passed the PC
tests but hasn't run on the calculator yet.


The setup
---------

**Hardware.** TI-Nspire CX II, OS 6.4.0.74, Ndless 4.5.5. ARM926EJ-S at
396 MHz (288 MHz while plugged into USB), 16 KB I-cache, **8 KB D-cache**,
read-allocate only (stores that miss don't fill it), and a read from memory
that misses costs about 120 CPU cycles per 32-byte line (measured in
PocketSNES, whose FINDINGS.md has the details). The LCD (PL111) reads a
240x320 portrait buffer, 5:6:5.

**Toolchain.** `. ~/nspire/build/env.sh` puts the Ndless toolchain (GCC 14)
on PATH; `make` builds `gpsp_libretro.tns` (-O3 -flto). qemu-arm 10.2.1 for
the ARM tests is unpacked in `~/nspire/build/tools/qemu`. `nspire-link`
(`~/nspire/build/tools`) talks to the calculator over USB: `ls`, `get`,
`put -f`.


Build, test, deploy
-------------------

    . ~/nspire/build/env.sh
    make -j                      # gpsp_libretro.tns
    tests/host/run.sh            # core_tests, frontend_tests, host_tests.py
    tests/arm/run.sh             # ARM code under qemu-arm (the blended pictures)
    python3 -I tests/calc_checks.py   # the calculator build's disassembly

Every build that goes on the calculator passes all of these first, and the
simulator's screenshots of anything changed on screen get looked at
(`tests/host/build/host_tests/NAME/out/*.ppm`). `python3 -I
tests/host/host_tests.py NAME` runs one scenario.

To the calculator: `nspire-link put -f gpsp_libretro.tns /gba/gpsp_qol.tns`,
then `get` it back and compare checksums. The calculator only answers over
USB at its home screen (not while a program runs), and other sessions may
be using `nspire-link`: check `pgrep -x nspire-link` first.


Where things are
----------------

| File | What |
|---|---|
| `nspire_host_main.c` | main, starting and leaving games, the game loop, hotkeys, the speed test |
| `nspire_frameskip.c` | frame pacing (32768 Hz timer), frameskip, the FPS counters, timing |
| `nspire_video_present.c` | a game frame to the LCD, with the text bands |
| `nspire_turn.c` | turning pictures for the portrait LCD (sharp scalings, rectangles) |
| `nspire_blend.c` | the blended scalings in one pass, with interframe blending |
| `nspire/nspire.c` | the screen buffers and flip, timer, caches |
| `nspire_video_ui.c` | the menus' drawing (a landscape 5:5:5 frame turned by flip_screen) |
| `nspire_loading.c` | the loading screen |
| `nspire_save_manager.c` | the save manager |
| `nspire_states.c` | save state slots, file names (`nspire_game_base`) |
| `nspire_backup.c` | in-game saves, plain or gzipped |
| `nspire_zip.c` | zipped ROMs; `nspire_gbz.c`: .gbz ROMs |
| `nspire_config.c` | the settings files (format at the top) |
| `nspire_keys.c`, `nspire_input.c` | key bindings, the keypad, hotkeys |
| `nspire_brightness.c`, `nspire_clock.c` | backlight, CPU speed |
| `gui.c` | gpSP's own menus and file list (kept; new settings are lines in them) |
| `libretro-gpsp/gba_memory.c` | the core's ROM loading: zip and progress hooks are here |


Decisions and why
-----------------

* **gpSP's own menus stay** (the user asked: "dont make a new menu"). New
  settings are lines in the existing submenus; the save manager is one line
  in the main menu that opens its own screen, as the speed test does.
* **Tear-free output**: frames are turned in software into one of three
  portrait buffers, never the one being shown, and the LCD switches buffers
  between refreshes. Ndless's compatibility mode (the panel turns the
  picture) is what made the diagonal tearing.
* **Blended pictures in one pass** (`nspire_blend.c`): the old path made
  four passes over memory (mixing, a copy into the old 5:5:5 format,
  notaz's scaler, turning and converting back), about 10 ms a frame; each
  pass reads a whole picture back from memory, which is slow here (8 KB
  data cache). The math is notaz's in 5:6:5: red and blue are exactly the
  same, green keeps its sixth bit. The 3:2 picture moved up one line so each
  column's 4 pixels are two aligned words.
* **Compress saves on by default**, as in PocketSNES. Off gives files other
  emulators and older gpSP builds read (plain `.sav`, stored states). The
  save manager converts old files either way. States keep their counter
  through it, so the newest one stays the newest.
* **Zips are unpacked whole** at load: a deflated file can only be read from
  its start, and gpSP pages ROMs in 32 KB pieces when the ROM buffer is
  smaller than the ROM. The buffer grows for a zip if memory allows; `.gbz`
  is the compressed format that pages. A zip's saves are named after the ROM
  inside, so zipping a game keeps its progress. `.zip` isn't registered as
  a file type with Ndless: PocketSNES registers it, and the first program to
  register a type keeps it.
* **Slot keys `)` next, `(` previous**, as in PocketSNES. Settings files
  from before them (QOL2, game files before version 3) get them only where
  no other action uses those keys; a QOL3 file keeps no key if one was
  cleared.
* **Fast forward's counter** is "drawn/expected" like the normal one, the
  expected count being what fast forward draws at normal speed (12, or 60
  with frameskip off), as in PocketSNES.
* **A game that fails to load** says why on the loading screen, then the
  file list comes back (from the menu: the menu, without a game). It used to
  quit gpSP.
* **The newest state** is the one with the highest save counter among this
  build's states, else the latest file time (states from older builds).


Gotchas
-------

* gui.c's menus are GCC nested functions called through trampolines on the
  stack, which Ndless maps write-back cached: `menu()` cleans the D-cache
  and invalidates the I-cache first (`nspire_sync_written_code`), and
  `tests/calc_checks.py` checks that in the disassembly. Without it the
  first QoL build's menu froze and glitched.
* Ndless's `os.h` undefines `st_mtime`: use `st.st_mtim.tv_sec`.
* `getcwd` is the OS's `NU_Current_Dir`, which returns 0 on success: check
  what it wrote, not what it returned.
* File times on the calculator are seconds since its last reset, so they
  don't order files across a reset (hence the state counter).
* Writing files by appending has garbled them on the calculator: results and
  logs are written whole each time.
* The main menu's option lines are what the host scenarios count down to:
  adding a line means updating `test_every_menu_line` and any script that
  presses down to a menu below it.
* `pgrep -f nspire-link` matches a shell loop that mentions it: use
  `pgrep -x`.


Status on hardware
------------------

Seen (on the calculator):
* Tear-free output, the menus, full-size picture, states and resuming,
  number keys in the file list, brightness (the user set level 9).
* 48 fps in Pokemon Emerald with scaled/blended 3:2 and interframe blending
  (before the one-pass blended pictures).

PC only (not seen on the calculator yet):
* The one-pass blended pictures (uploaded 2026-10-10; estimated picture
  time ~3.6 ms a frame with interframe blending, from ~10.4).
* The loading screen, zipped games, Compress saves and gzipped in-game
  saves, the save manager, the `(` `)` slot keys, the fast forward counter.


History
-------

* 2026-10-06: first QoL build (states, .gbz, keys, settings, CPU speed). It
  glitched on the calculator (menu trampolines, small picture).
* 2026-10-08: fixes for those, brightness, file list number keys and order,
  PC tests (`tests/host`).
* 2026-10-10: the blended pictures in one pass, text bands drawn only when
  they change; ARM tests under qemu (`tests/arm`).
* 2026-10-11: loading screen, zipped games, Compress saves (gzipped in-game
  saves, stored states), save manager, `(` `)` slot keys, fast forward
  counter as in PocketSNES, failed loads go back to the file list.


Open items and ideas
--------------------

* Measure the one-pass blended pictures on the calculator (detailed FPS
  counter: `out`), and the game and drawing times (`cpu`, `gfx`), which
  decide what to speed up next.
* Loading: a smaller ROM buffer starts big games faster (less to read) but
  reads pages while playing; reading the ROM as the game asks for it at
  first could give both.
* Turn the sharp pictures in strips right after their lines are drawn,
  while they're still in the data cache (~0.7-1 ms, uncertain).
* Profile-guided build (never tried here or in PocketSNES).
