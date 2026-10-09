#!/usr/bin/env python3
"""Instrument coverage of Dream banks and SF2 banks, drum-note render scans, per-song checks.

    coverage.py banks  BANK...                 coverage table: GM programs, variations, GS kits, kit notes 27-87
    coverage.py drums  --render R --bank L=X.sf2 ... [--kit 0] [--gate 0.05]
                                               every kit note rendered alone through sam2695render: peak level,
                                               energy, length; flags notes silent in one bank but sounding in another
    coverage.py song   SONG.mid [--render R --bank L=X.sf2 ...] [--bank-dxb B]
                                               every (channel, bank, program, note) the song uses; checks each
                                               against the Dream bank; renders the drum part note by note per bank
    coverage.py pitch  --render R --bank L=X.sf2 ... [--programs 0-127] [--keys 36,48,...]
                                               rendered pitch per program and key vs equal temperament (cents)

BANK is a .dxb / .b16 (parsed directly) or a .sf2. Write every output under scratch/.
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'verification', 'sam2695'))
from dreambank import BankError, DreamBank, EffectiveRanges  # noqa: E402
import smfwrite  # noqa: E402

GS_KITS = {0: 'Standard', 8: 'Room', 16: 'Power', 24: 'Electronic', 25: 'TR-808', 32: 'Jazz', 40: 'Brush',
           48: 'Orchestra', 56: 'SFX', 127: 'MT-32'}
GM_DRUMS = range(35, 82)
KIT_NOTES = range(27, 88)
DRUM_NAMES = {35: 'Acoustic Bass Drum', 36: 'Bass Drum 1', 37: 'Side Stick', 38: 'Acoustic Snare', 39: 'Hand Clap',
              40: 'Electric Snare', 41: 'Low Floor Tom', 42: 'Closed Hi-Hat', 43: 'High Floor Tom', 44: 'Pedal Hi-Hat',
              45: 'Low Tom', 46: 'Open Hi-Hat', 47: 'Low-Mid Tom', 48: 'Hi-Mid Tom', 49: 'Crash Cymbal 1',
              50: 'High Tom', 51: 'Ride Cymbal 1', 52: 'Chinese Cymbal', 53: 'Ride Bell', 54: 'Tambourine',
              55: 'Splash Cymbal', 56: 'Cowbell', 57: 'Crash Cymbal 2', 58: 'Vibraslap', 59: 'Ride Cymbal 2',
              60: 'Hi Bongo', 61: 'Low Bongo', 62: 'Mute Hi Conga', 63: 'Open Hi Conga', 64: 'Low Conga',
              65: 'High Timbale', 66: 'Low Timbale', 67: 'High Agogo', 68: 'Low Agogo', 69: 'Cabasa', 70: 'Maracas',
              71: 'Short Whistle', 72: 'Long Whistle', 73: 'Short Guiro', 74: 'Long Guiro', 75: 'Claves',
              76: 'Hi Wood Block', 77: 'Low Wood Block', 78: 'Mute Cuica', 79: 'Open Cuica', 80: 'Mute Triangle',
              81: 'Open Triangle'}


# --- bank models: {(bank, program): set(notes)} for kits, sets of present (bank, program) --------------------------
def Sf2Coverage(path):
    d = open(path, 'rb').read()
    chunks = {}
    pos = 12
    while pos < len(d):
        cid, size = d[pos:pos + 4], struct.unpack_from('<I', d, pos + 4)[0]
        if cid == b'LIST':
            p = pos + 12
            while p < pos + 8 + size:
                c2, s2 = d[p:p + 4], struct.unpack_from('<I', d, p + 4)[0]
                chunks[c2] = d[p + 8:p + 8 + s2]
                p += 8 + s2 + (s2 & 1)
        pos += 8 + size + (size & 1)

    def Recs(name, fmt):
        n = struct.calcsize(fmt)
        return [struct.unpack_from(fmt, chunks[name], i) for i in range(0, len(chunks[name]), n)]
    phdr, pbag, pgen = Recs(b'phdr', '<20sHHHIII'), Recs(b'pbag', '<HH'), Recs(b'pgen', '<HH')
    inst, ibag, igen = Recs(b'inst', '<20sH'), Recs(b'ibag', '<HH'), Recs(b'igen', '<HH')

    def Zones(bags, gens, a, b):
        out = []
        for i in range(a, b):
            z = {}
            for op, amt in gens[bags[i][0]:bags[i + 1][0]]:
                z[op] = amt
            out.append(z)
        return out
    presets = {}
    for i in range(len(phdr) - 1):
        name, prog, bank, bi = phdr[i][:4]
        notes = set()
        pz = Zones(pbag, pgen, bi, phdr[i + 1][3])
        for z in pz:
            if 41 not in z:
                continue
            klo, khi = (z[43] & 0xFF, z[43] >> 8) if 43 in z else (0, 127)
            ii = z[41]
            for iz in Zones(ibag, igen, inst[ii][1], inst[ii + 1][1]):
                if 53 not in iz:
                    continue
                lo, hi = (iz[43] & 0xFF, iz[43] >> 8) if 43 in iz else (0, 127)
                notes.update(range(max(lo, klo), min(hi, khi) + 1))
        presets.setdefault((bank, prog), set()).update(notes)
    return presets, {'skipped': None}


def DreamCoverage(path):
    b = DreamBank(path)
    presets = {}
    skipped = {'melodic': 0, 'drum': 0, 'drumNotes': []}
    for p, v, a in b.Programs():
        ins = b.Instrument(a)
        notes = set()
        for layer in ins['layers']:
            for s, r in zip(layer, EffectiveRanges(layer)):
                try:
                    ok = b.Split(s['addr'])['sample'] is not None
                except BankError:
                    ok = False
                if ok:
                    notes.update(range(r[0], r[1] + 1))
                else:
                    skipped['melodic'] += 1
        presets.setdefault((v, p), set()).update(notes)
    for v, ka in b.Kits():
        kit = b.Kit(ka)
        notes = set()
        for ia, ns in kit['instruments'].items():
            ins = b.Instrument(ia)
            for n in ns:
                hit = False
                for layer in ins['layers']:
                    for s, r in zip(layer, EffectiveRanges(layer)):
                        if r[0] <= n <= r[1] or len(layer) == 1:
                            try:
                                if b.Split(s['addr'])['sample'] is not None:
                                    hit = True
                                else:
                                    skipped['drum'] += 1
                            except BankError:
                                skipped['drum'] += 1
                if hit:
                    notes.add(n)
                else:
                    skipped['drumNotes'].append((v, n))
        presets[(128, v)] = notes
    return presets, skipped


def Coverage(path):
    return DreamCoverage(path) if path.lower().endswith(('.dxb', '.b16')) else Sf2Coverage(path)


def Ranges(notes):
    out, run = [], None
    for n in sorted(notes):
        if run and n == run[1] + 1:
            run[1] = n
        else:
            if run:
                out.append(run)
            run = [n, n]
    if run:
        out.append(run)
    return ','.join('%d' % a if a == b else '%d-%d' % (a, b) for a, b in out) or '-'


def CmdBanks(args):
    rows, details = [], []
    for path in args.bank:
        pre, skipped = Coverage(path)
        melodic = [p for p in range(128) if pre.get((0, p))]
        var = [(b, p) for (b, p), n in pre.items() if 1 <= b <= 126 and n]
        mt = [(b, p) for (b, p), n in pre.items() if b == 127 and n]
        kits = {p: n for (b, p), n in pre.items() if b == 128 and n}
        gsk = [k for k in GS_KITS if k in kits]
        kit0 = kits.get(0, set())
        name = os.path.basename(path)
        rows.append('| %s | %d / 128 %s | %d | %d | %s | %d / 47 | %d / 61 | %s |' % (
            name, len(melodic), ('(missing %s)' % Ranges(set(range(128)) - set(melodic))) if len(melodic) < 128 else '',
            len(var), len(mt), ' '.join(str(k) for k in gsk) + ((' +%s' % ','.join(str(k) for k in kits if k not in GS_KITS)) if any(k not in GS_KITS for k in kits) else ''),
            len(kit0 & set(GM_DRUMS)), len(kit0 & set(KIT_NOTES)),
            ('%d melodic, %d drum splits skipped' % (skipped['melodic'], skipped['drum'])) if skipped.get('skipped', 1) is not None and 'melodic' in skipped else 'n/a'))
        for k in sorted(kits):
            miss = set(KIT_NOTES) - kits[k]
            details.append('| %s | %d %s | %s | %s |' % (name, k, GS_KITS.get(k, ''), Ranges(miss & set(GM_DRUMS)),
                                                       Ranges(miss - set(GM_DRUMS))))
        if 'drumNotes' in skipped and skipped['drumNotes']:
            details.append('| %s | skipped drum notes | %s | |' % (name, skipped['drumNotes']))
    print('| Bank | GM programs (bank 0) | Variations (C0 1-126) | MT-32 map (C0 127) | Kits (GS numbers) | Kit 0: GM notes 35-81 | Kit 0: notes 27-87 | Parser |')
    print('|---|---|---|---|---|---|---|---|')
    print('\n'.join(rows))
    print()
    print('| Bank | Kit | Missing GM notes (35-81) | Missing GS-extended notes (27-34, 82-87) |')
    print('|---|---|---|---|')
    print('\n'.join(details))


# --- rendering helpers ------------------------------------------------------------------------------------------
def ReadWav(path):
    d = open(path, 'rb').read()
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        cid, size = d[pos:pos + 4], int.from_bytes(d[pos + 4:pos + 8], 'little')
        body = d[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = (int.from_bytes(body[0:2], 'little'), int.from_bytes(body[2:4], 'little'),
                   int.from_bytes(body[4:8], 'little'), int.from_bytes(body[14:16], 'little'))
        elif cid == b'data':
            data = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, bits = fmt
    x = np.frombuffer(data, dtype='<f4').astype(float) if tag in (3, 0xFFFE) else \
        np.frombuffer(data, dtype='<i2').astype(float) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), rate


def Render(render, bank, events, out, tag):
    mid = os.path.join(out, '%s.mid' % tag)
    wav = os.path.join(out, '%s.wav' % tag)
    smfwrite.WriteSmf(mid, events)
    subprocess.run([render, '--bank', bank, '--midi', mid, '--out', wav, '--rate', '44100', '--dry', '--tail', '3'],
                   check=True, capture_output=True)
    return ReadWav(wav)


def Audible(seg, rate):
    """the segment without its content below 40 Hz (a drum pitched into the sub-bass reads loud but is not heard)"""
    X = np.fft.rfft(seg)
    X[np.fft.rfftfreq(len(seg), 1 / rate) < 40] = 0
    return np.fft.irfft(X, len(seg))


def Level(x, rate, t0, t1):
    """peak RMS (5 ms frames, content above 40 Hz) dBFS, energy dB, length to -40 dB s, spectral centroid Hz"""
    seg = x[int(t0 * rate):int(t1 * rate)]
    if len(seg) < 64:
        return -150.0, -150.0, 0.0, 0.0
    spec = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(len(seg), 1 / rate)
    centroid = float((spec * freqs).sum() / (spec.sum() + 1e-30))
    seg = Audible(seg, rate)
    n = int(rate * 0.005)
    m = len(seg) // n
    env = np.sqrt(np.mean(seg[:m * n].reshape(m, n) ** 2, axis=1) + 1e-30)
    peak = 20 * math.log10(env.max())
    energy = 10 * math.log10(np.sum(seg ** 2) / rate + 1e-30)
    above = np.nonzero(env > env.max() * 10 ** (-40 / 20))[0]
    return peak, energy, (above[-1] + 1) * 0.005 if above.size else 0.0, centroid


def BankArgs(specs):
    return [s.split('=', 1) for s in specs]


def CmdDrums(args):
    os.makedirs(args.out, exist_ok=True)
    res = {}
    step = 3.0
    for label, bank in BankArgs(args.bank):
        ev = [(0.05, smfwrite.Cc(9, 0, 0)), (0.06, smfwrite.Program(9, args.kit))]
        t = 0.5
        for n in KIT_NOTES:
            ev += [(t, smfwrite.NoteOn(9, n, args.velocity)), (t + args.gate, smfwrite.NoteOff(9, n))]
            t += step
        x, rate = Render(args.render, bank, ev, args.out, 'drums-k%d-%s' % (args.kit, label))
        res[label] = {n: Level(x, rate, 0.5 + i * step, 0.5 + i * step + step - 0.05) for i, n in enumerate(KIT_NOTES)}
    labels = list(res)
    print('| Note | Name | ' + ' | '.join('%s peak dBFS / length s / centroid Hz' % l for l in labels) + ' | Flag |')
    print('|---|---|' + '---|' * len(labels) + '---|')
    flagged = []
    for n in KIT_NOTES:
        cells, flag = [], ''
        for l in labels:
            pk, en, ln, ce = res[l][n]
            cells.append('%.1f / %.2f / %.0f' % (pk, ln, ce) if pk > -100 else 'silent')
        if len(labels) > 1:
            a, b = res[labels[0]][n], res[labels[1]][n]
            if a[0] < -70 and b[0] > -60:
                flag = 'silent in %s' % labels[0]
            elif a[0] - b[0] < -18:
                flag = '%s %.0f dB quieter' % (labels[0], b[0] - a[0])
            elif b[2] > 0.3 and a[2] < 0.25 * b[2]:
                flag = '%s much shorter' % labels[0]
            elif b[3] > 0 and abs(math.log2(max(a[3], 1) / b[3])) > 1.5:
                flag = '%s centroid %.0f vs %.0f Hz' % (labels[0], a[3], b[3])
            if flag:
                flagged.append(n)
        print('| %d | %s | %s | %s |' % (n, DRUM_NAMES.get(n, ''), ' | '.join(cells), flag))
    json.dump({l: {str(n): v for n, v in r.items()} for l, r in res.items()},
              open(os.path.join(args.out, 'drums-k%d.json' % args.kit), 'w'), indent=1)
    print('\nflagged notes: %s' % (flagged or 'none'))


# --- songs ------------------------------------------------------------------------------------------------------
def ReadSmf(path):
    d = open(path, 'rb').read()
    ntr, div = struct.unpack('>HH', d[10:14])
    pos, out = 14, []
    for tr in range(ntr):
        ln = struct.unpack('>I', d[pos + 4:pos + 8])[0]
        p, end, pos = pos + 8, pos + 8 + ln, pos + 8 + ln
        tick, run = 0, None

        def Var(p):
            v = 0
            while True:
                b = d[p]
                p += 1
                v = (v << 7) | (b & 0x7F)
                if not b & 0x80:
                    return v, p
        while p < end:
            dt, p = Var(p)
            tick += dt
            st = d[p]
            if st == 0xFF:
                typ = d[p + 1]
                ln2, q = Var(p + 2)
                out.append((tick, 'meta', typ, d[q:q + ln2]))
                p = q + ln2
                continue
            if st in (0xF0, 0xF7):
                ln2, q = Var(p + 1)
                out.append((tick, 'sysex', st, d[q:q + ln2]))
                p = q + ln2
                continue
            if st & 0x80:
                run = st
                p += 1
            n = 1 if (run >> 4) in (0xC, 0xD) else 2
            out.append((tick, 'ch', run, d[p:p + n]))
            p += n
    out.sort(key=lambda e: e[0])
    # ticks -> seconds
    tempo, last, secs, res = 500000, 0, 0.0, []
    for tick, kind, st, data in out:
        secs += (tick - last) * tempo / div / 1e6
        last = tick
        if kind == 'meta' and st == 0x51:
            tempo = int.from_bytes(data, 'big')
        res.append((secs, kind, st, data))
    return res


def SongUse(events):
    state = {ch: {'msb': 0, 'prog': 0} for ch in range(16)}
    use = {}
    for t, kind, st, data in events:
        if kind != 'ch':
            continue
        ch, typ = st & 15, st >> 4
        if typ == 0xB and data[0] == 0:
            state[ch]['msb'] = data[1]
        elif typ == 0xC:
            state[ch]['prog'] = data[0]
        elif typ == 0x9 and data[1] > 0:
            k = (ch, state[ch]['msb'], state[ch]['prog'])
            use.setdefault(k, {})
            use[k][data[0]] = use[k].get(data[0], 0) + 1
    return use


def CmdSong(args):
    ev = ReadSmf(args.song)
    use = SongUse(ev)
    dream = DreamCoverage(args.bank_dxb)[0] if args.bank_dxb else None
    print('| Channel | C0 | Program | Notes (count) | In the Dream bank |')
    print('|---|---|---|---|---|')
    for (ch, msb, prog), notes in sorted(use.items()):
        if ch == 9:
            if dream is not None:
                kit = dream.get((128, prog)) or set()
                fall = '' if dream.get((128, prog)) else ' (kit %d absent: kit 0 is used)' % prog
                kit = kit or dream.get((128, 0), set())
                miss = [n for n in notes if n not in kit]
                status = ('all %d notes mapped%s' % (len(notes), fall)) if not miss else 'missing %s%s' % (miss, fall)
            else:
                status = ''
            print('| 10 | %d | kit %d | %s | %s |' % (msb, prog, ', '.join('%d %s (%d)' % (n, DRUM_NAMES.get(n, ''), c)
                                                                         for n, c in sorted(notes.items())), status))
        else:
            if dream is not None:
                z = dream.get((msb, prog)) or dream.get((0, prog)) or set()
                miss = sorted(n for n in notes if n not in z)
                status = 'variation %d present' % msb if dream.get((msb, prog)) else (
                    'falls back to C0 0' if msb else 'present')
                if not z:
                    status = 'ABSENT'
                elif miss:
                    status += ', keys outside every split: %s' % miss
            else:
                status = ''
            print('| %d | %d | %d | keys %d-%d (%d notes) | %s |' % (ch + 1, msb, prog, min(notes), max(notes),
                                                                  sum(notes.values()), status))
    if not args.bank:
        return
    os.makedirs(args.out, exist_ok=True)
    drumNotes = sorted({n for (ch, m, p), ns in use.items() if ch == 9 for n in ns})
    setup = [(0.0, (st, data)) for t, kind, st, data in ev if kind == 'ch' and (st & 15) == 9 and (st >> 4) in (0xB, 0xC)]
    res = {}
    for label, bank in BankArgs(args.bank):
        res[label] = {}
        for n in drumNotes:
            evs = [(0.0, bytes([st]) + bytes(data)) for t, (st, data) in setup]
            for t, kind, st, data in ev:
                if kind == 'ch' and (st & 15) == 9 and (st >> 4) in (8, 9) and data[0] == n:
                    evs.append((t + 0.01, bytes([st]) + bytes(data)))
            x, rate = Render(args.render, bank, evs, args.out, 'song-n%d-%s' % (n, label))
            hits = sum(1 for t, kind, st, data in ev if kind == 'ch' and st == 0x99 and data[0] == n and data[1])
            y = Audible(x, rate)
            en = 10 * math.log10(np.sum(y ** 2) / rate / max(1, hits) + 1e-30)
            spec = np.abs(np.fft.rfft(x))
            ce = float((spec * np.fft.rfftfreq(len(x), 1 / rate)).sum() / (spec.sum() + 1e-30))
            res[label][n] = (hits, en, ce)
    labels = list(res)
    print()
    print('| Drum note | Name | Hits | ' + ' | '.join('%s energy per hit dB / centroid Hz' % l for l in labels) + ' |')
    print('|---|---|---|' + '---|' * len(labels))
    for n in drumNotes:
        print('| %d | %s | %d | %s |' % (n, DRUM_NAMES.get(n, ''), res[labels[0]][n][0],
                                         ' | '.join('%.1f / %.0f' % (res[l][n][1], res[l][n][2]) for l in labels)))


# --- pitch ------------------------------------------------------------------------------------------------------
def F0(seg, rate, lo=25.0, hi=3000.0):
    seg = seg - seg.mean()
    if np.max(np.abs(seg)) < 1e-4:
        return None
    n = len(seg)
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg * np.hanning(n), 2 * n)) ** 2)[:n]
    ac /= ac[0]
    a, b = int(rate / hi), int(rate / lo)
    lag = a + int(np.argmax(ac[a:b]))
    if ac[lag] < 0.5:
        return None
    for div in (4, 3, 2):
        l2 = lag // div
        if l2 > a:
            w = max(1, l2 // 20)
            k = l2 - w + int(np.argmax(ac[l2 - w:l2 + w + 1]))
            if ac[k] > 0.85 * ac[lag]:
                lag = k
                break
    if not 1 <= lag < n - 1:
        return None
    y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
    den = y0 - 2 * y1 + y2
    return rate / (lag + ((y0 - y2) / (2 * den) if den else 0))


def CmdPitch(args):
    os.makedirs(args.out, exist_ok=True)
    lo, hi = (int(x) for x in args.programs.split('-'))
    progs = list(range(lo, hi + 1))
    keys = [int(k) for k in args.keys.split(',')]
    step = 0.9
    res = {}
    for label, bank in BankArgs(args.bank):
        ev, t, slots = [], 0.3, []
        for ch_i, prog in enumerate(progs):
            ch = ch_i % 9          # channels 1-9, never the rhythm channel
            ev.append((t - 0.2, smfwrite.Program(ch, prog)))
            for k in keys:
                ev += [(t, smfwrite.NoteOn(ch, k, 100)), (t + 0.7, smfwrite.NoteOff(ch, k))]
                slots.append((prog, k, t))
                t += step
        x, rate = Render(args.render, bank, ev, args.out, 'pitch-%s' % label)
        out = {}
        for prog, k, t0 in slots:
            f = F0(x[int((t0 + 0.15) * rate):int((t0 + 0.6) * rate)], rate)
            if f and f > 0:
                c = 1200 * math.log2(f / (440 * 2 ** ((k - 69) / 12)))
                out[(prog, k)] = c - 1200 * round(c / 1200)
        res[label] = out
    labels = list(res)
    json.dump({l: {'%d:%d' % k: v for k, v in r.items()} for l, r in res.items()},
              open(os.path.join(args.out, 'pitch.json'), 'w'), indent=1)
    print('| Program | ' + ' | '.join('%s cents per key (%s)' % (l, ','.join(map(str, keys))) for l in labels) + ' |')
    print('|---|' + '---|' * len(labels))
    summary = {l: [] for l in labels}
    for prog in progs:
        cells = []
        for l in labels:
            vals = [res[l].get((prog, k)) for k in keys]
            summary[l] += [abs(v) for v in vals if v is not None and abs(v) < 300]
            cells.append(' '.join('%+.0f' % v if v is not None else '-' for v in vals))
        print('| %d | %s |' % (prog, ' | '.join(cells)))
    for l in labels:
        a = np.array(summary[l])
        print('%s: %d measured notes, median |error| %.1f cents, %.0f %% within 10 cents, %.0f %% within 25 cents' % (
            l, len(a), np.median(a), 100 * np.mean(a <= 10), 100 * np.mean(a <= 25)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('banks')
    s.add_argument('bank', nargs='+')
    s.set_defaults(fn=CmdBanks)
    for name, fn in (('drums', CmdDrums), ('song', CmdSong), ('pitch', CmdPitch)):
        s = sub.add_parser(name)
        s.add_argument('--render')
        s.add_argument('--bank', action='append', default=[])
        s.add_argument('--out', default='scratch/sam7/coverage')
        s.set_defaults(fn=fn)
        if name == 'drums':
            s.add_argument('--kit', type=int, default=0)
            s.add_argument('--gate', type=float, default=0.05)
            s.add_argument('--velocity', type=int, default=100)
        if name == 'song':
            s.add_argument('song')
            s.add_argument('--bank-dxb')
        if name == 'pitch':
            s.add_argument('--programs', default='0-127')
            s.add_argument('--keys', default='36,48,55,60,67,72,84')
    args = ap.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
