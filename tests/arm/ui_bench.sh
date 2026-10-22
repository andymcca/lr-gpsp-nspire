#!/bin/bash
# What the menus' drawing costs on the calculator: runs tests/arm/ui_bench
# (see there) under qemu-arm with the ARM926 model and prints each step's
# estimated milliseconds at 396 MHz (the model's cycles times 1.05, as
# tests/calc_speed.py).
#   tests/arm/ui_bench.sh [FRAME.ppm [STATE.svs.tns]]
# STATE defaults to the Emerald state in $GBA_GAMES. Needs the Ndless
# toolchain and the plugin-enabled qemu-arm (QEMU=), as tests/calc_speed.sh.
set -eu
FRAME=${1:-none}
[ "$FRAME" = none ] || FRAME=$(realpath "$FRAME")
cd "$(dirname "$0")"
command -v nspire-gcc >/dev/null || . ~/nspire/build/env.sh
GBA_GAMES=${GBA_GAMES:-$HOME/nspire/build/testroms/gba/games}
export QEMU=${QEMU:-$HOME/nspire/build/tools/qemu-plugins/qemu-10.2.1/build/qemu-arm}
STATE=${2:-$GBA_GAMES/emerald_route_grass.svs.tns}
make -s build/ui_bench >/dev/null
python3 -I - "$FRAME" "$STATE" <<'PY'
import os, subprocess, sys
sys.path.insert(0, os.path.abspath('..'))
import calc_speed
plugin = calc_speed.build()
syms = {n: a for a, _, n in calc_speed.symbols('build/ui_bench')}
out = 'build/ui_bench.model'
frame, state = sys.argv[1:]
args = [frame if os.path.exists(frame) else 'none'] + ([state] if os.path.exists(state) else [])
subprocess.run([os.environ['QEMU'], '-cpu', 'arm926', '-plugin',
                '%s,mark=%x,out=%s' % (plugin, syms['perf_frame_mark'], out), 'build/ui_bench'] + args,
               check=True)
steps = ['the menu opens (the backdrop, the paused picture)', 'the main menu, drawn and turned',
         'a settings page, drawn and turned', 'the game list, drawn and turned',
         "a state's picture, made and packed (each save)", "a state's picture, unpacked and scaled",
         None, 'the state itself packed (each save)']
# the first mark only starts the count
frames = [int(l.split()[1]) for l in open(out) if l.startswith('frame')][1:]
for name, cycles in zip(steps, frames):
    if name:
        print('%7.1f ms  %s' % (calc_speed.ms(cycles), name))
PY
