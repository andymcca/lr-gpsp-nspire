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

To the calculator: `nspire-link put -f gpsp_libretro.tns /gba/_gpSP.tns`,
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
| `nspire_rom_manager.c`, `nspire_rom_convert.c` | the ROM manager: zipping and unzipping ROMs |
| `nspire_paths.c` | the saves folder, game order file names, moving older files in |
| `nspire_selfstore.c` | the settings kept inside the program file |
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
* **Saves in My Documents/zgpSP_saves** (`nspire_paths.c`), as PocketSNES
  has zPocketSNES_saves: in-game saves, states, a game's settings, game
  orders (`gpsp_order_<folder>.txt.tns`), logs, the clock marker, the last
  folder. Not in the ROM folder, so `_gpSP.tns` stays first there (a folder
  is listed above every file). Made at every start, from My Documents
  (`get_documents_dir`), wherever gpSP is: no gba folder needed (host
  scenario no_gba_folder runs gpSP right in My Documents), so a new
  calculator gets it. The user asked for it ("saves and states in a new
  folder ... even on a new calc"; "others may not have a gba folder").
  The z: the OS lists folders A to Z ignoring case (gbc, ndless,
  PocketSNES_saves, snes on the user's calculator) above files, and the user
  wanted the folder "at the bottom". `_PocketSNES` listing first in /snes
  means '_' sorts before letters, so the OS either lowercases (then '~'
  would sort last) or puts all symbols first (then '~' would be at the
  top); a leading z is at the bottom either way. Until 2026-10-11 the
  folder was gpSP_saves: `nspire_move_old_documents_files` moves its files
  in at start and removes it once it's empty, and takes gpSP's settings
  files from My Documents itself (older builds wrote `..cfg.tns` in the
  folder their list had open; it sorted below every folder). Older files move in at start (gpSP's folder) and when a game
  starts (its folder): the GBA games' saves (names ending .gba, .agb or .bin
  before the suffix, so `Game.gbc.sav.tns` stays), `gpsp_order.txt.tns`, and
  from gpSP's folder its own files, the BIOS and every settings file of
  gpSP's (gpsp*.cfg.tns, ..cfg.tns, Game.gba.cfg.tns, Game.cfg.tns when a
  Game ROM is there; not ndless.cfg.tns or other programs'). The user: "only
  games and the emulator in there, like pocketsnes". A file already in the
  saves folder wins; the old one stays. Settings found anywhere but the
  program file are written into it at start.
* **Settings inside the program file** (`nspire_selfstore.c`), as PocketSNES:
  a 4 KB area + 16-byte footer ("GPSPSETS") after the Zehn program, written
  in place; a copy in the saves folder. Read order: program, copy, this
  build's file next to gpSP, older builds' gpsp.cfg.tns. Ndless's Zehn
  loader reads only the header's `file_size` bytes (`zehn_loader.cpp`), so
  the area is never loaded; PocketSNES (a make-prg wrapper) does the same on
  the calculator.
* **ROM manager** (main menu line 9, `nspire_rom_manager.c`,
  `nspire_rom_convert.c`), the user's request: C zips (zlib level 6, one
  deflated file named as the game's saves, `Game.gba`), U unzips to `.gba`;
  .gbz goes either way. Written as `<new>.tmp`, read back (the zip unpacks
  to the CRC, a .gba has it), renamed, then the old file is deleted. The
  running game can be converted when its whole ROM is in memory
  (`nspire_rom_file_release` closes its file); gamepak_filename follows.
  Line 9 so no older line moves.
* **The program is `_gpSP.tns`** on the calculator (it was `gpsp_qol.tns`):
  the underscore sorts it above the games, so the folder opens on it, as
  `_PocketSNES.tns` does in PocketSNES. File types in `ndless.cfg.tns` that
  name a gpSP like this one (`gpsp_` or `_gpsp` names) are pointed at the
  name it runs as (`register_rom_types`), as PocketSNES does with its old
  names; `gpspnspire_gpsp_launcher` (another port) and other programs keep
  theirs. The build's own file stays `gpsp_libretro.tns`, upstream's name.


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
* The PC build can't play commercial games: its interpreter goes wrong
  within the first second (Advance Wars jumps to 0x910803DE at frame 15 and
  gpsp-host crashes), with the .gba as much as the .gbz. Check real ROMs'
  .gbz files page by page with `nspire_gbz.c` instead (all pages equal the
  ROM), and play-test on the calculator.
* `make | head` (or any pipe that closes early) stops make with SIGPIPE and
  leaves the old `.tns`: check the file's time and md5 after building.
* An old gpSP build wrote `..cfg.tns` (a game's settings with no game open)
  into its folder; by character order it comes before `_gpSP.tns`. This
  build never writes it (`save_game_config_file` does nothing here); delete
  one if it's there.


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
* The saves folder and moving the old files in, the settings in the
  program file, the ROM manager (how long zipping a 16 MB ROM takes).


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
* 2026-10-11, evening: saves folder My Documents/gpSP_saves (older files
  moved in), settings inside `_gpSP.tns`, ROM manager (zip/unzip ROMs on the
  calculator).
* 2026-10-11, later: renamed `_gpSP.tns` (first in its folder); file types
  follow the name. The user's five GBA games went onto the calculator as
  .zip (61 MB of ROMs in 32 MB; first as .gbz, then zips at the user's
  wish, like the SNES games), unpacked whole into the 32 MB ROM buffer.
* 2026-10-11, night: the saves folder renamed zgpSP_saves (below the game
  folders in My Documents; PocketSNES's zPocketSNES_saves the same way);
  gpSP_saves and My Documents' `..cfg.tns` move in at start. Confirmed on
  the calculator: the first start moved everything.
* 2026-10-11, night: the OS's file list is a cached copy that never
  re-reads a folder after a program moves files (the owner still saw old
  saves in /gba and gpSP_saves after the move). `nspire_restore` calls
  Ndless's `refresh_osscr()` (upstream had it commented out); uploaded as
  /gba/_gpSP.tns (465,292 B with the settings area, md5 dec49a0e…), not
  yet seen on the calculator. A GBA zip opened from My Documents starts
  PocketSNES (it owns .zip); PocketSNES now says to start it in gpSP
  instead of running it.


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
