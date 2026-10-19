#!/usr/bin/env python3
"""gpSP on the simulated calculator (build/gpsp-host): whole scenarios, as a
player would go through them, checked from the screenshots and the report.

    host_tests.py [NAME...]     all tests, or the ones named

Each test gets a folder in build/host_tests/NAME: gba/ is the calculator's
folder (gpSP, ROMs, settings), out/ has the screenshots (.ppm), the report
and the script. Only the standard library is needed. The ROMs are the
pattern ROM (rom/pattern.s): a picture with no black pixel and a white bar
that moves a column every frame, with its frame count at 0x02000000."""

import os
import shutil
import zipfile
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "build", "gpsp-host")
PROGRAM = "_gpSP.tns"   # what gpsp-host runs as, in the game folder
WORK = os.path.join(HERE, "build", "host_tests")

# What the calculator's CPU spends, roughly (PocketSNES's measurements on
# the user's CX II): a frame's emulation, a drawn line, turning a whole
# 320x240 picture, a touchpad read.
REAL_COSTS = """cost frame 7
cost line 25
cost turn 3
cost touchpad 0.4
"""

WHITE = (248, 252, 248)   # the menus' active item, as the LCD shows it
BLACK = (0, 0, 0)

failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print("  FAIL " + what)
    return cond


# ---- Files ----

def pattern_rom(path):
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "rom", "make_test_rom.py"), path], check=True)


def words_file(path, words):
    with open(path, "wb") as f:
        f.write(struct.pack("<%dI" % len(words), *words))


def read_words(path):
    data = open(path, "rb").read()
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


KEYS = {"8": 103, "5": 87, "4": 40, "6": 36, "del": 90, "var": 82, "scratch": 91, "doc": 100,
        "shift": 121, "ctrl": 122, "menu": 102, "F": 65, "L": 49, "S": 23, "Q": 34, "+": 99, "-": 83}


def old_config(scale):
    """gpsp.cfg.tns as gpSP builds before this one wrote it (148 bytes)."""
    o = [0] * 37
    o[0] = scale
    for i, k in enumerate(["8", "5", "4", "6", "del", "var", "scratch", "doc", "shift", "ctrl",
                           "menu", "F", "L", "S", "Q"]):
        o[7 + i] = KEYS[k]
    o[23] = 0
    o[24] = 1           # automatic frameskip
    o[25] = 4
    o[26] = 0xFF000000
    return o


def qol1_config(scale, cpu):
    """gpsp_qol.cfg.tns as the first QoL build wrote it (QOL1)."""
    o = old_config(scale) + [0] * (72 - 37)
    o[37] = 0x314C4F51
    for i in range(15):
        o[38 + i] = o[7 + i]
    o[58] = o[59] = o[60] = 1
    o[61] = cpu
    return o


# ---- Screenshots ----

class Shot:
    def __init__(self, path):
        data = open(path, "rb").read()
        header, self.pixels = data[:15], data[15:]
        assert header == b"P6\n320 240\n255\n", header
        self.w, self.h = 320, 240

    def at(self, x, y):
        i = (y * self.w + x) * 3
        return tuple(self.pixels[i:i + 3])

    def box(self, y0=0, y1=240):
        """The smallest rectangle around everything that isn't black."""
        xs, ys = [], []
        for y in range(y0, y1):
            row = [x for x in range(self.w) if self.at(x, y) != BLACK]
            if row:
                xs += [row[0], row[-1]]
                ys.append(y)
        return (min(xs), min(ys), max(xs), max(ys)) if xs else None

    def white_rows(self, x0, x1, y0, y1):
        return [y for y in range(y0, y1) if any(self.at(x, y) == WHITE for x in range(x0, x1))]

    def active_menu_line(self):
        """The highlighted line of a menu (gpSP draws line n at y = 40 + 10 n)."""
        rows = self.white_rows(10, 316, 40, 205)
        lines = sorted(set((y - 40) // 10 for y in rows))
        return lines[0] if len(lines) == 1 else lines

    def active_list_row(self):
        """The highlighted row of the file list (row n at y = 10 + 10 n)."""
        rows = self.white_rows(5, 255, 10, 210)
        lines = sorted(set((y - 10) // 10 for y in rows))
        return lines[0] if len(lines) == 1 else lines

    def bar_x(self):
        """The test ROM's white bar, in screen columns (None if not seen)."""
        y = 120
        xs = [x for x in range(self.w) if min(self.at(x, y)) >= 240]
        return xs[0] if xs else None


# ---- Running gpSP ----

class Run:
    def __init__(self, name, roms=("Pattern",), files=None, reuse=None, in_documents=False):
        """in_documents: gpSP and the games right in My Documents (no gba
        folder)."""
        self.name = name
        self.in_documents = reuse.in_documents if reuse else in_documents
        self.dir = os.path.join(WORK, name)
        self.gba = self.dir if self.in_documents else os.path.join(self.dir, "gba")
        self.out = os.path.join(self.dir, "out")
        if reuse is None:
            shutil.rmtree(self.dir, ignore_errors=True)
            os.makedirs(self.gba)
            for rom in roms:
                pattern_rom(os.path.join(self.gba, rom + ".gba.tns"))
            # The program file, which keeps the settings for all games.
            with open(os.path.join(self.gba, PROGRAM), "wb") as f:
                f.write(bytes(range(256)) * 16)
        else:
            # The same calculator as an earlier run (program, settings, saves).
            shutil.rmtree(self.dir, ignore_errors=True)
            shutil.copytree(reuse.dir, self.dir, ignore=shutil.ignore_patterns("out"))
        os.makedirs(self.out, exist_ok=True)
        for fname, content in (files or {}).items():
            path = os.path.join(self.gba, fname)
            if isinstance(content, list):
                words_file(path, content)
            else:
                with open(path, "w") as f:
                    f.write(content)

    def run(self, script, rom=None):
        path = os.path.join(self.out, "script.txt")
        with open(path, "w") as f:
            f.write(script)
        args = ([HOST] + (["--documents", self.dir] if self.in_documents else []) +
                [path, self.out, self.gba] + ([rom] if rom else []))
        result = subprocess.run(args, capture_output=True, text=True, timeout=300)
        self.exit = result.returncode
        self.stderr = result.stderr
        self.report = {}
        self.notes = {}
        for line in open(os.path.join(self.out, "report.txt")):
            parts = line.split()
            if parts[0] in ("shot", "note"):
                # "... backlight=120 game=Name with spaces.gba.tns"
                before, game = line.rstrip("\n").split(" game=", 1)
                fields = dict(p.split("=", 1) for p in before.split()[2:])
                fields["game"] = game
                for key in ("frame", "shown", "backlight"):
                    fields[key] = int(fields[key])
                self.notes[parts[1]] = fields
            else:
                self.report[parts[0]] = " ".join(parts[1:])
        check(self.exit == 0, "%s: gpsp-host exit %d %s" % (self.name, self.exit, self.stderr.strip()))
        check(int(self.report.get("writes_to_shown", 1)) == 0,
              "%s: no writes to the buffer on the screen (tearing): %s" % (self.name,
                                                                          self.report.get("writes_to_shown")))
        for line in self.stderr.splitlines():
            check(False, "%s: %s" % (self.name, line))
        return self

    def shot(self, name):
        return Shot(os.path.join(self.out, name + ".ppm"))

    def file(self, name):
        return os.path.join(self.gba, name)

    def save(self, name):
        """A file in the saves folder (My Documents/zgpSP_saves)."""
        return os.path.join(self.dir, "zgpSP_saves", name)


# ---- The tests ----

def test_play():
    """List, game, picture size and speed, leaving with Q."""
    r = Run("play").run(REAL_COSTS + """
wait 1
shot list
press enter
wait 3
shot game
note a
wait 1
note b
shot game2
press Q
wait 1
quit
""")
    check(r.shot("list").active_list_row() == 0, "play: the first game is picked")
    box = r.shot("game").box()
    check(box == (0, 13, 319, 226), "play: the 320x214 picture, %s" % (box,))
    a, b = r.notes["a"], r.notes["b"]
    check(57 <= b["frame"] - a["frame"] <= 61, "play: about 60 frames a second, %d" % (b["frame"] - a["frame"]))
    check(r.shot("game").bar_x() != r.shot("game2").bar_x(), "play: the picture moves")
    check(a["game"] == "Pattern.gba.tns", "play: the game")
    check(os.path.exists(r.save("Pattern.gba0.svs.tns")), "play: Q saved a state")
    check(r.report["lcd_base_end"] == "os", "play: the OS's screen is back at the end")
    return r


def test_menu():
    """The menu opens, moves with short presses and with arrows held, and
    closes, with the calculator's costs."""
    r = Run("menu").run(REAL_COSTS + """
wait 1
press enter
wait 2
press menu
wait 0.5
shot m0
tap down
wait 0.2
shot m1
tap down
wait 0.2
shot m2
press up
wait 0.2
shot m3
hold down
wait 0.7
release down
wait 0.2
shot held
press esc
wait 0.5
note back
wait 1
note later
shot game
quit
""")
    lines = [r.shot(n).active_menu_line() for n in ("m0", "m1", "m2", "m3")]
    check(lines == [0, 1, 3, 1], "menu: the highlight moves 0 1 3 1: %s" % (lines,))
    # From Emulator options (line 1), one step at once and then 15 a second
    # after 0.4 s: Cheats (8), About (10) or Load new game (11).
    held = r.shot("held").active_menu_line()
    check(held in (8, 9, 10), "menu: a held arrow repeats: line %s" % (held,))
    check(r.notes["later"]["frame"] - r.notes["back"]["frame"] >= 55, "menu: the game carries on after it")
    check(r.shot("game").box() == (0, 13, 319, 226), "menu: the picture is back whole")


def test_menu_slow_game():
    """A game too slow for full speed: the menu still answers."""
    r = Run("menu_slow").run(REAL_COSTS + """
cost frame 30
wait 1
press enter
wait 2
press menu
wait 0.5
tap down
wait 0.3
shot m1
press esc
wait 1
note a
wait 1
note b
quit
""")
    check(r.shot("m1").active_menu_line() == 1, "slow game: the menu moved")
    fps = r.notes["b"]["frame"] - r.notes["a"]["frame"]
    check(28 <= fps <= 34, "slow game: runs as fast as it can, %d frames a second" % fps)


def test_old_settings():
    """Settings from older builds and from the first QoL build: the picture
    is the full 320x214 one even if they said unscaled."""
    for name, files in (("old_cfg", {"gpsp.cfg.tns": old_config(0)}),
                        ("qol1_cfg", {"gpsp_qol.cfg.tns": qol1_config(0, 1)})):
        r = Run(name, files=files).run("""
wait 1
press enter
wait 3
shot game
press Q
wait 1
quit
""")
        box = r.shot("game").box()
        check(box == (0, 13, 319, 226), "%s: the 320x214 picture, %s" % (name, box))
        words = read_words(r.save("gpsp_qol.cfg.tns"))
        check(len(words) == 72 and words[37] == 0x334C4F51, "%s: saved as QOL3" % name)
        check(words[0] == 3, "%s: scaling saved as scaled/unblended: %d" % (name, words[0]))
        if name == "qol1_cfg":
            check(words[61] == 3, "qol1_cfg: 432 MHz kept: speed %d" % words[61])
        if name == "old_cfg":
            check(not os.path.exists(r.file("gpsp.cfg.tns")) and os.path.getsize(r.save("gpsp.cfg.tns")) == 148,
                  "old_cfg: the old build's file moved into zgpSP_saves as it was")


def test_unscaled_chosen():
    """Unscaled chosen in this build: the GBA's own 240x160."""
    first = Run("unscaled").run("""
wait 1
press enter
wait 2
press menu
wait 0.3
press enter
wait 0.3
press left
press left
press left
wait 0.3
shot option
press esc
press esc
wait 2.5
shot game
press Q
wait 1
quit
""")
    box = first.shot("game").box()
    check(box == (40, 40, 279, 199), "unscaled: 240x160 in the middle, %s" % (box,))
    again = Run("unscaled2", reuse=first).run("""
wait 1
press enter
wait 3
shot game
quit
""")
    check(again.shot("game").box() == (40, 40, 279, 199), "unscaled: kept for the next start")


def test_brightness():
    """+ and -, the menu line, and the calculator's own brightness back."""
    r = Run("brightness").run("""
backlight 120
wait 1
note list
press enter
wait 2
note start
press +
wait 0.3
note up1
press +
press +
press +
press +
press +
press +
wait 0.3
note top
press -
wait 0.3
note down1
press menu
wait 0.3
press enter
wait 0.3
press down
press left
press left
wait 0.3
note menu_dimmer
press esc
press esc
wait 1
press Q
wait 1
quit
""")
    n = r.notes
    check(n["list"]["backlight"] == 120 and n["start"]["backlight"] == 120, "brightness: untouched at first")
    check(n["up1"]["backlight"] == 100, "brightness: + one level up from the nearest (120 -> 100): %d"
          % n["up1"]["backlight"])
    check(n["top"]["backlight"] == 0, "brightness: + stops at the brightest: %d" % n["top"]["backlight"])
    check(n["down1"]["backlight"] == 25, "brightness: - one level: %d" % n["down1"]["backlight"])
    check(n["menu_dimmer"]["backlight"] == 75, "brightness: the menu line, two down: %d"
          % n["menu_dimmer"]["backlight"])
    check(int(r.report["backlight_end"]) == 120, "brightness: the calculator's own back when gpSP closes")
    words = read_words(r.save("gpsp_qol.cfg.tns"))
    check(words[63] == 7, "brightness: level 7 saved: %d" % words[63])

    again = Run("brightness2", reuse=r).run("""
backlight 160
wait 1
note list
press enter
wait 1
press Q
wait 1
quit
""")
    check(again.notes["list"]["backlight"] == 75, "brightness: level 7 from the start next time: %d"
          % again.notes["list"]["backlight"])
    check(int(again.report["backlight_end"]) == 160, "brightness: and the calculator's own back at the end")

    keep = Run("brightness3", files={"gpsp_qol.cfg.tns": qol1_config(3, 0)}).run("""
backlight 200
wait 1
press enter
wait 1
press menu
wait 0.3
press esc
wait 0.5
press Q
wait 1
quit
""")
    check(int(keep.report["backlight_writes"]) == 0, "brightness: never touched unless asked")


def test_states():
    """Save states: new slots, loading, and resuming on start."""
    r = Run("states").run(REAL_COSTS + """
wait 1
press enter
wait 2
press S
wait 1
note saved0
press S
wait 2.5
note saved1
press L
wait 0.1
note loaded
press Q
wait 1
quit
""")
    check(os.path.exists(r.save("Pattern.gba0.svs.tns")) and os.path.exists(r.save("Pattern.gba1.svs.tns")),
          "states: S saved slots 0 and 1")
    check(os.path.exists(r.save("Pattern.gba2.svs.tns")), "states: Q saved slot 2")
    check(r.notes["loaded"]["frame"] < r.notes["saved1"]["frame"], "states: L went back to slot 1")
    size = os.path.getsize(r.save("Pattern.gba0.svs.tns"))
    check(size < 20000, "states: compressed (%d bytes)" % size)

    again = Run("states2", reuse=r).run("""
wait 1
press enter
wait 0.2
note resumed
quit
""")
    check(again.notes["resumed"]["frame"] > 100, "states: the next start resumes from the newest: frame %d"
          % again.notes["resumed"]["frame"])

    fresh = Run("states3", reuse=r).run("""
wait 1
hold shift
press enter
release shift
wait 0.2
note fresh
quit
""")
    check(fresh.notes["fresh"]["frame"] < 30, "states: shift+enter starts without the state: frame %d"
          % fresh.notes["fresh"]["frame"])


def gpsz_to_raw(path):
    """A state as older gpSP builds wrote it: the raw 416 KB, no header."""
    import zlib
    data = open(path, "rb").read()
    assert data[:4] == b"GPSZ"
    with open(path, "wb") as f:
        f.write(zlib.decompress(data[32:]))


def gpsz_unflag(path):
    """A state as the first QoL build wrote it: no flags in its header."""
    data = bytearray(open(path, "rb").read())
    data[20:24] = b"\0\0\0\0"
    open(path, "wb").write(data)


def test_file_types():
    """ROMs opened from the documents start gpSP by its name (_gpSP): its
    earlier name's file types (gpsp_qol, as on the user's calculator) are
    pointed at it, another program's are left alone, and the file isn't
    rewritten when nothing changes."""
    cfg_lines = ["ext.sfc=_PocketSNES", "ext.zip=_PocketSNES", "ext.gba=gpsp_qol",
                 "ext.gbz=gpsp_qol", "ext.gbc=gbc4nspire", ""]
    r = Run("file_types")
    ndless = os.path.join(r.dir, "ndless")
    os.makedirs(ndless)
    cfg = os.path.join(ndless, "ndless.cfg.tns")
    with open(cfg, "w") as f:
        f.write("\n".join(cfg_lines))
    r.run("wait 1\nquit\n")
    expected = [l.replace("gpsp_qol", "_gpSP") for l in cfg_lines]
    check(open(cfg).read().split("\n") == expected, "file types: gpsp_qol's now _gpSP's: %r" % open(cfg).read())
    check(r.report["registered"] == "gba=_gpSP,gbz=_gpSP", "file types: registered %s" % r.report["registered"])

    other = Run("file_types_other")
    os.makedirs(os.path.join(other.dir, "ndless"))
    cfg = os.path.join(other.dir, "ndless", "ndless.cfg.tns")
    text = "ext.gba=gpspnspire_gpsp_launcher\r\next.gbz=_gpSP\r\n"
    with open(cfg, "w", newline="") as f:
        f.write(text)
    os.utime(cfg, (1000000000, 1000000000))
    other.run("wait 1\nquit\n")
    check(open(cfg, newline="").read() == text, "file types: another program's kept")
    check(os.stat(cfg).st_mtime == 1000000000, "file types: not rewritten when nothing changes")

    none = Run("file_types_none").run("wait 1\nquit\n")
    check(not os.path.exists(os.path.join(none.dir, "ndless")), "file types: no ndless folder made")
    check(none.report["registered"] == "gba=_gpSP,gbz=_gpSP", "file types: registered without a file")



def test_new_calculator():
    """On a calculator that never had gpSP: the first start makes My
    Documents/zgpSP_saves, everything gpSP writes goes there or into the
    program file, and the game folder keeps only the program and games."""
    r = Run("new_calculator", roms=("Pattern", "Second"))
    program = r.file(PROGRAM)
    bare = os.path.getsize(program)
    r.run(REAL_COSTS + """wait 1
press down
press tab
wait 0.2
press up
press enter
wait 0.3
press enter
wait 1.5
press S
wait 0.5
press +
wait 0.3
press Q
wait 1
""")
    check(sorted(os.listdir(r.gba)) == sorted([PROGRAM, "Pattern.gba.tns", "Second.gba.tns"]),
          "new calculator: only the program and games in the game folder: %s" % sorted(os.listdir(r.gba)))
    check(sorted(os.listdir(r.dir)) == ["gba", "out", "zgpSP_saves"],
          "new calculator: My Documents has the saves folder: %s" % sorted(os.listdir(r.dir)))
    saves = sorted(os.listdir(r.save("")))
    for name in ("gpsp_qol.cfg.tns", "gpsp_romdir.tns", "gpsp_order_gba.txt.tns", "Second.gba0.svs.tns"):
        check(name in saves, "new calculator: %s in zgpSP_saves: %s" % (name, saves))
    check(os.path.getsize(program) == bare + 4096 + 16, "new calculator: the settings in the program file (%d -> %d)"
          % (bare, os.path.getsize(program)))
    stored = open(program, "rb").read()[bare:bare + 328]
    check(stored == open(r.save("gpsp_qol.cfg.tns"), "rb").read(),
          "new calculator: the program's settings are the copy's")
    level = read_words(r.save("gpsp_qol.cfg.tns"))[63]
    check(level != 0, "new calculator: brightness up saved (level %d)" % level)

    # The settings come from the program: without the copy, still there.
    os.remove(r.save("gpsp_qol.cfg.tns"))
    again = Run("new_calculator2", reuse=r).run("wait 1\nquit\n")
    # gpSP sets its brightness level at start: (10 - level) * 25 on the register.
    check(int(again.report["backlight_end"]) == (10 - level) * 25,
          "new calculator: settings read from the program file (backlight %s, level %d)"
          % (again.report["backlight_end"], level))


def test_no_gba_folder():
    """gpSP needs no gba folder: right in My Documents with the games next
    to it, the first start still makes My Documents/zgpSP_saves, and the
    saves and gpSP's files go there; My Documents keeps the program, the
    games and that folder."""
    r = Run("no_gba_folder", roms=("Pattern", "Second"), in_documents=True)
    bare = os.path.getsize(r.file(PROGRAM))
    r.run(REAL_COSTS + """wait 1
shot list
press 2
wait 1.5
note game
press S
wait 0.5
press Q
wait 1
""")
    check(r.notes["game"]["game"] == "Second.gba.tns", "no gba folder: 2 starts Second: %s" % r.notes["game"]["game"])
    left = sorted(n for n in os.listdir(r.dir) if n != "out")
    check(left == sorted([PROGRAM, "Pattern.gba.tns", "Second.gba.tns", "zgpSP_saves"]),
          "no gba folder: My Documents has the program, the games and zgpSP_saves: %s" % left)
    saves = sorted(os.listdir(r.save(""))) if os.path.isdir(r.save("")) else []
    for name in ("gpsp_qol.cfg.tns", "gpsp_romdir.tns", "Second.gba0.svs.tns"):
        check(name in saves, "no gba folder: %s in zgpSP_saves: %s" % (name, saves))
    check(os.path.getsize(r.file(PROGRAM)) == bare + 4096 + 16, "no gba folder: the settings in the program file")
    again = Run("no_gba_folder2", reuse=r).run("wait 1\npress 2\nwait 1.5\nnote game\nquit\n")
    check(again.notes["game"]["frame"] > 90, "no gba folder: Second resumed from its state (frame %d)"
          % again.notes["game"]["frame"])


def test_old_saves_folder():
    """The calculator as the build before left it (the user's): everything
    in My Documents/gpSP_saves, the settings in the program, an older
    build's ..cfg.tns in My Documents. A new _gpSP.tns sent over the old
    one (no settings inside it yet) moves gpSP_saves into zgpSP_saves and
    removes it, takes the settings from the copy there, and moves
    ..cfg.tns in; the order, the brightness and the newest state hold, and
    another program's file in My Documents stays."""
    first = Run("old_saves_folder_first", roms=("Alpha", "Bravo")).run(REAL_COSTS + """wait 1
press down
press tab
wait 0.2
press up
press enter
wait 0.3
press enter
wait 1.5
press S
wait 0.5
press +
press +
wait 0.3
press Q
wait 1
""")
    r = Run("old_saves_folder", reuse=first)
    with open(r.save("gpsp_romdir.tns"), "w") as f:   # the last folder: this calculator's gba
        f.write(r.gba)
    os.rename(r.save(""), os.path.join(r.dir, "gpSP_saves"))
    words_file(os.path.join(r.dir, "..cfg.tns"), [0] * 16)
    with open(os.path.join(r.dir, "notes.txt.tns"), "w") as f:
        f.write("another program's file")
    with open(r.file(PROGRAM), "wb") as f:   # the new build, as sent
        f.write(bytes(range(256)) * 16)
    level = read_words(os.path.join(r.dir, "gpSP_saves", "gpsp_qol.cfg.tns"))[63]
    r.run(REAL_COSTS + """wait 1
note list
press enter
wait 1.5
note game
press Q
wait 1
""")
    left = sorted(n for n in os.listdir(r.dir) if n != "out")
    check(left == ["gba", "notes.txt.tns", "zgpSP_saves"],
          "old saves folder: gpSP_saves and ..cfg.tns gone from My Documents: %s" % left)
    check(sorted(os.listdir(r.gba)) == sorted([PROGRAM, "Alpha.gba.tns", "Bravo.gba.tns"]),
          "old saves folder: the game folder has the program and the games: %s" % sorted(os.listdir(r.gba)))
    for name in ("gpsp_qol.cfg.tns", "gpsp_order_gba.txt.tns", "gpsp_romdir.tns", "Bravo.gba0.svs.tns",
                 "..cfg.tns"):
        check(os.path.exists(r.save(name)), "old saves folder: %s in zgpSP_saves" % name)
    stored = open(r.file(PROGRAM), "rb").read()
    check(len(stored) == 4096 + 4096 + 16 and stored[4096:4096 + 328] == open(r.save("gpsp_qol.cfg.tns"), "rb").read(),
          "old saves folder: the settings went into the new program file")
    # gpSP sets its brightness level at start: (10 - level) * 25 on the register.
    check(level != 0 and r.notes["list"]["backlight"] == (10 - level) * 25,
          "old saves folder: the brightness kept (level %d, backlight %d)" % (level, r.notes["list"]["backlight"]))
    check(r.notes["game"]["game"] == "Bravo.gba.tns", "old saves folder: the list's first game is Bravo: %s"
          % r.notes["game"]["game"])
    check(r.notes["game"]["frame"] > 90, "old saves folder: Bravo resumed from its state (frame %d)"
          % r.notes["game"]["frame"])


def test_old_layout():
    """A calculator with files from builds before the saves folder (as the
    user's): states, saves, settings (older builds' too), game order, last
    folder and BIOS next to the games and gpSP move into zgpSP_saves on the
    first start (a game in another folder when it starts), and nothing is
    lost: the newest state resumes, the settings and the order hold. The
    game folder keeps only the program and the games; another emulator's
    save stays."""
    first = Run("old_layout_saves", roms=("Alpha", "Bravo")).run(REAL_COSTS + """wait 1
press down
press tab
wait 0.2
press up
press enter
wait 0.3
press enter
wait 1.5
press S
wait 0.5
press +
press +
wait 0.3
press Q
wait 1
""")
    r = Run("old_layout", roms=("Alpha", "Bravo"))
    os.makedirs(os.path.join(r.gba, "More"))
    pattern_rom(os.path.join(r.gba, "More", "Inner.gba.tns"))
    # The first run's files, put where the old builds kept them.
    for name in os.listdir(first.save("")):
        if name.startswith("gpsp_order"):
            shutil.copy(first.save(name), r.file("gpsp_order.txt.tns"))
        elif name != "gpsp_romdir.tns":
            shutil.copy(first.save(name), r.file(name))
    shutil.copy(r.file("Bravo.gba0.svs.tns"), os.path.join(r.gba, "More", "Inner.gba4.svs.tns"))
    for name, text in (("gpsp.cfg.tns", "older build"), ("Game.gbc.sav.tns", "other emulator"),
                       ("Alpha.gba.cfg.tns", "older build's game file"), ("Bravo.cfg.tns", "older still"),
                       ("gba_bios.bin.tns", "not a BIOS: the built-in one stays in use")):
        with open(r.file(name), "w") as f:
            f.write(text)
    r.run(REAL_COSTS + """wait 1
shot list
note list
press enter
wait 1.5
note game
press Q
wait 1
""")
    left = sorted(os.listdir(r.gba))
    check(left == sorted([PROGRAM, "Alpha.gba.tns", "Bravo.gba.tns", "More", "Game.gbc.sav.tns"]),
          "old layout: only the program, games, folders and another emulator's save stay: %s" % left)
    for name in ("Bravo.gba0.svs.tns", "gpsp_qol.cfg.tns", "gpsp_order_gba.txt.tns", "gpsp.cfg.tns",
                 "Alpha.gba.cfg.tns", "Bravo.cfg.tns", "gba_bios.bin.tns"):
        check(os.path.exists(r.save(name)), "old layout: %s moved into zgpSP_saves" % name)
    stored = open(r.file(PROGRAM), "rb").read()
    check(len(stored) == 4096 + 4096 + 16 and stored[4096:4096 + 328] == open(r.save("gpsp_qol.cfg.tns"), "rb").read(),
          "old layout: the settings went into the program file on the first start")
    check(open(r.save("gpsp_order_gba.txt.tns")).read().split()[0] == "Bravo.gba.tns",
          "old layout: the game order kept (Bravo first)")
    check(r.notes["game"]["game"] == "Bravo.gba.tns", "old layout: the list's first game is Bravo: %s"
          % r.notes["game"]["game"])
    check(r.notes["game"]["frame"] > 90, "old layout: Bravo resumed from its state (frame %d)" % r.notes["game"]["frame"])
    check(r.report["backlight_start"] != r.report["backlight_end"] or r.notes["list"]["backlight"] != 120,
          "old layout: the brightness setting kept")
    check(os.path.exists(os.path.join(r.gba, "More", "Inner.gba4.svs.tns")),
          "old layout: a game in another folder keeps its files there until it starts")

    inner = Run("old_layout_inner", reuse=r).run(REAL_COSTS + "wait 1.5\nquit\n",
                                                 os.path.join("More", "Inner.gba.tns"))
    check(not os.path.exists(os.path.join(inner.gba, "More", "Inner.gba4.svs.tns")) and
          os.path.exists(inner.save("Inner.gba4.svs.tns")), "old layout: moved when its game starts")


def zip_member(path):
    with zipfile.ZipFile(path) as z:
        check(z.testzip() is None, "rom manager: %s's CRC" % os.path.basename(path))
        names = z.namelist()
        return names, z.read(names[0])


def test_rom_manager():
    """The ROM manager, from the menu without a game: A marks all, C zips
    them (as zip tools read them), U unzips them back to the same bytes;
    the game order follows the new names. Then on the game that's running:
    it keeps running, from its new file."""
    r = Run("rom_manager", roms=("Alpha", "Bravo"))
    originals = {n: open(r.file(n + ".gba.tns"), "rb").read() for n in ("Alpha", "Bravo")}
    packed = os.path.join(r.dir, "Charlie.gba.tns")
    pattern_rom(packed)
    originals["Charlie"] = open(packed, "rb").read()
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "..", "..", "tools", "gbz.py"), "pack", "-o", r.gba,
                    packed], check=True, capture_output=True)
    open_manager = "press menu\nwait 0.3\n" + "press down\n" * 7 + "press enter\nwait 0.5\n"
    r.run("""wait 1
press down
press tab
wait 0.2
press up
press enter
wait 0.3
""" + open_manager + """shot manager
press A
wait 0.2
press C
wait 1
shot compressed
press esc
wait 0.3
press esc
wait 0.5
shot list
quit
""")
    files = sorted(os.listdir(r.gba))
    check(files == sorted([PROGRAM, "Alpha.zip.tns", "Bravo.zip.tns", "Charlie.zip.tns"]),
          "rom manager: zips for all (the .gbz too): %s" % files)
    for name, data in originals.items():
        path = r.file(name + ".zip.tns")
        if os.path.exists(path):
            inner, content = zip_member(path)
            check(inner == [name + ".gba"] and content == data, "rom manager: %s.zip holds %s.gba as it was"
                  % (name, name))
    order = open(r.save("gpsp_order_gba.txt.tns")).read().split("\n")
    check(order[:2] == ["Bravo.zip.tns", "Alpha.zip.tns"], "rom manager: the order follows: %s" % order)
    check(r.shot("manager").box(20, 50) is not None and r.shot("compressed").box(20, 50) is not None,
          "rom manager: its list")

    back = Run("rom_manager_back", reuse=r).run("""wait 1
""" + open_manager + """press A
wait 0.2
press U
wait 1
shot uncompressed
quit
""")
    files = sorted(os.listdir(back.gba))
    check(files == sorted([PROGRAM, "Alpha.gba.tns", "Bravo.gba.tns", "Charlie.gba.tns"]),
          "rom manager: U made .gba files: %s" % files)
    for name, data in originals.items():
        path = back.file(name + ".gba.tns")
        check(os.path.exists(path) and open(path, "rb").read() == data, "rom manager: %s.gba as it was" % name)

    # The game that's running: zipped under it, it plays on.
    play = Run("rom_manager_running", reuse=back).run(REAL_COSTS + """wait 1
press enter
wait 1.5
""" + open_manager + """press C
wait 1
shot running
press esc
wait 0.3
press esc
wait 0.5
note a
wait 1
note b
press S
wait 0.5
quit
""")
    check(not os.path.exists(play.file("Bravo.gba.tns")) and os.path.exists(play.file("Bravo.zip.tns")),
          "rom manager: the running game zipped")
    check(play.notes["a"]["game"] == "Bravo.zip.tns", "rom manager: it runs from its zip now: %s"
          % play.notes["a"]["game"])
    check(play.notes["b"]["frame"] > play.notes["a"]["frame"] + 50, "rom manager: the running game goes on")
    check(os.path.exists(play.save("Bravo.gba0.svs.tns")), "rom manager: its states keep their name")


def test_states_from_older_builds():
    """Older builds' states and the first QoL build's (as on the user's
    calculator: Emerald's slot 6 from the QoL build, then slots 0 and 1 from
    the old build): the most recently written one resumes, until this build
    has saved one."""
    a = Run("older_states").run("""
wait 1
press enter
wait 1
press S
note s0
wait 1
press S
note s1
wait 1
press S
note s2
wait 0.5
quit
""")
    s0, s1, s2 = (a.notes[n]["frame"] for n in ("s0", "s1", "s2"))
    gpsz_to_raw(a.save("Pattern.gba0.svs.tns"))
    gpsz_to_raw(a.save("Pattern.gba1.svs.tns"))
    gpsz_unflag(a.save("Pattern.gba2.svs.tns"))
    check(os.path.getsize(a.save("Pattern.gba1.svs.tns")) == 425984, "older states: a raw 416 KB state")
    os.utime(a.save("Pattern.gba2.svs.tns"), (1000 + 10 * 60, 1000 + 10 * 60))   # the QoL build's, 16:10
    os.utime(a.save("Pattern.gba0.svs.tns"), (1000 + 20 * 60, 1000 + 20 * 60))   # the old build's, 16:20
    os.utime(a.save("Pattern.gba1.svs.tns"), (1000 + 23 * 60, 1000 + 23 * 60))   # and 16:23

    b = Run("older_states2", reuse=a).run("""
wait 1
press enter
wait 0.3
note resumed
press S
note s3
wait 0.5
quit
""")
    resumed = b.notes["resumed"]["frame"]
    check(s1 <= resumed < s2, "older states: slot 1, the last written, resumed: frame %d (slots %d %d %d)"
          % (resumed, s0, s1, s2))
    check(os.path.exists(b.save("Pattern.gba3.svs.tns")), "older states: the next save goes to slot 3")

    # The calculator was reset since: this build's state has the older date,
    # but its count still makes it the newest.
    os.utime(b.save("Pattern.gba3.svs.tns"), (500, 500))
    c = Run("older_states3", reuse=b).run("""
wait 1
press enter
wait 0.3
note resumed
quit
""")
    s3 = b.notes["s3"]["frame"]
    check(c.notes["resumed"]["frame"] >= s3, "older states: this build's slot 3 resumes, whatever the dates: "
          "frame %d (slot 3 at %d)" % (c.notes["resumed"]["frame"], s3))


GAMES = ["Advance Wars", "Fire Emblem", "Golden Sun", "Minish Cap", "Pokemon Emerald", "Kirby",
         "Metroid", "Mario Kart", "Castlevania", "Zelda", "Wario"]


def test_number_keys():
    """1-9 and 0 start the first ten games; the list doesn't move with them."""
    first = Run("numbers", roms=GAMES)
    order = sorted(GAMES, key=str.lower)
    for key, index in (("3", 2), ("0", 9), ("1", 0)):
        r = Run("numbers_" + key, reuse=first).run("""
wait 1
press %s
wait 0.5
note started
quit
""" % key)
        check(r.notes["started"]["game"] == order[index] + ".gba.tns",
              "numbers: %s starts %s: %s" % (key, order[index], r.notes["started"]["game"]))
    r = Run("numbers_list", reuse=first).run("""
wait 1
press down
press down
wait 0.3
shot list
quit
""")
    check(r.shot("list").active_list_row() == 2, "numbers: arrows still move in the list")


def test_move_and_sort():
    """tab moves a game, the order stays, shift+tab sorts A to Z."""
    games = ["Alpha", "Bravo", "Charlie", "Delta"]
    r = Run("move", roms=games).run("""
wait 1
press down
press down
press down
press tab
wait 0.2
shot moving
press up
press up
press up
press enter
wait 0.3
shot moved
quit
""")
    order = open(r.save("gpsp_order_gba.txt.tns")).read().split()
    check(order == ["Delta.gba.tns", "Alpha.gba.tns", "Bravo.gba.tns", "Charlie.gba.tns"],
          "move: Delta to the top: %s" % order)
    check(r.shot("moved").active_list_row() == 0, "move: the highlight stays on it")

    again = Run("move2", reuse=r).run("""
wait 1
press 1
wait 0.5
note started
quit
""")
    check(again.notes["started"]["game"] == "Delta.gba.tns", "move: the order is kept, 1 starts Delta")

    cancel = Run("move3", reuse=r).run("""
wait 1
press tab
press down
press down
press esc
wait 0.3
shot list
press 1
wait 0.5
note started
quit
""")
    check(cancel.notes["started"]["game"] == "Delta.gba.tns", "move: esc puts it back")
    check(open(cancel.save("gpsp_order_gba.txt.tns")).read().split()[0] == "Delta.gba.tns",
          "move: esc doesn't save")

    sort = Run("move4", reuse=r).run("""
wait 1
hold shift
press tab
release shift
wait 0.3
press 1
wait 0.5
note started
quit
""")
    check(not os.path.exists(sort.save("gpsp_order_gba.txt.tns")), "sort: the order is forgotten")
    check(sort.notes["started"]["game"] == "Alpha.gba.tns", "sort: A to Z again, 1 starts Alpha")


def test_speed_test():
    """The speed test runs from the menu and writes its results."""
    r = Run("speed").run(REAL_COSTS + """
wait 1
press enter
wait 1
press menu
wait 0.3
press enter
wait 0.3
press up
press up
wait 0.3
shot option
press enter
wait 23
shot results
press enter
wait 1
press esc
wait 1
note back
quit
""")
    check(r.shot("option").active_menu_line() == 13, "speed test: on its line")
    path = r.save("gpsp_results.txt.tns")
    check(os.path.exists(path), "speed test: gpsp_results.txt written")
    if os.path.exists(path):
        text = open(path).read()
        check("Normal play" in text and "nothing drawn" in text, "speed test: all three parts")


def test_hotkeys_in_menu_keys():
    """Configure input lists the brightness keys, and a key can be changed."""
    r = Run("keys").run("""
wait 1
press enter
wait 1
press menu
wait 0.3
press down
press down
press down
press down
press down
wait 0.2
press enter
wait 0.3
shot keys_top
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
press up
wait 0.3
shot brighter
press enter
wait 0.2
press 9
wait 0.3
shot changed
press esc
press esc
wait 0.5
press 9
wait 0.3
note brighter
press Q
wait 1
quit
""")
    check(r.notes["brighter"]["backlight"] < 120, "keys: 9 now makes it brighter: %d"
          % r.notes["brighter"]["backlight"])


def test_folders():
    """A game in a folder: its saves go there, and gpSP opens there next time."""
    r = Run("folders")
    os.makedirs(os.path.join(r.gba, "Games"))
    pattern_rom(os.path.join(r.gba, "Games", "Inner.gba.tns"))
    r.run("""
wait 1
press right
press down
wait 0.2
press enter
wait 0.5
shot inside
press enter
wait 1
note playing
press Q
wait 1
quit
""")
    check(r.notes["playing"]["game"] == "Inner.gba.tns", "folders: the game in the folder")
    check(os.path.exists(r.save("Inner.gba0.svs.tns")), "folders: its state is there")
    again = Run("folders2", reuse=r).run("""
wait 1
press 1
wait 0.5
note playing
quit
""")
    check(again.notes["playing"]["game"] == "Inner.gba.tns", "folders: opens in the last folder")


def test_gbz():
    """A compressed ROM (.gbz.tns) is listed, plays, and has the .gba's saves."""
    r = Run("gbz", roms=())
    rom = os.path.join(r.dir, "Packed.gba.tns")
    pattern_rom(rom)
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "..", "..", "tools", "gbz.py"), "pack", "-o", r.gba,
                    rom], check=True, capture_output=True)
    r.run("""
wait 1
press 1
wait 2
note a
wait 1
note b
press S
wait 0.5
quit
""")
    check(r.notes["a"]["game"] == "Packed.gbz.tns", "gbz: listed and started: %s" % r.notes["a"]["game"])
    check(55 <= r.notes["b"]["frame"] - r.notes["a"]["frame"] <= 61, "gbz: plays")
    check(os.path.exists(r.save("Packed.gba0.svs.tns")), "gbz: states named after the .gba")


def test_menu_load_and_exit():
    """Load new game from the menu (the first game is saved), then Exit gpSP."""
    r = Run("load_exit", roms=("Alpha", "Bravo")).run(REAL_COSTS + """
wait 1
press 1
wait 1.5
note alpha
press menu
wait 0.3
press up
press up
press up
press up
wait 0.2
shot load_line
press enter
wait 0.5
press 2
wait 1
note bravo
press menu
wait 0.3
press up
wait 0.2
shot exit_line
press enter
wait 1
quit
""")
    check(r.shot("load_line").active_menu_line() == 11, "load: on Load new game")
    check(r.notes["bravo"]["game"] == "Bravo.gba.tns", "load: the other game runs")
    check(os.path.exists(r.save("Alpha.gba0.svs.tns")), "load: the first game was saved on leaving")
    check(r.shot("exit_line").active_menu_line() == 15, "exit: on Exit gpSP")
    check("quit" not in r.notes, "exit: gpSP closed by itself")
    check(os.path.exists(r.save("Bravo.gba0.svs.tns")), "exit: the game was saved")
    check(r.report["lcd_base_end"] == "os", "exit: the OS's screen back")
    check(r.report["osscr_refreshes"] == "1" and r.report["osscr_refresh_lcd"] == "os",
          "exit: the OS's file list refreshed once, after its screen is back (%s %s)"
          % (r.report["osscr_refreshes"], r.report["osscr_refresh_lcd"]))


def test_frameskip():
    """A game too slow to draw every frame keeps its speed by skipping drawing."""
    r = Run("frameskip").run("""
cost frame 9
cost line 50
cost turn 3
wait 1
press enter
wait 2
note a
wait 2
note b
quit
""")
    a, b = r.notes["a"], r.notes["b"]
    game_fps = (b["frame"] - a["frame"]) / 2.0
    shown_fps = (b["shown"] - a["shown"]) / 2.0
    check(57 <= game_fps <= 61, "frameskip: the game keeps full speed: %.1f" % game_fps)
    check(20 <= shown_fps < 55, "frameskip: some frames aren't drawn: %.1f shown" % shown_fps)


def test_fps_counter_and_fast_forward():
    """The FPS counter shows over the picture; F runs the game faster."""
    r = Run("fps").run(REAL_COSTS + """
wait 1
press enter
wait 1
press menu
wait 0.3
press enter
wait 0.3
press down
press down
press down
press down
press down
press down
press right
wait 0.2
shot option
press esc
press esc
wait 2
shot counter
note a
press F
wait 1
note ff_a
shot ff
wait 1
note ff_b
press F
wait 1
quit
""")
    box = r.shot("counter").box(0, 12)
    check(box is not None and box[0] < 10, "fps: the counter is drawn at the top: %s" % (box,))
    ff = r.notes["ff_b"]["frame"] - r.notes["ff_a"]["frame"]
    check(ff > 90, "fast forward: %d frames a second" % ff)
    box = r.shot("ff").box(0, 12)
    check(box is not None and box[2] < 60, "fast forward: the counter as drawn/expected, %s" % (box,))


def test_blended():
    """The blended pictures (one pass, nspire_blend.c) with interframe
    blending: where they are, the FPS counter and messages over them (a text
    band is only drawn again when its text changes), fullscreen, and the
    bands after the menu."""
    def three_shots(name):
        return "\n".join("shot %s_%d\nwait 0.02" % (name, i) for i in range(3)) + "\n"

    r = Run("blended").run(REAL_COSTS + """
wait 1
press enter
wait 1
press menu
wait 0.3
press enter
wait 0.3
press left
press left
""" + "press down\n" * 6 + """press right
wait 0.2
shot graphics
press esc
wait 0.3
press down
press enter
wait 0.3
""" + "press down\n" * 5 + """press right
wait 0.2
shot emulator
press esc
press esc
wait 2
shot aspect
note a
wait 1
note b
press S
wait 0.5
""" + three_shots("saved") + """wait 2.5
""" + three_shots("after") + """press menu
wait 0.3
press enter
wait 0.3
press right
press esc
press esc
wait 2
shot full
press S
wait 0.5
shot full_saved
wait 2.5
""" + three_shots("full_after") + """press menu
wait 0.3
press enter
wait 0.3
press left
press esc
press esc
wait 1.5
""" + three_shots("back") + """press Q
wait 1
quit
""")
    box = r.shot("aspect").box(12, 240)
    check(box == (0, 12, 319, 224), "blended: the 320x213 picture at lines 12-224, %s" % (box,))
    top = r.shot("aspect").box(0, 12)
    check(top is not None and top[2] < 100, "blended: the FPS counter in the top band, %s" % (top,))
    frames = r.notes["b"]["frame"] - r.notes["a"]["frame"]
    check(57 <= frames <= 61, "blended: about 60 frames a second, %d" % frames)
    for i in range(3):
        check(r.shot("saved_%d" % i).box(228, 240) is not None, "blended: the message, frame %d" % i)
        check(r.shot("after_%d" % i).box(225, 240) is None, "blended: the message gone, frame %d" % i)
        check(r.shot("after_%d" % i).box(0, 12) is not None, "blended: the counter still there, frame %d" % i)
    full = r.shot("full")
    check(full.box() == (0, 0, 319, 239), "blended: fullscreen fills the screen, %s" % (full.box(),))
    check(full.at(319, 239) != BLACK and full.at(160, 120) != BLACK, "blended: fullscreen's picture")
    check(r.shot("full_saved").at(160, 235) == BLACK, "blended: fullscreen's message on its plate")
    for i in range(3):
        check(r.shot("full_after_%d" % i).at(160, 235) != BLACK,
              "blended: fullscreen's picture back under the message, frame %d" % i)
    for i in range(3):
        back = r.shot("back_%d" % i)
        check(back.box(12, 240) == (0, 12, 319, 224), "blended: 3:2 again after the menu, frame %d" % i)
        top = back.box(0, 12)
        check(top is not None and top[2] < 100,
              "blended: only the counter in the top band after the menu, frame %d: %s" % (i, top))


def zip_rom(path, inner="Pattern.gba"):
    """A zip with the pattern ROM in it (and a text file before it)."""
    tmp = path + ".rom"
    pattern_rom(tmp)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("readme.txt", "a zipped game\n")
        z.write(tmp, inner)
    os.remove(tmp)


def test_loading_screen():
    """Starting a big game shows its name and a bar while the ROM is read."""
    r = Run("loading", roms=())
    path = r.file("Big.gba.tns")
    pattern_rom(path)
    with open(path, "ab") as f:
        f.write(b"\xff" * (4 * 1024 * 1024 - os.path.getsize(path)))
    r.run(REAL_COSTS + """cost read 600
wait 1
press enter
wait 1.2
shot loading
wait 3
shot game
quit
""")
    loading = r.shot("loading")
    back = loading.at(5, 5)
    text = [x for y in range(80, 102) for x in range(320) if loading.at(x, y) != back]
    check(text and min(text) == 30, "loading: \"Loading\" and the game's name from x 30")
    left, right = loading.at(31, 113), loading.at(288, 113)
    check(left != right, "loading: the bar is part way, %s %s" % (left, right))
    check(r.shot("game").box() == (0, 13, 319, 226), "loading: then the game")


def test_zipped_game():
    """A zipped game plays, its states are named after the ROM in the zip,
    and a zip without a ROM says so and goes back to the list."""
    r = Run("zipped", roms=())
    zip_rom(r.file("Game.zip.tns"))
    with zipfile.ZipFile(r.file("Broken.zip.tns"), "w") as z:
        z.writestr("notes.txt", "no game here")
    r.run(REAL_COSTS + """wait 1
shot list
press down
press enter
wait 2
shot game
note a
wait 1
note b
press S
wait 1
quit
""")
    check(r.shot("game").box() == (0, 13, 319, 226), "zip: the picture")
    check(r.notes["b"]["frame"] > r.notes["a"]["frame"] + 50, "zip: the game runs")
    check(os.path.exists(r.save("Pattern.gba0.svs.tns")), "zip: the state is named after the ROM in it")

    bad = Run("zipped_broken", reuse=r).run("""wait 1
press enter
wait 1
shot error
press enter
wait 1
shot list
quit
""")
    error = bad.shot("error").box(120, 160)
    check(error is not None, "broken zip: it says why")
    check(bad.shot("list").active_list_row() == 0, "broken zip: then the file list")


def test_damaged_rom():
    """A game that jumps where no code can run (as an Advance Wars from a bad
    dump does, to 910803DE, as it starts) stops with a message instead of
    crashing; a key brings back the file list, and no state is saved of it."""
    r = Run("damaged", roms=("Pattern",))
    # ldr r0, [pc]; bx r0; .word 0x910803DF: a Thumb jump past the GBA's memory
    words_file(r.file("Damaged.gba.tns"), [0xE59F0000, 0xE12FFF10, 0x910803DF])
    r.run(REAL_COSTS + """wait 1
press enter
wait 1
shot stopped
press enter
wait 1
shot list
press down
press enter
wait 2
shot game
quit
""")
    stopped = r.shot("stopped")
    back = stopped.at(5, 5)

    def text(y0, y1):
        return [stopped.at(x, y) for y in range(y0, y1) for x in range(320) if stopped.at(x, y) != back]

    title_x = [x for y in range(78, 104) for x in range(320) if stopped.at(x, y) != back]
    check(title_x and min(title_x) == 30, "damaged ROM: \"Stopped\" and the game's name from x 30")
    why = text(108, 132)
    check(any(p[0] > 200 and p[2] < 60 for p in why), "damaged ROM: it says why, in the error color")
    check(text(148, 160), "damaged ROM: and to press a key")
    check(not text(132, 146) and not text(162, 240), "damaged ROM: nothing else (no loading bar)")
    check(r.shot("list").active_list_row() == 0, "damaged ROM: then the file list")
    check(r.shot("game").box() == (0, 13, 319, 226), "damaged ROM: and another game starts")
    states = [f for f in os.listdir(os.path.join(r.dir, "zgpSP_saves")) if f.startswith("Damaged") and ".svs" in f]
    check(not states, "damaged ROM: no state saved of it: %s" % states)


def test_slot_keys():
    """) and ( pick the next and previous slot: L loads the one picked."""
    r = Run("slot_keys").run(REAL_COSTS + """wait 1
press enter
wait 1
press S
note saved
wait 1
press )
wait 0.3
press L
wait 0.1
note empty
wait 0.5
press (
wait 0.3
press L
wait 0.1
note loaded
quit
""")
    saved, empty, loaded = (r.notes[n]["frame"] for n in ("saved", "empty", "loaded"))
    check(empty > saved + 30, "slot keys: ) picked the empty slot 1: %d after %d" % (empty, saved))
    check(abs(loaded - saved) < 15, "slot keys: ( went back to slot 0: %d, saved at %d" % (loaded, saved))


def test_save_manager():
    """The save manager lists the states; U, A and C, and del (asking
    first) work on the files."""
    start = """wait 1
press enter
wait 1
"""
    open_manager = """press menu
wait 0.3
""" + "press down\n" * 4 + """press enter
wait 0.5
"""
    r = Run("save_manager").run(REAL_COSTS + start + """press S
wait 0.5
press S
wait 0.5
""" + open_manager + """shot manager
press U
wait 0.5
shot uncompressed
quit
""")
    slot0, slot1 = r.save("Pattern.gba0.svs.tns"), r.save("Pattern.gba1.svs.tns")
    check(os.path.getsize(slot0) > 400000, "save manager: U uncompressed slot 0 (%d bytes)" % os.path.getsize(slot0))
    check(os.path.getsize(slot1) < 100000, "save manager: slot 1 still compressed")
    check(r.shot("manager").box(20, 40) is not None, "save manager: its list")

    r2 = Run("save_manager2", reuse=r).run(start + open_manager + """press A
wait 0.2
press C
wait 0.5
press del
wait 0.3
press esc
wait 0.3
shot kept
press del
wait 0.3
shot confirm
press del
wait 0.5
shot deleted
quit
""")
    check(r2.exit == 0 and os.path.getsize(r2.save("Pattern.gba1.svs.tns")) < 100000,
          "save manager: A then C compressed them all")
    check(not os.path.exists(r2.save("Pattern.gba0.svs.tns")), "save manager: del, del deleted slot 0")
    check(os.path.exists(r2.save("Pattern.gba1.svs.tns")), "save manager: only the one picked")


def test_every_menu_line():
    """Every line of every menu: its help text fits the 3 lines under the
    options (gui.c checks in the PC build and complains on stderr), and the
    highlight is on it."""
    lines = ["wait 1", "press enter", "wait 1.5", "press menu", "wait 0.3"]
    menus = (("graphics", 0, 11, [2, 3, 5, 6, 7, 8, 9, 10, 11, 13, 15]),
             ("emulator", 1, 9, [0, 2, 3, 4, 5, 6, 7, 9, 13]),
             ("input", 5, 40, None),
             ("cheats", 6, 16, list(range(10)) + [10, 11, 12, 13, 14, 15]))
    for name, index, count, _ in menus:
        lines += ["press down"] * index + ["press enter", "wait 0.3", "shot %s_00" % name]
        for i in range(1, count):
            lines += ["press down", "wait 0.15", "shot %s_%02d" % (name, i)]
        lines += ["press esc", "wait 0.3"]
    lines += ["press down"] * 8 + ["press enter", "wait 0.3", "shot about", "press esc", "wait 0.3"]
    for i in range(13):
        lines += ["shot main_%02d" % i, "press down", "wait 0.15"]
    lines.append("quit")
    r = Run("every_menu_line").run("\n".join(lines) + "\n")
    for name, index, count, expected in menus:
        if expected:
            seen = [r.shot("%s_%02d" % (name, i)).active_menu_line() for i in range(count)]
            check(seen == expected, "every menu line: %s highlights %s" % (name, seen))
    main = [r.shot("main_%02d" % i).active_menu_line() for i in range(13)]
    check(main == [0, 1, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 15], "every menu line: main highlights %s" % main)


TESTS = [test_play, test_menu, test_menu_slow_game, test_old_settings, test_unscaled_chosen,
         test_brightness, test_states, test_number_keys, test_move_and_sort, test_speed_test,
         test_hotkeys_in_menu_keys, test_folders, test_gbz, test_menu_load_and_exit, test_frameskip,
         test_fps_counter_and_fast_forward, test_blended, test_loading_screen, test_zipped_game, test_damaged_rom,
         test_slot_keys, test_save_manager, test_every_menu_line, test_states_from_older_builds,
         test_file_types, test_new_calculator, test_no_gba_folder, test_old_layout, test_old_saves_folder,
         test_rom_manager]


def main():
    wanted = sys.argv[1:]
    os.makedirs(WORK, exist_ok=True)
    for test in TESTS:
        name = test.__name__[5:]
        if wanted and name not in wanted:
            continue
        print(name)
        test()
    print("%d checks, %d failed" % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
