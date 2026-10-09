#!/usr/bin/env python3
"""Render the same MIDI through libsam2695 with several banks and compare them objectively.

Writes a test MIDI (a C major scale and one held note per instrument, a few drum hits), renders it with
`sam2695render --dry` (voice mix only, no reverb / chorus) once per bank, then measures per note:

    f0 error     cents from equal temperament (A4 = 440 Hz), autocorrelation over 60..260 ms after the onset
    centroid     spectral centroid in Hz over the same window (brightness)
    attack       ms from onset to 90 % of the peak RMS (5 ms frames)
    decay        dB change of the RMS from 0.25 s to 1.5 s into the held note
    release      ms after note-off until the RMS drops 40 dB below its level at note-off
    distance     mean dB difference of 1/3-octave band levels (60 Hz..16 kHz) between two banks, same note

Every output (MIDI, WAV, JSON, Markdown) goes to --out (keep it under scratch/).

    python3 compare.py --render <build>/bin/sam2695render --out scratch/sam7/compare \
        --bank dream=scratch/sam7/gmbk5x128-203.sf2 --bank gugs=data/midi/generaluser-gs.sf2 \
        --bank sam2695sf2=testdata/midi/dream-sam2695-sf2/sam2695.sf2
"""
import argparse
import json
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'verification', 'sam2695'))
import smfwrite  # noqa: E402  (repo helper: minimal SMF writer)

PROGRAMS = [(0, 'Acoustic Grand Piano'), (24, 'Nylon Guitar'), (40, 'Violin'), (48, 'String Ensemble 1'),
            (56, 'Trumpet'), (73, 'Flute'), (19, 'Church Organ'), (33, 'Finger Bass')]
SCALE = [60, 62, 64, 65, 67, 69, 71, 72]
DRUMS = [(36, 'Kick'), (38, 'Snare'), (42, 'Closed Hi-hat'), (49, 'Crash')]
STEP, GATE, HOLD, GAP = 0.40, 0.35, 2.0, 1.5


def Score():
    """events + the list of measured notes (name, channel, key, on, off)"""
    ev, notes = [], []
    t = 0.5
    for ch, (prog, name) in enumerate(PROGRAMS):
        ch = ch if ch < 9 else ch + 1
        ev.append((t - 0.2, smfwrite.Program(ch, prog)))
        for k in SCALE:
            ev += [(t, smfwrite.NoteOn(ch, k, 100)), (t + GATE, smfwrite.NoteOff(ch, k))]
            notes.append((name, ch, k, t, t + GATE, False))
            t += STEP
        k = 48 if prog == 33 else 60
        ev += [(t, smfwrite.NoteOn(ch, k, 100)), (t + HOLD, smfwrite.NoteOff(ch, k))]
        notes.append((name, ch, k, t, t + HOLD, True))
        t += HOLD + GAP
    for k, name in DRUMS:
        ev += [(t, smfwrite.NoteOn(9, k, 110)), (t + 0.1, smfwrite.NoteOff(9, k))]
        notes.append((name, 9, k, t, t + 0.1, False))
        t += 1.2
    return ev, notes, t + 2


def ReadWav(path):
    """PCM16 or IEEE float WAV -> mono float array, rate"""
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
    if tag == 3 or (tag == 0xFFFE and bits == 32):
        x = np.frombuffer(data, dtype='<f4' if bits == 32 else '<f8').astype(float)
    else:
        x = np.frombuffer(data, dtype='<i2').astype(float) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), rate


def Rms(x, rate, frame=0.005):
    n = int(rate * frame)
    m = len(x) // n
    return np.sqrt(np.mean(x[:m * n].reshape(m, n) ** 2, axis=1) + 1e-20), frame


def F0(seg, rate, lo=25.0, hi=2000.0):
    seg = seg - seg.mean()
    if np.max(np.abs(seg)) < 1e-4:
        return None
    n = len(seg)
    spec = np.fft.rfft(seg * np.hanning(n), 2 * n)
    ac = np.fft.irfft(np.abs(spec) ** 2)[:n]
    ac /= ac[0]
    a, b = int(rate / hi), int(rate / lo)
    lag = a + int(np.argmax(ac[a:b]))
    if ac[lag] < 0.3:
        return None
    # prefer the shortest lag whose peak is close to the best one (octave errors)
    for div in (4, 3, 2):
        l2 = lag // div
        if l2 > a:
            w = max(1, l2 // 20)
            k = l2 - w + int(np.argmax(ac[l2 - w:l2 + w + 1]))
            if ac[k] > 0.85 * ac[lag]:
                lag = k
                break
    if 1 <= lag < n - 1:
        y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
        d = (y0 - y2) / (2 * (y0 - 2 * y1 + y2)) if (y0 - 2 * y1 + y2) != 0 else 0
        lag = lag + d
    return rate / lag


def Bands(seg, rate):
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    freqs = np.fft.rfftfreq(len(seg), 1 / rate)
    edges = 60 * 2 ** (np.arange(0, 28) / 3.0)
    lv = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (freqs >= lo) & (freqs < hi)
        lv.append(10 * math.log10(spec[m].sum() + 1e-12) if m.any() else -120)
    lv = np.array(lv)
    return lv - lv.max()


def Measure(x, rate, note):
    name, ch, key, on, off, held = note
    i0, i1 = int((on + 0.06) * rate), int((on + 0.26) * rate)
    seg = x[i0:i1]
    out = {'name': name, 'key': key, 'held': held}
    f0 = F0(seg, rate) if ch != 9 else None
    if f0:
        want = 440 * 2 ** ((key - 69) / 12)
        out['f0'] = f0
        c = 1200 * math.log2(f0 / want)
        out['centsRaw'] = c
        out['cents'] = c - 1200 * round(c / 1200)      # octave-folded (bank octave choice reported separately)
        out['octaveOffset'] = int(round(c / 1200))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1 / rate)
    out['centroid'] = float((spec * freqs).sum() / (spec.sum() + 1e-12))
    out['bands'] = Bands(seg, rate).tolist()
    env, fr = Rms(x[int(on * rate):int((off + 2.0) * rate)], rate)
    pk = int(np.argmax(env[:int(0.5 / fr)]))
    out['peakDb'] = 20 * math.log10(env[pk])
    out['attackMs'] = float(np.argmax(env >= 0.9 * env[pk]) * fr * 1000)
    if held:
        a, b = int(0.25 / fr), int(1.5 / fr)
        out['decayDb'] = 20 * math.log10(env[b] / env[a])
    io = int((off - on) / fr)
    ref = env[max(0, io - 2)]
    tail = env[io:]
    below = np.nonzero(tail < ref * 10 ** (-40 / 20))[0]
    out['releaseMs'] = float(below[0] * fr * 1000) if below.size else None
    return out


def BandDistance(a, b):
    """mean absolute difference in dB of the 1/3-octave levels (each normalized to its loudest band), over the
    bands where either note is within 40 dB of its peak"""
    a, b = np.array(a), np.array(b)
    m = (a > -40) | (b > -40)
    return float(np.mean(np.abs(np.maximum(a, -60) - np.maximum(b, -60))[m]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--render', required=True, help='sam2695render binary')
    ap.add_argument('--bank', action='append', required=True, help='label=path.sf2')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    ev, notes, length = Score()
    mid = os.path.join(args.out, 'sam7-compare.mid')
    smfwrite.WriteSmf(mid, ev)
    results = {}
    for spec in args.bank:
        label, path = spec.split('=', 1)
        wav = os.path.join(args.out, 'render-%s.wav' % label)
        subprocess.run([args.render, '--bank', path, '--midi', mid, '--out', wav, '--rate', '44100', '--dry',
                        '--tail', '2'], check=True, capture_output=True)
        x, rate = ReadWav(wav)
        results[label] = [Measure(x, rate, n) for n in notes]
    labels = list(results)
    json.dump(results, open(os.path.join(args.out, 'metrics.json'), 'w'), indent=1)
    # Markdown summary
    lines = ['# SAM-7 bank comparison (libsam2695 --dry, 44.1 kHz)', '']
    lines.append('| Instrument | ' + ' | '.join('%s cents (mean abs, scale)' % l for l in labels) + ' | ' +
                 ' | '.join('%s centroid Hz' % l for l in labels) + ' |')
    lines.append('|' + '---|' * (1 + 2 * len(labels)))
    for prog, name in PROGRAMS:
        row = [name]
        for l in labels:
            cs = [abs(m['cents']) for m in results[l] if m['name'] == name and not m['held'] and 'cents' in m]
            oct_ = sorted({m['octaveOffset'] for m in results[l] if m['name'] == name and 'octaveOffset' in m})
            row.append('%.1f (n=%d, oct %s)' % (np.mean(cs), len(cs), oct_) if cs else '-')
        for l in labels:
            cs = [m['centroid'] for m in results[l] if m['name'] == name and not m['held']]
            row.append('%.0f' % np.median(cs))
        lines.append('| ' + ' | '.join(row) + ' |')
    lines += ['', '| Held note | ' + ' | '.join('%s attack ms / decay dB (0.25-1.5 s) / release ms' % l
                                                  for l in labels) + ' |', '|' + '---|' * (1 + len(labels))]
    for prog, name in PROGRAMS:
        row = [name]
        for l in labels:
            m = [m for m in results[l] if m['name'] == name and m['held']][0]
            row.append('%.0f / %+.1f / %s' % (m['attackMs'], m['decayDb'],
                                              '%.0f' % m['releaseMs'] if m['releaseMs'] is not None else '>2000'))
        lines.append('| ' + ' | '.join(row) + ' |')
    lines += ['', '| Drum | ' + ' | '.join('%s peak dBFS / centroid Hz / -40 dB after ms' % l for l in labels) + ' |',
              '|' + '---|' * (1 + len(labels))]
    for k, name in DRUMS:
        row = [name]
        for l in labels:
            m = [m for m in results[l] if m['name'] == name][0]
            row.append('%.1f / %.0f / %s' % (m['peakDb'], m['centroid'],
                                            '%.0f' % m['releaseMs'] if m['releaseMs'] is not None else '>2000'))
        lines.append('| ' + ' | '.join(row) + ' |')
    lines += ['', '| Pair | 1/3-octave level distance, dB: median over all notes | melodic notes | drums |',
              '|---|---|---|---|']
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            pairs = list(zip(results[labels[i]], results[labels[j]]))
            s = [BandDistance(a['bands'], b['bands']) for a, b in pairs]
            mel = [d for d, (a, b) in zip(s, pairs) if a['name'] not in [n for k, n in DRUMS]]
            dr = [d for d, (a, b) in zip(s, pairs) if a['name'] in [n for k, n in DRUMS]]
            lines.append('| %s vs %s | %.1f | %.1f | %.1f |' % (labels[i], labels[j], float(np.median(s)),
                                                              float(np.median(mel)), float(np.median(dr))))
    md = '\n'.join(lines) + '\n'
    open(os.path.join(args.out, 'summary.md'), 'w').write(md)
    print(md)


if __name__ == '__main__':
    main()
