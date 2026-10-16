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
    tests/arm/run.sh             # ARM code under qemu-arm: blended pictures, the
                                 # scanline fill, the dynarec, an Emerald replay
    python3 -I tests/calc_checks.py   # the calculator build's disassembly

Every build that goes on the calculator passes all of these first, and the
simulator's screenshots of anything changed on screen get looked at
(`tests/host/build/host_tests/NAME/out/*.ppm`). `python3 -I
tests/host/host_tests.py NAME` runs one scenario.

`tests/arm` also runs gpSP's core as the calculator does (the ARM dynarec,
both renderers, the ROM page cache; built with the calculator's flags) on a
test ROM (`rom/ram_code.S`) and on a real game: `replay.c` plays a save
state with scripted keys and prints a hash of every 50th frame. The Emerald
replay needs the game, its BIOS and the owner's state in `$GBA_GAMES`
(default `~/nspire/build/testroms/gba/games`, see `tests/arm/run.sh`); it's
skipped without them. `build/replay ROM BIOS STATE MB POLICY RENDERER FRAMES
KEYS DIR` also writes those frames as pictures (`DIR/f*.ppm`) to look at.

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
| `libretro-gpsp/cpu_threaded.c` | the dynarec: translating blocks, linking their branches, Partial/Classic SMC |
| `nspire/scanline_fill16.S` | video.cc's backdrop fill in ARM assembly |
| `tests/arm/` | ARM code under qemu-arm: the blend and fill tests, the core on a test ROM (`dynarec_test.c`) and on real games (`replay.c`) |


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


The scene-change crash (upstream issues #3, #4, #5)
---------------------------------------------------

Since upstream's v0.4 games crashed or froze the calculator at scene
changes: Pokemon battle wipes, entering a house in Minish Cap, a Megaman
Zero cutscene (issue #3 is the owner's; v0.3 was fine; upstream's v0.4.1
removed RAM block reuse for it, but it went on crashing). The owner found it
stopped with the legacy renderer, Classic SMC and a 32 MB ROM buffer. Two
separate bugs, both reproduced with the core under qemu-arm on the owner's
own Emerald state (`tests/arm/replay.c`), both fixed 2026-10-11:

* **video.cc's backdrop fill** (`nspire/scanline_fill16.S`, added in v0.4 as
  an ARM9 speed-up; the v0.3 release binary doesn't have it) stored 16 bytes
  at a time with `stmia` from wherever it started. Wipes and transitions draw
  windows whose edges move every line, and an edge on an odd pixel started
  the fill 2 bytes off a word. The calculator checks alignment (gpSP turns
  the check back on at quit, `nspire_restore`), so the store aborted and the
  calculator reset or froze. The legacy renderer has its own fill. Fix: an
  odd start is stored alone first. `tests/arm/fill_test.c` tries every start
  and length (qemu stops at an unaligned `stmia` the same way), and
  `calc_checks.py` checks the build has the aligned version.
* **Partial SMC left branches between blocks of RAM code unlinked.** The
  emitter writes a placeholder (`B +0`) for every direct branch, and
  `translate_block` patches it once the target is translated; Partial
  patches only BIOS and ROM targets, as a direct link into RAM code could
  run stale code after a partial flush. The placeholder ran the next host
  instructions instead, so calls and loops in IWRAM code were skipped:
  Emerald froze right after loading a state and crashed within 100 frames of
  starting. Upstream's `docs/partial-flush-smc-fix.md` describes an emitter
  change for this that was never committed. Fix: those exits jump to a PC
  lookup (as BX does) placed after the block, so whatever translation the
  target has now runs. `tests/arm/dynarec_test.c` runs `rom/ram_code.S` (ARM
  and Thumb code in IWRAM and EWRAM that calls, loops back to before its
  block and jumps back, then is rewritten by DMA and by a CPU store) with
  both settings; before the fix Partial got 0 for every sum.
* The ROM buffer had nothing to do with it: with 8 MB the replay reads 29
  ROM pages during the walk and the battle, and zips are unpacked whole
  anyway. Nor did libretro gpSP's fix for host stack overflows during
  translation (70707ce): the replay peaks at 7 KB of Ndless's 128 KB stack.

With both fixed, all 12 settings the replay tries (both renderers, both SMC
settings, 8 and 32 MB, the .gba and the zip) reach the same battle frame for
frame; on the unfixed code only legacy + Classic got there.


The Advance Wars crash: a damaged ROM
-------------------------------------

The owner's Advance Wars crashed as it started, with every setting, under
qemu as well, and on the PC's interpreter (`gpsp-host` segfaulted at frame
15). It's the ROM: CRC32 418740D0 is no known dump (No-Intro: Rev 1
26FD0FC9, Rev 0 DBEF116C, Virtual Console BDE36F98; the other four games
match No-Intro exactly). Its table at 0x082E7CF8, which 11 places in the
game read, has wrong top bytes on its pointers (`df 03 08 91` where
0x080803DF belongs, `77 2e 08 00`...), so the game calls 0x910803DF a
quarter of a second in. A GBA would crash the same way; a good copy of the
game is the only fix. It was already like that on the calculator before
any of our transfers (same md5 as downloaded on 2026-10-11).

Found with the interpreter on the PC printing every instruction: build
`cpu.cc` and `main.c` with `-DTRACE_INSTRUCTIONS -DTRACE_REGISTERS`, run
the game, and keep the instructions before the first one outside memory
(`awk`'s ring buffer). The No-Intro list is libretro-database's
`metadat/no-intro/Nintendo - Game Boy Advance.dat`.

What gpSP does about such games now: **a jump where no code can run stops
the game with a message** instead of crashing the calculator (it reset).
The dynarec's lookup returns (u8*)~0 for regions with no code; the stubs
used to jump there. `block_lookup_address_*` now record the PC
(`nspire_bad_jump`, cpu.h) and return `arm_return_to_main`, so
`execute_arm_translate` returns; the interpreter (PC tests) checks the same
when the PC changes region. `game_stopped` (nspire_host_main.c) shows
"Stopped", the game's name and where it jumped, closes the game without
saving a state of it (its memory is in pieces; its in-game save is kept),
and the file list comes back as after a game that doesn't load. The speed
test just stops and puts the game back. Tests: `tests/arm/badjump_test.c`
(rom/bad_jump.S jumps there in Thumb and ARM state), the owner's damaged
ROM in the ARM tests (when it's in `$GBA_GAMES`), and the host scenario
damaged_rom (the screens, the file list after, no state saved).


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
  ROM). The ARM core under qemu (`tests/arm/replay.c`) does play them:
  Emerald, Fire Emblem, Golden Sun and Minish Cap ran 6000 frames from the
  start with every setting, frame for frame the same per renderer (video.cc
  blends a few things legacy draws solid, like Fire Emblem's place names
  fading in). (Advance Wars failed on both because the owner's ROM is
  damaged, above; the interpreter's segfault was its fetch past
  `memory_map_read`, which now stops instead.)
* The calculator checks alignment: an unaligned load or store crashes it
  (the video.cc crash above). qemu-arm only stops on unaligned `ldm`/`stm`,
  so casts of a pointer to a wider type need checking by hand.
* Semihosting (the qemu tests' newlib) splits the command line at spaces:
  `tests/arm/run.sh` links the games to names without them.
* A game without a save file starts with its save memory all 0xFF
  (`load_backup`); the qemu tests do the same (Advance Wars read zeros as
  a damaged save before).
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
* The scene-change crash fixes: a Pokemon Emerald wild encounter with
  video.cc, Partial SMC and a 16 MB ROM buffer, the settings that crashed
  (2026-10-11, build f2218af2 + settings).

PC only (not seen on the calculator yet):
* The one-pass blended pictures (uploaded 2026-10-10; estimated picture
  time ~3.6 ms a frame with interframe blending, from ~10.4).
* The loading screen, zipped games, Compress saves and gzipped in-game
  saves, the save manager, the `(` `)` slot keys, the fast forward counter.
* The saves folder and moving the old files in, the settings in the
  program file, the ROM manager (how long zipping a 16 MB ROM takes).
* A game jumping where no code can run stops with a message (the damaged
  Advance Wars): qemu-arm and the PC tests only.


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
* 2026-10-11, late: the scene-change crash (upstream issues #3-#5, see
  above): video.cc's fill and Partial SMC fixed. The core runs under
  qemu-arm now, on a test ROM and on real games from save states
  (`tests/arm`).
* 2026-10-11, late: the owner's Advance Wars turned out to be a damaged
  ROM; a game that jumps where no code can run now stops with a message
  instead of crashing the calculator.


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

Speed (2026-10-15)
------------------

Goal (the owner): fast forward at 2x, never below 120 game frames a
second, with the settings unchanged (scaled/blended 3:2, interframe
blending, frameskip off, video.cc, Partial SMC, 16 MB, 396 MHz).

**PC estimate**: `tests/calc_speed.sh [STATE] [--frames N] [--top N]
[--functions-of A-B] [--keys F]` runs `tests/arm/perf.c` (core, renderer,
picture output, calculator flags) under qemu-arm with an ARM926 model
(`tools/calc_speed/arm926.c`: pipeline, 16 KB I / 8 KB D cache, write
buffer, 120-cycle line fills and ~6 cycles per word written, as measured
in PocketSNES) and prints ms per frame for game / drawing / picture. It came
within 6% of PocketSNES's three calculator speed tests (scale 1.05). Needs
qemu-arm with plugins (`~/nspire/build/tools/qemu-plugins`, built as in
PocketSNES's TESTING.md). Not yet checked against gpSP on the calculator:
the speed test's new fast forward part is the check.

Done (estimates, owner's Emerald route state / grass-to-battle replay):
* Fast forward with frameskip off draws a picture only when the LCD has
  taken the last one (`nspire_flip_taken`; pictures drawn faster are never
  shown) and skips pictures while the game is behind its pace (at least one
  in 8 drawn). Game frames are never skipped. Speed test part 4 runs it.
  Seen on the calculator (2026-10-15): about 130 game frames a second in
  Emerald's overworld.
* Fast forward max speed (Graphics line 12, the owner's request): 1.1x to
  10.0x in tenths, then unlimited (default; `nspire_ff_max` is the menu's
  index, settings word 67 keeps tenths, 0 = unlimited, also per game).
  With a top speed frames wait for their time (`fast_forward_pace`); it's
  only a cap: a game that can't reach it runs as fast as it can with every
  picture drawn (the owner: "if not enough frames it just draws the maximum
  and doesnt try to reach that speed"). Unlimited leaves pictures out while
  the game is below 124 a second. In fast forward the counter shows game frames / 60
  (the owner: "still out of 60"), whatever the frameskip. The speed test's
  fast forward ignores the top speed. Host test fast_forward_max_speed.
* Dynarec: memory accesses `bl` a shared region-lookup stub
  (`mem_dispatch_*`) and block exits `bl` a shared cycle check
  (`arm_check_update_*`): translated code 488 -> 315 KB, game time
  5.8 -> 5.2 ms (route), 4.7 -> 4.3 (replay). The 16 KB instruction cache
  is what the game code waits on most.
* 3:2 blended picture: interframe blending in the same pass, frames drawn
  into two buffers in turn (`nspire_video_frame_start`) instead of copying
  the previous one: 4.5 -> 4.0 ms. Pixel-exact (`blend_test` against
  `tests/arm/blend_ref.c`, the old code).
* video.cc at -O2/-Os is slower (5.6/6.6 vs 4.9 ms): -O3 stays.

Still slow: the wild-encounter wipe (~0.5 s): an IRQ every scanline, so
the renderer, update_gba and the IRQ handler evict each other from the
instruction cache: ~14 ms of game time a frame there, so fast forward dips
to ~70 for that half second. Next ideas: hand-written ARM for the 3:2
picture (~9.8 instead of 13 instructions a pixel, GCC spills), rendering
lines into a cached buffer and turning them while hot (saves reading the
frame back), faster tile drawing in video.cc, block lookup cache.
