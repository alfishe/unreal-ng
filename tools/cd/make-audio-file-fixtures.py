#!/usr/bin/env python3
"""Write the MP3 and FLAC fixtures of the audio-CD-from-a-folder tests (testdata/media/audio/).

The tests check the decoders against the tones these files were made of, so every file is a
sine of a known frequency and amplitude, short enough to keep the fixtures tiny:

    tone-440-660-44k-stereo.mp3   0.5 s, 44100 Hz stereo, left 440 Hz / right 660 Hz, amplitude 0.5,
                                  lame -b 96 --resample 44.1 (CBR; LAME alone would pick 32 kHz):
                                  an Info frame with a LAME tag (encoder delay and padding: the
                                  decoder must cut them, 22050 samples exactly)
    tone-1000-48k-mono.mp3        0.5 s, 48000 Hz mono, 1000 Hz, amplitude 0.5, lame -b 64 -m m:
                                  resampled to 44100 Hz (22050 samples) and played on both sides
    tone-440-44k-stereo-16.flac   0.25 s, 44100 Hz stereo 16-bit, left 440 / right 660, amplitude 0.5
    tone-1000-96k-mono-24.flac    0.1 s, 96000 Hz mono 24-bit, 1000 Hz, amplitude 0.5
    tone-6ch-44k-16.flac          0.1 s, 44100 Hz 6 channels (FL FR C LFE BL BR) 16-bit: FL 440 Hz,
                                  FR 660 Hz, C 1000 Hz, the others silent (the downmix test)

Usage: python3 tools/cd/make-audio-file-fixtures.py [--out testdata/media/audio]
Needs lame and flac on the PATH (made with LAME 4.0 and flac 1.5.0). The tests do not need
the tools: they read the committed files.
"""

import argparse
import math
import struct
import subprocess
import tempfile
from pathlib import Path


def wav(path, rate, channels, bits, seconds, tones):
    """tones: one (hz, amplitude) per channel, (0, 0) for silence"""
    frames = int(rate * seconds)
    full = (1 << (bits - 1)) - 1
    data = bytearray()
    for n in range(frames):
        for hz, amplitude in tones:
            value = int(round(amplitude * full * math.sin(2 * math.pi * hz * n / rate)))
            data += value.to_bytes(bits // 8, "little", signed=True)
    block = channels * bits // 8
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * block, block, bits)
    header += b"data" + struct.pack("<I", len(data))
    Path(path).write_bytes(header + data)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="testdata/media/audio")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        t = Path(temp)
        wav(t / "a.wav", 44100, 2, 16, 0.5, [(440, 0.5), (660, 0.5)])
        subprocess.run(["lame", "--quiet", "-b", "96", "--cbr", "--resample", "44.1", t / "a.wav", out / "tone-440-660-44k-stereo.mp3"],
                       check=True)
        wav(t / "b.wav", 48000, 1, 16, 0.5, [(1000, 0.5)])
        subprocess.run(["lame", "--quiet", "-b", "64", "--cbr", "--resample", "48", "-m", "m", t / "b.wav",
                        out / "tone-1000-48k-mono.mp3"], check=True)
        wav(t / "c.wav", 44100, 2, 16, 0.25, [(440, 0.5), (660, 0.5)])
        subprocess.run(["flac", "--silent", "--force", "-8", "-o", out / "tone-440-44k-stereo-16.flac", t / "c.wav"], check=True)
        wav(t / "d.wav", 96000, 1, 24, 0.1, [(1000, 0.5)])
        subprocess.run(["flac", "--silent", "--force", "-8", "-o", out / "tone-1000-96k-mono-24.flac", t / "d.wav"], check=True)
        wav(t / "e.wav", 44100, 6, 16, 0.1, [(440, 0.5), (660, 0.5), (1000, 0.5), (0, 0), (0, 0), (0, 0)])
        subprocess.run(["flac", "--silent", "--force", "--channel-map=none", "-8", "-o", out / "tone-6ch-44k-16.flac", t / "e.wav"],
                       check=True)
    for f in sorted(out.iterdir()):
        print(f"{f}: {f.stat().st_size} bytes")


if __name__ == "__main__":
    main()
