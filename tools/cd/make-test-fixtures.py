#!/usr/bin/env python3
"""Build the CD image test fixtures in testdata/media/cd/ (a data track reference and MAME CD CHDs).

A small mixed-mode disc, 22 frames (0.29 s), laid out to exercise every rule the
CD image layer has:

    track 1  MODE1/2352  track1.bin frames 0-3    LBA 0-3     data frames with valid sync, header, EDC, ECC
    track 2  AUDIO       track2.bin frames 0-11   INDEX 00 at LBA 4 (4 pregap frames of silence stored
                                                  in the file), INDEX 01 at LBA 8, ramp A to LBA 15
    track 3  AUDIO       track3.bin frames 0-3    PREGAP 00:00:02 (2 frames NOT in the file: LBA 16-17),
                                                  INDEX 01 at LBA 18, ramp B to LBA 21
    lead-out LBA 22

One file per track, as Redump-style rips are. (Several tracks in one BIN with an INDEX 00
are tested with sheets the C++ tests write themselves: chdman 0.289 misplaces such a track,
storing it from the start of the BIN.)

The audio is a sawtooth ramp, so every sample is known from its position (the tests
check sample order, byte order and position exactly) and FLAC / LZMA compress it to
almost nothing:

    ramp A: left = (n * 64) & 0xFFFF, right = (-n * 96) & 0xFFFF   (n = sample index in the track)
    ramp B: left = (n * 32 + 1000) & 0xFFFF, right = (n * 48) & 0xFFFF

Files kept in testdata/media/cd/ (the CUE sheet and the other BINs are built in a
temporary folder for chdman; the C++ tests write their own CUE/BIN/WAVE discs):
    track1.bin                      the data track: frames built by this script's own EDC / ECC,
                                    checked against MAME's tables, the reference for cdecc.cpp
    disc-default.chd                chdman createcd, its default codecs (cdlz, cdzl, cdfl): hunks in cdlz and cdfl
    disc-cdzs-cdzl.chd              codecs cdzs, cdzl: hunks in both (chdman 0.289 writes CHDs it cannot read
                                    back itself - "chdman verify": Decompression error - when cdzs or cdzl is
                                    the only codec, so the two share a file)
Every CHD is checked with "chdman verify" after it is written.

Usage: python3 tools/cd/make-test-fixtures.py --chdman <path to chdman>
The data and the CUE sheets are deterministic; the CHDs depend on chdman's version
(made with chdman 0.289).
"""

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT = REPO_ROOT / "testdata" / "media" / "cd"
FRAME = 2352
SAMPLES = 588


# region ECMA-130 EDC / ECC (the same algorithm as core/src/emulator/io/storage/cd/cdecc.cpp)

def _tables():
    forward = [0] * 256
    backward = [0] * 256
    edc = [0] * 256
    for i in range(256):
        j = (i << 1) ^ (0x11D if i & 0x80 else 0)
        forward[i] = j & 0xFF
        backward[i ^ (j & 0xFF)] = i
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ (0xD8018001 if crc & 1 else 0)
        edc[i] = crc
    return forward, backward, edc


FORWARD, BACKWARD, EDC = _tables()


def edc32(data):
    crc = 0
    for b in data:
        crc = (crc >> 8) ^ EDC[(crc ^ b) & 0xFF]
    return crc


def ecc_block(src, major_count, minor_count, major_mult, minor_inc):
    size = major_count * minor_count
    out = bytearray(2 * major_count)
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            value = src[index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= value
            b ^= value
            a = FORWARD[a]
        a = BACKWARD[FORWARD[a] ^ b]
        out[major] = a
        out[major + major_count] = a ^ b
    return out


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def mode1_frame(lba, user):
    frames = lba + 150
    frame = bytearray(FRAME)
    frame[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
    frame[12] = bcd(frames // 4500)
    frame[13] = bcd((frames // 75) % 60)
    frame[14] = bcd(frames % 75)
    frame[15] = 1
    frame[16:2064] = user
    frame[2064:2068] = struct.pack("<I", edc32(frame[0:2064]))
    p = ecc_block(frame[12:], 86, 24, 2, 86)
    frame[0x81C:0x81C + 172] = p
    q = ecc_block(frame[12:], 52, 43, 86, 88)
    frame[0x8C8:0x8C8 + 104] = q
    return bytes(frame)

# endregion


def user_data(lba):
    # A text line per block, repeated: each block differs, every codec squeezes it
    line = f"UNREAL-NG CD TEST DISC, LBA {lba:02d}. ".encode()
    return (line * (2048 // len(line) + 1))[:2048]


def ramp(frames, left_step, right_step, left_base=0):
    out = bytearray()
    for n in range(frames * SAMPLES):
        left = (left_base + n * left_step) & 0xFFFF
        right = (n * right_step) & 0xFFFF
        out += struct.pack("<HH", left, right)
    return bytes(out)


DISC_CUE = """REM unreal-ng CD audio test disc (tools/cd/make-test-fixtures.py)
FILE "track1.bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
FILE "track2.bin" BINARY
  TRACK 02 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:00:04
FILE "track3.bin" BINARY
  TRACK 03 AUDIO
    PREGAP 00:00:02
    INDEX 01 00:00:00
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--chdman", help="MAME's chdman (the CHDs are skipped without it)")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    data = b"".join(mode1_frame(lba, user_data(lba)) for lba in range(4))
    pregap = bytes(4 * FRAME)
    track2 = ramp(8, 64, -96 & 0xFFFF)
    track3 = ramp(4, 32, 48, 1000)
    (OUT / "track1.bin").write_bytes(data)

    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        (work / "track1.bin").write_bytes(data)
        (work / "track2.bin").write_bytes(pregap + track2)
        (work / "track3.bin").write_bytes(track3)
        (work / "disc.cue").write_text(DISC_CUE, newline="\r\n")
        if args.chdman:
            for name, codecs in (("default", None), ("cdzs-cdzl", "cdzs,cdzl")):
                target = OUT / f"disc-{name}.chd"
                target.unlink(missing_ok=True)
                command = [args.chdman, "createcd", "-i", str(work / "disc.cue"), "-o", str(target)]
                if codecs:
                    command += ["-c", codecs]
                subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.run([args.chdman, "verify", "-i", str(target)], check=True, stdout=subprocess.DEVNULL)
    for path in sorted(OUT.iterdir()):
        print(f"{path.name:20} {path.stat().st_size:8}")


if __name__ == "__main__":
    main()
