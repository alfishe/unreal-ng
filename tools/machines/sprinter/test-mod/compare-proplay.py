#!/usr/bin/env python3
"""Compare the test MOD played by ProPlay on two emulators (Sprinter ISA phase I2, tdd §11 T-ISA-11).

    compare-proplay.py A.wav[:channels][@seconds] B.wav[:channels][@seconds]

Each WAV is mixed to mono over the listed channels (comma-separated, 0-based; default: all); @seconds starts the
search for the notes there (MAME's capture holds the boot first). A note's start is taken after silence, so give a
point inside the silent rows (or before the first note) to get the whole pattern. MAME's -wavwrite on the
Sprinter writes 8 channels; the NeoGS on the ISA adapter is channels 6 (left) and 7 (right) there. unreal-ng's
test capture (UNREAL_SPRINTER_PROPLAY_WAV) is the GS row, mono.

For each file: the dominant frequency every 20 ms (100 ms window), the start of each note (C-3, E-3, G-3 of the
generated MOD), each note's pitch from a 1 s window (FFT, 8x zero padding, parabolic peak) and the time between the
starts. Then the two side by side, aligned on their first C-3 start: pitch differences in cents, the difference of
the note intervals, the correlation of the 20 ms RMS envelopes over two pattern loops and the correlation of the
waveforms (resampled to the lower rate, best lag within +/- 5 ms).
"""

import sys
import wave

import numpy as np

PAL = 3546895.0
NOTES = [("C-3", PAL / 214 / 64), ("E-3", PAL / 170 / 64), ("G-3", PAL / 143 / 64)]


def load(spec):
    spec, _, _start = spec.partition("@")
    path, _, chans = spec.partition(":")
    w = wave.open(path)
    n, rate, nch = w.getnframes(), w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, nch).astype(float)
    cols = [int(c) for c in chans.split(",")] if chans else list(range(nch))
    mono = x[:, cols].mean(axis=1)
    return mono - np.median(mono), rate


def dominant(seg, rate, lo=150.0, hi=500.0):
    seg = seg - seg.mean()
    if np.sqrt(np.mean(seg ** 2)) < 30:
        return 0.0
    pad = 8 * len(seg)
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n=pad))
    f = np.fft.rfftfreq(pad, 1.0 / rate)
    band = (f >= lo) & (f <= hi)
    k = np.flatnonzero(band)[np.argmax(sp[band])]
    if 0 < k < len(sp) - 1:
        a, b, c = sp[k - 1], sp[k], sp[k + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        return f[k] + d * (f[1] - f[0])
    return f[k]


def analyse(x, rate, start=0.0):
    step, win = int(rate * 0.02), int(rate * 0.1)
    track = []
    for at in range(int(start * rate), len(x) - win, step):
        track.append(dominant(x[at:at + win], rate))
    near = lambda f, want: f > 0 and abs(f - want) / want < 0.03
    starts = []
    for i, f in enumerate(track):
        n = len(starts)
        if n < 3 and near(f, NOTES[n][1]) and (i == 0 or not near(track[i - 1], NOTES[n][1])):
            starts.append(start + i * 0.02)
    pitches = []
    for n, t in enumerate(starts):
        a = int((t + 0.45) * rate)
        pitches.append(dominant(x[a:a + rate], rate, NOTES[n][1] * 0.9, NOTES[n][1] * 1.1))
    return starts, pitches


def envelope(x, rate, t0, seconds):
    step = int(rate * 0.02)
    a = int(t0 * rate)
    # AC RMS: a DAC's resting level (the GS's #80 on one side) is not loudness
    return np.array([np.std(x[a + i * step:a + (i + 1) * step]) for i in range(int(seconds / 0.02))])


def main():
    args = sys.argv[1:]
    if len(args) != 2:
        print(__doc__)
        return 2
    data = []
    for spec in args:
        x, rate = load(spec)
        start = float(spec.partition("@")[2] or 0)
        starts, pitches = analyse(x, rate, start)
        data.append((spec, x, rate, starts, pitches))
        print(f"{spec}: {len(x) / rate:.2f} s at {rate} Hz")
        for n, (t, p) in enumerate(zip(starts, pitches)):
            want = NOTES[n][1]
            cents = 1200 * np.log2(p / want)
            print(f"  {NOTES[n][0]} starts {t:7.3f} s, {p:8.2f} Hz (expected {want:.2f}, {cents:+.1f} cents)")
        for n in range(1, len(starts)):
            print(f"  {NOTES[n - 1][0]} -> {NOTES[n][0]}: {starts[n] - starts[n - 1]:.3f} s (expected 1.920)")
    (sa, xa, ra, sta, pa), (sb, xb, rb, stb, pb) = data
    if len(sta) < 3 or len(stb) < 3:
        print("a note is missing: no comparison")
        return 1
    print("side by side (aligned on the first C-3):")
    for n in range(3):
        print(f"  {NOTES[n][0]}: {1200 * np.log2(pb[n] / pa[n]):+.2f} cents B vs A; "
              f"start offset {(stb[n] - stb[0]) - (sta[n] - sta[0]):+.3f} s")
    # Up to two pattern loops, as far as both captures reach
    seconds = min(2 * 7.68 - 0.2, len(xa) / ra - sta[0] + 0.1, len(xb) / rb - stb[0] + 0.1) - 0.02
    ea = envelope(xa, ra, sta[0] - 0.1, seconds)
    eb = envelope(xb, rb, stb[0] - 0.1, seconds)
    m = min(len(ea), len(eb))
    print(f"  envelope correlation (20 ms RMS, {seconds:.1f} s): {np.corrcoef(ea[:m], eb[:m])[0, 1]:.4f}")
    # Waveforms: the higher rate down to the lower one, the best lag within +/- 5 ms, two seconds of C-3
    rate = min(ra, rb)
    def take(x, r, t0):
        seg = x[int(t0 * r):int((t0 + 2.0) * r)]
        return np.interp(np.arange(0, len(seg) * rate / r) * r / rate, np.arange(len(seg)), seg)
    wa = take(xa, ra, sta[0] + 0.2)
    wb = take(xb, rb, stb[0] + 0.2)
    best = (-2.0, 0)
    for lag in range(-int(rate * 0.005), int(rate * 0.005) + 1):
        a = wa[max(0, lag):len(wa) + min(0, lag)]
        b = wb[max(0, -lag):len(wb) + min(0, -lag)]
        k = min(len(a), len(b))
        c = np.corrcoef(a[:k], b[:k])[0, 1]
        if c > best[0]:
            best = (c, lag)
    print(f"  waveform correlation (C-3, 2 s): {best[0]:.4f} at lag {best[1] / rate * 1000:+.2f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
