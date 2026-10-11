#!/usr/bin/env python3
"""Shrinks GBA files for gpSP on the TI-Nspire.

  gbz.py pack GAME.gba [GAME2.gba ...]     ROM -> GAME.gbz.tns (compressed ROM)
  gbz.py unpack GAME.gbz.tns [OUT.gba]     compressed ROM -> plain ROM
  gbz.py info GAME.gbz.tns                 what's in a compressed ROM

ROMs can be .gba, .gba.tns, .agb, .bin, or a .zip holding one of those.
pack writes next to the input unless -o DIR is given. gpSP reads compressed
ROMs page by page (nspire_gbz.h has the format), and a game's saves and states
are shared between GAME.gba.tns and GAME.gbz.tns.
"""

import argparse
import os
import struct
import sys
import zipfile
import zlib

PAGE = 32768
MAX_ROM = 32 * 1024 * 1024
ROM_EXTS = (".gba", ".agb", ".bin")


def read_rom(path):
    """The ROM bytes and a base name for the output."""
    name = os.path.basename(path)
    if name.lower().endswith(".tns"):
        name = name[:-4]
    if name.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            members = [m for m in z.namelist() if m.lower().endswith(ROM_EXTS)]
            if not members:
                raise ValueError("no .gba file inside")
            data = z.read(members[0])
            name = os.path.basename(members[0])
    else:
        with open(path, "rb") as f:
            data = f.read()
    base = name
    for ext in ROM_EXTS:
        if base.lower().endswith(ext):
            base = base[: -len(ext)]
            break
    if not data:
        raise ValueError("empty file")
    if len(data) > MAX_ROM:
        raise ValueError("bigger than 32 MB, the most a GBA ROM can be")
    if data[:4] == b"GBZ1":
        raise ValueError("already compressed")
    return data, base


def pack(data, level=9):
    pages = (len(data) + PAGE - 1) // PAGE
    header_size = 32 + 4 * (pages + 1)
    blobs = []
    for i in range(pages):
        page = data[i * PAGE:(i + 1) * PAGE]
        page = page + bytes(PAGE - len(page))
        packed = zlib.compress(page, level)
        blobs.append(packed if len(packed) < PAGE else page)
    offsets = [header_size]
    for b in blobs:
        offsets.append(offsets[-1] + len(b))
    header = b"GBZ1" + struct.pack("<7I", header_size, len(data), PAGE, pages,
                                   zlib.crc32(data) & 0xFFFFFFFF, 0, 0)
    header += struct.pack("<%dI" % (pages + 1), *offsets)
    return header + b"".join(blobs)


def unpack(blob):
    if blob[:4] != b"GBZ1":
        raise ValueError("not a compressed ROM (no GBZ1 at the start)")
    header_size, rom_size, page_size, pages, crc, _flags, _res = struct.unpack_from("<7I", blob, 4)
    if page_size != PAGE or pages != (rom_size + PAGE - 1) // PAGE or header_size != 32 + 4 * (pages + 1):
        raise ValueError("damaged header")
    offsets = struct.unpack_from("<%dI" % (pages + 1), blob, 32)
    out = bytearray()
    for i in range(pages):
        chunk = blob[offsets[i]:offsets[i + 1]]
        page = chunk if len(chunk) == PAGE else zlib.decompress(chunk)
        if len(page) != PAGE:
            raise ValueError("page %d is damaged" % i)
        out += page
    out = bytes(out[:rom_size])
    if zlib.crc32(out) & 0xFFFFFFFF != crc:
        raise ValueError("CRC mismatch: the file is damaged")
    return out


def human(n):
    return "%.1f MB" % (n / 1048576) if n >= 1048576 else "%d KB" % (n // 1024)


def cmd_pack(args):
    total_in = total_out = 0
    failed = 0
    for path in args.files:
        try:
            data, base = read_rom(path)
            blob = pack(data, args.level)
            if unpack(blob) != data:
                raise ValueError("internal check failed")
        except (OSError, ValueError, zipfile.BadZipFile, zlib.error) as e:
            print("%s: %s" % (path, e), file=sys.stderr)
            failed += 1
            continue
        out_dir = args.output_dir or os.path.dirname(path) or "."
        out = os.path.join(out_dir, base + ".gbz.tns")
        with open(out, "wb") as f:
            f.write(blob)
        total_in += len(data)
        total_out += len(blob)
        print("%s: %s -> %s (%d%%)  %s" % (os.path.basename(path), human(len(data)), human(len(blob)),
                                          100 * len(blob) // len(data), out))
    if len(args.files) > 1 and total_in:
        print("total: %s -> %s, %s saved" % (human(total_in), human(total_out), human(total_in - total_out)))
    return 1 if failed else 0


def cmd_unpack(args):
    with open(args.file, "rb") as f:
        data = unpack(f.read())
    out = args.output
    if not out:
        base = os.path.basename(args.file)
        for ext in (".tns", ".gbz"):
            if base.lower().endswith(ext):
                base = base[: -len(ext)]
        out = os.path.join(os.path.dirname(args.file) or ".", base + ".gba")
    with open(out, "wb") as f:
        f.write(data)
    print("%s: %s" % (out, human(len(data))))
    return 0


def cmd_info(args):
    with open(args.file, "rb") as f:
        blob = f.read()
    data = unpack(blob)
    title = data[0xA0:0xAC].decode("ascii", "replace").rstrip("\0 ")
    code = data[0xAC:0xB0].decode("ascii", "replace")
    print("%s: %s %s, ROM %s, file %s (%d%%), CRC OK" % (args.file, title, code, human(len(data)),
                                                       human(len(blob)), 100 * len(blob) // len(data)))
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("pack", help="compress ROMs into .gbz.tns")
    a.add_argument("files", nargs="+")
    a.add_argument("-o", "--output-dir")
    a.add_argument("--level", type=int, default=9, choices=range(1, 10))
    a.set_defaults(func=cmd_pack)
    a = sub.add_parser("unpack", help="compressed ROM back to .gba")
    a.add_argument("file")
    a.add_argument("output", nargs="?")
    a.set_defaults(func=cmd_unpack)
    a = sub.add_parser("info", help="check a compressed ROM")
    a.add_argument("file")
    a.set_defaults(func=cmd_info)
    args = p.parse_args()
    try:
        return args.func(args)
    except (OSError, ValueError, zlib.error) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
