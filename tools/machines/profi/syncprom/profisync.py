#!/usr/bin/env python3
"""Profi sync-PROM decoder, tick-level model of the sync generator (v3.2 and v5 wiring).

Derived from: Profi v3.2 schematic sheet 1 (Profiv32cl.pdf on speccy4ever), Profi 3+ v5.0 album sheet 1K
(profi50.pdf p.14) and its text (p.5), KLUG's RF5 note, the SAMX6P Pentagon-fix note, the
Profi32_Pent.png hardware mod and the bit labels inside Profi_ROM_viewer.exe.

  tick      = one count of the horizontal counter DA0 = 16 master clocks = 4 CPU T
              (Spectrum raster: 14 MHz master, CPU 3.5 MHz; 80DS=1 raster: 12 MHz, CPU 3 MHz)
  h         = DA0..DA5 (74LS161 x2, 6 bits); v = DA6..DA14 (toggle FF + CD4520 x2, 9 bits)
  PROM addr = A10 80DS | A9..A5 = DA14..DA10 (= v>>4) | A4..A0 = column
              v3.2 wiring: column = DA5..DA1            (every column lasts 2 ticks)
              v5   wiring: column = DA5..DA2, A0 = DA0&DA1 (even column 3 ticks, odd column 1 tick)
  bit6=0    synchronous parallel load of h (next h = the reload value, then on from there):
            63 (all ones) on v3.2 and on the v5.0 album drawing ('v5-album' wiring);
            61 on v5 boards with Kondor's MISTAK52 fix, DD53 pin 4 (D1) moved from +5V to GND
            ('v5' wiring; the v5.06 netlist and the 5.03 replica have it, zx-pk thread 17911)
  bit7      count pulse; v advances at the end of the pulse (v5: through the DD44 register, +1 tick)
  bit5=1    asynchronous reset of DA7..DA14 (DA6 not reset)
  bit3      INT source; INT/ = src OR /Q, FF D=src clocked by DA3 rising (v3 U27, v5 DD29):
            INT starts when src falls, ends at the next DA3 rising edge.
            Profi32_Pent mod: FF clocked by DA1&DA2&DA3 instead (rising at h = 14, 30, 46, 62).
  bit0 SYNC (hsync, or composite), bit4 VSYNC when used, bit1 BLANK (0=blank), bit2 FLD1
  (v5 registers bits 0..2 in DD44: +1 tick).  Paper column 0 is taken at h=0 of video line v=0.
"""
import sys, glob, os, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from printedmap import printed_v5_map

T_PER_TICK = 4

RELOAD = {'v3': 63, 'v5': 61, 'v5-album': 63}

def col_of(h, wiring):
    return (h >> 1) if wiring == 'v3' else (((h >> 2) << 1) | (1 if (h & 3) == 3 else 0))

def count_level(rom, a10):
    half = rom[a10 * 1024:a10 * 1024 + 21 * 32]
    return 1 if sum(b >> 7 for b in half) < len(half) / 2 else 0

def simulate(rom, a10, wiring):
    cl = count_level(rom, a10)
    cdelay = 1 if wiring.startswith('v5') else 0
    h = v = 0
    pend = []   # pending v increments (tick countdown)
    def read(h, v):
        for _ in range(3):
            d = rom[(a10 << 10) | (((v >> 4) & 31) << 5) | col_of(h, wiring)]
            if d & 0x20 and (v >> 1) != 0:
                v &= 1; continue
            break
        return d, v
    seen = {}; tr = []; t = 0
    while True:
        d, v = read(h, v)
        key = (h, v, tuple(pend))
        if key in seen and t - seen[key] > 2000:
            return tr[seen[key]:], cl
        seen.setdefault(key, t)
        tr.append((h, v, d))
        nh = RELOAD[wiring] if not d & 0x40 else (h + 1) & 63
        nd, _ = read(nh, v)
        pend = [p - 1 for p in pend]
        if ((d >> 7) & 1) == cl and ((nd >> 7) & 1) != cl:
            pend.append(cdelay)
        while pend and pend[0] <= 0:
            pend.pop(0); v = (v + 1) & 511
        h = nh; t += 1
        if t > 300000: raise RuntimeError('no cycle')

def da3_rise(h0, h1):
    return not (h0 >> 3) & 1 and (h1 >> 3) & 1

def mod_rise(h0, h1):
    f = lambda h: (h & 14) == 14
    return not f(h0) and f(h1)

def analyse(rom, a10, wiring):
    fr, cl = simulate(rom, a10, wiring)
    n = len(fr)
    starts = [i for i in range(n) if fr[i][0] == 0]
    llen = sorted({(starts[(k + 1) % len(starts)] - starts[k]) % n for k in range(len(starts))})
    p0 = next(i for i in starts if fr[i][1] == 0)
    src = [(d >> 3) & 1 for (_, _, d) in fr]
    ints = []
    for i in range(n):
        if src[i - 1] == 1 and src[i] == 0:
            res = {}
            for name, rise in (('DA3', da3_rise), ('mod', mod_rise)):
                k = 0
                while not rise(fr[(i + k) % n][0], fr[(i + k + 1) % n][0]) or src[(i + k) % n]:
                    k += 1
                    if k > 200: break
                res[name] = (k + 1) * T_PER_TICK
            ints.append((i, fr[i][0], fr[i][1], (p0 - i) % n, res))
    # line classification from paper line 0
    od = 1 if wiring.startswith('v5') else 0
    paper_v = 192 if a10 == 0 else 240
    vs_used = len({(b >> 4) & 1 for b in rom[a10 * 1024:a10 * 1024 + 21 * 32]}) > 1
    act = rom[a10 * 1024:a10 * 1024 + 21 * 32]
    vs_lvl = 1 if sum((b >> 4) & 1 for b in act) < len(act) / 2 else 0
    k0 = starts.index(p0); order = starts[k0:] + starts[:k0]
    cls = []
    for k, st in enumerate(order):
        en = order[(k + 1) % len(order)]
        idx = [(st + j) % n for j in range((en - st) % n or n)]
        if fr[st][1] < paper_v: c = 'P'
        elif (vs_used and any(((fr[i][2] >> 4) & 1) == vs_lvl for i in idx)) or sum(1 for i in idx if not fr[i][2] & 1) > len(idx) // 2: c = 'V'
        elif any((fr[i][2] >> 1) & 1 for i in idx): c = 'b'
        else: c = 'B'
        cls.append(c)
    segs = []
    for c in cls:
        if segs and segs[-1][0] == c: segs[-1][1] += 1
        else: segs.append([c, 1])
    # horizontal profile of a paper line (delayed by output register on v5)
    st = order[100] if a10 == 0 else order[100]
    L = llen[0]
    line = [fr[(st + j - od) % n] for j in range(L)]
    prof = ''.join('P' if (d >> 2) & 1 else ('S' if not d & 1 else ('b' if (d >> 1) & 1 else 'B')) for (_, _, d) in line)
    return dict(n=n, llen=llen, lines=len(starts), ints=ints, segs=segs, prof=prof, cl=cl, vs_used=vs_used)

def run(name, rom, wirings):
    out = [f'=== {name}  CRC32(2048)={zlib.crc32(rom[:2048]):08X}']
    for a10, mode, mhz in ((0, 'Spectrum 256x192', 14), (1, '80DS 512x240', 12)):
        for w in wirings:
            try:
                r = analyse(rom, a10, w)
            except StopIteration:
                out.append(f'  [{mode}, {w} wiring] no frame: the counters never reach video line 0 with this PROM and this reload')
                continue
            us = 16 / mhz
            out.append(f'  [{mode}, {w} wiring] line {r["llen"]} ticks = {[x*4 for x in r["llen"]]} T '
                       f'({r["llen"][0]*us:.2f} us), {r["lines"]} lines, frame {r["n"]*4} T ({r["n"]*us/1000:.3f} ms); '
                       f'count pulse active-{"high" if r["cl"] else "low"}; bit4 {"VSYNC" if r["vs_used"] else "unused"}')
            out.append(f'     line from h=0 (P paper/FLD1, b border, B blank, S sync): {r["prof"]}')
            out.append(f'     vertical from paper line 0 (P paper, b border, B blank, V vsync): ' + ' '.join(f'{c}{k}' for c, k in r['segs']))
            for (i, h, v, d, res) in r['ints']:
                L = r['llen'][0]
                out.append(f'     INT bit3 falls at h={h} (line counter {v}): INT->paper col0 = {d*4} T '
                           f'({d//L} lines + {d%L*4} T); INT length {res["DA3"]} T stock (DA3 FF), {res["mod"]} T with Profi32_Pent mod')
    return '\n'.join(out)

if __name__ == '__main__':
    # Download the VR*.ROM dumps from https://speccy4ever.speccy.org/_PR.htm into one folder and pass it here
    if len(sys.argv) != 2:
        sys.exit('usage: profisync.py <folder with VR*.ROM>')
    roms = sorted(glob.glob(os.path.join(sys.argv[1], 'VR*.ROM')))
    for f in roms:
        b = os.path.basename(f)
        print(run(b, open(f, 'rb').read()[:2048], ['v3'] if b.startswith('VR3') else ['v5', 'v5-album', 'v3']))
    print(run('PRINTED map, profi50.pdf p.9 (Kondor 1994)', printed_v5_map(), ['v5-album', 'v5', 'v3']))
