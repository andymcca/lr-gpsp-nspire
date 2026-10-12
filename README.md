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

Output: `gpsp_libretro.tns`. Copy it to the calculator as `_gpSP.tns`, in the
folder with your games: the underscore puts it at the top of the folder, so
opening the folder and pressing enter starts gpSP.

See [README.txt](README.txt) and [DOCKER_BUILD.md](DOCKER_BUILD.md) for device file layout, RAM flags, and Windows/Docker notes.

## Device files

    My Documents/
      gba/
        _gpSP.tns                    the program, first in the folder; the settings
                                     for all games are kept inside it
        Game.gba.tns                 ROMs, or zipped (Game.zip.tns), or .gbz.tns
      zgpSP_saves/                   made on the first start, wherever gpSP is (the z
                                     lists it below your game folders)
        gba_bios.bin.tns             the GBA BIOS (optional; one put next to gpSP moves here)
        Game.gba.sav.tns / .sav.gz.tns   in-game saves, plain or gzipped
        Game.gba0.svs.tns ...        save states, slots 0 to 999
        Game.gba.settings.tns        a game's own settings and keys
        gpsp_qol.cfg.tns             a copy of the settings for all games
        gpsp_order_gba.txt.tns       the gba folder's game order
        gpsp_romdir.tns, gpsp_results.txt.tns, gpsp_perf.txt.tns, gpsp_clock.tns

The game folder has only the program and the games. gpSP doesn't need a
`gba` folder: it can be in any folder, or right in My Documents, and the
saves folder is always My Documents/zgpSP_saves. Builds before the saves
folder kept these next to the ROMs and next to gpSP: the first start
moves them into `zgpSP_saves` (a game in another folder, when it starts),
with the BIOS and older gpSP builds' settings files (`gpsp.cfg.tns`,
`Game.gba.cfg.tns`, `Game.cfg.tns`, and `..cfg.tns` from My Documents too).
Other programs' files stay.

## What this build adds (TI-Nspire QoL, 2026-10)

The menus are gpSP's own; the new settings are extra lines in them.

**No more crashes at scene changes.** Since v0.4 games could reset or freeze
the calculator at Pokemon battle wipes, door transitions and some cutscenes
(issues [#3](https://github.com/andymcca/lr-gpsp-nspire/issues/3),
[#4](https://github.com/andymcca/lr-gpsp-nspire/issues/4),
[#5](https://github.com/andymcca/lr-gpsp-nspire/issues/5)). There were two
causes, both fixed: the *libretro video.cc* renderer's fast fill crashed when
a transition's window edge fell on an odd pixel, and *Partial SMC* (Emulator
options, *Dynarec RAM / SMC*) ran code that games keep in RAM wrongly, so
Pokemon Emerald froze right after loading a state. Both renderers, both
*Dynarec RAM / SMC* settings and every ROM buffer size work now.

**Screen.** On the CX II the picture is turned for the portrait LCD in software
and shown from one of three buffers, which the LCD switches between only
between refreshes: no diagonal tearing (that came from Ndless's compatibility
mode, which turned the picture inside the panel), and no tearing at all.
Frames are paced with a 32768 Hz timer at the GBA's 59.73 frames a second
instead of waiting for the LCD after every frame, so **automatic frameskip
works** now (it never skipped before): it draws every frame unless the game
falls behind.

*Display scaling* now does what it says. Older builds showed the sharp
320x214 picture (*scaled/unblended*) whatever it was set to, so settings from
them, and from the first QoL build, start with that one; *unscaled* is the
GBA's own 240x160 in the middle.

**Faster blended pictures.** *scaled/blended 3:2* and *fullscreen/blended*
(and *Interframe blending* with them) are drawn in one pass now
(`nspire_blend.c`): the GBA frame is read once, blended, and written straight
into the turned screen buffer. Before, each frame went through four passes
over memory (frame mixing, a copy into the old 5:5:5 format, notaz's scaler,
turning and converting back), about 10 ms of a 16.7 ms frame on the
calculator; the CPU waits about 120 cycles for every 32 bytes it reads from
memory, and its data cache is only 8 KB. The math is notaz's, in the LCD's
5:6:5: red and blue are exactly as before, green keeps its sixth bit. The 3:2
picture is one line higher (lines 12-224), so each column's pixels are whole
words. The FPS counter's and messages' text bands are only drawn again when
their text changes.

**Brightness (CX II).** **+** and **-** make the screen brighter or dimmer, in
games ("Brightness 7/10" shows at the bottom), menus and the file list.
*Screen brightness* in Graphics options sets it too: *calculator's* (the
default) leaves the calculator's own brightness alone, 1 to 10 are gpSP's.
When gpSP closes, the calculator's own brightness comes back. (It is the LCD
backlight at 0x90130014, which the OS sets from 0, brightest, to 225,
darkest.)

**File list.** Games are listed A to Z, and the first ten have a number:
press it (**1**-**9**, **0**) to start that game; with **shift** held it
starts without loading its state, like **shift+enter**. **tab** picks a game
up to move it: up/down move it, **enter** puts it there, **esc** puts it back.
The folder keeps that order (in `zgpSP_saves`); **shift+tab** sorts it
A to Z again. A folder opens on its first game, and gpSP opens in the folder
it was last in.

**Save states.** Slots 0 to 999, named as before (`Game.gba12.svs.tns`, in
`zgpSP_saves`), so older states still load. In *Cheats and Miscellaneous options*:

| Setting | Default | |
|---|---|---|
| Auto-increment slot on save | on | every save goes to a new slot after the highest, so nothing is overwritten |
| Save a state when leaving | on | Save+Exit, Exit gpSP and Load new game save first |
| Load newest state on start | on | starting a game resumes its newest state; **shift+enter** in the file list starts without it |

In the main menu the slot lines say what they do ("Save state to new slot
007", "Load state from slot: 005 (empty)"); hold left/right to move faster.

The newest state is the one this build saved last: its states count their
saves, which stays right when a calculator reset restarts its clock. Until a
game has one of this build's states (it has only older builds' or the first
QoL build's), the newest is the most recently written file.

**Smaller files.** With *Compress saves* on (Cheats and Miscellaneous
options, the default, as in PocketSNES), states are zlib-compressed (gpSP's
states were 416 KB each) and in-game saves gzipped: `Game.gba.sav.gz.tns`,
which a PC unpacks like any `.gz` (`gunzip`, 7-Zip). Off, new states are
stored as they are and in-game saves are plain `.sav` files, which other
gpSP builds and PC emulators read. Writing a save removes its other form;
loading takes the newer of the two. Older states still load.

**Save manager** (main menu): the game's in-game save and states, with their
sizes, compressed or not. **enter** marks files (**A**: all of them), **C**
compresses and **U** uncompresses the marked ones (or the one picked), and
**del** deletes them after asking (**del** again). A state keeps its place
among the newest when it's compressed or uncompressed. A deleted in-game
save comes back if the game saves again while it's open.

**ROM manager** (main menu): the games in the folder (the running game's,
or the file list's), their sizes, zipped or not. **enter** marks games
(**A**: all of them), **C** compresses the marked ones (or the one picked)
into `.zip.tns` files, as zip tools make them, and **U** uncompresses them
into `.gba.tns` files (a `.gbz` goes either way). The new file is read back
and checked before the old one is deleted, the game order follows, and
saves keep working (they go by the ROM's name). The game that's running can
be done too while its whole ROM is in memory (the ROM buffer setting).

**Zipped games.** A zip from the internet renamed to `Game.zip.tns` plays
like the ROM in it (the first `.gba` or `.agb` file, else the biggest); its
saves, states and settings are the ROM's (`Pokemon.gba` in the zip uses
`Pokemon.gba.sav.tns` and so on), so a game can be zipped without losing
them. A zip is unpacked into memory whole when the game starts, and its
CRC is checked; when the ROM buffer is smaller than the ROM, more memory is
taken for it if there is any. Opening a `.zip.tns` from the calculator's
documents isn't set up, as PocketSNES may have that file type.

**Loading screen.** Starting a game shows its name and a bar while its ROM
is read into memory (with a big ROM buffer, all of it: 16 MB takes a
while), then the state it resumes from. A game that can't be loaded says
why (no ROM in the zip, not enough memory...), and the file list comes back.

ROMs can also be compressed on a PC, into `.gbz` files that are read in
pieces like a big `.gba` (no need to unpack them whole):

    python3 tools/gbz.py pack "Pokemon - Emerald.gba"     # -> Pokemon - Emerald.gbz.tns
    python3 tools/gbz.py pack -o out/ *.gba               # many at once
    python3 tools/gbz.py unpack Game.gbz.tns              # back to Game.gba

A `.gbz.tns` is read 32 KB at a time like a big `.gba` (`nspire_gbz.h` has the
format) and shares the `.gba`'s saves, states and settings. Older gpSP builds
can't read compressed states or ROMs.

**Keys.** *Configure input* has two keys per action (left/right pick one,
enter sets it, del clears it), key combinations (hold a key and press another),
and new hotkeys: brightness up and down (+ and -), next and previous save
slot (**)** and **(**, as in PocketSNES; settings from before them get these
keys where they're free), and, unbound at first, load newest state, FPS
counter, restart game. Save, load and slot changes show a message on screen. The number pad's 7 9 1 3 are diagonals
unless bound to something. esc goes back in menus, and held arrows repeat.

**Per game.** *Settings for* (Emulator options) and *Keys for* (Configure input)
can be "this game only": the game keeps its own graphics, emulator and save
options or keys in `Game.gba.settings.tns` in `zgpSP_saves`.

**Settings.** The settings for all games are kept inside `_gpSP.tns` itself
(an area after the program, which Ndless doesn't load), so they go wherever
the program goes, with a copy in `zgpSP_saves/gpsp_qol.cfg.tns`: a new
`_gpSP.tns` copied over the old one takes them from the copy. The first
start of this build takes them from `gpsp_qol.cfg.tns` next to gpSP (builds
before), else from older builds' `gpsp.cfg.tns` (moved into `zgpSP_saves`,
only read), and puts them into the program file right away.

**Speed.** *Show FPS counter* (Graphics options): off, on ("58/60": frames drawn
a second / the most there can be; in fast forward the same, against the 12
it draws a second at normal speed, as in PocketSNES) or detailed (ms per frame running the game,
drawing, showing, idle; logged to `gpsp_perf.txt.tns`). *Run speed test* runs
the game from where it is for 20 seconds (normal play, no speed limit with every
frame drawn, no limit with nothing drawn) and writes `gpsp_results.txt.tns`.
*CPU speed (CX II)* in Emulator options: normal, or 408 to 504 MHz in 12 MHz
steps while a game runs (PocketSNES's clock switch and list). Try 432 first:
on the calculator this was made on, 456 glitched now and then and 480 froze
in PocketSNES. If a speed freezes the calculator, the next start goes back to
normal (marker file `gpsp_clock.tns`). The speed test's file also records the
clock register and the backlight value gpSP found.

**Other.** Opening a `.gba.tns` or `.gbz.tns` from the calculator's documents
starts gpSP. Ndless finds the program for a file type by its name, so gpSP
registers the name it was started as; file types that name an earlier gpSP
(`gpsp_qol`, or any name starting with `gpsp_` or `_gpsp`) are moved to it,
so renaming it keeps games opening, and another program's are left alone.

**Fixed since the first QoL build** (the one that glitched): the menu could run
stale code on the calculator. gpSP's menu calls its options' functions through
small pieces of code GCC writes on the stack (trampolines, for nested
functions), and Ndless maps the stack write-back cached; the menu now writes
the data cache back and empties the instruction cache before using them
(`nspire_sync_written_code`). Older builds got away with it by always opening
the menu at the same stack address. The menu also no longer clears the
screen buffer being shown, which made it flicker. Help texts fit the screen,
and the second keys in *Configure input* line up.

### PC tests

`tests/host/run.sh` builds and runs three sets of tests on a PC (needs gcc,
g++, zlib, python3 and [jsmolka's gba-tests](https://github.com/jsmolka/gba-tests)
in `~/nspire/build/testroms/gba/gba-tests`):

* `core_tests`: the core with its interpreter: compressed and zipped ROMs
  (stored, deflated, damaged, without a ROM, bigger than the ROM buffer),
  save states packed and stored, in-game saves plain and gzipped (which one
  loads, damaged ones), and what the save manager does to both.
* `frontend_tests`: frame turning, key bindings, the settings files and their
  older versions (the slot keys added where free), brightness, the file
  list's order.
* `host_tests.py`: whole scenarios on a simulated CX II (`gpsp-host`,
  `host_hw.c`): gpSP's real main loop, menus and file list against a
  simulated LCD (which notices writes to the buffer being shown, i.e.
  tearing), 32 kHz timer, keypad, touchpad and backlight, with a script of
  key presses in virtual time and the calculator's costs for emulating,
  drawing and turning frames. They check screenshots and a report: the
  picture's size, the menu highlight moving with short presses, brightness,
  states, number keys, moving and sorting games, folders, `.gbz` ROMs, auto
  frameskip, the FPS counter and fast forward, the speed test, the blended
  pictures with their text bands, the loading screen, zipped games and one
  that won't load, the slot keys, the save manager, and that every menu
  line's help fits. `python3 tests/host/host_tests.py menu` runs one.

`tests/calc_checks.py` (needs the Ndless toolchain) checks the calculator
build's disassembly for what a PC can't show: that `menu()` syncs the caches
before its first trampoline call, and that video.cc's fill stores an odd
first pixel alone before its 16-byte stores.

`tests/arm/run.sh` (needs the Ndless toolchain and qemu-arm) builds code for
the calculator's CPU and runs it under qemu-arm: the one-pass blended
pictures against the old path (notaz's ARM scalers, the 5:5:5 copy and
turning) on noise, gradients and extreme colors, with and without
interframe blending: same red and blue, green within 3 of 63, every pixel as
a plain per-pixel version of the math says, and nothing written outside the
picture. Also video.cc's backdrop fill at every start pixel and length (it
crashed on odd ones), and the core with its dynarec on a small test ROM
whose code in RAM calls, loops and is rewritten by DMA and by the CPU, with
both *Dynarec RAM / SMC* settings. With the game, its BIOS and a save state
in `$GBA_GAMES` (see `tests/arm/run.sh`), Pokemon Emerald is played from the
state into a wild battle with both renderers, both SMC settings, 8 and 32 MB
ROM buffers and as a zip: all must reach the same battle frame for frame.

## Releases

Older releases (including v0.5) remain on the [Releases](https://github.com/andymcca/lr-gpsp-nspire/releases) page. This commit restores the tested v0.4.1 sources on `main`.
