#!/usr/bin/env python3
"""Build The Link from its sources with sjasmplus: every effect, the main block, the release
loader, and a disk that boots into the demo.

    SJASMPLUS=/path/to/sjasmplus python3 build.py [out]        (out: ./out by default)

Steps, all in <out>/build:
  1. tables.py writes the four tables the author computed in BASIC (sin+, sectg, sin32768, uv256d);
  2. each effect unit (src/GSTUNNE4.asm ...) assembles into its ZX and NeoGS objects
     (TUNNELZX, TUNNELGS, ...), which ALASM's SAVEOBJ4 used to write to disk;
  3. src/THELINK.asm assembles the main block #7800 ("thelink") around those objects;
  4. loader/loader.asm assembles the release loader;
  5. <out>/TheLink.scl and <out>/TheLink.trd are written: boot, THELINK (BASIC), loader, the
     title picture, the main block and the 28 page files the loader puts at #C000 of their pages.

Every object, table and the main block is checked against the bytes on the author's working
disk (sha256 below); a difference stops the build (--no-check builds anyway, for experiments).

sjasmplus: $SJASMPLUS, else `sjasmplus` on the PATH (written against 1.23).
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tables  # noqa: E402

# Effect units, in the demo's order, and the objects each writes
UNITS = [
    ("GSTUNNE4", ["TUNNELZX", "TUNNELGS"]),
    ("GSROTAT7", ["ROTATEZX", "ROTPREGS", "ROTATEGS"]),
    ("GSRBAR24", ["ROTBARZX", "ROBPREGS", "ROTBARGS"]),
    ("GSMULBA3", ["MULBARZX", "MULBARGS"]),
    ("hedge12", ["HEDGEZX1", "HEDGEZX2", "HEDGEGS"]),
    ("baba4", ["BABAZX", "BABAGS"]),
    ("TEX28", ["TEXZX", "TEXGS"]),
]

# sha256 of the files on the author's working disk (TheLink.trd), the ones built here
EXPECTED = {
    "sin+": "e5480080f522f00bbfef4794f1cb9dff01c9cf34718ebfc95c88405662400184",
    "sectg": "2d2af29ff51dcde8370578307bc5ed7f777e61c3c3773b59ff852dce5bcdcaf2",
    "sin32768": "ca9ced7298ceff70ea526364c5672eb6812e1c4009343268d0ff956b76f481d7",
    "uv256d": "b9ff0b9cc69bbf8f91b86b35c12af4db335da0ccbd175de9ecccf34171478a1a",
    "TUNNELZX": "2ffb86c212c51a4d84d55277fdba1cb9312647a2df168c23ce07603a2a3bdf07",
    "TUNNELGS": "ed184d59a9916c57f8d3fc8b28e32b961b386b3149c2aed8eea70208a381926e",
    "ROTATEZX": "07f5698b33cfe39bd9bfb30484b72007598c33e01f327d618841796320e98792",
    "ROTPREGS": "68aec8a29a3296e8d9aebe0f9ab7da25c3322b9a46db0bf2ccfb7b2d75945658",
    "ROTATEGS": "6b7ea53341f3352e34f3deeb53668425a849292927036d6b5310c7f3cddfa0b2",
    "ROTBARZX": "6570b908ee0b55dfda44e5e5399564cbb35c4b46b6f713466c9660ecefcf4acc",
    "ROBPREGS": "cb3044671c5e1e55af806c326a27572dca4d5290b763cce78fb9fbe3427d040d",
    "ROTBARGS": "348a2da9afe17736b0a7b484731a66954db56e3dd34602cfdc3bde4fd27a06c9",
    "MULBARZX": "278dd9c03e3046c2eaabf0e152745c2a0a83e570417091403542af6e2a71c1d1",
    "MULBARGS": "8890df4b71022ea30964449908c0e1028c34bcdd2d25d7bff1541f342ab0847b",
    "HEDGEZX1": "5fcfab06e91245c9ec6fd0c1540ff31fcd0517006089be8d208c978d5a8e8273",
    "HEDGEZX2": "b5a5097734cc04793de492d9b0a6cf76a4dc04b2e8223edaee852122961e0544",
    "HEDGEGS": "c3e6fb279cd7a3beb36ea9a1ada81eed1245e23f46333e0c3ede258d6c0fa2a4",
    "BABAZX": "f258b87c43270ddccaeff6cd6cf5bf22af77b7a09cab13a961d2998d756a949e",
    "BABAGS": "26c0f35ed24bc6547a7b6b337f0c609a7a33b7d399b9d3267be76f2145a34bf4",
    "TEXZX": "4955f4a3147655d9b0a5266f88d27a7a5adca03cb48a3c6fa6620a4a7a617af3",
    "TEXGS": "162533e0b591fa56ad904e100cefc2e28e80fc7415d0b4bde50c145021721e18",
    # #7800..#B154 as ALASM assembles THELINK.asm (14677 bytes)
    "thelink": "d1df30757f04f5ac302bd62e1c82f6a1bee2f63c95e0238b4454ff1dd302b147",
}

MAIN_ORG = 0x7800
MAIN_END = 0xB155   # loader.asm's MAIN_END

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

def assemble(sjasmplus, source, work):
    """sjasmplus in <work>: INCLUDE / INCBIN look in the source's folder, then src/, data/ and the
    built objects; SAVEBIN writes to <work>"""
    name = os.path.splitext(os.path.basename(source))[0]
    command = [sjasmplus, "--nologo", "--fullpath", "-I" + os.path.join(HERE, "src"), "-I" + os.path.join(HERE, "data"),
               "-I" + work, "--lst=" + name + ".lst", source]
    r = subprocess.run(command, cwd=work, capture_output=True, text=True, errors="replace")
    with open(os.path.join(work, name + ".log"), "w") as f:
        f.write(r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit("sjasmplus failed on %s:\n%s%s" % (source, r.stdout, r.stderr))


def read(path):
    with open(path, "rb") as f:
        return f.read()


def check(work, names, enforce):
    bad = []
    for name in names:
        data = read(os.path.join(work, name))
        digest = hashlib.sha256(data).hexdigest()
        if digest != EXPECTED[name]:
            bad.append("%s (%d bytes, sha256 %s)" % (name, len(data), digest))
    if bad and enforce:
        sys.exit("differs from the author's disk: " + ", ".join(bad))
    for b in bad:
        print("warning: differs from the author's disk:", b)
    return not bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default=os.path.join(HERE, "out"), help="output folder (default: ./out)")
    ap.add_argument("--no-check", action="store_true", help="build even when bytes differ from the author's disk")
    args = ap.parse_args()

    sjasmplus = os.environ.get("SJASMPLUS") or shutil.which("sjasmplus")
    if not sjasmplus:
        sys.exit("sjasmplus not found: set SJASMPLUS or put it on the PATH")
    enforce = not args.no_check
    work = os.path.abspath(os.path.join(args.out, "build"))
    os.makedirs(work, exist_ok=True)

    tables.write_all(work)
    check(work, list(tables.TABLES), enforce)
    for unit, objects in UNITS:
        assemble(sjasmplus, os.path.join(HERE, "src", unit + ".asm"), work)
        check(work, objects, enforce)
        print("%-9s -> %s" % (unit, " ".join(objects)))
    assemble(sjasmplus, os.path.join(HERE, "src", "THELINK.asm"), work)
    main_block = read(os.path.join(work, "thelink"))
    check(work, ["thelink"], enforce)
    if MAIN_ORG + len(main_block) != MAIN_END:
        sys.exit("main block ends at #%04X, loader.asm's MAIN_END says #%04X" % (MAIN_ORG + len(main_block), MAIN_END))
    print("THELINK   -> thelink #%04X..#%04X" % (MAIN_ORG, MAIN_END - 1))
    assemble(sjasmplus, os.path.join(HERE, "loader", "loader.asm"), work)
    loader = read(os.path.join(work, "loader.bin"))
    print("loader    -> loader #6000..#%04X" % (0x6000 + len(loader) - 1))

    boot, boot_start, boot_len = basic_file(
        [basic_line(10, ["RANDOMIZE", "USR", "VAL", '"15619"', ":", "REM", ":", "RUN", '"THELINK"'])], 10)
    run, run_start, run_len = basic_file(
        [basic_line(10, ["CLEAR", "VAL", '"24575"', ":", "RANDOMIZE", "USR", "VAL", '"15619"', ":", "REM", ":",
                         "LOAD", '"loader"', "CODE"]),
         basic_line(20, ["RANDOMIZE", "USR", "VAL", '"24576"'])], 10)

    def page_file(name, ftype):
        for folder in (work, os.path.join(HERE, "data")):
            for host in ("%s.%s" % (name, ftype), name if ftype == "C" else None):
                if host and os.path.exists(os.path.join(folder, host)):
                    return read(os.path.join(folder, host))
        sys.exit("no file for %s.%s" % (name, ftype))

    title = read(os.path.join(HERE, "data", "thelink$"))
    entries = [
        ("boot", "B", boot_start, boot_len, boot),
        ("THELINK", "B", run_start, run_len, run),
        ("loader", "C", 0x6000, len(loader), loader),
        ("thelink$", "C", 0x4000, len(title), title),
        ("thelink", "C", MAIN_ORG, len(main_block), main_block),
    ]
    for name, ftype in PAGE_FILES:
        data = page_file(name, ftype)
        entries.append((name, ftype, 0xC000, len(data), data))

    scl = os.path.join(args.out, "TheLink.scl")
    trd = os.path.join(args.out, "TheLink.trd")
    write_scl(scl, entries)
    write_trd(trd, entries)
    sectors = sum((len(e[4]) + 255) // 256 for e in entries)
    print("%s: %d files, %d sectors" % (scl, len(entries), sectors))
    print(trd)


if __name__ == "__main__":
    main()
