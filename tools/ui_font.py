#!/usr/bin/env python3
"""Makes nspire_ui_font.h: the menu fonts as pre-drawn glyphs.

    python3 -I tools/ui_font.py Inter.ttf > nspire_ui_font.h

Inter (https://rsms.me/inter, SIL Open Font License 1.1, docs/fonts/) is a
variable font; each menu font is one size, weight and optical size of it.
Glyphs are drawn unhinted (8x8 supersampled), with 16 levels of coverage
(4 bits a pixel), at PHASES positions between pixels so text can be spaced
by the font's own fractional widths. Kerning pairs are kept when they move
a glyph by at least 1/4 pixel.

Characters: ASCII 32-126, plus a few drawn from control codes (see
nspire_ui.h): ellipsis, middle dot, single angle quotes, arrows.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

PHASES = 2
SS = 8
GAMMA = 0.85     # a little heavier than linear coverage: thin stems survive

# name, px size, optical size, weight, tracking (px)
FONTS = [
    ("title", 15, 18, 650, 0.0),
    ("body", 12, 14, 500, 0.0),
    ("bold", 12, 14, 620, 0.0),
    ("small", 11, 14, 500, 0.0),
    ("caps", 9, 14, 600, 0.2),
    ("num", 10, 14, 650, 0.0),
]

EXTRA = {1: "…", 2: "·", 3: "‹", 4: "›", 5: "←", 6: "↑", 7: "→", 8: "↓"}
CODES = list(EXTRA) + list(range(32, 127))


def char(code):
    return EXTRA.get(code, chr(code))


def load(path, size, opsz, wght):
    f = ImageFont.truetype(path, size)
    f.set_variation_by_axes([opsz, wght])
    return f


def glyph(big, f, ch, phase, ascent, descent):
    pad = 4
    w = int(f.getlength(ch)) + 2 * pad + 4
    h = ascent + descent + 2 * pad
    im = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(im).text((pad * SS + phase * SS // PHASES, (pad + ascent) * SS), ch, font=big, fill=255,
                            anchor="ls")
    im = im.resize((w, h), Image.BOX)
    im = im.point(lambda v: min(15, round((v / 255) ** GAMMA * 15)))
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, b""
    im = im.crop(bb)
    rows = []
    for y in range(im.height):
        line = [im.getpixel((x, y)) for x in range(im.width)]
        if len(line) & 1:
            line.append(0)
        rows += [line[i] | (line[i + 1] << 4) for i in range(0, len(line), 2)]
    return bb[0] - pad, bb[1] - pad - ascent, im.width, im.height, bytes(rows)


def main(path):
    out = ["/* Made by tools/ui_font.py from Inter (SIL Open Font License 1.1, see",
           " * docs/fonts/Inter-OFL.txt). Don't edit. */", "",
           "#define UI_FONT_PHASES %d" % PHASES, ""]
    total = 0
    for name, size, opsz, wght, track in FONTS:
        f = load(path, size, opsz, wght)
        big = load(path, size * SS, opsz, wght)
        ascent, descent = f.getmetrics()
        data = bytearray()
        glyphs, advances = [], []
        for code in range(0, 127):
            if code not in CODES:
                glyphs += [(0, 0, 0, 0, 0)] * PHASES
                advances.append(0)
                continue
            ch = char(code)
            advances.append(round((f.getlength(ch) + track) * 64))
            for ph in range(PHASES):
                x, y, w, h, bits = glyph(big, f, ch, ph, ascent, descent)
                glyphs.append((len(data), x, y, w, h))
                data += bits
        kern = []
        for a in CODES:
            for b in CODES:
                if a < 32 or b < 32:
                    continue
                k = f.getlength(char(a) + char(b)) - f.getlength(char(a)) - f.getlength(char(b))
                if abs(k) >= 0.25:
                    kern.append(((a << 8) | b, round(k * 64)))
        kern.sort()
        total += len(data) + len(glyphs) * 8 + len(kern) * 4
        out.append("static const u8 ui_font_%s_bits[%d] = {" % (name, len(data)))
        for i in range(0, len(data), 24):
            out.append("  " + ",".join("%d" % v for v in data[i:i + 24]) + ",")
        out.append("};")
        out.append("static const struct ui_glyph ui_font_%s_glyphs[%d] = {" % (name, len(glyphs)))
        for i in range(0, len(glyphs), 4):
            out.append("  " + ", ".join("{%d,%d,%d,%d,%d}" % g for g in glyphs[i:i + 4]) + ",")
        out.append("};")
        out.append("static const u16 ui_font_%s_advance[127] = {" % name)
        for i in range(0, 127, 16):
            out.append("  " + ",".join(str(a) for a in advances[i:i + 16]) + ",")
        out.append("};")
        out.append("static const struct ui_kern ui_font_%s_kern[%d] = {" % (name, max(1, len(kern))))
        for i in range(0, len(kern), 8):
            out.append("  " + ", ".join("{0x%04X,%d}" % k for k in kern[i:i + 8]) + ",")
        if not kern:
            out.append("  {0, 0}")
        out.append("};")
        out.append("const struct ui_font ui_font_%s = { ui_font_%s_glyphs, ui_font_%s_bits, "
                   "ui_font_%s_advance, ui_font_%s_kern, %d, %d, %d };"
                   % (name, name, name, name, name, len(kern), ascent, descent))
        out.append("")
    out.append("/* %d bytes */" % total)
    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1])
