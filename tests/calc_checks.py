#!/usr/bin/env python3
"""Checks on the calculator build that the PC tests can't make, read from
its disassembly. Needs the Ndless toolchain on PATH (arm-none-eabi-objdump).

    calc_checks.py [gpsp_libretro.elf]

* gui.c's menu() calls its options' functions through trampolines: code GCC
  writes onto the stack, which Ndless maps write-back cached. The data cache
  must be written back and the instruction cache emptied before the first
  of those calls, or the CPU can run stale bytes (nspire_sync_written_code).
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


def main():
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", ELF], capture_output=True, text=True,
                         check=True).stdout
    failed = 0
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
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
