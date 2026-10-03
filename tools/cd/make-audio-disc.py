#!/usr/bin/env python3
"""Write a CD image (CUE + one BIN) with audio tracks of sine tones to try CD audio with.

Two layouts:

  enhanced (default)  An Enhanced CD / CD-Extra (Blue Book): session 1 holds the audio tracks
                      1..N, session 2 one CD-ROM XA mode 2 form 1 data track N+1. Audio players
                      see only session 1; computer drives read both. The CUE marks the sessions
                      with "REM SESSION nn" (the cdrwin / IsoBuster / Redump extension MAME reads);
                      the lead-out of session 1 (1:30) and the lead-in of session 2 (1:00) are not
                      in the BIN: a reader puts them back (11250 frames), then the data track's
                      2-second pregap (stored) and its data.
  mixed               The older mixed-mode layout (Yellow Book): data track 1 (mode 1) first, then
                      the audio tracks 2..N+1, one session.

Audio track k plays 330 * k Hz on the left and 1.5 times that on the right. Every audio track
after the first has a 2-second pregap of silence stored in the BIN (INDEX 00 / INDEX 01), as a
disc burned track-at-once has; track 1's pregap is the lead-in (not stored). Data frames are whole
2352-byte frames with sync, header, EDC and ECC (ECMA-130); their user data is a text line naming
the LBA.

    python3 tools/cd/make-audio-disc.py scratch/cd --tracks 3 --seconds 20
    python3 tools/cd/make-audio-disc.py scratch/cd-mixed --layout mixed

writes <folder>/music.cue and <folder>/music.bin. Insert the .cue into a CD drive
(media insert cd scratch/cd/music.cue) and play it with the guest's CD player or with
`cdaudio play track=1`. The same layouts as cdtestdisc.h WriteMusicDisc (core tests).
"""

import argparse
import math
import struct
from pathlib import Path

FRAME = 2352
SAMPLES = 588
FIRST_LEAD_OUT = 6750  # session 1 lead-out, 1:30 (cdrecord README.multi)
LEAD_IN = 4500         # the next session's lead-in, 1:00
PREGAP = 150           # 2 seconds


def tone(frames, hz, amplitude=12000):
    out = bytearray()
    for n in range(frames * SAMPLES):
        t = n / 44100.0
        left = int(amplitude * math.sin(2 * math.pi * hz * t))
        right = int(amplitude * math.sin(2 * math.pi * hz * 1.5 * t))
        out += struct.pack("<hh", left, right)
    return bytes(out)


# region ECMA-130 EDC / ECC (the same tables as core/src/emulator/io/storage/cd/cdecc.cpp)
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


FORWARD, BACKWARD, EDC_TABLE = _tables()


def edc(data):
    value = 0
    for byte in data:
        value = (value >> 8) ^ EDC_TABLE[(value ^ byte) & 0xFF]
    return value


def _parity_block(src, major_count, minor_count, major_mult, minor_inc, dest, dest_at):
    size = major_count * minor_count
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
        dest[dest_at + major] = a
        dest[dest_at + major + major_count] = a ^ b


def add_ecc(frame):
    work = bytearray(frame)
    if frame[15] == 2:
        work[12:16] = b"\x00\x00\x00\x00"  # mode 2: the parity counts the address as zero
    body = memoryview(work)[12:]
    _parity_block(body, 86, 24, 2, 86, work, 0x81C)
    _parity_block(memoryview(work)[12:], 52, 43, 86, 88, work, 0x8C8)
    frame[0x81C:FRAME] = work[0x81C:FRAME]
# endregion


def bcd(value):
    return ((value // 10) << 4) | (value % 10)


def data_frame(lba, mode):
    frames = lba + 150
    frame = bytearray(FRAME)
    frame[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
    frame[12:16] = bytes([bcd(frames // 4500), bcd((frames // 75) % 60), bcd(frames % 75), mode])
    line = f"UNREAL-NG AUDIO TEST DISC, LBA {lba}. ".encode()
    user = (line * (2048 // len(line) + 1))[:2048]
    if mode == 1:
        frame[16:2064] = user
        frame[2064:2068] = struct.pack("<I", edc(frame[0:2064]))
    else:
        # XA mode 2 form 1: subheader (file 0, channel 0, submode 08h data, coding 0) twice
        frame[16:24] = bytes([0, 0, 0x08, 0, 0, 0, 0x08, 0])
        frame[24:2072] = user
        frame[2072:2076] = struct.pack("<I", edc(frame[16:2072]))
    add_ecc(frame)
    return bytes(frame)


def msf(frames):
    return f"{frames // 4500:02d}:{(frames // 75) % 60:02d}:{frames % 75:02d}"


def write_enhanced(folder, tracks, seconds, data_frames):
    """Session 1: audio 1..N; session 2: data N+1 (mode 2 form 1)"""
    cue = ['REM SESSION 01', 'FILE "music.bin" BINARY']
    file_frame = 0
    with open(folder / "music.bin", "wb") as bin_file:
        for track in range(tracks):
            cue.append(f"  TRACK {track + 1:02d} AUDIO")
            if track:
                cue.append(f"    INDEX 00 {msf(file_frame)}")
                bin_file.write(bytes(PREGAP * FRAME))
                file_frame += PREGAP
            cue.append(f"    INDEX 01 {msf(file_frame)}")
            bin_file.write(tone(seconds * 75, 330.0 * (track + 1)))
            file_frame += seconds * 75
        lead_out = file_frame  # LBA of session 1's lead-out (track 1's INDEX 01 is LBA 0)
        pregap_lba = lead_out + FIRST_LEAD_OUT + LEAD_IN
        cue += ["REM SESSION 02", f"  TRACK {tracks + 1:02d} MODE2/2352", f"    INDEX 00 {msf(file_frame)}",
                f"    INDEX 01 {msf(file_frame + PREGAP)}"]
        for i in range(PREGAP + data_frames):
            bin_file.write(data_frame(pregap_lba + i, 2))
    (folder / "music.cue").write_text("\n".join(cue) + "\n")
    data_lba = pregap_lba + PREGAP
    print(f"{folder / 'music.cue'}: Enhanced CD, session 1 audio tracks 1-{tracks} (lead-out LBA {lead_out}, "
          f"{msf(lead_out + 150)}), session 2 data track {tracks + 1} at LBA {data_lba} ({msf(data_lba + 150)}), "
          f"lead-out LBA {data_lba + data_frames}")


def write_mixed(folder, tracks, seconds, data_frames):
    """Track 1 data (mode 1), tracks 2..N+1 audio, one session"""
    cue = ['FILE "music.bin" BINARY', "  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"]
    with open(folder / "music.bin", "wb") as bin_file:
        for lba in range(data_frames):
            bin_file.write(data_frame(lba, 1))
        frame = data_frames
        for track in range(tracks):
            cue += [f"  TRACK {track + 2:02d} AUDIO", f"    INDEX 00 {msf(frame)}", f"    INDEX 01 {msf(frame + PREGAP)}"]
            bin_file.write(bytes(PREGAP * FRAME))
            bin_file.write(tone(seconds * 75, 330.0 * (track + 1)))
            frame += PREGAP + seconds * 75
    (folder / "music.cue").write_text("\n".join(cue) + "\n")
    print(f"{folder / 'music.cue'}: mixed mode, track 1 data, tracks 2-{tracks + 1} audio, lead-out {msf(frame + 150)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", help="where music.cue and music.bin go")
    parser.add_argument("--layout", choices=["enhanced", "mixed"], default="enhanced",
                        help="enhanced: audio first, data in session 2 (default); mixed: data track 1, then audio")
    parser.add_argument("--tracks", type=int, default=2, help="audio tracks (default 2)")
    parser.add_argument("--seconds", type=int, default=10, help="length of each audio track (default 10, at least 4)")
    parser.add_argument("--data-frames", type=int, default=300,
                        help="frames of the data track after its pregap (default 300 = 4 s, at least 300)")
    args = parser.parse_args()
    if not 1 <= args.tracks <= 98:
        parser.error("--tracks: 1-98 (99 tracks with the data track)")
    if args.seconds < 4 or args.data_frames < 300:
        parser.error("a track lasts at least 4 seconds (300 frames, Red Book)")

    folder = Path(args.folder)
    folder.mkdir(parents=True, exist_ok=True)
    if args.layout == "enhanced":
        write_enhanced(folder, args.tracks, args.seconds, args.data_frames)
    else:
        write_mixed(folder, args.tracks, args.seconds, args.data_frames)


if __name__ == "__main__":
    main()
