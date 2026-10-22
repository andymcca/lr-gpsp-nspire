#!/usr/bin/env python3
"""Checks on the calculator build that the PC tests can't make, read from
its disassembly. Needs the Ndless toolchain on PATH (arm-none-eabi-objdump).

    calc_checks.py [gpsp_libretro.elf]

* gui.c's menu() calls its options' functions through trampolines: code GCC
  writes onto the stack, which Ndless maps write-back cached. The data cache
  must be written back and the instruction cache emptied before the first
  of those calls, or the CPU can run stale bytes (nspire_sync_written_code).
* video_fill_u16_strip_arm (nspire/scanline_fill16.S) stores 16 bytes at a
  time with stmia, which needs a word address: the calculator checks
  alignment and crashed when video.cc filled from an odd pixel (Pokemon
  battle wipes). An odd start must be stored alone before the first stmia.
  tests/arm/fill_test.c runs it; this checks the build has that version.
* The code every scanline event and interrupt runs (common.h NSPIRE_HOT,
  .text.hot.gpsp: update_gba, the block lookups, arm_stub.S...) has to be
  one stretch smaller than the 16 KB instruction cache, with nothing else
  (main once landed in the middle of it).
* The dynarec's fast push/pop (arm_emit.h nspire_block_fast) finds IWRAM at
  a fixed distance below reg (NSPIRE_IWRAM_DATA, arm_stub.S's .bss); if the
  layout moves, the fast path turns itself off, which would cost speed
  without anything else showing it.
"""

import re
import subprocess
import sys

ELF = sys.argv[1] if len(sys.argv) > 1 else "gpsp_libretro.elf"


def function(dis, name):
    lines, on = [], False
    for line in dis.splitlines():
        if re.match(r"^[0-9a-f]+ <%s>:$" % re.escape(name), line):
            on = True
            continue
        if on:
            if not line.strip():
                break
            lines.append(line)
    return lines


def symbols():
    out = subprocess.run(["arm-none-eabi-nm", "-S", ELF], capture_output=True, text=True,
                         check=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3:
            try:
                syms[p[-1]] = (int(p[0], 16), int(p[1], 16) if len(p) == 4 else 0)
            except ValueError:
                pass
    return syms


HOT = ["update_gba", "block_lookup_address_arm", "block_lookup_address_thumb", "set_cpu_mode",
       "check_and_raise_interrupts", "nspire_flush_lines", "arm_update_gba_check_thumb",
       "execute_spsr_restore", "mem_dispatch_load_u32", "ext_store_iwram_u32"]
IWRAM_DATA = 0xA0000   # arm_emit.h NSPIRE_IWRAM_DATA


def check_layout():
    syms = symbols()
    failed = 0
    missing = [n for n in HOT + ["reg", "iwram", "main"] if n not in syms]
    if missing:
        print("FAIL: no %s in %s" % (", ".join(missing), ELF))
        return 1
    lo = min(syms[n][0] for n in HOT)
    hi = max(syms[n][0] + max(syms[n][1], 4) for n in HOT)
    main_at = syms["main"][0]
    if hi - lo > 16 * 1024 or lo <= main_at < hi:
        print("FAIL: the hot code spans 0x%x-0x%x (%d bytes)%s" %
              (lo, hi, hi - lo, ", main is inside it" if lo <= main_at < hi else ""))
        failed += 1
    else:
        print("hot code: one stretch of %.1f KB (0x%x-0x%x)" % ((hi - lo) / 1024.0, lo, hi))
    gap = syms["reg"][0] - (syms["iwram"][0] + 0x8000)
    if gap != IWRAM_DATA:
        print("FAIL: IWRAM's data is 0x%x below reg, the fast push/pop expects 0x%x" % (gap, IWRAM_DATA))
        failed += 1
    else:
        print("IWRAM's data is 0x%x below reg, as the fast push/pop expects" % gap)
    return failed


def main():
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", ELF], capture_output=True, text=True,
                         check=True).stdout
    failed = check_layout()
    menu = function(dis, "menu")
    if not menu:
        print("FAIL: no menu() in %s" % ELF)
        return 1
    first_indirect = next((i for i, l in enumerate(menu) if re.search(r"\sblx\s+r\d+", l)), None)
    clean = next((i for i, l in enumerate(menu) if "<clean_dcache>" in l or "cr7, cr10, {3}" in l), None)
    invalidate = next((i for i, l in enumerate(menu) if re.search(r"mcr\s+15, 0, r\d+, cr7, cr5, \{0\}", l)),
                      None)
    if first_indirect is None:
        print("note: menu() makes no indirect calls")
    elif clean is None or invalidate is None or not clean < invalidate < first_indirect:
        print("FAIL: menu() doesn't write back the data cache and empty the instruction cache before "
              "its first call through a trampoline (clean %s, invalidate %s, first call %s)"
              % (clean, invalidate, first_indirect))
        failed += 1
    else:
        print("menu(): caches synced before its first trampoline call")
    fill = function(dis, "video_fill_u16_strip_arm")
    stm = next((i for i, l in enumerate(fill) if re.search(r"\sstm(ia)?\s+r0!", l)), None)
    align = next((i for i, l in enumerate(fill) if re.search(r"\stst\s+r0, #2\b", l)), None)
    if not fill:
        print("FAIL: no video_fill_u16_strip_arm in %s" % ELF)
        failed += 1
    elif stm is None:
        print("note: video_fill_u16_strip_arm has no stmia")
    elif align is None or align > stm:
        print("FAIL: video_fill_u16_strip_arm stores with stmia without aligning an odd start first")
        failed += 1
    else:
        print("video_fill_u16_strip_arm: an odd start is stored alone before the first stmia")
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
