"""DS80 (512x240) frame per sync PROM, in sync-generator ticks (1 tick = 16 F0 clocks = 1.3333 us at 12 MHz) and in
CPU T at each board's DS80 CPU clock. Uses analyse() from tools/machines/profi/syncprom/profisync.py.
usage: python3 ds80frames.py <repo worktree> <folder with VR*.ROM>"""
import sys, os, zlib
sys.path.insert(0, os.path.join(sys.argv[1], 'tools/machines/profi/syncprom'))
import profisync
from fractions import Fraction as F
romdir = sys.argv[2]
cases = [('VR3-0A1DFAFD.ROM', 'v3'), ('VR3-15E9B638.ROM', 'v3'), ('VR3-5A0AB56B.ROM', 'v3'),
         ('VR5-D2D4A7C8.ROM', 'v5'), ('VR5-57D728AD.ROM', 'v5')]
clocks = {'v3': [('3 MHz (12/4)', F(3)), ('6 MHz turbo (12/2)', F(6))],
          'v5': [('4 MHz (ZQ3 16/4)', F(4)), ('5 MHz (ZQ3 20/4)', F(5)), ('6 MHz (ZQ3 24/4)', F(6)),
                 ('10 MHz turbo (ZQ3 20/2)', F(10))]}
def fmt(x):
    return str(int(x)) if x.denominator == 1 else f'{float(x):.2f}'
for fn, w in cases:
    rom = open(os.path.join(romdir, fn), 'rb').read()[:2048]
    r = profisync.analyse(rom, 1, w)
    L = r['llen'][0]; n = r['n']
    paper = r['prof'].count('P')
    print(f'=== {fn} (CRC {zlib.crc32(rom):08X}), {w} wiring, DS80 half (A10 = 1)')
    print(f'  ticks: line {L}, lines {r["lines"]}, frame {n} ({n * 16 / 12:.0f} us, {12e6 / 16 / n:.3f} Hz); '
          f'FLD1 (paper fetch) {paper} ticks of each line, 240 lines; vertical {" ".join(c + str(k) for c, k in r["segs"])}')
    for (i, h, v, d, res) in r['ints']:
        il = res['DA3'] // 4
        print(f'  INT: falls at h={h} (line counter {v}); INT -> first paper tick {d} ticks; INT length {il} ticks (DA3 model)')
        for name, f in clocks[w]:
            tpt = F(16) * f / 12     # CPU T per tick
            print(f'    CPU {name:24s}: {fmt(tpt)} T/tick, line {fmt(L * tpt)} T, frame {fmt(n * tpt)} T, '
                  f'INT->paper {fmt(d * tpt)} T, INT length {fmt(il * tpt)} T, paper fetch {fmt(paper * tpt)} T/line')
