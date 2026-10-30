#!/usr/bin/env python3
"""Build a Sprinter DSS EXE that plays one converted track on the Wild Sound card, by relinking
prosiak.exe (Rob F.'s "Boot Loader For Sprinter" with the Wild Sound XM player).

    wslink.py PREFIX -o OUT.EXE [--prosiak PROSIAK.EXE] [--banner TEXT | --keep-banner]
              [--compare-with EXE] [--lzsa LZSA_BINARY]
    wslink.py --unpack EXE OUTDIR

PREFIX names the chunks of one track: PREFIX.trk00.. / PREFIX.ins00.. (wsconv.py) or MasonXM's
PREFIX.xmPatPack00.. / PREFIX.xmInsPack00... The EXE keeps prosiak.exe's primary loader (with the
stage-2 loader VM inside it) and its block 0 (the player) byte for byte; the VM script at #100 and the
packed records of the chunks are new. --banner replaces the line stage 2 prints ("!!! WOW HOW DOW !!!",
19 characters at most; default: the track name); --keep-banner leaves the loader untouched.

Every record is checked after linking: the file is unpacked again (script parse + LZSA2 decode),
the in-place unpacking of the VM is emulated on a 64 KB memory image, and the results are compared
with the chunks. --lzsa also decodes every record with the official lzsa tool (a second check only).
--compare-with decodes the blocks of another EXE and compares them with the new one's.
--unpack writes the blocks and the script of any EXE of this layout to OUTDIR.

Layout and VM: docs/disasm/software/sprinter/prosiak-wildsound/README.md; README.md here.
"""
import argparse
import binascii
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzsa2 import Lzsa2, compress_in_place, literal_block, decode, read_file, write_file  # noqa: E402

PROSIAK_CRC = 0x2114B9B9
PROSIAK_PATHS = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "testdata", "sound",
                 "wildsound", "prosiak.exe"),
    os.path.expanduser("~/Downloads/zx-spectrum/sprinter/peripherals/developer-interest/disk-notes/prosiak.exe"),
]

SCRIPT_AT = 0x100         # the VM script: the file's second 256-byte sector
BLOCK0_AT = 0x180         # prosiak's block 0 offset (kept while the script fits below it)
LOADER_LOAD = 0x4327      # the primary loader's load address
BANNER_ADDR = 0x44D4      # the string stage 2 prints with DSS #5C (PChars)
BANNER_ORIG = b"!!! WOW HOW DOW !!!\r\n\0"
DATA_BASE = 0x4E00        # chunks unpack here (end of the player's area, as prosiak does)
SAVE = 32                 # bytes behind a record that the VM saves; the stream moves up by this much

CALL_INIT = 0x4500        # PlayerInit: probe the card
CALL_TRACK = 0x4755       # LoadTrack: first call reads the u32 total, sends NOP/CLEAR/ERASE/XMAST
CALL_INSHDR = 0x4796      # LoadInsHeaders: XMBST
CALL_SAMPLES = 0x47AB     # LoadSamples: XMBDO
CALL_PLAY = 0x47C0        # Play: DIGPLAY, wait for a key
CALL_EXIT = 0x011C        # VmExit: back to the loader, "Done ..."


# ---------------------------------------------------------------------------------------------
# Reading an EXE of this layout

def parse_script(exe):
    """-> list of ('load', a, de, hl, bc, file_offset) / ('call', addr), until VmExit."""
    ops, i = [], SCRIPT_AT
    while i < len(exe):
        op = exe[i]
        if op == 0x05:
            a, de, hl, bc = struct.unpack_from("<BHHH", exe, i + 1)
            ops.append(("load", a, de, hl, bc, 0x100 * (de + 1) + a))
            i += 8
        elif op == 0x24:
            addr = struct.unpack_from("<H", exe, i + 1)[0]
            ops.append(("call", addr))
            i += 3
            if addr == CALL_EXIT:
                break
        else:
            raise ValueError(f"script op {op:#04x} at file {i:#x}: not 05 / 24")
    return ops


def emulate_unpack(record, hl):
    """The VM's UnpackInPlace (#02EB) on a 64 KB memory image: the record at HL, the 32 bytes behind
    it saved, the stream moved up by 32, LZSA2 forward from there to (record end - unpacked length).
    -> (output address, output bytes). Raises ValueError when the unpacking would corrupt itself or
    touch memory outside the record and the 32 bytes behind it."""
    bc = len(record)
    end = hl + bc
    if hl < 0 or end - 1 + SAVE > 0xFFFF:
        raise ValueError(f"record {hl:#06x}+{bc:#x}: the 32 bytes behind it pass #FFFF (the VM's other path)")
    fill = bytes((i * 7 + 0x5A) & 0xFF for i in range(0x10000))
    mem = bytearray(fill)
    mem[hl:end] = record
    ulen = mem[hl] | mem[hl + 1] << 8
    out = end - ulen
    saved = bytes(mem[end:end + SAVE])
    mem[hl + 2 + SAVE:end + SAVE] = mem[hl + 2:end]             # LDDR: the stream moves up 32 bytes
    d = Lzsa2(mem, hl + 2 + SAVE)
    stop = d.decode(out=mem, dst=out)
    if stop != end:
        raise ValueError(f"in-place unpack ended at {stop:#06x}, expected {end:#06x}")
    if d.pos > end + SAVE:
        raise ValueError("in-place unpack read past the moved stream")
    mem[end:end + SAVE] = saved
    low = min(hl, out)
    if mem[:low] != fill[:low] or mem[end + SAVE:] != fill[end + SAVE:]:
        raise ValueError("in-place unpack wrote outside the record area")
    return out, bytes(mem[out:end])


def unpack_exe(exe):
    """-> dict: header fields, loader, script ops, blocks (list of dicts: offset, hl, bc, out, data, call)."""
    if exe[:3] != b"EXE":
        raise ValueError("not a DSS EXE file")
    loader_off, loader_len = struct.unpack_from("<II", exe, 4)
    load, start, sp = struct.unpack_from("<HHH", exe, 0x10)
    ops = parse_script(exe)
    blocks = []
    for k, op in enumerate(ops):
        if op[0] != "load":
            continue
        _, a, de, hl, bc, off = op
        rec = exe[off:off + bc]
        ulen = rec[0] | rec[1] << 8
        data, used = decode(rec, 2)
        if len(data) != ulen or used != bc - 2:
            raise ValueError(f"record at file {off:#x}: {len(data)} bytes from {used + 2} of {bc}, header says {ulen}")
        out, mdata = emulate_unpack(rec, hl)
        if mdata != data:
            raise ValueError(f"record at file {off:#x}: the in-place unpack differs from a plain decode")
        call = ops[k + 1][1] if k + 1 < len(ops) and ops[k + 1][0] == "call" else None
        blocks.append({"offset": off, "hl": hl, "bc": bc, "out": out, "data": data, "call": call,
                       "record": rec})
    return {"loader_off": loader_off, "loader_len": loader_len, "load": load, "start": start, "sp": sp,
            "loader": exe[loader_off:loader_off + loader_len], "ops": ops, "blocks": blocks}


# ---------------------------------------------------------------------------------------------
# Linking

def find_chunks(prefix):
    """-> (track chunks, instrument chunks) as bytes, in the wsconv or the MasonXM naming."""
    for trk, ins in ((".trk", ".ins"), (".xmPatPack", ".xmInsPack")):
        tc, ic = [], []
        while os.path.exists(f"{prefix}{trk}{len(tc):02d}"):
            tc.append(read_file(f"{prefix}{trk}{len(tc):02d}"))
        while os.path.exists(f"{prefix}{ins}{len(ic):02d}"):
            ic.append(read_file(f"{prefix}{ins}{len(ic):02d}"))
        if tc or ic:
            if not tc or not ic:
                raise ValueError(f"{prefix}: track or instrument chunks missing")
            return tc, ic
    raise ValueError(f"{prefix}: no chunks ({prefix}.trk00 / {prefix}.xmPatPack00)")


def check_chunks(tc, ic):
    if len(tc[0]) < 4 + 292:
        raise ValueError("track chunk 00 is too short")
    total = struct.unpack_from("<I", tc[0], 0)[0]
    have = len(tc[0]) - 4 + sum(len(c) for c in tc[1:])
    if total != have:
        raise ValueError(f"track chunks: the u32 prefix says {total} bytes, the chunks hold {have}")
    for c in tc + ic:
        if len(c) > 0x8004:
            raise ValueError(f"a chunk of {len(c)} bytes: more than #8000 (+4) does not fit the player's buffer")


def make_record(chunk):
    """-> (record bytes, HL): the record ends where the unpacked data ends, at or above #4E00."""
    stream = compress_in_place(chunk, SAVE)
    for attempt in (stream, literal_block(chunk)):
        rec = struct.pack("<H", len(chunk)) + attempt
        end = DATA_BASE + max(len(chunk), len(rec))
        hl = end - len(rec)
        try:
            out, data = emulate_unpack(rec, hl)
        except ValueError:
            continue
        if data == chunk and out >= DATA_BASE:
            return rec, hl
    raise ValueError(f"a chunk of {len(chunk)} bytes cannot be unpacked in place below #10000")


def template_parts(prosiak):
    """prosiak.exe -> (header, block 0 record, block 0 script entry, loader). Checks the layout."""
    info = unpack_exe(prosiak)
    ops = info["ops"]
    if ops[0][0] != "load" or ops[1] != ("call", CALL_INIT):
        raise ValueError("the template's script does not start with block 0 + call #4500")
    b0 = info["blocks"][0]
    if b0["out"] != 0x4500 or len(b0["data"]) != 0x8D5:
        raise ValueError("the template's block 0 is not the player at #4500")
    if info["load"] != LOADER_LOAD:
        raise ValueError("the template's loader does not load at #4327")
    return prosiak[:SCRIPT_AT], b0["record"], b0["hl"], info["loader"]


def patch_banner(loader, text):
    at = BANNER_ADDR - LOADER_LOAD
    if loader[at:at + len(BANNER_ORIG)] != BANNER_ORIG:
        raise ValueError("the loader has no '!!! WOW HOW DOW !!!' line at #44D4; use --keep-banner")
    raw = text.encode("ascii", "replace")
    if len(raw) > len(BANNER_ORIG) - 3:
        raise ValueError(f"banner longer than {len(BANNER_ORIG) - 3} characters")
    if any(b < 0x20 or b > 0x7E for b in raw):
        raise ValueError("banner: printable ASCII only")
    new = (raw + b"\r\n\0").ljust(len(BANNER_ORIG), b"\0")
    return loader[:at] + new + loader[at + len(new):]


def track_name(tc):
    return tc[0][8:28].split(b"\0")[0].decode("latin-1").strip()


def link(prosiak, tc, ic, banner=None):
    """-> (exe bytes, list of report lines). banner None = keep the loader as it is."""
    check_chunks(tc, ic)
    header, rec0, hl0, loader = template_parts(prosiak)
    if banner is not None:
        loader = patch_banner(loader, banner)
    items = [(c, CALL_TRACK) for c in tc] + [(ic[0], CALL_INSHDR)] + [(c, CALL_SAMPLES) for c in ic[1:]]
    records = [make_record(c) for c, _ in items]
    script_len = 8 + 3 + 11 * len(items) + 3 + 3
    pos = max(BLOCK0_AT, SCRIPT_AT + script_len)
    offsets = [pos]
    pos += len(rec0)
    for rec, _ in records:
        offsets.append(pos)
        pos += len(rec)
    loader_off = pos

    def load_op(off, hl, bc):
        return struct.pack("<BBHHH", 0x05, off & 0xFF, (off >> 8) - 1, hl, bc)

    def call_op(addr):
        return struct.pack("<BH", 0x24, addr)

    script = load_op(offsets[0], hl0, len(rec0)) + call_op(CALL_INIT)
    for (rec, hl), (_, call), off in zip(records, items, offsets[1:]):
        script += load_op(off, hl, len(rec)) + call_op(call)
    script += call_op(CALL_PLAY) + call_op(CALL_EXIT)
    assert len(script) == script_len

    exe = bytearray(header)
    struct.pack_into("<I", exe, 4, loader_off)
    exe += script
    exe += bytes(offsets[0] - len(exe))
    exe += rec0
    for rec, _ in records:
        exe += rec
    exe += loader
    report = [f"script {len(script)} bytes at #100, block 0 at {offsets[0]:#x}, loader at {loader_off:#x}, "
              f"{len(exe)} bytes in all"]
    names = [f"trk{i:02d}" for i in range(len(tc))] + [f"ins{i:02d}" for i in range(len(ic))]
    for name, (c, call), (rec, hl), off in zip(names, items, records, offsets[1:]):
        report.append(f"  {name}: {len(c):5d} -> {len(rec):5d} bytes at file {off:#07x}, load {hl:#06x}, "
                      f"unpack to {hl + len(rec) - len(c):#06x}, call {call:#06x}")
    return bytes(exe), report


def verify(exe, prosiak, tc, ic, banner=None, lzsa=None):
    """Unpack the new EXE again and compare everything with what went in. Raises on a mismatch."""
    info = unpack_exe(exe)
    header, rec0, hl0, loader = template_parts(prosiak)
    want_loader = patch_banner(loader, banner) if banner is not None else loader
    if info["loader"] != want_loader:
        raise ValueError("the loader differs from the template's")
    if exe[:SCRIPT_AT] != header[:4] + exe[4:8] + header[8:SCRIPT_AT]:
        raise ValueError("the header differs from the template's beyond the loader offset")
    blocks = info["blocks"]
    want = [(None, CALL_INIT)] + [(c, CALL_TRACK) for c in tc] + [(ic[0], CALL_INSHDR)] + \
           [(c, CALL_SAMPLES) for c in ic[1:]]
    if len(blocks) != len(want):
        raise ValueError(f"{len(blocks)} blocks in the script, {len(want)} expected")
    if blocks[0]["record"] != rec0:
        raise ValueError("block 0 is not the template's record")
    for b, (c, call) in zip(blocks, want):
        if b["call"] != call:
            raise ValueError(f"block at file {b['offset']:#x}: call {b['call']}, expected {call:#06x}")
        if c is not None and b["data"] != c:
            raise ValueError(f"block at file {b['offset']:#x}: the unpacked data differs from the chunk")
    tail = info["ops"][-2:]
    if tail != [("call", CALL_PLAY), ("call", CALL_EXIT)]:
        raise ValueError("the script does not end with Play + VmExit")
    if lzsa:
        with tempfile.TemporaryDirectory() as tmp:
            for k, b in enumerate(blocks):
                src, dst = os.path.join(tmp, f"{k}.lz"), os.path.join(tmp, f"{k}.bin")
                write_file(src, b["record"][2:])
                subprocess.run([lzsa, "-d", "-r", "-f", "2", src, dst], check=True, capture_output=True)
                if read_file(dst) != b["data"]:
                    raise ValueError(f"block {k}: the lzsa tool decodes it differently")
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("prefix", nargs="?", help="chunk prefix (PREFIX.trk00 / PREFIX.xmPatPack00 ...)")
    ap.add_argument("-o", "--output", help="the EXE to write")
    ap.add_argument("--prosiak", help="prosiak.exe (default: testdata/sound/wildsound/ or ~/Downloads/...)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--banner", help="the line stage 2 prints (default: the track name)")
    g.add_argument("--keep-banner", action="store_true", help="leave the loader byte for byte")
    ap.add_argument("--compare-with", metavar="EXE", help="compare the decoded blocks with another EXE's")
    ap.add_argument("--lzsa", help="the official lzsa binary, as a second decoder check")
    ap.add_argument("--unpack", nargs=2, metavar=("EXE", "OUTDIR"), help="unpack an EXE of this layout")
    a = ap.parse_args(argv)

    if a.unpack:
        exe = read_file(a.unpack[0])
        info = unpack_exe(exe)
        os.makedirs(a.unpack[1], exist_ok=True)
        lines = [f"loader at file {info['loader_off']:#x}, {info['loader_len']:#x} bytes"]
        for k, b in enumerate(info["blocks"]):
            write_file(os.path.join(a.unpack[1], f"block{k}.bin"), b["data"])
            call = f"{b['call']:#06x}" if b["call"] is not None else "-"
            lines.append(f"block {k}: file {b['offset']:#07x}, {b['bc']:#06x} bytes to {b['hl']:#06x}, "
                         f"unpacked {len(b['data']):#06x} bytes at {b['out']:#06x}, call {call}")
        write_file(os.path.join(a.unpack[1], "script.txt"), ("\n".join(lines) + "\n").encode())
        print("\n".join(lines))
        return 0

    if not a.prefix or not a.output:
        ap.error("PREFIX and -o OUT.EXE are needed")
    path = a.prosiak or next((p for p in PROSIAK_PATHS if os.path.exists(p)), None)
    if not path:
        ap.error("prosiak.exe not found; give --prosiak")
    prosiak = read_file(path)
    crc = binascii.crc32(prosiak)
    if crc != PROSIAK_CRC:
        print(f"note: {path} has CRC32 {crc:08x}, not the known {PROSIAK_CRC:08x}; the layout is checked anyway")
    tc, ic = find_chunks(a.prefix)
    banner = None
    if not a.keep_banner:
        banner = a.banner if a.banner is not None else track_name(tc)[:len(BANNER_ORIG) - 3] or None
    exe, report = link(prosiak, tc, ic, banner)
    verify(exe, prosiak, tc, ic, banner, a.lzsa)
    with open(a.output, "wb") as f:
        f.write(exe)
    print(f"{a.output}: track '{track_name(tc)}', {len(tc)} track chunk(s), {len(ic)} instrument chunk(s)"
          + (f", banner '{banner}'" if banner else ""))
    print("\n".join(report))
    print("verified: script, every record decoded and unpacked in place (emulated), equal to the chunks"
          + (", lzsa agrees" if a.lzsa else ""))
    if a.compare_with:
        other = unpack_exe(read_file(a.compare_with))
        mine = unpack_exe(exe)
        ob, mb = other["blocks"], mine["blocks"]
        same = [k for k in range(min(len(ob), len(mb))) if ob[k]["data"] == mb[k]["data"]]
        print(f"compared with {a.compare_with}: {len(mb)} / {len(ob)} blocks, equal: {same}")
        if len(same) != len(ob) or len(ob) != len(mb):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
