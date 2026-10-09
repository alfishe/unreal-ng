#!/usr/bin/env python3
"""wavspectrum.py a.wav [b.wav ...]: per file the duration, RMS and peak of L / R and the strongest spectral peaks of each side"""
import sys, wave, array, math
try:
    import numpy as np
except ImportError:
    np = None
for path in sys.argv[1:]:
    try:
        w = wave.open(path)
    except Exception as e:
        print(path, 'unreadable', e); continue
    n = w.getnframes(); ch = w.getnchannels(); rate = w.getframerate()
    a = array.array('h'); a.frombytes(w.readframes(n))
    if np is None:
        print(path, n, 'frames (numpy missing)'); continue
    d = np.array(a, dtype=float).reshape(-1, ch)
    d = d[int(rate * 0.2):]  # skip the start
    print(f"{path.split('/')[-1]:12s} {len(d)/rate:5.2f}s ", end='')
    for side, name in enumerate('LR'[:ch]):
        x = d[:, side]
        rms = math.sqrt(float(((x - x.mean()) ** 2).mean())) if len(x) else 0  # AC rms
        peak = np.abs(x).max() if len(x) else 0
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
        freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
        top = []
        s = spec.copy()
        for _ in range(4):
            i = int(s.argmax())
            if s[i] < spec.max() * 0.15: break
            top.append(round(float(freqs[i])))
            s[max(0, i - 8):i + 9] = 0
        print(f"| {name}: rms {rms:7.0f} pk {int(peak):6d} peaks {top}", end=' ')
    print()
