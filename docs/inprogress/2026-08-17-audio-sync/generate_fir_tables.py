#!/usr/bin/env python3
"""FIR table generator for the multi-rate audio core plan.

Reproduces both shipped 44.1 kHz tables BIT-EXACTLY (validated 2026-08-17):
  FilterDecimator: fir_kaiser(96,  fc=20000, fs=218750,  beta=5)
  FilterUnreal:    fir_hamming(128, fc=11025, fs=2822400)

and generates the per-rate tables embedded in
multirate-core-implementation-plan.md. The C++ runtime designer
(common/sound/filters/fir_designer.h, plan phase 2) must match this
implementation; its unit test asserts coefficient equality against the
shipped tables the same way this script was validated.

Usage: python3 generate_fir_tables.py > tables.md
"""

import math


def i0(x: float) -> float:
    """Modified Bessel function of the first kind, order 0 (power series)."""
    s, t, k = 1.0, 1.0, 1
    while True:
        t *= (x / (2.0 * k)) ** 2
        s += t
        k += 1
        if t < 1e-21 * s:
            return s


def sinc(x: float) -> float:
    return 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)


def fir_kaiser(taps: int, fc: float, fs: float, beta: float) -> list:
    """Kaiser-windowed sinc lowpass, DC-normalized (sum == 1)."""
    M = taps - 1
    h = []
    for n in range(taps):
        m = n - M / 2.0
        w = i0(beta * math.sqrt(max(0.0, 1 - (2.0 * m / M) ** 2))) / i0(beta)
        h.append(2.0 * fc / fs * sinc(2.0 * fc / fs * m) * w)
    s = sum(h)
    return [x / s for x in h]


def fir_hamming(taps: int, fc: float, fs: float) -> list:
    """Hamming-windowed sinc lowpass, DC-normalized (matches MATLAB fir1)."""
    M = taps - 1
    h = []
    for n in range(taps):
        m = n - M / 2.0
        w = 0.54 - 0.46 * math.cos(2 * math.pi * n / M)
        h.append(2.0 * fc / fs * sinc(2.0 * fc / fs * m) * w)
    s = sum(h)
    return [x / s for x in h]


RATES = [44100, 48000, 88200, 96000, 176400, 192000]


def emit_cpp(name: str, h: list, comment: str, sci: bool = True) -> None:
    print("```cpp")
    print(f"// {comment}")
    print(f"static constexpr double {name}[{len(h)}] = {{")
    for i in range(0, len(h), 4):
        chunk = h[i:i + 4]
        fmt = "%23.18e" if sci else "%.18f"
        line = "        " + ", ".join(fmt % x for x in chunk)
        print(line + ("," if i + 4 < len(h) else ""))
    print("};")
    print("```\n")


def main() -> None:
    # Self-validation against shipped table heads
    d = fir_kaiser(96, 20000, 218750, 5.0)
    assert abs(d[0] - 2.052603086353451149e-04) < 1e-18, "Decimator validation failed"
    u = fir_hamming(128, 11025, 2822400)
    assert abs(u[0] - 0.000797243121022152) < 1e-15, "FilterUnreal validation failed"

    emit_cpp("FIR_COEFFS_REFERENCE", d,
             "96-tap lowpass, Fc=20kHz @ Fs=218.75kHz, Kaiser beta=5 (all output rates)")
    for rate, fc in ((96000, 40000), (192000, 80000)):
        emit_cpp(f"FIR_COEFFS_EXT_{rate}", fir_kaiser(96, fc, 218750, 5.0),
                 f"96-tap lowpass, Fc={fc // 1000}kHz @ Fs=218.75kHz, Kaiser beta=5 - "
                 f"extended BW for {rate} Hz output")
    # HighFidelity tier: 192 taps, beta=9 (~90+ dB); coefficients depend only
    # on cutoff, not output rate (fixed 218.75 kHz input side)
    emit_cpp("FIR_HF_REFERENCE", fir_kaiser(192, 20000, 218750, 9.0),
             "192-tap lowpass, Fc=20kHz @ Fs=218.75kHz, Kaiser beta=9 - HighFidelity, all rates")
    emit_cpp("FIR_HF_EXT_40K", fir_kaiser(192, 40000, 218750, 9.0),
             "192-tap, Fc=40kHz, Kaiser beta=9 - HighFidelity extended BW, 88.2/96 kHz output")
    emit_cpp("FIR_HF_EXT_80K", fir_kaiser(192, 80000, 218750, 9.0),
             "192-tap, Fc=80kHz, Kaiser beta=9 - HighFidelity extended BW, 176.4/192 kHz output")
    for rate in RATES:
        fs = rate * 64
        emit_cpp(f"OVERSAMPLING_FIR_{rate}", fir_hamming(128, 11025, fs),
                 f"128-tap Hamming lowpass, fc=11025 Hz @ fs={fs} Hz (core {rate})",
                 sci=False)


if __name__ == "__main__":
    main()
