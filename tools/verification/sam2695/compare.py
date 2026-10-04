#!/usr/bin/env python3
"""FluidSynth reference comparison for libsam2695 (SF2 semantics).

Writes a synthetic SF2 bank and a MIDI scenario, renders both with FluidSynth and with sam2695render at the
chip's internal rate (37 500 Hz, effects off, no polyphony limit), and compares per note: onset time, pitch,
levels (velocity curve, attenuation, pan, filter), the envelope shape (attack, hold, decay and release slopes,
sustain level), the LFOs (tremolo depth, vibrato swing), the modulation envelope on pitch, loop-until-release
and key-scaled decay. Every check has a tolerance; known model differences are expected deviations with an id
from README.md (D1..D3). Exit status 0 when every check passes.

    ./compare.py [--render PATH] [--out DIR] [-v]
"""
import argparse
import math
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
from sf2write import GEN, Sample, SineSample, WriteSf2  # noqa: E402
from smfwrite import Cc, NoteOff, NoteOn, Program, WriteSmf  # noqa: E402

RATE = 37500
BLOCK = 64 / RATE           # FluidSynth's block: scenario events sit on this grid
TPQ, TEMPO = 3, 5120        # one SMF tick = one block (smfwrite.py)
D1_SCALE = 1000.0 / 960.0   # D1: our envelope dB slope / FluidSynth's (100 dB vs 96 dB per time constant)


def Grid(seconds):
    return round(seconds / BLOCK) * BLOCK


def BuildBank(path):
    sine = SineSample('sine', rate=44000, cycles=10, period=100)  # 440 Hz at key 69
    tail = []
    for i in range(8800):
        amp = 16384 if i < 4400 else 8192
        tail.append(amp * math.sin(2 * math.pi * i / 100))
    looptail = Sample('looptail', tail, 44000, 69, 0, (3400, 4400))
    lfo = lambda hz: int(round(1200 * math.log2(hz / 8.176)))
    presets = [
        ('Sine', {GEN['sampleModes']: 1}),
        ('Adsr', {GEN['sampleModes']: 1, GEN['attackVolEnv']: -1200, GEN['holdVolEnv']: -3600,
                  GEN['decayVolEnv']: 0, GEN['sustainVolEnv']: 300, GEN['releaseVolEnv']: -1200}),
        ('Atten', {GEN['sampleModes']: 1, GEN['initialAttenuation']: 120}),
        ('Pan', {GEN['sampleModes']: 1, GEN['pan']: -300}),
        ('Filter', {GEN['sampleModes']: 1, GEN['initialFilterFc']: 7200}),
        ('Tremolo', {GEN['sampleModes']: 1, GEN['modLfoToVolume']: 60, GEN['freqModLFO']: lfo(2.0)}),
        ('Vibrato', {GEN['sampleModes']: 1, GEN['vibLfoToPitch']: 50, GEN['freqVibLFO']: lfo(5.0)}),
        ('LoopTail', {GEN['sampleModes']: 3, GEN['releaseVolEnv']: -1200}),
        ('KeyDecay', {GEN['sampleModes']: 1, GEN['decayVolEnv']: -1200, GEN['sustainVolEnv']: 900,
                      GEN['keynumToVolEnvDecay']: 50}),
        ('ModEnvPitch', {GEN['sampleModes']: 1, GEN['modEnvToPitch']: 1200, GEN['attackModEnv']: -1200}),
    ]
    WriteSf2(path, [sine, looptail],
             [{'name': n, 'bank': 0, 'program': i,
               'zones': [{'sample': 'looptail' if n == 'LoopTail' else 'sine', 'gens': g}]}
              for i, (n, g) in enumerate(presets)])
    return {n: i for i, (n, _) in enumerate(presets)}


def BuildScenario(path, programs):
    """Notes in sequence on channel 0; returns [(label, program, key, velocity, on, off)]"""
    events = [(0.0, Cc(0, 7, 127)), (0.0, Cc(0, 11, 127))]
    notes = []
    t = Grid(0.1)

    def Note(label, program, key, vel, length, gap):
        nonlocal t
        events.append((t - BLOCK, Program(0, programs[program])))
        on, off = t, Grid(t + length)
        events.append((on, NoteOn(0, key, vel)))
        events.append((off, NoteOff(0, key)))
        notes.append((label, program, key, vel, on, off))
        t = Grid(off + gap)

    for v in (127, 100, 64, 32):
        Note('vel%d' % v, 'Sine', 69, v, 0.3, 0.1)
    Note('key81', 'Sine', 81, 127, 0.3, 0.1)
    Note('key57', 'Sine', 57, 127, 0.3, 0.1)
    Note('adsr', 'Adsr', 69, 127, 1.6, 1.2)
    Note('atten', 'Atten', 69, 127, 0.3, 0.1)
    Note('pan', 'Pan', 69, 127, 0.3, 0.1)
    Note('filter', 'Filter', 69, 127, 0.3, 0.1)
    Note('tremolo', 'Tremolo', 69, 127, 1.6, 0.1)
    Note('vibrato', 'Vibrato', 69, 127, 1.2, 0.1)
    Note('looptail', 'LoopTail', 69, 127, 0.4, 0.7)
    Note('keydecay48', 'KeyDecay', 48, 127, 1.2, 0.1)
    Note('keydecay72', 'KeyDecay', 72, 127, 1.2, 0.1)
    Note('modenv', 'ModEnvPitch', 57, 127, 0.8, 0.1)
    WriteSmf(path, events, TPQ, TEMPO)
    return notes


def Rms(x, a, b):
    seg = x[max(a, 0):max(b, 0)]
    return math.sqrt(float(np.mean(seg * seg))) if len(seg) else 0.0


def Db(v):
    return 20 * math.log10(max(v, 1e-12))


def Curve(x, a, b, win=188):
    """RMS dB in windows of `win` samples (5 ms) from a to b: (times in s relative to a, dB)"""
    ts, ds = [], []
    for s in range(a, b - win, win // 2):
        ts.append((s - a) / RATE)
        ds.append(Db(Rms(x, s, s + win)))
    return np.array(ts), np.array(ds)


def Frequency(x, a, b):
    seg = x[a:b] * np.hanning(b - a)
    n = 1 << int(math.ceil(math.log2(len(seg) * 8)))
    spec = np.abs(np.fft.rfft(seg, n))
    k = int(np.argmax(spec[1:])) + 1
    y0, y1, y2 = np.log(spec[k - 1] + 1e-30), np.log(spec[k] + 1e-30), np.log(spec[k + 1] + 1e-30)
    delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return (k + delta) * RATE / n


def Periods(x, a, b):
    """Instantaneous frequency from rising zero crossings"""
    seg = x[a:b]
    idx = np.nonzero((seg[:-1] < 0) & (seg[1:] >= 0))[0]
    t = idx + seg[idx] / (seg[idx] - seg[idx + 1])
    return RATE / np.diff(t)


def Onset(x, on, threshold=1e-4):
    nz = np.nonzero(np.abs(x[on:on + RATE // 4]) > threshold)[0]
    return int(nz[0]) if len(nz) else -1


def SlopeDbPerSecond(t, d, hi, lo):
    m = (d <= hi) & (d >= lo)
    if m.sum() < 4:
        return float('nan')
    return float(np.polyfit(t[m], d[m], 1)[0])


class Report:
    def __init__(self, verbose):
        self.rows, self.failed, self.verbose = [], 0, verbose

    def Check(self, name, ours, ref, tol, expected=None, unit=''):
        target = ref if expected is None else expected(ref)
        ok = abs(ours - target) <= tol
        self.failed += 0 if ok else 1
        self.rows.append((name, ours, ref, target, tol, unit, ok, expected is not None))


def Analyze(ours, ref, notes, rep):
    mono = lambda sig: sig[:, 0] + sig[:, 1]
    o, r = mono(ours), mono(ref)
    byLabel = {n[0]: n for n in notes}

    def Steady(sig, label, ch=None, a=0.15, b=0.28):
        on = int(round(byLabel[label][4] * RATE))
        x = sig if ch is None else ch
        return Db(Rms(x, on + int(a * RATE), on + int(b * RATE)))

    # global gain offset: D2, FluidSynth's filter is always on with DC gain 1/sqrt(q) = +1.5 dB at Q = 0;
    # D4, FluidSynth's modulator curves are 128-step tables read by truncation, so velocity, CC 7 and CC 11 at
    # 127 attenuate 0 cB there and 960 x concave(1/128) = 1.36 cB each on the continuous SF2 curve
    concave = -(20.0 / 96.0) * math.log10((127.0 / 128.0) ** 2)
    expectedOffset = 20 * math.log10(1 / math.sqrt(10 ** (-3.01 / 20))) + 3 * 960 * concave / 10.0
    rep.Check('gain offset (D2 + D4)', Steady(r, 'vel127') - Steady(o, 'vel127'), expectedOffset, 0.05, unit='dB')

    # onsets (D3): FluidSynth runs its envelopes per 64-sample block, so its 1 ms default delay becomes one or
    # two whole blocks; ours is the exact 37 samples (+1 for the attack's first step from silence)
    for n in notes:
        on = int(round(n[4] * RATE))
        rep.Check('onset %s (D3)' % n[0], Onset(o, on), Onset(r, on), 64, expected=lambda v: v - 64,
                  unit='samples')

    # velocity curve, attenuation, pan, filter (levels relative to vel127, offset removed)
    for lab in ('vel100', 'vel64', 'vel32', 'atten', 'filter'):
        rep.Check('level %s' % lab, Steady(o, lab) - Steady(o, 'vel127'), Steady(r, lab) - Steady(r, 'vel127'),
                  0.1, unit='dB')
    on = int(round(byLabel['pan'][4] * RATE))
    a, b = on + int(0.15 * RATE), on + int(0.28 * RATE)
    rep.Check('pan L/R', Db(Rms(ours[:, 0], a, b)) - Db(Rms(ours[:, 1], a, b)),
              Db(Rms(ref[:, 0], a, b)) - Db(Rms(ref[:, 1], a, b)), 0.05, unit='dB')

    # pitch
    for lab in ('vel127', 'key81', 'key57'):
        on = int(round(byLabel[lab][4] * RATE))
        fo, fr = Frequency(o, on + 4000, on + 11000), Frequency(r, on + 4000, on + 11000)
        rep.Check('pitch %s' % lab, 1200 * math.log2(fo / fr), 0.0, 0.5, unit='cents')

    # ADSR shape
    n = byLabel['adsr']
    on, off = int(round(n[4] * RATE)), int(round(n[5] * RATE))
    to, do = Curve(o, on, off + int(1.1 * RATE))
    tr, dr = Curve(r, on, off + int(1.1 * RATE))
    peakO, peakR = do.max(), dr.max()
    reach = lambda t, d, pk: float(t[np.argmax(d >= pk - 1.0)])
    rep.Check('attack: time to -1 dB of peak', reach(to, do, peakO), reach(tr, dr, peakR), 0.01, unit='s')
    holdEndO = float(to[np.nonzero(do >= peakO - 0.3)[0][-1]])
    holdEndR = float(tr[np.nonzero(dr >= peakR - 0.3)[0][-1]])
    rep.Check('hold end (attack + hold)', holdEndO, holdEndR, 0.01, unit='s')
    decayMask = lambda t: (t > 0.7) & (t < 1.0)
    so = SlopeDbPerSecond(to[decayMask(to)], do[decayMask(to)] - peakO, -3, -27)
    sr = SlopeDbPerSecond(tr[decayMask(tr)], dr[decayMask(tr)] - peakR, -3, -27)
    rep.Check('decay slope (D1)', so, sr, abs(sr) * 0.01, expected=lambda v: v * D1_SCALE, unit='dB/s')
    sus = lambda t, d, pk: float(np.median(d[(t > 1.3) & (t < 1.55)]) - pk)
    rep.Check('sustain level (D1)', sus(to, do, peakO), sus(tr, dr, peakR), 0.1, expected=lambda v: v * D1_SCALE,
              unit='dB')
    rel = lambda t: (t > 1.62) & (t < 2.6)
    so = SlopeDbPerSecond(to[rel(to)], do[rel(to)] - peakO, -35, -80)
    sr = SlopeDbPerSecond(tr[rel(tr)], dr[rel(tr)] - peakR, -35, -80)
    rep.Check('release slope (D1)', so, sr, abs(sr) * 0.01, expected=lambda v: v * D1_SCALE, unit='dB/s')

    # tremolo depth and rate
    n = byLabel['tremolo']
    on = int(round(n[4] * RATE))
    to, do = Curve(o, on + int(0.2 * RATE), on + int(1.5 * RATE))
    tr, dr = Curve(r, on + int(0.2 * RATE), on + int(1.5 * RATE))
    rep.Check('tremolo depth', do.max() - do.min(), dr.max() - dr.min(), 0.3, unit='dB')
    rep.Check('tremolo phase (time of first minimum)', float(to[np.argmin(do[:120])]), float(tr[np.argmin(dr[:120])]),
              0.01, unit='s')

    # vibrato swing
    n = byLabel['vibrato']
    on = int(round(n[4] * RATE))
    po, pr = Periods(o, on + 4000, on + 40000), Periods(r, on + 4000, on + 40000)
    swing = lambda p: 1200 * math.log2(np.percentile(p, 99) / np.percentile(p, 1))
    rep.Check('vibrato swing', swing(po), swing(pr), 3.0, unit='cents')

    # loop until release: after the note-off the sample plays on out of its loop into the half-level tail
    n = byLabel['looptail']
    off = int(round(n[5] * RATE))

    def TailStart(sig):
        peaks = [np.max(np.abs(sig[off + k * 85:off + (k + 1) * 85])) for k in range(40)]
        k = next(i for i in range(1, 40) if peaks[i] < 0.65 * peaks[i - 1])
        return k * 85

    rep.Check('loop-until-release tail start (D3)', TailStart(o), TailStart(r), 128, unit='samples')
    lo = lambda sig: Db(Rms(sig, off + 2000, off + 3000)) - Db(Rms(sig, off - 1500, off - 500))
    # tolerance: FluidSynth starts its release on a block boundary, up to ~3 ms (0.6 dB at 200 dB/s) late (D3)
    rep.Check('loop-until-release tail level (D1, D3)', lo(o), lo(r), 0.6,
              expected=lambda v: -6.02 + (v + 6.02) * D1_SCALE, unit='dB')
    end = lambda sig: (np.nonzero(np.abs(sig[off:off + RATE // 2]) > 1e-3)[0][-1]) / RATE
    rep.Check('loop-until-release sample end', end(o), end(r), 0.003, unit='s')

    # key-scaled decay: both keys, slope ratio 2^(24 * 50 / 1200) = 2
    slopes = {}
    for which, sig in (('ours', o), ('ref', r)):
        for lab in ('keydecay48', 'keydecay72'):
            n = byLabel[lab]
            on = int(round(n[4] * RATE))
            t, d = Curve(sig, on, on + int(1.1 * RATE))
            slopes[(which, lab)] = SlopeDbPerSecond(t, d - d.max(), -3, -60)
    rep.Check('key-scaled decay ratio', slopes[('ours', 'keydecay72')] / slopes[('ours', 'keydecay48')],
              slopes[('ref', 'keydecay72')] / slopes[('ref', 'keydecay48')], 0.02)

    # modulation envelope to pitch: half way through its 0.5 s attack the pitch is 600 cents up
    n = byLabel['modenv']
    on = int(round(n[4] * RATE))
    for at in (0.25, 0.6):
        a = on + int(at * RATE)
        fo, fr = Frequency(o, a - 600, a + 600), Frequency(r, a - 600, a + 600)
        rep.Check('mod env pitch at %.2f s' % at, 1200 * math.log2(fo / fr), 0.0, 6.0, unit='cents')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--render', default=str(REPO_ROOT / 'cmake-build-agent-release' / 'bin' / 'sam2695render'))
    ap.add_argument('--out', default=str(HERE / 'out'))
    ap.add_argument('-v', action='store_true')
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    bank, midi = out / 'harness.sf2', out / 'harness.mid'
    programs = BuildBank(str(bank))
    notes = BuildScenario(str(midi), programs)
    refWav, oursWav = out / 'fluidsynth.wav', out / 'sam2695.wav'
    subprocess.run(['fluidsynth', '-ni', '-q', '-R', '0', '-C', '0', '-g', '1', '-r', str(RATE), '-O', 'float',
                    '-T', 'wav', '-F', str(refWav), str(bank), str(midi)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([args.render, '--bank', str(bank), '--midi', str(midi), '--out', str(oursWav), '--rate', str(RATE),
                    '--gain', '1', '--polyphony', '64', '--no-reset-delay', '--tail', '1'], check=True,
                   stderr=subprocess.DEVNULL)
    ours, _ = soundfile.read(str(oursWav))
    ref, _ = soundfile.read(str(refWav))
    n = min(len(ours), len(ref))
    ours, ref = ours[:n], ref[:n]
    rep = Report(args.v)
    Analyze(ours, ref, notes, rep)
    print('%-42s %12s %12s %12s %8s  %s' % ('check', 'libsam2695', 'FluidSynth', 'expected', 'tol', ''))
    for name, ov, rv, target, tol, unit, ok, dev in rep.rows:
        print('%-42s %12.4f %12.4f %12.4f %8.3g  %s %s' % (name, ov, rv, target, tol, 'ok' if ok else 'FAIL', unit))
    print('\n%d checks, %d failed (FluidSynth %s)' % (len(rep.rows), rep.failed,
          subprocess.run(['fluidsynth', '--version'], capture_output=True, text=True).stdout.split('\n')[0]))
    return 0 if rep.failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
