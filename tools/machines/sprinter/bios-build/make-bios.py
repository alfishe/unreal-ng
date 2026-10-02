#!/usr/bin/env python3
"""Build a 256 KB Sprinter Sp2000 BIOS image from the community sources (BIOS-TT).

The community BIOS (3.05 and later) is published as sources only:
https://zxgit.org/Tolik-Trek/Sprinter-BIOS (branch `master` = releases and
hotfixes, `beta` = the next version), with the include files in the submodule
https://zxgit.org/Tolik-Trek/Shared_Includes. No ROM images are released, so to
run a build on unreal-ng it has to be assembled here.

What the tool does (docs/inprogress/2026-09-28-sprinter/bios-versions.md §3):
  1. exports the BIOS tree at COMMIT and the Shared_Includes commit that COMMIT's
     gitlink names (both from local clones), into WORKDIR;
  2. assembles bios/BIOS.asm (or src/bios/BIOS.asm in trees before 2026-05) with
     sjasmplus; this writes Build/Bin/ROM.BIN (page 0), EXP.BIN (page 8),
     LOGO.BIN (page 1) and LOADER_K30.BIN (pages #C-#F: loader + bitstream);
  3. lays the 16 pages out as bios/BUILD.a80's ROM_BUILD macro does: page 0 ROM,
     1 logo, 2-4 the ZX ROMs (128, 48, TR-DOS), 5-7 + 9-11 the recovery ROM disk,
     8 EXP, #C-#F the loader with the 1K30 bitstream, #FF filler between.
     (BUILD.a80 itself needs a sjasmplus that lets ORG pass #FFFF without a device.)

Reproducibility: the build stamps its date (and, in a BETA build, the time) into
the screen text. --date / --time fix them (default: COMMIT's author date, 12:00:00),
so the same commit gives the same bytes. Two sjasmplus differences are patched in
the exported copy only: newer versions reject "BLOCK 0,filler" (a zero-length block
emits nothing, so it is guarded with IF), and versions from 1.22 reject the empty
string in TEXT 128,{""," "} used by trees before 2026-06 - use sjasmplus 1.21.1 for
those (for the 2026-09 beta, 1.21.1 and 1.23.1 give identical bytes; the images in
data/rom/sprinter are built with 1.21.1).

Trees before 2026-06-26 do not carry the PLD bitstream binary (it is a MAX+plus II
output); --bitstream supplies one (59 215 bytes, e.g. the stream at #100 of
Build/Bin/LOADER_K30.BIN from commit 4c5d44a, which is what the sources of
2024-08..2026-09 compile to).

Usage:
  make-bios.py --repo ~/emulators/zxgit/Sprinter-BIOS --includes ~/emulators/zxgit/Shared_Includes \
                --commit c14a8c5 --sjasmplus /path/to/sjasmplus --out sp2k-3.06-hf2.rom \
                [--bitstream K30.bin] [--date 2026-05-01] [--time 12:00:00] [--workdir DIR]
"""

import argparse
import hashlib
import io
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zlib
from pathlib import Path

PAGE = 0x4000


def gitArchive(repo: Path, commit: str, dest: Path) -> None:
    data = subprocess.run(["git", "-C", str(repo), "archive", commit], check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(dest)


def gitlink(repo: Path, commit: str, path: str) -> str:
    out = subprocess.run(["git", "-C", str(repo), "ls-tree", commit, path], check=True, capture_output=True, text=True).stdout
    return out.split()[2]


def authorDate(repo: Path, commit: str) -> str:
    return subprocess.run(["git", "-C", str(repo), "log", "-1", "--format=%as", commit], check=True, capture_output=True,
                          text=True).stdout.strip()


def findFile(root: Path, relative: str) -> Path:
    """The sources were written on Windows: match the path case-insensitively"""
    want = relative.lower()
    for path in root.rglob("*"):
        if path.is_file() and path.relative_to(root).as_posix().lower() == want:
            return path
    raise FileNotFoundError(relative)


def layout(work: Path, prefix: str) -> bytes:
    def read(relative: str) -> bytes:
        return findFile(work, relative).read_bytes()

    image = bytearray()

    def put(data: bytes, end: int) -> None:
        image.extend(data)
        if len(image) > end:
            raise ValueError(f"block overflows #{end:05X}")
        image.extend(b"\xff" * (end - len(image)))

    recovery = read(prefix + "bios/shared/RECOVERY.IMG")
    zx = prefix + "ZX_ROMS/new/"
    put(read("Build/Bin/ROM.BIN"), 0x04000)
    put(read("Build/Bin/LOGO.BIN"), 0x08000)
    put(read(zx + "SP_128.bin"), 0x0C000)
    put(read(zx + "SP__48.bin"), 0x10000)
    put(read(zx + "SP_TRDOS.bin"), 0x14000)
    put(recovery[0x0000:0xC000], 0x20000)
    put(read("Build/Bin/EXP.BIN"), 0x24000)
    put(recovery[0xC000:0x18000], 0x30000)
    put(read("Build/Bin/LOADER_K30.BIN"), 0x40000)
    return bytes(image)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", type=Path, required=True, help="clone of zxgit.org/Tolik-Trek/Sprinter-BIOS")
    ap.add_argument("--includes", type=Path, required=True, help="clone of zxgit.org/Tolik-Trek/Shared_Includes")
    ap.add_argument("--commit", required=True)
    ap.add_argument("--sjasmplus", default="sjasmplus")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--bitstream", type=Path, help="1K30 bitstream for trees without Build/ACEX/K30.ACX")
    ap.add_argument("--date", help="build date YYYY-MM-DD (default: the commit's author date)")
    ap.add_argument("--time", default="12:00:00", help="build time of a BETA build")
    ap.add_argument("--workdir", type=Path, help="keep the build tree here (default: a temporary folder)")
    args = ap.parse_args()

    date = args.date or authorDate(args.repo, args.commit)
    work = args.workdir or Path(tempfile.mkdtemp(prefix="sprinter-bios-"))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    gitArchive(args.repo, args.commit, work)
    includes = work / "Shared_Includes"
    includes.mkdir(exist_ok=True)
    gitArchive(args.includes, gitlink(args.repo, args.commit, "Shared_Includes"), includes)

    # sjasmplus >= 1.22: "BLOCK 0,filler" is an error; the block is empty either way
    macros = findFile(includes, "macroses/macros.z80")
    text = macros.read_bytes()
    text = re.sub(rb"(\n)([ \t]+BLOCK[ \t]+endAddr,filler\r?\n)", rb"\1 IF endAddr > 0\n\2 ENDIF\n", text)
    macros.write_bytes(text)

    prefix = "src/" if (work / "src" / "bios").is_dir() else ""
    bios = findFile(work, prefix + "bios/BIOS.asm")
    bios.write_bytes(bios.read_bytes().replace(b'sj.get_define("__DATE__")', b"'\"" + date.encode() + b"\"'"))
    for messages in work.rglob("*"):
        if messages.is_file() and messages.name.lower() == "messages.z80":
            messages.write_bytes(messages.read_bytes().replace(b"__TIME__", b'"' + args.time.encode() + b'"'))

    (work / "Build" / "Bin" / "temp").mkdir(parents=True, exist_ok=True)
    acex = work / "Build" / "ACEX"
    acex.mkdir(parents=True, exist_ok=True)
    if not (acex / "K30.ACX").exists():
        if not args.bitstream:
            print("this tree carries no bitstream (Build/ACEX/K30.ACX): pass --bitstream", file=sys.stderr)
            return 2
        shutil.copy(args.bitstream, acex / "K30.BIN")
        shutil.copy(args.bitstream, acex / "K50.BIN")  # the 1K50 image is assembled too; it is not used here

    result = subprocess.run([args.sjasmplus, "--nologo", "--syntax=w", "--fullpath", "--exp=Build/export.inc", "--lst=Build/BIOS.LST",
                             prefix + "bios/BIOS.asm"], cwd=work, capture_output=True, text=True, errors="replace")
    errors = [line for line in result.stdout.splitlines() + result.stderr.splitlines() if "error" in line.lower()
              and "(0 errors)" not in line and "Errors: 0" not in line]
    if result.returncode != 0 or errors:
        print("\n".join(errors[:20]) or result.stdout[-2000:], file=sys.stderr)
        return 1

    image = layout(work, prefix)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    banner = sorted({m.group().decode("latin-1") for m in re.finditer(rb"Firmware v[0-9.]+( BETA \d+| RC\d)?|Hotfix \d+|Release [0-9.]+",
                                                                         image[:PAGE] + image[8 * PAGE:9 * PAGE])})
    print(f"{args.out}: {len(image)} bytes, CRC32 {zlib.crc32(image) & 0xFFFFFFFF:08x}, "
          f"SHA-256 {hashlib.sha256(image).hexdigest()}")
    print(f"  commit {args.commit}, build date {date}; {', '.join(banner)}")
    if not args.workdir:
        shutil.rmtree(work)
    return 0


if __name__ == "__main__":
    sys.exit(main())
