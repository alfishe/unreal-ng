#!/usr/bin/env python3
"""Build a Sprinter DSS EXE that plays PT3 music on the Wild Sound ISA card's AY chips (wsay.asm).

    wsaybuild.py TRACK.PT3 [SECOND.PT3] -o OUT.EXE [--single] [--sjasmplus PATH] [--keep DIR]

One module plays on chip 0. Two modules play as TurboSound, the first on chip 0 and the second on chip 1:
either two files, or one TurboSound PT3 (two PT3 modules back to back, usually with Vortex Tracker's 16-byte
"PT3!" size1 "PT3!" size2 "02TS" footer; without the footer the second "ProTracker 3." / "Vortex Tracker II"
signature splits the file). --single plays only the first module of such a file.

The assembler is sjasmplus 1.20+ (--sjasmplus, $SJASMPLUS, or on PATH). The EXE is the 22-byte DSS header
("EXE", 0; +4 code offset #16; +#10 load, +#12 start, +#14 SP) and the code from #8100; DSS loads at
most #7F00 bytes there, so the player and the module(s) must fit in 32 512 bytes.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LOAD = 0x8100
HEADER_SIZE = 0x16
MAX_CODE = 0x10000 - LOAD
SIGNATURE = re.compile(rb"ProTracker 3\.|Vortex Tracker II")


def split_turbosound(data):
    """(first, second or None) of a PT3 file: the 02TS footer's sizes, else the second signature."""
    if len(data) > 16 and data[-4:] == b"02TS" and data[-16:-12] == b"PT3!" and data[-10:-6] == b"PT3!":
        size1, = struct.unpack("<H", data[-12:-10])
        size2, = struct.unpack("<H", data[-6:-4])
        if size1 + size2 + 16 == len(data):
            return data[:size1], data[size1:size1 + size2]
    starts = [m.start() for m in SIGNATURE.finditer(data)]
    if not starts or starts[0] != 0:
        raise SystemExit("not a PT3 module (no \"ProTracker 3.\" / \"Vortex Tracker II\" at offset 0)")
    if len(starts) > 1:
        return data[:starts[1]], data[starts[1]:]
    return data, None


def title(module):
    """'<title> by <author>' from the PT3 header (offsets 30..97)"""
    t = module[30:62].decode("cp866", "replace").strip()
    a = module[66:98].decode("cp866", "replace").strip()
    return f"{t} by {a}" if a else t


def find_sjasmplus(given):
    for cand in (given, os.environ.get("SJASMPLUS"), shutil.which("sjasmplus")):
        if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    raise SystemExit("sjasmplus not found: give --sjasmplus PATH or set SJASMPLUS")


def build(modules, out, sjasmplus, keep=None):
    work = os.path.abspath(keep) if keep else tempfile.mkdtemp(prefix="wsay-")
    os.makedirs(work, exist_ok=True)
    try:
        lines = []
        for i, mod in enumerate(modules, 1):
            path = os.path.join(work, f"mod{i}.pt3")
            with open(path, "wb") as f:
                f.write(mod)
            lines += [f"Mod{i}:", f'\tINCBIN "{path}"']
        with open(os.path.join(work, "wsay_mods.inc"), "w") as f:
            f.write("; written by wsaybuild.py\n" + "\n".join(lines) + "\n")
        raw, sym = os.path.join(work, "wsay.bin"), os.path.join(work, "wsay.sym")
        cmd = [sjasmplus, "--nologo", "--msg=war", f"--raw={raw}", f"--sym={sym}", f"-I{work}"]
        if len(modules) == 2:
            cmd.append("-DMOD2")
        cmd.append(os.path.join(HERE, "wsay.asm"))
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or res.stdout.strip() or res.stderr.strip():
            sys.stderr.write(res.stdout + res.stderr)
            if res.returncode != 0:
                raise SystemExit("sjasmplus failed")
        with open(raw, "rb") as f:
            code = f.read()
        if len(code) > MAX_CODE:
            raise SystemExit(f"too big: {len(code)} bytes of code and modules, DSS loads at most {MAX_CODE}")
        symbols = {}
        with open(sym) as f:
            for line in f:
                m = re.match(r"(\S+): EQU 0x([0-9A-Fa-f]+)", line)
                if m:
                    symbols[m.group(1)] = int(m.group(2), 16)
        sp = symbols["StackTop"]
        header = b"EXE\0" + struct.pack("<I", HEADER_SIZE) + bytes(8) + struct.pack("<HHH", LOAD, symbols["Start"], sp)
        assert len(header) == HEADER_SIZE
        with open(out, "wb") as f:
            f.write(header + code)
        return len(header) + len(code), symbols
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("tracks", nargs="+", help="one PT3 (TurboSound when it holds two modules) or two PT3 files")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--single", action="store_true", help="only the first module of a TurboSound file")
    ap.add_argument("--sjasmplus")
    ap.add_argument("--keep", help="keep the work files (wsay_mods.inc, wsay.bin, wsay.sym) in this folder")
    args = ap.parse_args()
    if len(args.tracks) > 2:
        ap.error("one or two PT3 files")
    datas = []
    for path in args.tracks:
        with open(path, "rb") as f:
            datas.append(f.read())
    if len(datas) == 2:
        modules = [split_turbosound(d)[0] for d in datas]
    else:
        first, second = split_turbosound(datas[0])
        modules = [first] if second is None or args.single else [first, second]
    size, symbols = build(modules, args.output, find_sjasmplus(args.sjasmplus), args.keep)
    for i, mod in enumerate(modules):
        print(f"AY {i}: {title(mod)} ({len(mod)} bytes)")
    print(f"{args.output}: {size} bytes ({'TurboSound' if len(modules) == 2 else 'one chip'}), "
          f"code #{LOAD:04X}-#{symbols['CodeEnd'] - 1:04X}, modules up to #{LOAD + size - HEADER_SIZE - 1:04X}")


if __name__ == "__main__":
    main()
