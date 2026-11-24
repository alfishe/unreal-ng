#!/usr/bin/env python3
"""Check core/src/debugger/ttd/ttd.ksy against the real session files.

Generates the Python parser from ttd.ksy with kaitai-struct-compiler, reads every
.ttd under testdata/ttd with it and compares what it finds with the shared Python
reader (tools/python/ttd/ttdlib, itself checked against the C++ writer) and with
the C++ expectations stored next to the fixtures (testdata/ttd/v2/expected.json,
testdata/ttd/branches/expected.json, testdata/ttd/v2/chunked/chunked.json):

- the header: flags, UUID, creation time, zstd version, stream table, the session
  tables (regions, device table);
- every record header in file order, every part end (frames, branch, record
  offsets, dependencies, extra), the index (part table, stream totals, extra; a
  recording folder's seek index against the records it names) and the trailer;
- every payload decompressed (Kaitai has no zstd) and parsed with the payload type
  of its stream, to its last byte: pieces (trunk and branch), piece tables (each
  against the versions the checkpoints before it give every piece), checkpoints, events,
  configuration, write journal, bus and media journals, frame-boundary streams,
  the branch table, the branch state; holder streams other than 18 are opaque;
- the session: checkpoint frames, piece versions (with their content CRCs),
  events, journal record counts of the trunk; per branch its parent, fork and
  checkpoint frames; the chunked fixture's chunk counts, sizes and CRCs.

A damaged or unfinished fixture (a reader has to scan it) is only parsed: a
sequential Kaitai parser stops in it, which is reported, not a failure.

kaitai-struct-compiler (brew install kaitai-struct-compiler; it needs a JRE) and
the kaitaistruct runtime (pip install kaitaistruct) are optional: without them
the script says so and exits 0. It is not part of the CMake build.

Run from anywhere:
    python3 tools/verification/ttd-analyzer/scripts/check_ksy.py          # every fixture
    python3 tools/verification/ttd-analyzer/scripts/check_ksy.py FILE...  # some files

The parser is generated into scratch/ttd-ksy/ (--out to change). Exit 1 when the
schema and a file disagree.
"""

from __future__ import annotations

import argparse
import importlib
import io
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[4]
KSY = ROOT / "core/src/debugger/ttd/ttd.ksy"
FIXTURES = ROOT / "testdata/ttd"
sys.path.insert(0, str(ROOT / "tools/python/ttd"))

FLAG_COMPRESSED, FLAG_PART_END, FLAG_CONTINUED = 1, 2, 4
RECORD_HEADER = 32
CHUNKED = 0x8000
BRANCH_TABLE, BRANCH_STATE = 0x0200, 18


def skip(why: str) -> int:
    print(f"SKIP: {why}")
    return 0


def find_compiler(given: Optional[str]) -> Optional[str]:
    if given:
        return given
    for name in ("kaitai-struct-compiler", "ksc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def generate(ksc: str, ksy: Path, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([ksc, "-t", "python", "--outdir", str(out), str(ksy)], capture_output=True, text=True)
    # The style hints (num_* / len_* ids) are warnings, not errors
    errors = [line for line in (run.stdout + run.stderr).splitlines() if "error" in line.lower()]
    if run.returncode != 0 or errors:
        raise SystemExit(f"kaitai-struct-compiler failed ({run.returncode}):\n{run.stdout}{run.stderr}")
    sys.path.insert(0, str(out))
    return importlib.import_module("ttd").Ttd


class Check:
    """The problems found in one file."""

    def __init__(self) -> None:
        self.problems: List[str] = []

    def eq(self, what: str, got, want) -> None:
        if got != want:
            g, w = repr(got), repr(want)
            self.problems.append(f"{what}: ksy {g[:160]} != {w[:160]}")


def payload_type(Ttd, sid: int):
    """(the payload class of stream @p sid, trunk stream it mirrors, is a branch twin)."""
    branch = False
    base = sid
    if 0x0201 <= sid <= 0x023F:
        base, branch = sid - 0x0200, True
    elif 0x0240 <= sid <= 0x027F:
        base, branch = 0x0100 + sid - 0x0240, True
    if base == 1:
        return (Ttd.PayloadBranchPieces if branch else Ttd.PayloadPieces), base, branch
    types = {2: Ttd.PayloadPieceTables, 3: Ttd.PayloadCheckpoints, 5: Ttd.PayloadEvents, 6: Ttd.PayloadConfiguration,
             7: Ttd.PayloadWriteJournal, 13: Ttd.PayloadPorts, 14: Ttd.PayloadPorts, 15: Ttd.PayloadPorts,
             16: Ttd.PayloadMediaReads, BRANCH_STATE: Ttd.PayloadBranchState}
    if sid == BRANCH_TABLE:
        return Ttd.PayloadBranchTable, sid, False
    if 0x0100 <= base <= 0x013F:
        return Ttd.PayloadFrameStream, base, branch
    return types.get(base), base, branch


def check_file(Ttd, KaitaiStream, path: Path, expected: Dict[str, dict]) -> Tuple[str, str]:
    """('ok' | 'damaged' | 'FAIL', summary)."""
    import zstandard
    from ttdlib import session as tses
    from ttdlib import ttdcontainer as tc

    data = path.read_bytes()
    chk = Check()

    # The reference: the shared reader (branch-aware for the structure)
    try:
        ref = tses.open_branched(data)
    except tc.TtdContainerError as e:
        ref = None
        why = str(e)
    damaged = ref is None or not ref.finalized or any(p.damaged for p in ref.parts) or any(
        "damaged" in n or "cut" in n or "incomplete" in n for n in ref.notes)
    if ref is not None and not damaged:
        for part in ref.parts:
            for rec in part.physical:
                try:
                    tc.read_record(ref, rec)
                except ValueError as e:
                    damaged, why = True, f"part {part.index}: {e}"
                    ref.notes.insert(0, why)

    try:
        t = Ttd(KaitaiStream(io.BytesIO(data)))
        index = t.index
        _ = t.trailer
    except Exception as e:   # a sequential parser stops in a damaged file
        if damaged:
            return "damaged", f"the parser stops ({type(e).__name__}: {str(e)[:80]}); reader: " + (
                why if ref is None else "; ".join(ref.notes[:2]) or "not finalized")
        return "FAIL", f"the parser fails on a sound file: {type(e).__name__}: {e}"
    if damaged:
        return "damaged", "parsed sequentially; the reader drops damaged parts (" + (
            why if ref is None else "; ".join(ref.notes[:2]) or "not finalized") + ")"

    # Header
    h = t.header
    chk.eq("header flags", h.flags, ref.flags)
    chk.eq("uuid", h.uuid, ref.uuid)
    chk.eq("created", h.created_micros, ref.created_micros)
    chk.eq("zstd version", h.zstd_version, ref.zstd_version)
    chk.eq("header size", h.header_size, ref.data_start)
    chk.eq("streams", [(s.id, s.layout_version, s.kind == 0, s.name) for s in h.streams],
           [(s.id, s.layout, s.required, s.name) for s in ref.streams.values()])
    trunk_c = tc.open_container(data)
    trunk = tc.read_session(trunk_c)
    st = h.session_tables
    chk.eq("snapshot interval", st.snapshot_interval, trunk.snapshot_interval)
    chk.eq("regions", [{"id": r.id, "name": r.name.value, "owner_type": r.owner_type,
                        "owner_instance": r.owner_instance.value, "pieces": r.pieces, "bytes": r.bytes,
                        "dirty_granularity": r.dirty_granularity, "block_pieces": r.block_pieces}
                       for r in st.regions], trunk.regions)
    chk.eq("devices", [{"type": d.type, "instance": d.instance.value, "legacy_id": d.legacy_id,
                        "layout_version": d.layout_version, "state_size": d.state_size,
                        "variable_size": bool(d.variable_size), "firmware": d.firmware_fingerprint,
                        "restore_after": [(k.type, k.instance.value) for k in d.restore_after],
                        "time_fields": [(f.offset, f.width) for f in d.time_fields],
                        "runs_behind_cpu": bool(d.runs_behind_cpu)} for d in st.devices], trunk.devices)

    # Records, part ends
    offset = h.header_size
    ends: Dict[int, Tuple[int, object]] = {}
    members: List[Tuple[int, object]] = []
    for rec in t.records:
        if rec.stream_id == 0:
            if not rec.flags & FLAG_PART_END:
                chk.problems.append(f"record at {offset}: stream 0 without the part-end flag")
            ends[rec.part_index] = (offset, rec.payload)
        else:
            members.append((offset, rec))
        offset += RECORD_HEADER + rec.stored_size
    chk.eq("parts", len(ends), len(ref.parts))
    for part in ref.parts:
        if part.index not in ends:
            chk.problems.append(f"part {part.index}: no part end")
            continue
        _, pe = ends[part.index]
        deps, prev = [], 0
        for d in pe.dependency_deltas:
            prev += d.value
            deps.append(prev)
        chk.eq(f"part {part.index}", (pe.first_frame, pe.frame_count, pe.branch, list(pe.record_offsets), deps,
                                       pe.extra), (part.first_frame, part.frame_count, part.branch,
                                                   [r.offset for r in part.physical], part.dependencies, part.extra))
    listed = sorted(o for _, pe in ends.values() for o in pe.record_offsets)
    chk.eq("records listed by part ends", listed, [o for o, _ in members])

    # Index and trailer
    totals: Dict[int, List[int]] = {}
    for _, rec in members:
        tt = totals.setdefault(rec.stream_id, [0, 0, 0])
        tt[0] += 1
        tt[1] += rec.stored_size
        tt[2] += rec.raw_size
    chk.eq("trailer magic", t.trailer.magic, b"TTDX")
    seek = None
    if index is None:
        chk.problems.append("no index in a finished file")
    else:
        chk.eq("index parts", [(p.part_end_offset, p.first_frame, p.frame_count, p.branch) for p in index.parts],
               [(ends[p.index][0], p.first_frame, p.frame_count, p.branch) for p in ref.parts if p.index in ends])
        chk.eq("index stream totals", {s.id: [s.records, s.stored_bytes, s.raw_bytes] for s in index.streams},
               totals)
        chk.eq("index extra", index._raw_extra, ref.index_extra)
        seek = index.extra.seek
        if seek is None and ref.index_extra[:1] == b"S":
            chk.problems.append("the index extra starts like a seek index and is not parsed as one")
        chk.eq("index offset", t.trailer.index_offset, offset)

    # Payloads: each decompressed, chunked twins joined, parsed to its last byte
    def raw_of(rec) -> bytes:
        stored = rec.payload   # bytes: only stream 0 has a type in the record
        if tc.crc32c(stored) != rec.payload_crc:
            chk.problems.append(f"stream {rec.stream_id:#x}: payload CRC")
        if rec.flags & FLAG_COMPRESSED:
            return zstandard.ZstdDecompressor().decompress(stored, max_output_size=rec.raw_size)
        return stored

    by_part: Dict[int, List[Tuple[int, bytes, int]]] = {}    # part -> (stream, payload, chunks)
    pending: Optional[List] = None
    for _, rec in members:
        if pending is not None:
            if rec.stream_id != pending[0] | CHUNKED or rec.part_index != pending[3]:
                chk.problems.append(f"part {pending[3]}: a chunked run of stream {pending[0]:#x} is cut")
                pending = None
            else:
                pending[1] += raw_of(rec)
                pending[2] += 1
                if not rec.flags & FLAG_CONTINUED:
                    by_part.setdefault(pending[3], []).append((pending[0], bytes(pending[1]), pending[2]))
                    pending = None
                continue
        if rec.stream_id & CHUNKED:
            pending = [rec.stream_id & ~CHUNKED, bytearray(raw_of(rec)), 1, rec.part_index]
            if not rec.flags & FLAG_CONTINUED:
                by_part.setdefault(pending[3], []).append((pending[0], bytes(pending[1]), pending[2]))
                pending = None
            continue
        by_part.setdefault(rec.part_index, []).append((rec.stream_id, raw_of(rec), 1))

    # A recording folder's segment file: the seek index against the records it names, the piece tables against the
    # versions the checkpoints give each piece (numbered from the file's start, per line)
    seek_parts = {i: sp for i, sp in enumerate(seek.parts)} if seek is not None else {}
    if seek is not None:
        chk.eq("seek index: index offset", seek.index_offset, t.trailer.index_offset)
        chk.eq("seek index: parts", len(seek.parts), len(ref.parts))
    offsets_of: Dict[int, Dict[int, Tuple[int, int]]] = {}    # part -> trunk stream -> (offset, stored)
    for off, rec in members:
        base_sid = rec.stream_id - 0x0200 if 0x0201 <= rec.stream_id <= 0x023F else rec.stream_id
        offsets_of.setdefault(rec.part_index, {})[base_sid] = (off, rec.stored_size)
    next_item: Dict[int, int] = {}                             # line -> the next version's number
    live: Dict[int, Dict[Tuple[int, int], int]] = {}           # line -> (region, piece) -> version number
    tables_seen = 0

    count = {"checkpoints": 0, "versions": 0, "events": 0, 13: 0, 14: 0, 15: 0, 16: 0, "writes": 0}
    frames: List[int] = []
    crcs: List[int] = []
    lines: Dict[int, dict] = {}
    chunks: Dict[int, Tuple[int, int, int]] = {}
    for part in ref.parts:
        parsed: Dict[int, object] = {}
        for sid, payload, n in by_part.get(part.index, []):
            if n > 1:
                chunks[sid] = (n, len(payload), tc.crc32c(payload))
            cls, base, _ = payload_type(Ttd, sid)
            if cls is None:
                continue   # a holder stream, opaque
            try:
                stream = KaitaiStream(io.BytesIO(payload))
                obj = cls(stream, None, t)
                if not stream.is_eof():
                    chk.problems.append(f"part {part.index} stream {sid:#x}: {stream.size() - stream.pos()} "
                                        f"byte(s) left after {cls.__name__}")
            except Exception as e:
                chk.problems.append(f"part {part.index} stream {sid:#x}: {cls.__name__}: {type(e).__name__} {e}")
                continue
            parsed[base if sid != BRANCH_TABLE else sid] = obj
        cps = parsed.get(3)
        pieces = parsed.get(1)
        if cps is None:
            continue
        changes = sum(rc.count.value for cp in cps.checkpoints for rc in cp.regions)
        chk.eq(f"part {part.index} versions vs changed pieces", len(pieces.versions) if pieces else 0, changes)
        frame, part_frames = 0, []
        for cp in cps.checkpoints:
            frame += cp.frame_delta.value
            part_frames.append(frame)
        # Piece tables and the part's place in the seek index
        if part.extra and part.extra[0] & 1:
            next_item[part.branch] = 0
            live[part.branch] = {}
        first_item = next_item.setdefault(part.branch, 0)
        now = live.setdefault(part.branch, {})
        tables = {tb.checkpoint.value: tb for tb in parsed[2].tables} if 2 in parsed else {}
        for ordinal, cp in enumerate(cps.checkpoints):
            for rc in cp.regions:
                for piece in rc.pieces:
                    now[(rc.region.value, piece.value)] = next_item[part.branch]
                    next_item[part.branch] += 1
            if ordinal in tables:
                tables_seen += 1
                for tr in tables[ordinal].regions:
                    chk.eq(f"part {part.index} table at checkpoint {ordinal}, region {tr.region.value}",
                           [v.value for v in tr.versions_plus_one],
                           [now.get((tr.region.value, p), -1) + 1 for p in range(tr.piece_count.value)])
        if part.index in seek_parts:
            sp = seek_parts[part.index]
            at = offsets_of.get(part.index, {})
            table_frames, prev = [], sp.first_frame.value
            for d in sp.table_frame_deltas:
                prev += d.value
                table_frames.append(prev)
            chk.eq(f"part {part.index} seek entry",
                   ((sp.pieces_offset.value, sp.pieces_stored.value), (sp.checkpoints_offset.value,
                    sp.checkpoints_stored.value), (sp.tables_offset.value, sp.tables_stored.value),
                    sp.first_item.value, sp.items.value, sp.first_frame.value, table_frames),
                   (at.get(1, (0, 0)), at.get(3, (0, 0)), at.get(2, (0, 0)), first_item,
                    len(pieces.versions) if pieces else 0, part.first_frame,
                    [part_frames[o] for o in sorted(tables)]))
        if part.branch == 0:
            frames += part_frames
            crcs += [v.content_crc for v in pieces.versions] if pieces else []
            count["checkpoints"] += len(part_frames)
            count["versions"] += len(pieces.versions) if pieces else 0
            count["events"] += parsed[5].count.value if 5 in parsed else 0
            for sid in (13, 14, 15, 16):
                count[sid] += parsed[sid].count.value if sid in parsed else 0
            count["writes"] += parsed[7].record_count.value if 7 in parsed else 0
        line = lines.setdefault(part.branch, {"parent": -1, "fork_frame": 0, "fork_t": 0, "frames": []})
        if BRANCH_TABLE in parsed:
            create = parsed[BRANCH_TABLE]
            chk.eq(f"part {part.index} create line", create.line.value, part.branch)
            line.update(parent=create.parent.value, fork_frame=create.fork_frame.value, fork_t=create.fork_t.value)
        line["frames"] += part_frames

    if seek is not None and tables_seen == 0:
        chk.problems.append("a seek index without a piece table")

    # The session against the reader's trunk and the C++ expectations
    if not trunk.stopped_at:
        chk.eq("trunk checkpoint frames", frames, [cp.frame for cp in trunk.checkpoints])
        chk.eq("trunk version CRCs", crcs, [v.crc for v in trunk.versions])
        chk.eq("trunk counts", (count["events"], count[13], count[14], count[15], count[16], count["writes"]),
               (trunk.events, trunk.bus_reads, trunk.bus_writes, trunk.bus_vectors, trunk.media_reads,
                trunk.write_journal))
    try:
        branched = tses.read_branched(ref)
    except tc.TtdError:
        branched = None   # a folder's branch file alone: its parent is in another file
    if branched and not branched.notes and not branched.stopped_at and len(lines) > 1:
        chk.eq("lines", {i: (l["parent"], l["fork_frame"], l["fork_t"], l["frames"]) for i, l in lines.items()},
               {i: (l.parent, l.fork_frame, l.fork_t, [cp.frame for cp in l.checkpoints])
                for i, l in branched.lines.items()})
    want = expected.get(str(path.resolve()))
    if want and "checkpoints" in want and isinstance(want["checkpoints"], int) and "versions" in want:
        chk.eq("C++ counts", (count["checkpoints"], count["versions"], len(ends), count["events"], count[13],
                              count[14], index is not None, bool(h.flags & 1)),
               (want["checkpoints"], want["versions"], want["parts"], want["events"], want["bus_reads"],
                want["bus_writes"], want["finalized"], want["converted_from_v1"]))
    elif want and "lines" in want:
        chk.eq("C++ parts", len(ends), want["parts"])
        chk.eq("C++ lines", {int(i): (l["parent"], l["fork_frame"], l["fork_t"], l["frames"]) for i, l in lines.items()},
               {int(i): (l["parent"], l["fork_frame"], l["fork_t"], [f for f, _ in l["checkpoints"]])
                for i, l in want["lines"].items()})
    elif want and "write_journal_chunks" in want:
        chk.eq("C++ checkpoints", count["checkpoints"], want["checkpoints"])
        chk.eq("C++ write records", count["writes"], want["write_records"])
        chk.eq("C++ write journal chunks", chunks.get(7), (want["write_journal_chunks"], want["write_journal_bytes"],
                                                            want["write_journal_crc32c"]))
        chk.eq("C++ holder chunks", chunks.get(want["holder_stream"]),
               (want["holder_chunks"], want["holder_bytes"], want["holder_crc32c"]))

    summary = (f"{len(t.records)} records, {len(ends)} parts, {count['checkpoints']} checkpoints, "
               f"{count['versions']} versions, {len(lines)} line(s)" + (", checked against C++" if want else ""))
    if chk.problems:
        return "FAIL", summary + "\n      " + "\n      ".join(chk.problems)
    return "ok", summary


def load_expected() -> Dict[str, dict]:
    """The C++ expectations by absolute fixture path."""
    out: Dict[str, dict] = {}
    v2 = FIXTURES / "v2/expected.json"
    if v2.exists():
        for name, want in json.loads(v2.read_text()).items():
            out[str((v2.parent / name).resolve())] = want
    br = FIXTURES / "branches/expected.json"
    if br.exists():
        for name, want in json.loads(br.read_text()).get("files", {}).items():
            if name.endswith(".ttd"):
                out[str((br.parent / name).resolve())] = want
    ch = FIXTURES / "v2/chunked/chunked.json"
    if ch.exists():
        want = json.loads(ch.read_text())
        out[str((ch.parent / want["file"]).resolve())] = want
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="*", type=Path, help="session files (default: every .ttd under testdata/ttd)")
    ap.add_argument("--out", type=Path, default=ROOT / "scratch/ttd-ksy", help="where the parser is generated")
    ap.add_argument("--ksc", help="the compiler (default: kaitai-struct-compiler or ksc on PATH)")
    ap.add_argument("--ksy", type=Path, default=KSY, help="the schema (default: core/src/debugger/ttd/ttd.ksy)")
    args = ap.parse_args()

    ksc = find_compiler(args.ksc)
    if not ksc:
        return skip("kaitai-struct-compiler is not installed (brew install kaitai-struct-compiler; needs a JRE)")
    try:
        from kaitaistruct import KaitaiStream
    except ImportError:
        return skip("the kaitaistruct runtime is not installed (pip install kaitaistruct, in a venv)")
    try:
        import zstandard  # noqa: F401
    except ImportError:
        return skip("zstandard is not installed (pip install zstandard; tools/python/ttd/requirements.txt)")

    Ttd = generate(ksc, args.ksy, args.out)
    files = args.files or sorted(FIXTURES.rglob("*.ttd"))
    expected = load_expected()
    tally = {"ok": 0, "damaged": 0, "FAIL": 0}
    for f in files:
        verdict, text = check_file(Ttd, KaitaiStream, f, expected)
        tally[verdict] += 1
        try:
            name = f.resolve().relative_to(ROOT)
        except ValueError:
            name = f
        print(f"{verdict:8s}{name}: {text}")
    print(f"{len(files)} file(s): {tally['ok']} ok, {tally['damaged']} damaged (parse only), "
          f"{tally['FAIL']} failed")
    return 1 if tally["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
