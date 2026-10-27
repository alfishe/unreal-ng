#!/usr/bin/env python3
"""Stage 1: the release disk of The Link with a plain loader (no machine check yet).

    python3 docs/disasm/demo/thelink/stages/1-loader/build.py \
        testdata/machines/pentagon1024sl/TheLink.trd scratch/thelink-release

Takes the demo's binaries from the author's working disk (TheLink.trd), assembles the main
block (thelink_main.asm) and the loader (loader.asm) with sjasmplus, checks the main block
against the bytes ALASM builds at #7800, and writes <out>/TheLink.scl (and TheLink.trd, the
same files on a 640K disk, for tools that want one).

Disk contents, in this order:
  boot     B  10 RANDOMIZE USR VAL "15619": REM : RUN "THELINK"        (autostart, silent)
  THELINK  B  10 CLEAR VAL "24575": RANDOMIZE USR VAL "15619": REM : LOAD "loader" CODE
              20 RANDOMIZE USR VAL "24576"                             (autostart)
  loader   C  #6000  loader.asm
  thelink$ C  #4000  the title picture, shown while the rest loads
  thelink  C  #7800  thelink_main.asm
  ...         #C000  the 28 page files of loader.asm's PageFiles table

sjasmplus: $SJASMPLUS, else `sjasmplus` on the PATH.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# sha256 of #7800..#B154 as ALASM assembles THELINK.asm on the working disk (14677 bytes)
MAIN_SHA256 = "d1df30757f04f5ac302bd62e1c82f6a1bee2f63c95e0238b4454ff1dd302b147"

# Page files, in loader.asm's PageFiles order: (TR-DOS name, type)
PAGE_FILES = [
    ("kishki", "C"), ("baba", "C"), ("baba", "0"), ("mill", "C"), ("mill", "0"), ("girl13", "2"),
    ("blugr", "C"), ("blugr", "0"), ("blugr", "1"), ("blugr", "2"),
    ("ROTPREGS", "C"), ("ROBPREGS", "C"), ("TUNNELGS", "C"), ("ROTATEGS", "C"), ("ROTBARGS", "C"),
    ("MULBARGS", "C"), ("HEDGEGS", "C"), ("BABAGS", "C"), ("TEXGS", "C"),
    ("TUNNELZX", "C"), ("ROTATEZX", "C"), ("ROTBARZX", "C"), ("MULBARZX", "C"), ("HEDGEZX1", "C"),
    ("HEDGEZX2", "C"), ("BABAZX", "C"), ("TEXZX", "C"),
    ("thelinkm", "C"),
]


# ---------------------------------------------------------------------------------------------
# TR-DOS image (.trd): 16 sectors of 256 bytes per track, catalog in track 0 sectors 0-7

def read_trd(path):
    """{(name, type): (start, length, data)} of every file on the disk"""
    with open(path, "rb") as f:
        image = f.read()
    files = {}
    for i in range(128):
        e = image[i * 16:(i + 1) * 16]
        if e[0] == 0:
            break
        if e[0] == 1:  # deleted
            continue
        name = e[0:8].decode("latin-1").rstrip()
        ftype = chr(e[8])
        start = e[9] | e[10] << 8
        length = e[11] | e[12] << 8
        offset = (e[15] * 16 + e[14]) * 256
        files[(name, ftype)] = (start, length, image[offset:offset + e[13] * 256])
    return files


# ---------------------------------------------------------------------------------------------
# BASIC

TOKENS = {"CLEAR": 0xFD, "VAL": 0xB0, "RANDOMIZE": 0xF9, "USR": 0xC0, "REM": 0xEA, "LOAD": 0xEF,
          "CODE": 0xAF, "RUN": 0xF7}


def basic_line(number, parts):
    """parts: keywords (TOKENS) and literal text; numbers are written as VAL "n" by the caller"""
    body = bytearray()
    for p in parts:
        body += bytes([TOKENS[p]]) if p in TOKENS else p.encode("latin-1")
    body.append(0x0D)
    return bytes([number >> 8, number & 0xFF, len(body) & 0xFF, len(body) >> 8]) + bytes(body)


def basic_file(lines, autostart):
    """TR-DOS BASIC file body and its descriptor start/length: the program, then #80 #AA and the
    autostart line (THELINK.B on the working disk is stored the same way)"""
    program = b"".join(lines)
    return program + bytes([0x80, 0xAA, autostart & 0xFF, autostart >> 8]), len(program), len(program)


# ---------------------------------------------------------------------------------------------
# Disk images

def descriptor(name, ftype, start, length, sectors):
    return (name.ljust(8)[:8].encode("latin-1") + ftype.encode("latin-1")
            + bytes([start & 0xFF, start >> 8, length & 0xFF, length >> 8, sectors]))


def write_scl(path, entries):
    """entries: (name, type, start, length, body)"""
    out = bytearray(b"SINCLAIR") + bytes([len(entries)])
    data = bytearray()
    for name, ftype, start, length, body in entries:
        sectors = (len(body) + 255) // 256
        out += descriptor(name, ftype, start, length, sectors)
        data += body.ljust(sectors * 256, b"\0")
    out += data
    out += (sum(out) & 0xFFFFFFFF).to_bytes(4, "little")
    with open(path, "wb") as f:
        f.write(out)


def write_trd(path, entries, label="THE LINK"):
    image = bytearray(160 * 16 * 256)  # 80 tracks, 2 sides
    track, sector, used = 1, 0, 0
    for i, (name, ftype, start, length, body) in enumerate(entries):
        sectors = (len(body) + 255) // 256
        e = descriptor(name, ftype, start, length, sectors) + bytes([sector, track])
        image[i * 16:(i + 1) * 16] = e
        offset = (track * 16 + sector) * 256
        image[offset:offset + len(body)] = body
        used += sectors
        sector += sectors
        track += sector // 16
        sector %= 16
    info = 8 * 256
    image[info + 0xE1] = sector
    image[info + 0xE2] = track
    image[info + 0xE3] = 0x16  # 80 tracks, double sided
    image[info + 0xE4] = len(entries)
    free = 160 * 16 - 16 - used
    image[info + 0xE5] = free & 0xFF
    image[info + 0xE6] = free >> 8
    image[info + 0xE7] = 0x10  # TR-DOS id
    image[info + 0xEA:info + 0xF3] = b" " * 9
    image[info + 0xF5:info + 0xFD] = label.ljust(8)[:8].encode("latin-1")
    with open(path, "wb") as f:
        f.write(image)


# ---------------------------------------------------------------------------------------------

def assemble(sjasmplus, source, workdir):
    r = subprocess.run([sjasmplus, "--nologo", "--fullpath", "--lst=" + os.path.splitext(os.path.basename(source))[0] + ".lst",
                        source], cwd=workdir, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("sjasmplus failed on %s:\n%s%s" % (source, r.stdout, r.stderr))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trd", help="the author's working disk (testdata/machines/pentagon1024sl/TheLink.trd)")
    ap.add_argument("out", help="output folder (scratch/...)")
    args = ap.parse_args()

    sjasmplus = os.environ.get("SJASMPLUS") or shutil.which("sjasmplus")
    if not sjasmplus:
        sys.exit("sjasmplus not found: set SJASMPLUS or put it on the PATH")

    files = read_trd(args.trd)
    work = os.path.join(args.out, "build")
    os.makedirs(work, exist_ok=True)
    for key in [("credits", "C"), ("medieval", "C")]:  # INCBINs of thelink_main.asm
        with open(os.path.join(work, "%s.%s" % key), "wb") as f:
            f.write(files[key][2][:files[key][1]])

    assemble(sjasmplus, os.path.join(HERE, "thelink_main.asm"), work)
    assemble(sjasmplus, os.path.join(HERE, "loader.asm"), work)
    with open(os.path.join(work, "thelink_main.bin"), "rb") as f:
        main_block = f.read()
    with open(os.path.join(work, "loader.bin"), "rb") as f:
        loader = f.read()
    digest = hashlib.sha256(main_block).hexdigest()
    if digest != MAIN_SHA256:
        sys.exit("main block differs from ALASM's (%d bytes, sha256 %s)" % (len(main_block), digest))

    boot, boot_start, boot_len = basic_file(
        [basic_line(10, ["RANDOMIZE", "USR", "VAL", '"15619"', ":", "REM", ":", "RUN", '"THELINK"'])], 10)
    run, run_start, run_len = basic_file(
        [basic_line(10, ["CLEAR", "VAL", '"24575"', ":", "RANDOMIZE", "USR", "VAL", '"15619"', ":", "REM", ":",
                         "LOAD", '"loader"', "CODE"]),
         basic_line(20, ["RANDOMIZE", "USR", "VAL", '"24576"'])], 10)

    entries = [
        ("boot", "B", boot_start, boot_len, boot),
        ("THELINK", "B", run_start, run_len, run),
        ("loader", "C", 0x6000, len(loader), loader),
    ]
    title = files[("thelink$", "C")]
    entries.append(("thelink$", "C", 0x4000, title[1], title[2][:title[1]]))
    entries.append(("thelink", "C", 0x7800, len(main_block), main_block))
    for key in PAGE_FILES:
        start, length, data = files[key]
        entries.append((key[0], key[1], 0xC000, length, data[:length]))

    scl = os.path.join(args.out, "TheLink.scl")
    trd = os.path.join(args.out, "TheLink.trd")
    write_scl(scl, entries)
    write_trd(trd, entries)
    sectors = sum((len(e[4]) + 255) // 256 for e in entries)
    print("main block #7800..#%04X ok (ALASM bytes), loader #6000..#%04X" % (0x7800 + len(main_block) - 1,
                                                                            0x6000 + len(loader) - 1))
    print("%s: %d files, %d sectors" % (scl, len(entries), sectors))
    print(trd)


if __name__ == "__main__":
    main()
