#!/usr/bin/env python3
"""rasm data helpers: regenerates the fixtures of unreal-asm's rasmdata_test.cpp from the rasm binary.

Each case is a small rasm source (floats, INCBIN transforms, WAV imports, SAVE headers, pokers, colour functions,
BUILDZX snapshots, strings) assembled by rasm; the bytes it wrote, its errors and warnings go into
core/src/3rdparty/unreal-asm/testdata/rasm/data/cases.json, the generated input files into inputs/. The test feeds
the same inputs to the C++ port (src/dialects/rasm/engine/rasmdata.*) and compares.

    python3 tools/verification/unreal-asm/checks/rasmdatacheck.py --rasm <path to rasm>
    python3 tools/verification/unreal-asm/checks/rasmdatacheck.py --rasm <path to rasm> --check   # compare only

Spec: docs/inprogress/2026-10-05-unreal-asm/research-rasm-data.md. Python 3, standard library only.
"""

import argparse
import json
import math
import random
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
DATA = REPO_ROOT / "core/src/3rdparty/unreal-asm/testdata/rasm/data"
WORK = REPO_ROOT / "scratch/rasmdatacheck"


class Rasm:
    def __init__(self, binary):
        self.binary = binary
        self.count = 0

    def run(self, source, files=None, options=(), timeout=10, outputs=("p.bin",)):
        """Assembles source (bytes or text) in a fresh directory; returns dict with rc, outputs, errors, warnings."""
        self.count += 1
        work = WORK / f"case{self.count:04d}"
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        for name, data in (files or {}).items():
            (work / name).write_bytes(data)
        (work / "p.asm").write_bytes(source if isinstance(source, bytes) else source.encode("utf-8"))
        try:
            proc = subprocess.run([self.binary, "p.asm", "-o", "p", *options], cwd=work, capture_output=True,
                                  timeout=timeout)
            rc = proc.returncode
            text = proc.stdout.decode("latin-1") + proc.stderr.decode("latin-1")
        except subprocess.TimeoutExpired:
            return {"hang": True}
        # rasm colours its messages: red errors, yellow warnings (green progress)
        errors, warnings = [], []
        for colour, body in re.findall(r"\x1b\[(\d+)m([^\x1b]*)", text):
            for line in body.splitlines():
                line = re.sub(r"^\[[^\]]*:\d+\] ", "", line.strip())
                line = re.sub(r"^Warning:? -? ?", "", line)
                if not line or re.fullmatch(r"\d+ errors?", line):
                    continue
                if colour == "31":
                    errors.append(line)
                elif colour == "33" and line != "Not a single byte to output":  # the engine's, not the data's
                    warnings.append(line)
        result = {"rc": rc, "errors": errors, "warnings": warnings, "files": {}}
        for name in outputs:
            path = work / name
            result["files"][name] = path.read_bytes() if path.exists() else None
        return result


def random_bytes(seed, count):
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(count))


def hexs(data):
    return data.hex().upper() if data else ""


def fnv1a(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016X}"


def output(data):
    """The bytes rasm wrote: hex, or for more than 512 bytes their length and FNV-1a 64 hash (keeps the fixture small)."""
    data = data or b""
    if len(data) <= 512:
        return {"bytes": hexs(data)}
    return {"length": len(data), "fnv": fnv1a(data)}


def bits(value):
    return struct.pack(">d", value).hex().upper()


# ----------------------------------------------------------------------------------------------------------------------
# 1. Reals
# ----------------------------------------------------------------------------------------------------------------------

def real_hangs(v):
    """rasm's two endless loops (see MakeReal40 in rasmdata.cpp)."""
    v = abs(v)
    if v >= 1.0:
        integer = (1 << 64) - 1 if v >= 2.0 ** 64 else int(v)
        return integer & 0xFFFFFFFF == 0
    if v == 0.0:
        return False
    m = int(v * 4294967296.0 + 0.5)
    if m & 0xFF00000000:
        m = 0xFFFFFFFF
    return m & 0xFFFFFFFF == 0


def real_values():
    rng = random.Random(1234)
    values = []  # (expression, value)
    for p, q in [(0, 1), (1, 1), (1, 2), (-1, 2), (1, 3), (-7, 9), (2, 3), (355, 113), (43375, 1000), (1, 10),
                 (9994433, 10000000), (9994434, 10000000), (-9994433, 10000000), (7, 100000), (1, 1000000),
                 (4294967295, 1), (4294967297, 1), (8589934591, 2), (99999999999, 7), (12345678901234, 1)]:
        values.append((f"{p}/{q}", p / q))
    for k in range(-33, 70, 3):
        values.append((f"pow(2,{k})", 2.0 ** k))
        values.append((f"3/7*pow(2,{k})", (3 / 7) * 2.0 ** k))
        values.append((f"-5/3*pow(2,{k})", (-5 / 3) * 2.0 ** k))
    for _ in range(150):
        p = rng.randint(-10 ** 9, 10 ** 9)
        q = rng.randint(1, 10 ** 6)
        values.append((f"{p}/{q}", p / q))
    for _ in range(50):
        p = rng.randint(1, 2 ** 52)
        q = rng.randint(1, 2 ** 40)
        values.append((f"{p}/{q}", p / q))
    # just above / below the rounding points of the 32-bit mantissa
    for e in range(1, 20):
        base = 2.0 ** e
        for p in (1, 3, 5):
            values.append((f"{2 ** e}+{p}/{2 ** (33 - e)}", base + p / 2 ** (33 - e)))
    return values


def gen_reals(rasm, cases):
    values = [(e, v) for e, v in real_values() if not real_hangs(v)]
    for directive, fmt, width in (("defr", "amsdos", 5), ("deff", "microsoft", 5), ("df24", "float24", 3)):
        src = "".join(f" {directive} {e}\n" for e, _ in values)
        out = rasm.run(src)
        data = out["files"]["p.bin"]
        if out["errors"] or data is None or len(data) != width * len(values):
            sys.exit(f"reals {directive}: unexpected rasm result {out['errors']}")
        cases.append({"kind": "reals", "format": fmt,
                      "values": [[bits(v), hexs(data[i * width:(i + 1) * width])] for i, (_, v) in enumerate(values)]})
    for e, v in (("4294967296", 4294967296.0), ("pow(2,40)", 2.0 ** 40), ("1/pow(2,34)", 2.0 ** -34),
                 ("-1/pow(2,40)", -(2.0 ** -40))):
        for directive, fmt in (("defr", "amsdos"), ("deff", "microsoft")):
            out = rasm.run(f" {directive} {e}\n", timeout=3)
            if not out.get("hang") or not real_hangs(v):
                sys.exit(f"{directive} {e}: expected rasm to hang")
            cases.append({"kind": "real", "format": fmt, "expr": e, "bits": bits(v), "hang": True})


# ----------------------------------------------------------------------------------------------------------------------
# 2. INCBIN
# ----------------------------------------------------------------------------------------------------------------------

def gen_inputs(rasm):
    rng = random.Random(42)
    inputs = {
        "inc256.bin": bytes(range(256)),
        "rnd1000.bin": bytes(rng.randrange(256) for _ in range(1000)),
        "empty.bin": b"",
        "tiles.bin": bytes(rng.randrange(256) for _ in range(8 * 16 * 40)),
        "save512.bin": random_bytes(9, 512),
        "tiles24.bin": bytes((i * 7 + i // 24) & 0xFF for i in range(24 * 8 * 11)),
    }
    # a file with an AMSDOS header, written by rasm itself
    src = " org #4000\n" + "".join(f" defb {b}\n" for b in inputs["rnd1000.bin"][:300]) + \
          " save 'amsdos.bin',#4000,300,AMSDOS\n"
    out = rasm.run(src, outputs=("amsdos.bin",))
    inputs["amsdos.bin"] = out["files"]["amsdos.bin"]
    # the same header with a wrong checksum
    broken = bytearray(inputs["amsdos.bin"])
    broken[67] ^= 1
    inputs["amsdosbad.bin"] = bytes(broken)
    inputs.update(gen_wavs())
    return inputs


INCBIN_CASES = [
    ("inc256.bin", ""), ("inc256.bin", "10"), ("inc256.bin", "10,20"), ("inc256.bin", "-10"), ("inc256.bin", "0,-10"),
    ("inc256.bin", "-20,10"), ("inc256.bin", "100,0"), ("inc256.bin", "256"), ("inc256.bin", "300"),
    ("inc256.bin", "0,300"), ("inc256.bin", "250,10"), ("inc256.bin", "1,1,0"), ("inc256.bin", "0,1,1"),
    ("inc256.bin", "0,-256"), ("inc256.bin", "-300"), ("inc256.bin", "-300,10"), ("inc256.bin", "0,70000"),
    ("inc256.bin", "REVERT"), ("inc256.bin", "10,20,REVERT"), ("inc256.bin", "-16,REVERT"),
    ("inc256.bin", "GTILES,4"), ("inc256.bin", "ITILES,4"), ("inc256.bin", "GTILES,5"), ("inc256.bin", "ITILES,0"),
    ("inc256.bin", "ITILES,2,REVERT"), ("inc256.bin", "0,64,ITILES,1"), ("inc256.bin", "64,128,GTILES,2"),
    ("rnd1000.bin", "0,960,ITILES,3"), ("inc256.bin", "REMAP,4"), ("inc256.bin", "REMAP,3"),
    ("inc256.bin", "REMAP,0"), ("inc256.bin", "REMAP,1"), ("inc256.bin", "REMAP,512"), ("inc256.bin", "16,96,REMAP,8"),
    ("inc256.bin", "VTILES,8"), ("inc256.bin", "VTILES,0"), ("inc256.bin", "VTILES,3"), ("inc256.bin", "VTILES,-2"),
    ("rnd1000.bin", "VTILES,10"), ("rnd1000.bin", "100,500,VTILES,25"), ("inc256.bin", "REPACK"),
    ("inc256.bin", "1,5,REPACK"), ("inc256.bin", "GTILES,4,REMAP,4"), ("inc256.bin", "REMAP,4,REVERT"),
    ("amsdos.bin", ""), ("amsdos.bin", "SKIPHEADER"), ("amsdos.bin", "SKIPHEADER,10,32"),
    ("amsdos.bin", "SKIPHEADER,-10"), ("amsdos.bin", "SKIPHEADER,10,32,GTILES,4"), ("amsdosbad.bin", "SKIPHEADER"),
    ("inc256.bin", "SKIPHEADER"), ("amsdos.bin", "SKIPHEADER,0,0,REVERT"),
    ("tiles.bin", "SLICE,8,16"), ("tiles.bin", "ISLICE,8,16"), ("tiles.bin", "SLICE,8,16,MISALIGNED"),
    ("tiles.bin", "ISLICE,8,12"), ("tiles.bin", "SLICE,0,4"), ("tiles.bin", "SLICE,7,16"), ("tiles.bin", "SLICE,16,8"),
    ("tiles.bin", "ISLICE,8,16,REVERT"), ("tiles.bin", "0,100,SLICE,8,16,REVERT"), ("tiles24.bin", "SLICE,24,8"),
    ("tiles24.bin", "ISLICE,24,8"), ("tiles.bin", "SLICE,256,1"), ("tiles.bin", "SLICE,257,1"),
    ("empty.bin", ""), ("empty.bin", "5"), ("empty.bin", "REVERT"),
]


# offset + size still negative after the offset rule: rasm reads before its file buffer (research-rasm-data.md §2.4)
READS_OUTSIDE = {"-300,10"}


def gen_incbin(rasm, inputs, cases):
    for name, options in INCBIN_CASES:
        for org in ((0, 1) if "SLICE" in options else (0,)):
            src = f" org {org}\n incbin '{name}'{',' + options if options else ''}\n"
            out = rasm.run(src, files={name: inputs[name]})
            data = out["files"]["p.bin"] or b""
            if options in READS_OUTSIDE:
                # rasm copies bytes from before its buffer: different on every run, only the fact is stored
                cases.append({"kind": "incbin", "file": name, "options": options, "org": org, "outside": True})
                continue
            cases.append({"kind": "incbin", "file": name, "options": options, "org": org, **output(data),
                          "errors": out["errors"], "warnings": out["warnings"]})


# ----------------------------------------------------------------------------------------------------------------------
# WAV
# ----------------------------------------------------------------------------------------------------------------------

def wav(samples, rate, channels, bits, fmt=1, riff=b"RIFF", extra_before=b"", trailer=True, fmt_size=16,
        data_size=None):
    frames = b""
    for frame in samples:
        for s in frame:
            if fmt == 3:
                frames += struct.pack(">f" if riff == b"RIFX" else "<f", s)
            elif bits == 8:
                frames += bytes([max(0, min(255, int(round(s * 127)) + 128))])
            else:
                v = max(-(1 << (bits - 1)), min((1 << (bits - 1)) - 1, int(round(s * ((1 << (bits - 1)) - 1)))))
                b = v.to_bytes(4, "little", signed=True)[:bits // 8]
                frames += b[::-1] if riff == b"RIFX" else b
    block = channels * bits // 8
    fmt_chunk = b"fmt " + struct.pack("<IHHIIHH", fmt_size, fmt, channels, rate, rate * block, block, bits)
    fmt_chunk += b"\0" * (fmt_size - 16)
    data_chunk = b"data" + struct.pack("<I", len(frames) if data_size is None else data_size) + frames
    # a zero chunk after the data: rasm's SM2 / SM4 read a few samples past the data chunk
    tail = b"junk" + struct.pack("<I", 32) + b"\0" * 32 if trailer else b""
    body = b"WAVE" + fmt_chunk + extra_before + data_chunk + tail
    return riff + struct.pack("<I", len(body)) + body


def signal(n, channels, seed):
    rng = random.Random(seed)
    out = []
    for i in range(n):
        base = 0.7 * math.sin(i * 0.21) + 0.25 * math.sin(i * 1.7 + seed)
        out.append([max(-1.0, min(1.0, base + rng.uniform(-0.05, 0.05) * (c + 1))) for c in range(channels)])
    return out


def gen_wavs():
    list_chunk = b"LIST" + struct.pack("<I", 10) + b"INFOabcdef"
    return {
        "w8m.wav": wav(signal(301, 1, 1), 15625, 1, 8),
        "w8flat.wav": wav([[0.0]] * 60 + [[0.6]] * 40 + signal(50, 1, 7) + [[0.6]] * 4500 + [[0.0]] * 3, 15600, 1, 8),
        "w16s.wav": wav(signal(300, 2, 2), 22050, 2, 16, extra_before=list_chunk),
        "w24m.wav": wav(signal(257, 1, 3), 15600, 1, 24),
        "wf32.wav": wav(signal(200, 1, 4), 16000, 1, 32, fmt=3),
        "w16x.wav": wav(signal(150, 1, 5), 15600, 1, 16, riff=b"RIFX"),
        "raw.bin": random_bytes(6, 150),
        "wfmt18.wav": wav(signal(20, 1, 1), 15600, 1, 8, fmt_size=18),
        "wadpcm.wav": wav(signal(20, 1, 1), 15600, 1, 8, fmt=2),
        "w32pcm.wav": wav(signal(20, 1, 1), 15600, 1, 32),
        "w12.wav": wav(signal(20, 1, 1), 15600, 1, 16)[:34] + struct.pack("<H", 12) +
                   wav(signal(20, 1, 1), 15600, 1, 16)[36:],
        "wnodata.wav": wav(signal(20, 1, 1), 15600, 1, 8)[:36] + b"abcd" + struct.pack("<I", 4) + b"wxyz" * 3,
        "wshort.wav": wav(signal(20, 1, 1), 15600, 1, 8, data_size=5000),
        "wchan0.wav": wav(signal(20, 1, 1), 15600, 0 or 1, 8)[:22] + b"\0\0" + wav(signal(20, 1, 1), 15600, 1, 8)[24:],
    }


WAV_CASES = [
    ["w8m.wav:SMP"], ["w8m.wav:SMP,1.3"], ["w8m.wav:SMP,0.5"], ["w8m.wav:SM2"], ["w8m.wav:SM2,2"], ["w8m.wav:SM4"],
    ["w8m.wav:DMA"], ["w8m.wav:DMA,DMA_INT"], ["w8m.wav:DMA,DMA_REPEAT,3"], ["w8m.wav:DMA,DMA_CHANNEL_A,DMA_INT"],
    ["w8m.wav:DMA,DMA_CHANNEL_B,DMA_REPEAT,4095"], ["w8m.wav:DMA,DMA_REPEAT,0"], ["w8m.wav:DMA,DMA_REPEAT,4096"],
    ["w8flat.wav:DMA"], ["w8flat.wav:SMP"],
    ["w16s.wav:SMP"], ["w16s.wav:SM2,0.8"], ["w16s.wav:SM4"], ["w16s.wav:DMA"],
    ["w24m.wav:SMP,1.1"], ["w24m.wav:SM2"], ["w24m.wav:DMA,DMA_CHANNEL_C"],
    ["wf32.wav:SMP"], ["wf32.wav:SMP,0.3"], ["wf32.wav:SM4"], ["wf32.wav:DMA"],
    ["w16x.wav:SMP"], ["w16x.wav:DMA"],
    ["raw.bin:SMP"], ["raw.bin:SMP,1.7"], ["raw.bin:DMA"],
    ["wfmt18.wav:SMP"], ["wadpcm.wav:SMP"], ["w32pcm.wav:SMP"], ["w12.wav:SMP"], ["wnodata.wav:SMP"],
    ["wshort.wav:SMP"], ["wchan0.wav:SMP"],
    # the DMA filter keeps its state from one import to the next
    ["w8m.wav:DMA", "w16s.wav:DMA"], ["w8m.wav:DMA", "w8m.wav:SMP", "w8m.wav:DMA"],
]


def gen_wav(rasm, inputs, cases):
    for steps in WAV_CASES:
        src = "".join(f" incbin '{s.split(':')[0]}',{s.split(':')[1]}\n" for s in steps)
        files = {s.split(":")[0]: inputs[s.split(":")[0]] for s in steps}
        out = rasm.run(src, files=files)
        cases.append({"kind": "wav", "steps": steps, **output(out["files"]["p.bin"]),
                      "errors": out["errors"], "warnings": out["warnings"]})


# ----------------------------------------------------------------------------------------------------------------------
# 3. SAVE headers, pokers, colours
# ----------------------------------------------------------------------------------------------------------------------

SAVE_CASES = [
    ("a.bin", "#4000", "300", None, "AMSDOS"), ("b.bin", "#4010", "100", None, "HOBETA"),
    ("c.bin", "#4000", "50", None, ""), ("Run.Bin", "#4000", "300", "#4123", "AMSDOS"),
    ("verylongname.binary", "#4000", "20", None, "AMSDOS"), ("Sub.X", "#4000", "20", None, "AMSDOS"),
    ("nine12345.b", "#4000", "20", None, "HOBETA"), ("e.bin", "#4000", "0", None, "AMSDOS"),
    ("f.bin", "-1", "10", None, ""), ("g.bin", "#FFF0", "#20", None, "HOBETA"), ("h.bin", "#4000", "#C000", None, ""),
    ("ab", "#4000", "256", None, "HOBETA"), ("x.y.z", "#4000", "1", "0", "AMSDOS"),
]


def gen_save(rasm, inputs, cases):
    image = inputs["save512.bin"]
    for name, offset, size, run, fmt in SAVE_CASES:
        src = " org #4000\n" + "".join(f" defb {b}\n" for b in image)
        if run is not None:
            src += f" run {run}\n"
        src += f" save '{name}',{offset},{size}{',' + fmt if fmt else ''}\n"
        out = rasm.run(src, outputs=(name,))
        cases.append({"kind": "save", "name": name, "offset": offset, "size": size, "run": run, "format": fmt,
                      **output(out["files"][name]), "errors": out["errors"],
                      "warnings": out["warnings"]})


POKER_CASES = [
    "summem 0,64", "xormem 0,64", "sum16 0,64", "summem 10,10", "xormem 3,40", "sum16 1,2",
    "ciphermem 0,64", "ciphermem 0,64,0", "ciphermem 0,64,1", "ciphermem 0,64,2", "ciphermem 5,20,2",
    "ciphermem 0,64,3", "ciphermem 7,60,3", "ciphermem 0,64,7", "ciphermem 0,64,4,'Key#1'", "ciphermem 2,50,4,'zz'",
    "ciphermem 0,64,4,'k'",
]


def gen_pokers(rasm, cases):
    data = random_bytes(11, 64)
    for poker in POKER_CASES:
        src = " org 0\n" + "".join(f" defb {b}\n" for b in data) + f" {poker}\n"
        out = rasm.run(src)
        cases.append({"kind": "poker", "poker": poker, "data": hexs(data), "bytes": hexs(out["files"]["p.bin"] or b""),
                      "errors": out["errors"]})


def gen_colours(rasm, cases):
    values = [0, 1, 15, 16, 0x123, 0xFFF, 0x1000, -1, 3.7, -2.5, 255.9, 4096 + 0x5A5, 100000]
    groups = [
        ("s2h_ink", list(range(27)) + [3.9, 26.5]),
        ("h2s_ink", list(range(64)) + [255, 300, -1, 7.8]),
        ("getr", values), ("getv", values), ("getb", values),
        ("setr", [-3, 0, 5, 15, 16, 7.9, -0.5]), ("setv", [-3, 0, 5, 15, 16, 7.9]), ("setb", [-3, 0, 5, 15, 16, 7.9]),
    ]
    for function, args in groups:
        src = "".join(f" defw {function}({a})\n" for a in args)
        out = rasm.run(src)
        data = out["files"]["p.bin"]
        for i, a in enumerate(args):
            cases.append({"kind": "colour", "function": function, "arg": bits(float(a)), "expr": str(a),
                          "value": data[2 * i] | (data[2 * i + 1] << 8)})
    for a in (27, -1):
        out = rasm.run(f" defw s2h_ink({a})\n")
        cases.append({"kind": "colour", "function": "s2h_ink", "arg": bits(float(a)), "expr": str(a),
                      "errors": out["errors"]})


# ----------------------------------------------------------------------------------------------------------------------
# 4. BUILDZX
# ----------------------------------------------------------------------------------------------------------------------

ZX_CASES = [
    {"bankset0": False, "run": 0x8000, "stack": 0x1234,
     "blocks": [[0, 0xC000, "01"], [0, 0, "0708"], [5, 0, "02"], [5, 0x4000, "03"], [2, 0x3FFE, "0909"]]},
    {"bankset0": True, "run": 0x8000, "stack": 0xFFFE, "blocks": [[0, 0x4000, "F3AF"], [0, 0x8000, "C30080"]]},
    {"bankset0": True, "run": 0x6000, "stack": 0x5FFE, "blocks": [[0, 0x0100, "AA"], [0, 0xBFFF, "55"]]},
    {"bankset0": False, "run": 0x1122, "stack": 0x7FF0, "blocks": [[5, 0x3FF0, "AB"], [2, 0x3FF0, "CD"]]},
    {"bankset0": False, "run": 0xBEEF, "stack": 0x3FFF, "blocks": [[7, 0, "77"], [1, 0, "11"], [0, 0x3FF0, "1234"]]},
]


def sparse(data):
    runs, i = [], 0
    while i < len(data):
        if data[i]:
            j = i
            while j < len(data) and data[j]:
                j += 1
            runs.append([i, data[i:j].hex().upper()])
            i = j
        else:
            i += 1
    return runs


def gen_zx(rasm, cases):
    for case in ZX_CASES:
        src = " buildzx\n"
        for bank, at, data in case["blocks"]:
            src += (" bankset 0\n" if case["bankset0"] else f" bank {bank}\n") + f" org #{at:04X}\n"
            src += "".join(f" defb #{b:02X}\n" for b in bytes.fromhex(data))
        src += f" run #{case['run']:04X},#{case['stack']:04X}\n"
        out = rasm.run(src, outputs=("p.sna",))
        sna = out["files"]["p.sna"] or b""
        cases.append({"kind": "zx", **case, "size": len(sna), "sna": sparse(sna), "errors": out["errors"]})


# ----------------------------------------------------------------------------------------------------------------------
# 5. Strings
# ----------------------------------------------------------------------------------------------------------------------

def q(text):
    return text.encode("utf-8").hex().upper()


STRING_CASES = [
    # (rasm options, setup lines, directive line)
    ([], [], "defb 'abc','x\\ty\\n\\r\\0\\e\\a\\b\\v\\f'"),
    ([], [], "defb '\\xFFa','a\\x41\\x0f','\\\\','\\'','\\q\\\"'"),
    ([], [], "defb 'a\\xG1'"), ([], [], "defb '\\x4'"),
    ([], [], "str 'roudou',0+'d','ou'"), ([], [], "str 'roudou','grouik'"), ([], [], "str 'roudou','grouik',45"),
    ([], [], "str 'a\\tb\\x41','\\\\','\\e\\a'"), ([], [], "str 'x\\xZZ'"),
    ([], ["charset 'abcde',0"], "defb 'abcde','\\a\\b','\\\\'"),
    ([], ["charset 97,97+26,0"], "defb 'roua'"), ([], ["charset 97,10"], "defb 'roua'"),
    ([], ["charset 'o',5"], "defb 'roua'"), ([], ["charset 'ou',6"], "defb 'roua'"),
    ([], ["charset 'ABCDEFGHIJKLMNOPQRSTUVWXYZ',\"QWERTYUIOPASDFGHJKLZXCVBNM\""], "defb 'THE QUICK BROWN FOX'"),
    ([], ["charset 'rd',0"], "defb 'rdd','\\r\\d':str 'rdd','\\r\\d'"),
    ([], ["charset 'a\\'b','xyz'"], "defb 'a\\'b'"),
    ([], ["charset 'ab','xyz'"], "defb 'ab'"), ([], ["charset 300,1"], "defb 'a'"),
    ([], ["charset 10,5,1"], "defb 'a'"), ([], ["charset 'a\\\\',40"], "defb 'a\\\\'"),
    ([], ["charset 'abc',1", "charset"], "defb 'abc'"),
    ([], ["charset 'a',200"], "defb 'a'*1,'\\t'*1,'\\q'*1"),
    (["-fq"], ["utf8remap 'é',50"], "defb 'aéb'"),
    (["-fq"], ["utf8remap 'é',50"], "defb 'üx'"),
    (["-fq"], ["utf8remap 'é',50", "utf8remap 'è',51", "utf8remap", "utf8remap 'à',52"], "defb 'àé'"),
    (["-fq"], [], "defb 'aéb'"),
    (["-fq"], ["utf8remap 'é',50"], "str 'aéb'"),
    (["-utf8"], [], "defb 'é è à ù ç ñ Ñ ¿ ¡'"),
    (["-utf8"], [], "defb 'a€b'"),
    (["-utf8"], [], "defb 'çx','¿'"),
    ([], [], "defb 'aéb'"),
    ([], [], "defb 'a\tb'"),
    (["-ass"], [], "defb 'a\\tb\\\\c'"),
    (["-ass"], ["charset 'b',66"], "defb 'abc'"),
    ([], ["struct st", "fld defb 'ab\\x41\\e\\a\\t'", "endstruct"], "struct st inst"),
    ([], ["struct st", "fld defs 6", "endstruct"], "struct st inst,1,'ab\\x41'"),
    ([], ["struct st", "fld defs 4", "endstruct"], "struct st inst,1,'abcdefg'"),
    (["-fq"], ["utf8remap 'é',20", "struct st", "fld defs 5", "endstruct"], "struct st inst,1,'éaéüb'"),
]


def gen_strings(rasm, cases):
    for options, setup, line in STRING_CASES:
        src = "".join(f" {s}\n" for s in setup) + f" {line}\n"
        out = rasm.run(src, options=options)
        cases.append({"kind": "string", "options": options, "setup": setup, "line": line,
                      "bytes": hexs(out["files"]["p.bin"] or b""), "errors": out["errors"],
                      "warnings": out["warnings"]})


# ----------------------------------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rasm", required=True, help="path to the rasm binary")
    parser.add_argument("--check", action="store_true", help="compare with the stored fixtures, write nothing")
    args = parser.parse_args()
    rasm = Rasm(str(Path(args.rasm).resolve()))
    if WORK.exists():
        shutil.rmtree(WORK)

    inputs = gen_inputs(rasm)
    cases = []
    gen_reals(rasm, cases)
    gen_incbin(rasm, inputs, cases)
    gen_wav(rasm, inputs, cases)
    gen_save(rasm, inputs, cases)
    gen_pokers(rasm, cases)
    gen_colours(rasm, cases)
    gen_zx(rasm, cases)
    gen_strings(rasm, cases)
    # one case per line
    text = '{"generator": "tools/verification/unreal-asm/checks/rasmdatacheck.py", "rasm": "v3.3.3", "cases": [\n'
    text += ",\n".join(json.dumps(c, separators=(",", ":"), ensure_ascii=True) for c in cases) + "\n]}\n"

    if args.check:
        same = (DATA / "cases.json").read_text() == text and all(
            (DATA / "inputs" / n).read_bytes() == d for n, d in inputs.items())
        print(f"{len(cases)} cases, {rasm.count} rasm runs: {'same as the fixtures' if same else 'DIFFERENT'}")
        if not same:
            old = (DATA / "cases.json").read_text().splitlines()
            for line, (a, b) in enumerate(zip(old, text.splitlines()), 1):
                if a != b:
                    print(f"first difference, cases.json line {line}:\n  stored: {a[:300]}\n  now:    {b[:300]}")
                    break
        shutil.rmtree(WORK)
        return 0 if same else 1
    (DATA / "inputs").mkdir(parents=True, exist_ok=True)
    for name, data in inputs.items():
        (DATA / "inputs" / name).write_bytes(data)
    (DATA / "cases.json").write_text(text)
    size = sum(p.stat().st_size for p in DATA.rglob("*") if p.is_file())
    print(f"{len(cases)} cases, {rasm.count} rasm runs, fixtures {size // 1024} KB in {DATA.relative_to(REPO_ROOT)}")
    shutil.rmtree(WORK)
    return 0


if __name__ == "__main__":
    sys.exit(main())
