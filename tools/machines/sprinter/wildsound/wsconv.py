#!/usr/bin/env python3
"""Convert an XM or S3M module to the Wild Sound card's upload chunks (the MasonXM layout).

    wsconv.py MODULE [-o OUTDIR] [--base NAME] [--title TEXT] [--masonxm] [--quiet]
    wsconv.py --check-against MUSICDIR [-o OUTDIR]

Writes OUTDIR/NAME.trk00, .trk01, ... (the track pack in 32 KB chunks, chunk 00 starts with the
u32 total pack size), OUTDIR/NAME.ins00 (the instrument table, 21-byte records) and OUTDIR/NAME.ins01,
.ins02, ... (the 8-bit unsigned sample stream in 32 KB chunks). NAME defaults to the module's file
name without the extension, OUTDIR to the current directory. --title sets the 20-byte track name
(MasonXM's second argument), else the module's song name is used.

--masonxm reproduces MasonXM's output byte for byte, bugs included (bytewise delta decoding of
16-bit samples, the raw sample type byte, used-but-empty patterns left out, ping-pong loops
unrolled its way); it exists for the comparison and is not meant for real use.

--check-against MUSICDIR converts the author's tracks that have MasonXM outputs in MUSICDIR
(MasonXM/Music of github.com/RobusAsmCoder/Wild-Sound) both ways and explains every byte in which
the normal output differs from MasonXM's.

Formats: docs/inprogress/2026-10-09-wildsound/reference-firmware.md section C; README.md here.
Python 3, standard library only.
"""
import argparse
import math
import os
import struct
import sys


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)


CHUNK = 32768             # bytes per uploaded chunk (MasonXM: 32768; the chunk 00 prefix counts in)
HEADER = 292              # tPatternsPacked up to PatREC
PATREC = 10               # {u32 OFS, u32 SIZE, u16 Rows}
INSREC = 21               # tInstrumentRec
STORAGE = 999424          # the card's flash storage for one track
MAX_INSTRUMENTS = 61      # 64 descriptors - track - table - blank instrument 0
MAX_CHANNELS = 32

XM_ID = b"Extended Module: "


class Sample:
    """One sample, as the source module stores it."""

    def __init__(self):
        self.length = 0           # in bytes of the source data (XM) or in frames (S3M, already 8-bit)
        self.loop_start = 0
        self.loop_len = 0
        self.volume = 64
        self.finetune = 0
        self.type = 0             # XM type byte: bits 0-1 loop (0 none, 1 forward, 2 ping-pong), bit 4 16-bit
        self.panning = 0x80
        self.relnote = 0
        self.raw = b""            # XM: delta-coded bytes; S3M: signed 8-bit PCM
        self.delta = True         # XM delta coding (S3M samples are decoded already)


class Instrument:
    def __init__(self, name=b""):
        self.name = name
        self.samples = []


class Module:
    def __init__(self):
        self.kind = "XM"
        self.name = bytes(20)     # 20 bytes as stored
        self.channels = 0
        self.speed = 6
        self.bpm = 125
        self.flags = 0            # XM flags: bit 0 = linear frequency table
        self.song_len = 0
        self.restart = 0
        self.order_table = bytes(256)
        self.patterns = []        # index -> (rows, XM-packed bytes)
        self.instruments = []
        self.warnings = []


def warn(mod, text):
    mod.warnings.append(text)


# ---------------------------------------------------------------------------------------------
# XM packing

def unpack_cells(packed, count):
    """XM packed pattern data -> list of [note, ins, vol, eff, par]; also returns bytes used."""
    cells, p = [], 0
    while len(cells) < count and p < len(packed):
        b = packed[p]
        p += 1
        if b & 0x80:
            cell = []
            for bit in range(5):
                if b & (1 << bit):
                    cell.append(packed[p] if p < len(packed) else 0)
                    p += 1
                else:
                    cell.append(0)
        else:
            cell = [b] + list(packed[p:p + 4])
            cell += [0] * (5 - len(cell))
            p += 4
        cells.append(cell)
    return cells, p


def pack_cells(cells):
    """[note, ins, vol, eff, par] cells -> XM packed bytes (an empty cell is the single byte 0x80)."""
    out = bytearray()
    for cell in cells:
        flags = 0
        for bit, v in enumerate(cell):
            if v:
                flags |= 1 << bit
        if flags == 0x1F and cell[0] < 0x80:
            out += bytes(cell)
        else:
            out.append(0x80 | flags)
            out += bytes(v for v in cell if v)
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# XM reader

def parse_xm(data):
    if data[:17] != XM_ID:
        raise ValueError("not an XM file (no 'Extended Module: ' signature)")
    mod = Module()
    mod.kind = "XM"
    mod.name = bytes(data[17:37])
    version = struct.unpack_from("<H", data, 58)[0]
    if version < 0x0104:
        warn(mod, f"XM version {version:#06x}: only 0x0104 is supported; the result may be wrong")
    hdr_size = struct.unpack_from("<I", data, 60)[0]
    (mod.song_len, mod.restart, mod.channels, npat, nins, mod.flags, mod.speed,
     mod.bpm) = struct.unpack_from("<8H", data, 64)
    mod.order_table = bytes(data[80:80 + 256]).ljust(256, b"\0")
    pos = 60 + hdr_size
    with_env = []
    for n in range(npat):
        hl = struct.unpack_from("<I", data, pos)[0]
        rows, psize = struct.unpack_from("<HH", data, pos + 5)
        mod.patterns.append((rows, bytes(data[pos + hl:pos + hl + psize])))
        pos += hl + psize
    for n in range(nins):
        ihs = struct.unpack_from("<I", data, pos)[0]
        ins = Instrument(bytes(data[pos + 4:pos + 26]))
        nsmp = struct.unpack_from("<H", data, pos + 27)[0]
        shs = 40
        if nsmp:
            shs = struct.unpack_from("<I", data, pos + 29)[0]
            vol_type, pan_type = data[pos + 233], data[pos + 234]
            if (vol_type & 1) or (pan_type & 1):
                with_env.append(n + 1)
            if nsmp > 1:
                warn(mod, f"instrument {n + 1}: {nsmp} samples, only sample 0 is used (format limit)")
        pos += ihs
        for m in range(nsmp):
            s = Sample()
            (s.length, s.loop_start, s.loop_len, s.volume, s.finetune, s.type, s.panning,
             s.relnote) = struct.unpack_from("<IIIBbBBb", data, pos + m * shs)
            ins.samples.append(s)
        pos += nsmp * shs
        for s in ins.samples:
            s.raw = bytes(data[pos:pos + s.length])
            if len(s.raw) < s.length:
                warn(mod, f"instrument {n + 1}: sample data truncated ({len(s.raw)} of {s.length} bytes)")
                s.raw = s.raw.ljust(s.length, b"\0")
            pos += s.length
        mod.instruments.append(ins)
    if with_env:
        warn(mod, "envelopes ignored (the card has none) on instrument(s) " + ", ".join(map(str, with_env)))
    return mod


# ---------------------------------------------------------------------------------------------
# S3M reader

S3M_NOTE_CUT = 254


def c2spd_to_tune(c2spd):
    """S3M C-4 speed -> XM (relative note, finetune in 1/128 semitone)."""
    if c2spd <= 0:
        c2spd = 8363
    semis = 12.0 * math.log2(c2spd / 8363.0)
    rel = int(round(semis))
    fine = int(round((semis - rel) * 128))
    if fine >= 128:
        rel, fine = rel + 1, fine - 128
    rel = max(-96, min(95, rel))
    return rel, max(-128, min(127, fine))


def s3m_volslide(info):
    """S3M Dxy -> ('A', param) for normal slides or ('EA'/'EB', x) for fine slides."""
    hi, lo = info >> 4, info & 0x0F
    if lo == 0x0F and hi:
        return "EA", hi
    if hi == 0x0F and lo:
        return "EB", lo
    if hi and lo:
        return "A?", lo            # both nibbles: ST3 slides down
    return "A", info


def s3m_effect(cmd, info, order_map, dropped):
    """One S3M effect -> XM (effect, param) or None (dropped, counted in `dropped`)."""
    letter = chr(ord("A") + cmd - 1) if 1 <= cmd <= 26 else "?"
    hi, lo = info >> 4, info & 0x0F

    def count(key):
        dropped[key] = dropped.get(key, 0) + 1

    def drop(why):
        count(f"{letter}{why}")
        return None

    if letter == "A":
        if info == 0:
            return drop("00 (speed 0)")
        return 0x0F, min(info, 0x1F)
    if letter == "B":
        if info not in order_map:
            return drop("xx (jump past the order list)")
        return 0x0B, order_map[info]
    if letter == "C":
        row = hi * 10 + lo
        if row > 63:
            row = 0
        return 0x0D, (row // 10) << 4 | row % 10
    if letter == "D":
        kind, v = s3m_volslide(info)
        if kind == "EA":
            return 0x0E, 0xA0 | v
        if kind == "EB":
            return 0x0E, 0xB0 | v
        if kind == "A?":
            count("Dxy (both nibbles set, kept as slide down)")
        return 0x0A, v
    if letter in "EF":
        up = letter == "F"
        if hi == 0x0F:
            return 0x0E, (0x10 if up else 0x20) | lo          # fine porta
        if hi == 0x0E:
            return 0x21, (0x10 if up else 0x20) | lo          # extra fine porta (XM X1x / X2x)
        return (0x01 if up else 0x02), info
    if letter == "G":
        return 0x03, info
    if letter == "H":
        return 0x04, info
    if letter == "I":
        return 0x1D, info                                     # XM T: tremor
    if letter == "J":
        if info == 0:
            return drop("00 (arpeggio memory)")
        return 0x00, info
    if letter in "KL":
        kind, v = s3m_volslide(info)
        if kind in ("EA", "EB"):
            count(f"{letter}xy fine slide (kept as {letter} without slide)")
            v = 0
        return (0x06 if letter == "K" else 0x05), v
    if letter == "O":
        return 0x09, info
    if letter == "Q":
        return 0x1B, info                                     # XM R: retrigger + volume
    if letter == "R":
        return 0x07, info
    if letter == "S":
        sub = {0x1: 0x30, 0x2: 0x50, 0x3: 0x40, 0x4: 0x70, 0xB: 0x60, 0xC: 0xC0, 0xD: 0xD0, 0xE: 0xE0}
        if hi in sub:
            return 0x0E, sub[hi] | lo
        if hi == 0x8:
            return 0x08, lo * 17
        return drop(f"{hi:X}x")
    if letter == "T":
        if info < 0x20:
            return drop("xx below 20 (tempo slide)")
        return 0x0F, info
    if letter == "V":
        return 0x10, min(info, 64)                            # XM G: global volume
    if letter == "X":
        if info <= 0x80:
            return 0x08, min(info * 2, 255)
        return drop("xx (surround or out of range)")
    return drop("xx")


def parse_s3m(data):
    if data[0x2C:0x30] != b"SCRM":
        raise ValueError("not an S3M file (no 'SCRM' signature)")
    mod = Module()
    mod.kind = "S3M"
    title = bytes(data[0:28]).split(b"\0")[0]
    mod.name = title[:20].ljust(20, b"\0")
    ordnum, insnum, patnum, flags, cwtv, ffi = struct.unpack_from("<6H", data, 0x20)
    gvol, speed, tempo, mvol, _uc, defpan = data[0x30:0x36]
    chset = data[0x40:0x60]
    orders = data[0x60:0x60 + ordnum]
    p = 0x60 + ordnum
    ins_ptr = struct.unpack_from(f"<{insnum}H", data, p)
    p += 2 * insnum
    pat_ptr = struct.unpack_from(f"<{patnum}H", data, p)
    p += 2 * patnum
    pantab = data[p:p + 32] if defpan == 252 else b""
    signed = ffi == 1
    stereo = bool(mvol & 0x80)

    mod.speed = speed if 0 < speed < 255 else 6
    mod.bpm = tempo if tempo >= 33 else 125
    mod.flags = 0                                            # Amiga frequency table
    mod.restart = 0

    # channels: the enabled PCM ones, in order
    chmap, pans = {}, []
    for ch in range(32):
        cs = chset[ch]
        if cs & 0x80 or cs >= 16:
            if cs != 0xFF and cs < 0x80 and cs >= 16:
                warn(mod, f"channel {ch + 1}: AdLib channel dropped")
            continue
        chmap[ch] = len(chmap)
        pan = 0x3 if cs < 8 else 0xC
        if len(pantab) == 32 and pantab[ch] & 0x20:
            pan = pantab[ch] & 0x0F
        pans.append(pan * 17 if stereo else 0x80)
    if not chmap:
        raise ValueError("S3M has no enabled PCM channel")
    mod.channels = len(chmap)

    # orders: 254 = marker (skipped), 255 = end of song
    order_list, order_map = [], {}
    for i, o in enumerate(orders):
        if o == 255:
            break
        order_map[i] = len(order_list)
        if o == 254:
            continue
        order_list.append(o)
    for i in list(order_map):
        if order_map[i] >= len(order_list):
            del order_map[i]
    if not order_list:
        raise ValueError("S3M order list is empty")
    if len(order_list) > 256:
        warn(mod, f"{len(order_list)} orders, only 256 kept")
        order_list = order_list[:256]
    mod.song_len = len(order_list)
    mod.order_table = bytes(order_list).ljust(256, b"\0")

    # instruments: PCM samples become one-sample instruments
    for n, para in enumerate(ins_ptr):
        h = para * 16
        ins = Instrument()
        mod.instruments.append(ins)
        if para == 0 or h + 0x50 > len(data):
            continue
        typ = data[h]
        ins.name = bytes(data[h + 0x30:h + 0x30 + 22])
        if typ == 0:
            continue
        if typ != 1:
            warn(mod, f"instrument {n + 1}: AdLib instrument dropped")
            continue
        memseg = data[h + 0x0D] << 16 | struct.unpack_from("<H", data, h + 0x0E)[0]
        length, lbeg, lend = struct.unpack_from("<III", data, h + 0x10)
        vol, pack, sflags = data[h + 0x1C], data[h + 0x1E], data[h + 0x1F]
        c2spd = struct.unpack_from("<I", data, h + 0x20)[0]
        if pack:
            warn(mod, f"instrument {n + 1}: packed sample (DP30ADPCM) not supported, left empty")
            continue
        width = 2 if sflags & 4 else 1
        off = memseg * 16
        raw = bytes(data[off:off + length * width])
        if len(raw) < length * width:
            warn(mod, f"instrument {n + 1}: sample data truncated")
            raw = raw.ljust(length * width, b"\0")
        if sflags & 2:
            warn(mod, f"instrument {n + 1}: stereo sample, the left channel is used")
        if width == 2:
            words = struct.unpack(f"<{length}H", raw)
            pcm = bytes(((w if signed else w ^ 0x8000) >> 8) & 0xFF for w in words)
        else:
            pcm = raw if signed else bytes(b ^ 0x80 for b in raw)
        s = Sample()
        s.delta = False
        s.raw = pcm
        s.length = length
        s.volume = min(vol, 64)
        s.relnote, s.finetune = c2spd_to_tune(c2spd)
        s.panning = 0x80
        if sflags & 1 and lend > lbeg and lbeg < length:
            s.type = 1
            s.loop_start = lbeg
            s.loop_len = min(lend, length) - lbeg
        ins.samples.append(s)

    # patterns
    dropped = {}
    note_cuts_lost = 0
    for n, para in enumerate(pat_ptr):
        grid = [[[0, 0, 0, 0, 0] for _ in range(mod.channels)] for _ in range(64)]
        h = para * 16
        if para and h + 2 <= len(data):
            p, end = h + 2, h + struct.unpack_from("<H", data, h)[0]
            end = min(max(end, h + 2), len(data))
            row = 0
            while row < 64 and p < end:
                what = data[p]
                p += 1
                if what == 0:
                    row += 1
                    continue
                note = ins = vol = cmd = info = None
                if what & 0x20:
                    note, ins = data[p], data[p + 1]
                    p += 2
                if what & 0x40:
                    vol = data[p]
                    p += 1
                if what & 0x80:
                    cmd, info = data[p], data[p + 1]
                    p += 2
                ch = what & 0x1F
                if ch not in chmap:
                    continue
                cell = grid[row][chmap[ch]]
                if note is not None and note != 255:
                    if note == S3M_NOTE_CUT:
                        cell[0] = -1                          # resolved below
                    elif (note & 0x0F) < 12:
                        x = (note >> 4) * 12 + (note & 0x0F) + 1
                        if 1 <= x <= 96:
                            cell[0] = x
                        else:
                            dropped["note out of the XM range"] = dropped.get("note out of the XM range", 0) + 1
                if ins:
                    cell[1] = ins
                if vol is not None and vol != 255:
                    cell[2] = 0x10 + min(vol, 64)
                if cmd:
                    e = s3m_effect(cmd, info, order_map, dropped)
                    if e is not None and (e[0] or e[1]):
                        cell[3], cell[4] = e
        # note cut (^^): volume 0 in the volume column, else C00 (the card plays key-off 97 as a note)
        for row in grid:
            for cell in row:
                if cell[0] == -1:
                    cell[0] = 0
                    if not cell[2]:
                        cell[2] = 0x10
                    elif not cell[3] and not cell[4]:
                        cell[3], cell[4] = 0x0C, 0
                    else:
                        note_cuts_lost += 1
        mod.patterns.append([64, grid])

    # the header's global volume and channel panning: Gxx / 8xx on the first row of the first order
    first = order_list[0]
    if first < len(mod.patterns):
        row0 = mod.patterns[first][1][0]
        init = []
        for ch, pan in enumerate(pans):
            if pan != 0x80:
                init.append((0x08, pan, ch))
        if gvol < 64:
            init.append((0x10, gvol))                       # any free cell of the row
        skipped = 0
        for e in init:
            if len(e) == 3:
                cell = row0[e[2]]
                if not cell[3] and not cell[4]:
                    cell[3], cell[4] = e[0], e[1]
                else:
                    skipped += 1
            else:
                for cell in row0:
                    if not cell[3] and not cell[4]:
                        cell[3], cell[4] = e[0], e[1]
                        break
                else:
                    skipped += 1
        if skipped:
            warn(mod, f"{skipped} initial global volume / panning effect(s) not placed: row 0 has effects there")
    for i, (rows, grid) in enumerate(mod.patterns):
        mod.patterns[i] = (rows, pack_cells([cell for row in grid for cell in row]))
    for key in sorted(dropped):
        warn(mod, f"S3M effect {key} dropped/changed: {dropped[key]}x")
    if note_cuts_lost:
        warn(mod, f"{note_cuts_lost} note cut(s) lost (volume column and effect both in use)")
    return mod


# ---------------------------------------------------------------------------------------------
# Conversion to the card's format

def decode_sample(s, masonxm):
    """-> signed 8-bit sample bytes (as 0..255) and loop start / length in 8-bit samples."""
    sixteen = s.type & 0x10
    raw, start, llen = s.raw, s.loop_start, s.loop_len
    if not s.delta:
        return bytearray(raw), start, llen
    if sixteen and not masonxm:
        n = len(raw) // 2
        out, acc = bytearray(n), 0
        for i in range(n):
            acc = (acc + (raw[2 * i] | raw[2 * i + 1] << 8)) & 0xFFFF
            out[i] = acc >> 8                                # the high byte of the decoded value
        return out, start // 2, llen // 2
    acc, dec = 0, bytearray(len(raw))
    for i, b in enumerate(raw):                              # bytewise (MasonXM does this for 16-bit too)
        acc = (acc + b) & 0xFF
        dec[i] = acc
    if sixteen:                                              # MasonXM: then keep every second byte
        return dec[1::2], start // 2, llen // 2
    return dec, start, llen


def convert_sample(s, masonxm, fixes, index):
    """-> (record fields, 8-bit unsigned sample bytes). Record fields: length, loop, loop_end, stype."""
    pcm, loop, llen = decode_sample(s, masonxm)
    size = len(pcm)
    data = bytearray(b ^ 0x80 for b in pcm)
    for i, b in enumerate(data):
        if b == 0:
            data[i] = 1                                      # 0 is the firmware's loop marker
    kind = s.type & 3
    if masonxm:
        M = 0xFFFFFFFF
        if kind in (0, 3):
            loop, llen = size, 0
        elif kind == 2:
            n = loop + llen
            out = data[:n]
            if llen not in (0, 1):
                loop += 1
                e = len(out) - 1
                for _ in range(llen - 1):
                    e -= 1
                    out.append(out[e])
            data, size = out, len(out)
        if (loop + llen) & M > size:
            llen = (size - loop) & M
        loop_end = (loop + llen) & M
        return (size, loop, loop_end, s.type), bytes(data)

    if kind in (1, 2) and llen > 0 and loop < size:
        loop_end = min(loop + llen, size)
        stype = kind
    else:
        loop = loop_end = size
        stype = 0
        if kind == 3:
            fixes.append(("loop-type-3", index))
    if s.type & 0x10:
        fixes.append(("16bit", index))
    if stype != s.type:
        fixes.append(("stype", index, s.type, stype))
    if kind == 2 and stype == 2:
        fixes.append(("pingpong", index))
    elif kind == 1 and llen == 0:
        fixes.append(("empty-loop", index))
    return (size, loop, loop_end, stype), bytes(data)


class Result:
    pass


def convert(mod, masonxm=False, title=None):
    """Module -> Result: pack (track pack), table, stream, fixes, warnings, numbers."""
    r = Result()
    r.fixes = []
    r.warnings = list(mod.warnings)
    name = mod.name
    if title is not None:
        name = title.encode("latin-1", "replace")[:20].ljust(20, b"\0")

    # track pack: only the patterns the order list uses, in pattern number order
    used = sorted(set(mod.order_table[:mod.song_len]))
    recs = {}
    pdata = bytearray()
    keyoffs = 0
    for n in used:
        if n < len(mod.patterns):
            rows, packed = mod.patterns[n]
        else:
            if masonxm:
                continue
            rows, packed = 64, b""
            r.warnings.append(f"order list uses pattern {n}, which does not exist: an empty 64-row pattern")
        if rows == 0:
            if masonxm:
                continue
            rows = 64
            r.warnings.append(f"pattern {n} has 0 rows: stored as an empty 64-row pattern")
            packed = b""
        if packed:
            cells, used_bytes = unpack_cells(packed, rows * mod.channels)
            if len(cells) < rows * mod.channels or used_bytes != len(packed):
                r.warnings.append(f"pattern {n}: packed data does not match {rows} rows x {mod.channels} channels")
            keyoffs += sum(1 for c in cells if c[0] == 97)
        elif not masonxm:
            packed = bytes([0x80]) * (rows * mod.channels)
            r.fixes.append(("empty-pattern", n, len(packed)))
        recs[n] = [len(pdata), len(packed), rows]
        pdata += packed
    if keyoffs:
        r.warnings.append(f"{keyoffs} key-off note(s): the card's firmware plays note 97 as a note")
    patnum = max((n for n in recs if recs[n][1]), default=-1) + 1
    hsize = HEADER + PATREC * patnum
    head = struct.pack("<I20s6H", len(pdata), name, mod.channels, mod.speed, mod.bpm, mod.flags,
                       mod.song_len, mod.restart) + mod.order_table
    table = bytearray()
    for n in range(patnum):
        ofs, size, rows = recs.get(n, [0, 0, 0])
        if size:
            ofs += hsize
        elif not masonxm:
            ofs = hsize                                      # unused: a valid pointer (record 0 must be this)
            if n not in recs:
                r.fixes.append(("unused-record", n))
        table += struct.pack("<IIH", ofs, size, rows)
    r.pack = head + bytes(table) + bytes(pdata)
    r.patnum = patnum
    r.patterns_stored = len(recs)

    # instruments: sample 0 of each
    nins = len(mod.instruments)
    records, stream = [], bytearray()
    for i, ins in enumerate(mod.instruments):
        if not ins.samples:
            records.append(bytes(INSREC))
            continue
        s = ins.samples[0]
        (size, loop, loop_end, stype), data = convert_sample(s, masonxm, r.fixes, i)
        records.append(struct.pack("<IIIIBbBBb", INSREC * nins + len(stream), size, loop, loop_end,
                                   s.volume, s.finetune, stype, s.panning, s.relnote))
        stream += data
    r.table = b"".join(records)
    r.stream = bytes(stream)
    r.instruments = nins
    r.channels = mod.channels
    r.orders = mod.song_len
    lengths = [struct.unpack_from("<I", rec, 4)[0] for rec in records]
    r.total = len(r.pack) + INSREC * (nins + 1) + (16 + 48) + sum(n + 48 for n in lengths)
    if mod.channels > MAX_CHANNELS:
        r.warnings.append(f"{mod.channels} channels: the card plays at most {MAX_CHANNELS}")
    if mod.flags & 1:
        r.warnings.append("linear frequency table: the card's firmware uses Amiga periods for every module "
                          "(notes match, slides and vibrato do not)")
    if nins > MAX_INSTRUMENTS:
        r.warnings.append(f"{nins} instruments: the card has descriptors for {MAX_INSTRUMENTS}")
    if r.total > STORAGE:
        r.warnings.append(f"{r.total} bytes in the card's storage: more than its {STORAGE}")
    return r


def chunks(r):
    """-> (track chunks, instrument chunks): ins00 = the table, ins01.. = the sample stream."""
    trk = struct.pack("<I", len(r.pack)) + r.pack
    tc = [trk[i:i + CHUNK] for i in range(0, len(trk), CHUNK)]
    ic = [r.table] + [r.stream[i:i + CHUNK] for i in range(0, len(r.stream), CHUNK)]
    return tc, ic


def load(path):
    data = read_file(path)
    if data[:17] == XM_ID:
        return parse_xm(data)
    if len(data) > 0x30 and data[0x2C:0x30] == b"SCRM":
        return parse_s3m(data)
    raise ValueError(f"{path}: neither XM nor S3M")


def write_chunks(r, outdir, base):
    os.makedirs(outdir, exist_ok=True)
    tc, ic = chunks(r)
    files = []
    for i, c in enumerate(tc):
        files.append((os.path.join(outdir, f"{base}.trk{i:02d}"), c))
    for i, c in enumerate(ic):
        files.append((os.path.join(outdir, f"{base}.ins{i:02d}"), c))
    for path, c in files:
        with open(path, "wb") as f:
            f.write(c)
    return [p for p, _ in files]


def summary(mod, r, files):
    name = "".join(ch if " " <= ch <= "~" else "." for ch in r.pack[4:24].replace(b"\0", b" ").decode("latin-1")).rstrip()
    lines = [f"{mod.kind} '{name}': {r.channels} channels, {r.patterns_stored} patterns stored "
             f"(PatNum {r.patnum}), {r.orders} orders, {r.instruments} instruments, speed {mod.speed}, "
             f"BPM {mod.bpm}, {'linear' if mod.flags & 1 else 'Amiga'} frequencies",
             f"track pack {len(r.pack)} bytes, instrument table {len(r.table)}, samples {len(r.stream)}; "
             f"card storage {r.total} of {STORAGE} bytes"]
    for path in files:
        lines.append(f"  {path} ({os.path.getsize(path)} bytes)")
    for w in r.warnings:
        lines.append(f"warning: {w}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------------------------
# Self-check against MasonXM's outputs

AUTHOR_TRACKS = [            # (module in Music/, MasonXM output prefix)
    ("CONSUL.XM", "Consul"),
    ("PROSIAK.XM", "Prosiak"),
    ("FASTER-2.XM", "FASTER-2"),
    ("BALLADAL.XM", "BALLADAL"),
    ("DISCHARZ.XM", "DISCHARZ"),
    ("CHECKNO1.XM", "checkno1"),
]


def read_masonxm(music, prefix):
    """MasonXM's files: (whole pack, track chunks, table, sample chunks)."""
    p = os.path.join(music, prefix)
    pack = read_file(p + ".xmPatPack")
    tc, ic, i = [], [], 0
    while os.path.exists(f"{p}.xmPatPack{i:02d}"):
        tc.append(read_file(f"{p}.xmPatPack{i:02d}"))
        i += 1
    i = 0
    while os.path.exists(f"{p}.xmInsPack{i:02d}"):
        ic.append(read_file(f"{p}.xmInsPack{i:02d}"))
        i += 1
    return pack, tc, ic


def explain(mod, fixed, ref_pack, ref_table, ref_stream):
    """Explain every difference between the normal output and MasonXM's. Returns (lines, unexplained)."""
    lines, bad = [], 0
    kinds = {}
    for f in fixed.fixes:
        kinds.setdefault(f[0], []).append(f)

    # track pack: header fields, pattern records, pattern data
    a, b = fixed.pack, ref_pack
    empties = {f[1]: f[2] for f in kinds.get("empty-pattern", [])}
    unused = {f[1] for f in kinds.get("unused-record", [])}
    if a == b:
        lines.append("track pack: identical")
    else:
        lines.append(f"track pack: {sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))} byte(s) differ")
        fa = struct.unpack_from("<I20s6H", a, 0)
        fb = struct.unpack_from("<I20s6H", b, 0)
        names = ["Size", "TrackName", "Channels", "TMP", "BPM", "FreqTable", "PatLen", "PatRes"]
        for k in range(8):
            if fa[k] != fb[k]:
                if k == 0 and fa[0] - fb[0] == sum(empties.values()):
                    lines.append(f"track pack: Size {fb[0]} -> {fa[0]}: +{fa[0] - fb[0]} bytes of empty patterns")
                else:
                    lines.append(f"track pack: header field {names[k]} differs: UNEXPLAINED")
                    bad += 1
        if a[36:292] != b[36:292]:
            lines.append("track pack: order table differs: UNEXPLAINED")
            bad += 1
        pa = (len(a) - 292 - fa[0]) // PATREC
        pb = (len(b) - 292 - fb[0]) // PATREC
        if pa != pb:
            lines.append(f"track pack: PatNum {pb} -> {pa}")
        # pattern data: MasonXM's data with the empty patterns inserted must equal ours
        ra = {n: struct.unpack_from("<IIH", a, 292 + PATREC * n) for n in range(pa)}
        rb = {n: struct.unpack_from("<IIH", b, 292 + PATREC * n) for n in range(pb)}
        da, db = a[292 + PATREC * pa:], b[292 + PATREC * pb:]
        rebuilt, shift = bytearray(), {}
        for n in range(max(pa, pb)):
            if n in empties:
                shift[n] = len(rebuilt)
                rebuilt += bytes([0x80]) * empties[n]
            elif n in rb and rb[n][1]:
                shift[n] = len(rebuilt)
                rebuilt += db[rb[n][0] - (292 + PATREC * pb):][:rb[n][1]]
        if bytes(rebuilt) != da:
            lines.append("track pack: pattern data differs beyond the inserted empty patterns: UNEXPLAINED")
            bad += 1
        elif empties:
            lines.append("track pack: pattern data = MasonXM's + the empty patterns "
                         + ", ".join(f"{n} ({empties[n]} bytes)" for n in sorted(empties)))
        moved = 0
        for n in range(pa):
            rec_a = ra[n]
            rec_b = rb.get(n, (0, 0, 0))
            if rec_a == rec_b:
                continue
            if n in empties:
                if rec_b[1] == 0 and rec_a[1] == empties[n] and rec_a[2] == rec_b[2]:
                    lines.append(f"  record {n}: used but empty in the XM: MasonXM SIZE 0, OFS {rec_b[0]} "
                                 f"(not relocated); here SIZE {rec_a[1]}, OFS {rec_a[0]}")
                    continue
            elif n in unused:
                if rec_b == (0, 0, 0) and rec_a[1] == 0 and rec_a[0] == 292 + PATREC * pa:
                    lines.append(f"  record {n}: unused: MasonXM OFS 0, here OFS {rec_a[0]} (the header size)")
                    continue
            elif rec_a[1] == rec_b[1] and rec_a[2] == rec_b[2] and rec_a[1] and \
                    rec_a[0] == 292 + PATREC * pa + shift.get(n, -1):
                moved += 1                                     # moved by inserted bytes / a longer table
                continue
            lines.append(f"  record {n}: {rec_b} -> {rec_a}: UNEXPLAINED")
            bad += 1
        if moved:
            lines.append(f"  {moved} record(s): OFS moved by the inserted empty patterns / record table")

    # instruments
    nins = len(fixed.table) // INSREC
    if fixed.table == ref_table and fixed.stream == ref_stream:
        lines.append("instrument table and samples: identical")
        return lines, bad
    by_ins = {}
    for f in fixed.fixes:
        if f[0] in ("16bit", "stype", "pingpong", "empty-loop", "loop-type-3"):
            by_ins.setdefault(f[1], []).append(f)
    names = ["OFS", "Length", "Loop", "LoopEnd", "Volume", "FineTune", "SType", "Panning", "RelativeNote"]
    length_changed, moved = False, 0
    for i in range(nins):
        fa = struct.unpack_from("<IIIIBbBBb", fixed.table, INSREC * i)
        fb = struct.unpack_from("<IIIIBbBBb", ref_table, INSREC * i)
        sa = fixed.stream[fa[0] - INSREC * nins:][:fa[1]] if fa[1] else b""
        sb = ref_stream[fb[0] - INSREC * nins:][:fb[1]] if fb[1] else b""
        if fa == fb and sa == sb:
            continue
        why = {f[0] for f in by_ins.get(i, [])}
        diff = [names[k] for k in range(9) if fa[k] != fb[k]]
        notes = []
        ok = True
        if "OFS" in diff:
            diff.remove("OFS")
            if not length_changed:
                ok = False
        if "SType" in diff:
            diff.remove("SType")
            notes.append(f"SType {fb[6]:#04x} -> {fa[6]}")
            ok &= "stype" in why
        if "pingpong" in why:
            for k in ("Length", "Loop", "LoopEnd"):
                if k in diff:
                    diff.remove(k)
            notes.append(f"ping-pong kept as a loop (MasonXM unrolled it: Length {fb[1]} -> {fa[1]}, "
                         f"Loop {fb[2]} -> {fa[2]}, LoopEnd {fb[3]} -> {fa[3]})")
            if fa[1] != fb[1]:
                length_changed = True
            # the bytes up to the loop end are the same; MasonXM appended the reversed loop instead of the tail
            ok &= "16bit" in why or sa[:fa[3]] == sb[:fa[3]]
        if "empty-loop" in why or "loop-type-3" in why:
            for k in ("Loop", "LoopEnd"):
                if k in diff:
                    diff.remove(k)
            notes.append(f"loop without length / type 3 -> no loop (Loop {fb[2]} -> {fa[2]}, LoopEnd {fb[3]} -> {fa[3]})")
        if sa != sb:
            if "16bit" in why:
                n = sum(1 for x, y in zip(sa, sb) if x != y)
                notes.append(f"16-bit sample decoded properly: {n} of {len(sa)} bytes differ")
            elif "pingpong" not in why:
                ok = False
        if diff:
            ok = False
            notes.append("fields " + ", ".join(diff) + " differ")
        if ok and not notes:
            moved += 1                                       # only OFS, moved by an earlier length change
            continue
        lines.append(f"instrument {i + 1}: " + "; ".join(notes) + ("" if ok else ": UNEXPLAINED"))
        if not ok:
            bad += 1
    if moved:
        lines.append(f"  {moved} later instrument(s): OFS moved by the shorter ping-pong sample(s)")
    return lines, bad


def check_against(music, outdir=None, out=print):
    total_bad = 0
    for module, prefix in AUTHOR_TRACKS:
        path = os.path.join(music, module)
        if not os.path.exists(path) or not os.path.exists(os.path.join(music, prefix + ".xmPatPack")):
            out(f"{module}: not found, skipped")
            continue
        ref_pack, ref_tc, ref_ic = read_masonxm(music, prefix)
        title = ref_pack[4:24]
        mod = load(path)
        compat = convert(mod, masonxm=True, title=title.decode("latin-1"))
        ctc, cic = chunks(compat)
        same = compat.pack == ref_pack and ctc == ref_tc and cic == ref_ic
        out(f"{module} -> {prefix}: --masonxm output {'byte-identical to' if same else 'DIFFERS from'} "
            f"MasonXM's ({len(ref_tc)} track chunk(s), {len(ref_ic)} instrument chunk(s))")
        if not same:
            total_bad += 1
        fixed = convert(mod, title=title.decode("latin-1"))
        lines, bad = explain(mod, fixed, ref_pack, ref_ic[0], b"".join(ref_ic[1:]))
        for line in lines:
            out("  " + line)
        total_bad += bad
        if outdir:
            write_chunks(fixed, outdir, prefix.lower())
    out(f"unexplained differences: {total_bad}")
    return total_bad


# ---------------------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("module", nargs="?", help="XM or S3M file")
    ap.add_argument("-o", "--outdir", default=None, help="output folder (default: current)")
    ap.add_argument("--base", help="output file name stem (default: the module's name)")
    ap.add_argument("--title", help="track name, 20 bytes at most (default: the module's song name)")
    ap.add_argument("--masonxm", action="store_true", help="reproduce MasonXM byte for byte, bugs included")
    ap.add_argument("--check-against", metavar="MUSICDIR", help="compare with MasonXM's outputs in MUSICDIR")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    if a.check_against:
        return 1 if check_against(a.check_against, a.outdir) else 0
    if not a.module:
        ap.error("a module file or --check-against is needed")
    mod = load(a.module)
    r = convert(mod, masonxm=a.masonxm, title=a.title)
    base = a.base or os.path.splitext(os.path.basename(a.module))[0]
    files = write_chunks(r, a.outdir or ".", base)
    if not a.quiet:
        print(summary(mod, r, files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
