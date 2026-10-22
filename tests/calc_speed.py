#!/usr/bin/env python3
"""Estimates how fast gpSP runs a game on the calculator (TI-Nspire CX II,
ARM926EJ-S at 396 MHz), from a PC. Run it through tests/calc_speed.sh, which
puts the Ndless toolchain on PATH.

It builds tests/arm/perf (the core with its ARM dynarec, the renderer and the
picture output, compiled with the calculator's flags), plays a game from a
save state under qemu-arm with the ARM926 model (tools/calc_speed/arm926.c:
pipeline, 16 KB / 8 KB caches, write buffer, the memory timings measured on
the calculator), and reports milliseconds per frame by part of the emulator
and the frame rates that gives:

  game     running the game: the dynarec's code and what it calls (memory,
           I/O), translating code, DMA, the timers and other events
  drawing  the GBA picture, line by line (the renderer)
  picture  scaling, blending and turning it into the LCD's buffer

Checked against PocketSNES's speed tests on the calculator (Super Mario
World, 2026-10-10): the model's milliseconds times 1.05 came within 6% of
all three (3.6 vs 3.70 ms a frame running the game on the ARM core, 4.8 vs
4.74 on the C core, 10.0 vs 10.6 drawing every frame). gpSP's own speed test
on the calculator (Menu > speed test) is the real thing.
"""

import argparse
import bisect
import collections
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARM = os.path.join(ROOT, 'tests', 'arm')
BUILD = os.path.join(ARM, 'build')
RUN = os.path.join(BUILD, 'speed')
QEMU = os.environ.get('QEMU', os.path.expanduser('~/nspire/build/tools/qemu-plugins/qemu-10.2.1/build/qemu-arm'))
GAMES = os.environ.get('GBA_GAMES', os.path.expanduser('~/nspire/build/testroms/gba/games'))
SCALE = 1.05      # model -> calculator (fitted on PocketSNES, see above)
MHZ = 396.0

# Where each function's cycles go: the call and everything it calls.
REGIONS = [('update_scanline', 'drawing'), ('nspire_present_frame', 'picture'),
           ('translate_block_arm', 'translate'), ('translate_block_thumb', 'translate'),
           ('update_gba', 'events'), ('dma_transfer', 'dma')]
GAME_PARTS = ('cpu', 'translate', 'events', 'dma')


def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def build():
    sh(['make', '-s', '-C', ARM, '-j%d' % os.cpu_count(), 'build/perf'], stdout=subprocess.DEVNULL)
    plugin = os.path.join(BUILD, 'arm926.so')
    src = os.path.join(ROOT, 'tools', 'calc_speed', 'arm926.c')
    if not os.path.exists(plugin) or os.path.getmtime(src) > os.path.getmtime(plugin):
        inc = os.path.join(os.path.dirname(os.path.dirname(QEMU)), 'include', 'qemu')
        glib = subprocess.run(['pkg-config', '--cflags', '--libs', 'glib-2.0'], capture_output=True,
                              text=True, check=True).stdout.split()
        sh(['gcc', '-O2', '-shared', '-fPIC', '-I' + inc, src, '-o', plugin] + glib)
    return plugin


def symbols(elf):
    out = subprocess.run(['arm-none-eabi-nm', '-n', '-S', elf], capture_output=True, text=True,
                         check=True).stdout
    syms = []
    for line in out.split('\n'):
        p = line.split()
        if len(p) == 4:
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
        elif len(p) == 3:
            syms.append((int(p[0], 16), 0, p[2]))
    syms.sort()
    return syms


def link(name, target):
    """Semihosting splits the command line at spaces: plain names (one set
    per --tag, so runs can go side by side)."""
    path = os.path.join(RUN, name)
    tmp = path + '.new'
    if os.path.lexists(tmp):
        os.remove(tmp)
    os.symlink(os.path.abspath(target), tmp)
    os.replace(tmp, path)
    return os.path.relpath(path, ARM)


def run(args, plugin, tag):
    os.makedirs(RUN, exist_ok=True)
    elf = os.path.join(BUILD, 'perf')
    syms = symbols(elf)
    by_name = {n: a for a, _, n in syms}
    regions = os.path.join(RUN, tag + '.regions')
    with open(regions, 'w') as f:
        for fn, name in REGIONS:
            if fn in by_name:
                f.write('%x %s\n' % (by_name[fn], name))
    out = os.path.join(RUN, tag + '.model')
    plugin_args = '%s,mark=%x,regions=%s,out=%s' % (plugin, by_name['perf_frame_mark'], regions, out)
    if args.functions_of:
        first, last = args.functions_of.split('-')
        plugin_args += ',first=%d,last=%d' % (int(first) - 1, int(last))
    if args.model:
        plugin_args += ',' + args.model
    cmd = [QEMU, '-cpu', 'arm926', '-plugin', plugin_args,
           os.path.relpath(elf, ARM), link(tag + '.rom', args.rom), link(tag + '.bios', args.bios),
           link(tag + '.state', args.state) if args.state != '-' else '-', str(args.frames),
           link(tag + '.keys', args.keys) if args.keys != '-' else '-', str(args.draw)] + args.set
    res = subprocess.run(cmd, cwd=ARM, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit('perf failed (exit %d):\n%s%s' % (res.returncode, res.stdout, res.stderr))
    return out, syms, res.stdout


def analyse(out, syms):
    lines = open(out).read().split('\n')
    names = lines[2].split()[1:]
    n = len(names)
    frames = []
    for line in lines:
        if line.startswith('frame'):
            v = [int(x) for x in line.split()[1:]]
            frames.append(dict(zip(names, v[:n])))
    addrs = [a for a, _, _ in syms]
    funcs = collections.defaultdict(lambda: [0, 0, 0, 0, 0])
    for line in lines[3:]:
        p = line.split()
        if len(p) != 7:
            continue
        pc = int(p[0], 16)
        i = bisect.bisect_right(addrs, pc) - 1
        fn = '?'
        if i >= 0:
            a, size, sym = syms[i]
            fn = sym if not size or pc < a + size else '?'
        acc = funcs[(names[int(p[1])], fn)]
        for j in range(5):
            acc[j] += int(p[2 + j])
    return frames, funcs


def ms(cycles):
    return cycles * SCALE / (MHZ * 1000.0)


def demangle(names):
    res = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True)
    return dict(zip(names, res.stdout.split('\n')))


def report(args, frames, funcs, skip):
    timed = frames[skip:]
    nf = len(timed)
    if not nf:
        sys.exit('no frames timed')
    parts = collections.Counter()
    for fr in timed:
        for k, v in fr.items():
            parts[k] += v
    game = sum(parts[k] for k in GAME_PARTS) / nf
    drawn = [fr for fr in timed if fr.get('picture', 0)]
    nd = max(1, len(drawn))
    draw = sum(fr.get('drawing', 0) for fr in drawn) / nd
    pic = sum(fr.get('picture', 0) for fr in drawn) / nd
    totals = [sum(fr.values()) for fr in timed]

    print('%d frames timed (%d drawn); estimated ms per frame on the calculator at %.0f MHz:'
          % (nf, len(drawn), MHZ))
    print('  game     %5.2f   (dynarec code %.2f, translating %.2f, events %.2f, DMA %.2f)'
          % (ms(game), ms(parts['cpu'] / nf), ms(parts['translate'] / nf), ms(parts['events'] / nf),
             ms(parts['dma'] / nf)))
    if drawn:
        print('  drawing  %5.2f   per drawn frame' % ms(draw))
        print('  picture  %5.2f   per drawn frame' % ms(pic))
    avg = sum(totals) / nf
    print('  all      %5.2f   per frame on average: %.1f game frames a second with no speed limit'
          % (ms(avg), 1000.0 / ms(avg)))
    # The slowest stretches: a second's worth of frames (the FPS counter's
    # window) and the slowest single frames.
    win = min(nf, args.window)
    worst = max(sum(totals[i:i + win]) for i in range(nf - win + 1))
    print('  slowest %d frames in a row: %.1f game frames a second; slowest frame %.2f ms'
          % (win, win * 1000.0 / ms(worst), ms(max(totals))))
    if drawn:
        # Fast forward drawing at the LCD's pace: 60 pictures a second, the
        # other frames only run (from this run's averages): N frames of game
        # and 60 of drawing and picture fit in a second.
        budget = 1000.0 - 60 * ms(draw + pic)
        print('  fast forward drawing 60 pictures a second (from these averages): %.0f game frames a second'
              % (budget / ms(game) if budget > 0 else 0))

    if args.top:
        dem = demangle([k[1] for k in funcs])
        print('\n%-8s %-46s %8s %8s %5s %7s %7s %7s %6s' % ('part', 'function', 'insns', 'cycles', 'CPI',
                                                           'ifetch', 'data', 'wbuf', 'ms'))
        per = len(frames)
        if args.functions_of:
            first, last = args.functions_of.split('-')
            per = min(len(frames), int(last)) - int(first) + 1
        for (region, fn), v in sorted(funcs.items(), key=lambda kv: -kv[1][1])[:args.top]:
            print('%-8s %-46s %8.0f %8.0f %5.2f %7.0f %7.0f %7.0f %6.3f'
                  % ({'cpu': 'game'}.get(region, region), dem.get(fn, fn)[:46], v[0] / per, v[1] / per,
                     v[1] / max(1, v[0]), v[2] / per, v[3] / per, v[4] / per, ms(v[1] / per)))
        print('(per frame, over all frames run; ifetch/data/wbuf: cycles waiting for instruction'
              ' fetches, data reads and the write buffer)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('state', nargs='?', default=os.path.join(GAMES, 'emerald_route_grass.svs.tns'),
                    help='a gpSP save state to start from, or - (default: Emerald by the tall grass)')
    ap.add_argument('--rom', default=os.path.join(GAMES, 'Pokemon - Emerald Version (USA, Europe).gba'))
    ap.add_argument('--bios', default=os.path.join(GAMES, 'gba_bios.bin'))
    ap.add_argument('--frames', type=int, default=600, help='frames to run (default 600)')
    ap.add_argument('--skip', type=int, default=30, help='first frames left out (default 30)')
    ap.add_argument('--keys', default='-', help='keys to press (replay.c format)')
    ap.add_argument('--draw', type=int, default=1,
                    help='draw every frame (1, default), none (0) or one in N')
    ap.add_argument('--window', type=int, default=120,
                    help='frames in the slowest-stretch figure (default 120: one second at 2x)')
    ap.add_argument('--top', type=int, default=0, help='list the N costliest functions')
    ap.add_argument('--functions-of', metavar='A-B',
                    help='list the functions of frames A to B only (with --top)')
    ap.add_argument('--model', metavar='ARGS',
                    help='more arguments for the ARM926 model, e.g. repl=random or icache=32768'
                         ' (tools/calc_speed/arm926.c)')
    ap.add_argument('--tag', default='run', help='name of the output files in tests/arm/build/speed')
    ap.add_argument('--set', action='append', default=[], metavar='NAME=VALUE',
                    help='a setting other than the owner\'s (rombuf=16 policy=0 renderer=0 scale=1 mix=1'
                         ' sprlim=1)')
    args = ap.parse_args()
    plugin = build()
    out, syms, stdout = run(args, plugin, args.tag)
    frames, funcs = analyse(out, syms)
    print(stdout.strip().split('\n')[-1])
    report(args, frames, funcs, min(args.skip, max(0, len(frames) - 1)))


if __name__ == '__main__':
    main()
