#!/usr/bin/env python3
"""Write the ISA I2 test MOD (Sprinter ISA open question Q10): one looped sine sample, three notes, fixed tempo.

What it plays (ProTracker "M.K.", 4 channels, one pattern, song length 1, so it repeats):

    rows  0-15  C-3 (period 214)   channel 1, sample 1, volume 64
    rows 16-31  E-3 (period 170)
    rows 32-47  G-3 (period 143)
    rows 48-63  silence (effect C00: volume 0)

Speed 6, tempo 125 BPM (no effect changes them): a row is 6 ticks of 20 ms = 120 ms, a note 16 rows = 1.92 s,
the pattern 7.68 s. The sample is one cycle of a sine in 64 signed bytes, looped over its whole length, so the
pitch a player produces is

    f = clock / period / 64        (PAL Amiga clock 3 546 895 Hz: C-3 259.0 Hz, E-3 326.0 Hz, G-3 387.6 Hz)

Both numbers - the pitch of each note and the 1.92 s between note starts - follow from the file alone, whatever
firmware plays it (the test asserts them; the waveform comparison with MAME is documented separately).

    make-test-mod.py OUT.MOD [--print]
"""

import math
import struct
import sys

PAL_CLOCK = 3546895
SAMPLE_LENGTH = 64           # bytes, one sine cycle
NOTES = [(214, "C-3"), (170, "E-3"), (143, "G-3")]
ROWS_PER_NOTE = 16
SPEED = 6
BPM = 125


def sample_data():
    return bytes((int(round(127 * math.sin(2 * math.pi * i / SAMPLE_LENGTH))) & 0xFF) for i in range(SAMPLE_LENGTH))


def cell(sample, period, effect=0, param=0):
    return bytes([(sample & 0xF0) | (period >> 8), period & 0xFF, ((sample & 0x0F) << 4) | effect, param])


def build():
    out = bytearray()
    out += b"unreal-ng isa test".ljust(20, b"\0")
    # Sample 1: name, length (words), finetune, volume, loop start (words), loop length (words)
    out += b"sine 64".ljust(22, b"\0") + struct.pack(">HBBHH", SAMPLE_LENGTH // 2, 0, 64, 0, SAMPLE_LENGTH // 2)
    for _ in range(30):
        out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([1, 127]) + bytes(128) + b"M.K."
    pattern = bytearray()
    for row in range(64):
        note = row // ROWS_PER_NOTE
        channels = [bytes(4)] * 4
        if row % ROWS_PER_NOTE == 0:
            if note < len(NOTES):
                channels[0] = cell(1, NOTES[note][0])
            else:
                channels[0] = cell(0, 0, 0xC, 0)   # volume 0: the channel falls silent
        for c in channels:
            pattern += c
    out += pattern
    out += sample_data()
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    if "--print" in sys.argv:
        row_s = SPEED * 2.5 / BPM
        print(f"{sys.argv[1]}: {len(data)} bytes, row {row_s * 1000:.0f} ms, note {row_s * ROWS_PER_NOTE:.2f} s")
        for period, name in NOTES:
            print(f"  {name} period {period}: {PAL_CLOCK / period / SAMPLE_LENGTH:.2f} Hz (PAL clock)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
