#!/usr/bin/env python3
"""Write a mixed-mode CD image (CUE + BIN) to try CD audio with: a data track and audio tracks
of sine tones (track N: 330 * (N - 1) Hz on the left, 1.5 times that on the right), each audio
track after a 2-second pregap of silence stored in the BIN (INDEX 00 / INDEX 01).

    python3 tools/cd/make-audio-disc.py scratch/cd --tracks 3 --seconds 20

writes scratch/cd/music.cue and scratch/cd/music.bin. Insert the .cue into a CD drive
(media insert cd scratch/cd/music.cue) and play it with the guest's CD player or with
`cdaudio play track=2`. The same layout as cdtestdisc.h WriteMusicDisc (core tests).
"""

import argparse
import math
import struct
from pathlib import Path

FRAME = 2352
SAMPLES = 588


def tone(frames, hz, amplitude=12000):
    out = bytearray()
    for n in range(frames * SAMPLES):
        t = n / 44100.0
        left = int(amplitude * math.sin(2 * math.pi * hz * t))
        right = int(amplitude * math.sin(2 * math.pi * hz * 1.5 * t))
        out += struct.pack("<hh", left, right)
    return bytes(out)


def data_frame(lba):
    # A plain mode 1 frame: sync, header, user data (no EDC / ECC: the drive does not check them)
    frames = lba + 150
    bcd = lambda v: ((v // 10) << 4) | (v % 10)
    frame = bytearray(FRAME)
    frame[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
    frame[12:16] = bytes([bcd(frames // 4500), bcd((frames // 75) % 60), bcd(frames % 75), 1])
    line = f"UNREAL-NG AUDIO TEST DISC, LBA {lba}. ".encode()
    frame[16:2064] = (line * (2048 // len(line) + 1))[:2048]
    return bytes(frame)


def msf(frames):
    return f"{frames // 4500:02d}:{(frames // 75) % 60:02d}:{frames % 75:02d}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", help="where music.cue and music.bin go")
    parser.add_argument("--tracks", type=int, default=2, help="audio tracks (default 2)")
    parser.add_argument("--seconds", type=int, default=10, help="length of each audio track (default 10)")
    parser.add_argument("--data-frames", type=int, default=300, help="frames of the data track 1 (default 300 = 4 s)")
    args = parser.parse_args()

    folder = Path(args.folder)
    folder.mkdir(parents=True, exist_ok=True)
    cue = ['FILE "music.bin" BINARY', "  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"]
    with open(folder / "music.bin", "wb") as bin_file:
        for lba in range(args.data_frames):
            bin_file.write(data_frame(lba))
        frame = args.data_frames
        for track in range(args.tracks):
            cue += [f"  TRACK {track + 2:02d} AUDIO", f"    INDEX 00 {msf(frame)}", f"    INDEX 01 {msf(frame + 150)}"]
            bin_file.write(bytes(150 * FRAME))
            bin_file.write(tone(args.seconds * 75, 330.0 * (track + 1)))
            frame += 150 + args.seconds * 75
    (folder / "music.cue").write_text("\n".join(cue) + "\n")
    print(f"{folder / 'music.cue'}: track 1 data, tracks 2-{args.tracks + 1} audio, lead-out {msf(frame + 150)}")


if __name__ == "__main__":
    main()
