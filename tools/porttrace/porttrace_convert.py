#!/usr/bin/env python3
"""porttrace_convert.py — Unreal-NG Port Access Trace converter and analyzer.

Standalone (no emulator required). Reads traces saved by the Port Diagnostic
Recorder (`port-trace save`, `porttrace_save()`, or the WebAPI save endpoint)
in JSON / CSV / binary form and converts between formats, filters events, and
produces summary and decode-strictness analyses.

Usage:
  porttrace_convert.py trace.json --to csv -o trace.csv
  porttrace_convert.py trace.bin  --to markdown -o report.md
  porttrace_convert.py trace.json --summary
  porttrace_convert.py trace.json --filter-unmapped --to text
  porttrace_convert.py trace.json --analyze-strictness
  porttrace_convert.py --selftest

Formats are self-describing: exports embed the model's decode-rule table
(mask/match/port), so --analyze-strictness never hardcodes per-model masks.
Design: docs/inprogress/2026-08-24-diagnostic-observability/
"""

import argparse
import csv
import io
import json
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Tuple

# ── Constants (mirror core/src/emulator/ports/portdiagrecorder.h) ──────────

DEVICE_NAMES = {
    0x00: "None",
    0x01: "ULA_FE",
    0x02: "Memory_7FFD",
    0x03: "Memory_1FFD",
    0x04: "AY_FFFD",
    0x05: "AY_BFFD",
    0x06: "WD1793_Status",
    0x07: "WD1793_Track",
    0x08: "WD1793_Sector",
    0x09: "WD1793_Data",
    0x0A: "Beta128_System",
    0x0B: "Covox",
    0x0C: "Memory_DFFD",
    0x0E: "Custom",
}
DEVICE_IDS = {name: dev_id for dev_id, name in DEVICE_NAMES.items()}

FLAG_DIRECTION_OUT = 1 << 0
FLAG_WAS_DECODED = 1 << 1
FLAG_HAD_HANDLER = 1 << 2
FLAG_BETA128_GATED = 1 << 3
FLAG_HANDLED_INLINE = 1 << 4
FLAG_CF_TRDOS = 1 << 5
FLAG_VIA_LEGACY = 1 << 6

RULE_NO_MATCH = 0xFF
RULE_BDI_FALLBACK = 0xFE
RULE_NO_TABLE = 0xFD

# Binary format v1: 32-byte header, decode-rule table, packed 24-byte events.
# Event field order matches the C++ PortTraceEvent layout (static_assert-pinned):
#   u64 timestamp, u32 frame, u16 raw, u16 dec, u16 pc, u8 val, u8 rule, u8 dev, u8 flags, 2 pad
BINARY_MAGIC = b"PTRC"
BINARY_EVENT = struct.Struct("<QIHHHBBBBxx")
BINARY_RULE = struct.Struct("<HHH")

# Binary format v2 ("PTR2", .binz): same 32-byte header prefix (compressedSize
# u64 at offset 20), decode-rule table, then ONE zstd frame containing the
# columnar delta/xor payload (22 bytes/event):
#   u64 tsDelta[n], u32 frameDelta[n], u16 rawXor[n], u16 decXor[n],
#   u16 pcXor[n], u8 value[n], u8 rule[n], u8 dev[n], u8 flags[n]
# (deltas/xors are against the previous event; first event vs zero)
BINARY2_MAGIC = b"PTR2"
V2_BYTES_PER_EVENT = 22


def _zstd_decompress(buf: bytes, expected_size: int) -> bytes:
    """Decompress a zstd frame using whatever this interpreter has:
    stdlib compression.zstd (3.14+) -> zstandard package -> zstd CLI binary.
    Raises RuntimeError with remediation hints when none is available —
    including the emulator's WebAPI readfile endpoint (see --via-webapi)."""
    try:
        from compression import zstd as _z  # Python 3.14+
        return _z.decompress(buf)
    except ImportError:
        pass
    try:
        import zstandard
        return zstandard.ZstdDecompressor().decompress(buf, max_output_size=expected_size)
    except ImportError:
        pass
    import subprocess
    try:
        result = subprocess.run(["zstd", "-d", "-c"], input=buf, capture_output=True)
        if result.returncode == 0:
            return result.stdout
        raise RuntimeError(f"zstd CLI failed: {result.stderr.decode(errors='replace').strip()}")
    except FileNotFoundError:
        raise RuntimeError(
            "No zstd available to read this compressed trace. Options: "
            "run on Python 3.14+, `pip install zstandard`, install the `zstd` CLI, "
            "or use --via-webapi URL to let the emulator core decompress it") from None


def _zstd_compress(buf: bytes, level: int = 19) -> bytes:
    try:
        from compression import zstd as _z  # Python 3.14+
        return _z.compress(buf, level)
    except ImportError:
        pass
    try:
        import zstandard
        return zstandard.ZstdCompressor(level=level).compress(buf)
    except ImportError:
        pass
    import subprocess
    try:
        result = subprocess.run(["zstd", f"-{level}", "-c"], input=buf, capture_output=True)
        if result.returncode == 0:
            return result.stdout
        raise RuntimeError(f"zstd CLI failed: {result.stderr.decode(errors='replace').strip()}")
    except FileNotFoundError:
        raise RuntimeError("No zstd available to write a compressed trace "
                           "(Python 3.14+, `pip install zstandard`, or the `zstd` CLI)") from None


def encode_v2_payload(events: List["PortTraceEvent"]) -> bytes:
    out = bytearray()
    prev = 0
    for e in events:
        out += struct.pack("<Q", (e.timestamp - prev) & 0xFFFFFFFFFFFFFFFF)
        prev = e.timestamp
    prev = 0
    for e in events:
        out += struct.pack("<I", (e.frame - prev) & 0xFFFFFFFF)
        prev = e.frame
    for field in ("raw_port", "decoded_port", "pc"):
        prev = 0
        for e in events:
            v = getattr(e, field)
            out += struct.pack("<H", v ^ prev)
            prev = v
    for field in ("value", "decode_rule", "device_id", "flags"):
        out += bytes(getattr(e, field) for e in events)
    return bytes(out)


def decode_v2_payload(payload: bytes, n: int) -> List["PortTraceEvent"]:
    if len(payload) != n * V2_BYTES_PER_EVENT:
        raise ValueError(f"PTR2 payload size mismatch: {len(payload)} != {n * V2_BYTES_PER_EVENT}")

    off = 0
    ts, acc = [], 0
    for d in struct.unpack_from(f"<{n}Q", payload, off):
        acc = (acc + d) & 0xFFFFFFFFFFFFFFFF
        ts.append(acc)
    off += 8 * n
    frames, acc = [], 0
    for d in struct.unpack_from(f"<{n}I", payload, off):
        acc = (acc + d) & 0xFFFFFFFF
        frames.append(acc)
    off += 4 * n

    cols16 = []
    for _ in range(3):  # raw, dec, pc
        col, acc = [], 0
        for x in struct.unpack_from(f"<{n}H", payload, off):
            acc ^= x
            col.append(acc)
        cols16.append(col)
        off += 2 * n

    cols8 = []
    for _ in range(4):  # value, rule, dev, flags
        cols8.append(payload[off:off + n])
        off += n

    return [PortTraceEvent(ts[i], frames[i], cols16[0][i], cols16[1][i], cols16[2][i],
                           cols8[0][i], cols8[1][i], cols8[2][i], cols8[3][i])
            for i in range(n)]


@dataclass
class PortTraceEvent:
    timestamp: int
    frame: int
    raw_port: int
    decoded_port: int
    pc: int
    value: int
    decode_rule: int
    device_id: int
    flags: int

    @property
    def direction(self) -> str:
        return "OUT" if (self.flags & FLAG_DIRECTION_OUT) else "IN"

    @property
    def decoded(self) -> bool:
        return bool(self.flags & FLAG_WAS_DECODED)

    @property
    def had_handler(self) -> bool:
        return bool(self.flags & FLAG_HAD_HANDLER)

    @property
    def beta128_gated(self) -> bool:
        return bool(self.flags & FLAG_BETA128_GATED)

    @property
    def handled_inline(self) -> bool:
        return bool(self.flags & FLAG_HANDLED_INLINE)

    @property
    def cf_trdos(self) -> bool:
        return bool(self.flags & FLAG_CF_TRDOS)

    @property
    def via_legacy(self) -> bool:
        return bool(self.flags & FLAG_VIA_LEGACY)

    @property
    def device_name(self) -> str:
        return DEVICE_NAMES.get(self.device_id, f"UNKNOWN_{self.device_id:#04x}")

    def flags_string(self) -> str:
        out = ""
        if self.decoded: out += "D"
        if self.had_handler: out += "H"
        if self.beta128_gated: out += "G"
        if self.handled_inline: out += "I"
        if self.cf_trdos: out += "T"
        if self.via_legacy: out += "L"
        return out


@dataclass
class SessionInfo:
    emulator_id: str = ""
    model: str = ""
    tstates_per_frame: int = 0
    filter_desc: str = "All ports"
    capacity: int = 0
    total_captured: int = 0
    total_evicted: int = 0
    total_filtered: int = 0
    decode_rules: List[Tuple[int, int, int]] = field(default_factory=list)  # (mask, match, port)


# ── Readers ────────────────────────────────────────────────────────────────

def read_json(path: Path) -> Tuple[SessionInfo, List[PortTraceEvent]]:
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    s = data.get("session", {})
    session = SessionInfo(
        emulator_id=s.get("emulator_id", ""),
        model=s.get("model", ""),
        tstates_per_frame=s.get("tstates_per_frame", 0),
        filter_desc=s.get("filter", "All ports"),
        capacity=s.get("capacity", 0),
        total_captured=s.get("total_captured", 0),
        total_evicted=s.get("total_evicted", 0),
        total_filtered=s.get("total_filtered", 0),
        decode_rules=[(r["mask"], r["match"], r["port"]) for r in data.get("decode_rules", [])],
    )
    events = [
        PortTraceEvent(
            timestamp=e["ts"], frame=e["frame"], raw_port=e["raw"], decoded_port=e["dec"],
            pc=e["pc"], value=e["val"], decode_rule=e["rule"], device_id=e["dev"], flags=e["flags"],
        )
        for e in data.get("events", [])
    ]
    return session, events


def read_csv(path: Path) -> Tuple[SessionInfo, List[PortTraceEvent]]:
    session = SessionInfo()
    body_lines = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                if line.startswith("# Model:"):
                    parts = line[len("# Model:"):].split(", Emulator:")
                    session.model = parts[0].strip()
                    if len(parts) > 1:
                        session.emulator_id = parts[1].strip()
                elif line.startswith("# TStatesPerFrame:"):
                    session.tstates_per_frame = int(line.split(":", 1)[1].strip())
                elif line.startswith("# Filter:"):
                    session.filter_desc = line.split(":", 1)[1].strip()
                elif line.startswith("# DecodeRule"):
                    fields = dict(part.split("=") for part in line.split(":", 1)[1].split())
                    session.decode_rules.append(
                        (int(fields["mask"], 16), int(fields["match"], 16), int(fields["port"], 16)))
            else:
                body_lines.append(line)

    events = []
    reader = csv.DictReader(io.StringIO("".join(body_lines)))
    for row in reader:
        flags = (
            (FLAG_DIRECTION_OUT if row["direction"] == "OUT" else 0)
            | (FLAG_WAS_DECODED if row["decoded"] == "1" else 0)
            | (FLAG_HAD_HANDLER if row["had_handler"] == "1" else 0)
            | (FLAG_BETA128_GATED if row["beta128_gated"] == "1" else 0)
            | (FLAG_HANDLED_INLINE if row["handled_inline"] == "1" else 0)
            | (FLAG_CF_TRDOS if row.get("cf_trdos") == "1" else 0)
            | (FLAG_VIA_LEGACY if row.get("via_legacy") == "1" else 0)
        )
        events.append(PortTraceEvent(
            timestamp=int(row["timestamp"]),
            frame=int(row["frame"]),
            raw_port=int(row["raw_port"], 16),
            decoded_port=int(row["decoded_port"], 16),
            pc=int(row["pc"], 16),
            value=int(row["value"], 16),
            decode_rule=int(row["decode_rule"]),
            device_id=DEVICE_IDS.get(row["device"], 0x0E),
            flags=flags,
        ))
    return session, events


def read_binary(path: Path) -> Tuple[SessionInfo, List[PortTraceEvent]]:
    session = SessionInfo()
    with open(path, "rb") as f:
        header = f.read(32)
        if len(header) < 32 or header[:4] not in (BINARY_MAGIC, BINARY2_MAGIC):
            raise ValueError(f"Not a PTRC/PTR2 trace: {path}")
        is_v2 = header[:4] == BINARY2_MAGIC
        version = struct.unpack_from("<H", header, 4)[0]
        if version != (2 if is_v2 else 1):
            raise ValueError(f"Unsupported trace version {version}")
        count = struct.unpack_from("<I", header, 6)[0]
        session.capacity = struct.unpack_from("<I", header, 10)[0]
        session.tstates_per_frame = struct.unpack_from("<I", header, 14)[0]
        rule_count = struct.unpack_from("<H", header, 18)[0]
        compressed_size = struct.unpack_from("<Q", header, 20)[0] if is_v2 else 0

        for _ in range(rule_count):
            session.decode_rules.append(BINARY_RULE.unpack(f.read(BINARY_RULE.size)))

        if is_v2:
            frame_data = f.read(compressed_size)
            if len(frame_data) < compressed_size:
                raise ValueError("Truncated PTR2 trace")
            payload = _zstd_decompress(frame_data, count * V2_BYTES_PER_EVENT)
            events = decode_v2_payload(payload, count)
        else:
            events = []
            for _ in range(count):
                data = f.read(BINARY_EVENT.size)
                if len(data) < BINARY_EVENT.size:
                    raise ValueError("Truncated PTRC trace")
                ts, frame, raw, dec, pc, val, rule, dev, flags = BINARY_EVENT.unpack(data)
                events.append(PortTraceEvent(ts, frame, raw, dec, pc, val, rule, dev, flags))

    session.total_captured = len(events)
    return session, events


def read_via_webapi(url: str, path: Path, emulator: str = "") -> Tuple[SessionInfo, List[PortTraceEvent]]:
    """Let the emulator core read (and decompress) a saved binary trace:
    POST /profiler/porttrace/readfile. Needs a running instance on the same
    machine as the file; no local zstd required."""
    import json as _json
    import urllib.request

    base = url.rstrip("/")

    def post(p, body):
        req = urllib.request.Request(base + p, data=_json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            return _json.loads(resp.read())

    if not emulator:
        with urllib.request.urlopen(base + "/api/v1/emulator", timeout=10) as resp:
            listing = _json.loads(resp.read())
        emulators = listing.get("emulators", [])
        if not emulators:
            raise ValueError("--via-webapi: no emulator instances running")
        emulator = emulators[0]["id"]

    data = post(f"/api/v1/emulator/{emulator}/profiler/porttrace/readfile",
                {"path": str(Path(path).resolve())})
    session = SessionInfo(
        tstates_per_frame=data.get("session", {}).get("tstates_per_frame", 0),
        decode_rules=[(r["mask"], r["match"], r["port"]) for r in data.get("decode_rules", [])],
    )
    events = [PortTraceEvent(e["ts"], e["frame"], e["raw"], e["dec"], e["pc"], e["val"],
                             e["rule"], e["dev"], e["flags"]) for e in data.get("events", [])]
    session.total_captured = len(events)
    return session, events


def read_any(path: Path, via_webapi: str = "") -> Tuple[SessionInfo, List[PortTraceEvent]]:
    with open(path, "rb") as f:
        head = f.read(4)
    if head in (BINARY_MAGIC, BINARY2_MAGIC):
        if via_webapi:
            return read_via_webapi(via_webapi, path)
        return read_binary(path)
    if path.suffix.lower() == ".csv":
        return read_csv(path)
    return read_json(path)


# ── Writers ────────────────────────────────────────────────────────────────

def write_json(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    data = {
        "format": "unreal-ng-porttrace-v1",
        "session": {
            "emulator_id": session.emulator_id,
            "model": session.model,
            "tstates_per_frame": session.tstates_per_frame,
            "filter": session.filter_desc,
            "capacity": session.capacity,
            "total_captured": session.total_captured,
            "total_evicted": session.total_evicted,
            "total_filtered": session.total_filtered,
        },
        "decode_rules": [
            {"index": i, "mask": m, "match": v, "port": p}
            for i, (m, v, p) in enumerate(session.decode_rules)
        ],
        "device_map": {str(k): v for k, v in DEVICE_NAMES.items()},
        "events": [
            {"ts": e.timestamp, "frame": e.frame, "raw": e.raw_port, "dec": e.decoded_port,
             "rule": e.decode_rule, "val": e.value, "pc": e.pc, "dev": e.device_id, "flags": e.flags}
            for e in events
        ],
    }
    json.dump(data, out, indent=2)
    out.write("\n")


def write_csv(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write("# Unreal-NG Port Access Trace v1\n")
    out.write(f"# Model: {session.model}, Emulator: {session.emulator_id}\n")
    out.write(f"# TStatesPerFrame: {session.tstates_per_frame}\n")
    out.write(f"# Filter: {session.filter_desc}\n")
    out.write(f"# Events: {len(events)} ({session.total_evicted} evicted, "
              f"{session.total_filtered} filtered out)\n")
    for i, (mask, match, port) in enumerate(session.decode_rules):
        out.write(f"# DecodeRule {i}: mask=0x{mask:04X} match=0x{match:04X} port=0x{port:04X}\n")
    writer = csv.writer(out)
    writer.writerow(["index", "timestamp", "frame", "direction", "raw_port", "decoded_port",
                     "decode_rule", "value", "pc", "device", "decoded", "had_handler",
                     "beta128_gated", "handled_inline", "cf_trdos", "via_legacy"])
    for i, e in enumerate(events):
        writer.writerow([i, e.timestamp, e.frame, e.direction,
                         f"0x{e.raw_port:04X}", f"0x{e.decoded_port:04X}", e.decode_rule,
                         f"0x{e.value:02X}", f"0x{e.pc:04X}", e.device_name,
                         int(e.decoded), int(e.had_handler), int(e.beta128_gated),
                         int(e.handled_inline), int(e.cf_trdos), int(e.via_legacy)])


def write_markdown(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write("## Port Access Trace\n\n")
    out.write(f"**Model**: {session.model} | **Filter**: {session.filter_desc} "
              f"| **Events**: {len(events)}\n\n")
    out.write("| # | Frame | T-State | Dir | Raw | Decoded | Value | PC | Device | Flags |\n")
    out.write("|---|-------|---------|-----|-----|---------|-------|----|--------|-------|\n")
    for i, e in enumerate(events):
        out.write(f"| {i} | {e.frame} | {e.timestamp} | {e.direction} "
                  f"| {e.raw_port:04X} | {e.decoded_port:04X} | {e.value:02X} "
                  f"| {e.pc:04X} | {e.device_name} | {e.flags_string()} |\n")


def write_text(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write(f"Port Access Trace: {session.model} ({session.emulator_id})\n")
    out.write(f"Filter: {session.filter_desc} | Events: {len(events)}\n")
    out.write("=" * 88 + "\n")
    out.write(f" {'#':>4}  {'Frame':>6}  {'T-State':>13}  Dir  {'Raw':>5}  "
              f"{'Decoded':>7}  {'Value':>5}  {'PC':>5}  {'Device':<14} Flags\n")
    out.write(f" {'-'*4}  {'-'*6}  {'-'*13}  ---  {'-'*5}  {'-'*7}  {'-'*5}  {'-'*5}  {'-'*14} {'-'*6}\n")
    for i, e in enumerate(events):
        out.write(f" {i:>4}  {e.frame:>6}  {e.timestamp:>13}  {e.direction:<3}  "
                  f"{e.raw_port:04X}   {e.decoded_port:04X}     {e.value:02X}     "
                  f"{e.pc:04X}   {e.device_name:<14} {e.flags_string()}\n")
    out.write("Flags: D=decoded H=hadHandler G=beta128Gated I=handledInline T=cfTrdos L=legacyPath\n")


def write_binz(session: SessionInfo, events: List[PortTraceEvent], path: Path) -> None:
    """Write the compressed PTR2 v2 container (mirrors the C++ writer)."""
    payload = encode_v2_payload(events)
    frame = _zstd_compress(payload)
    with open(path, "wb") as f:
        header = bytearray(32)
        header[:4] = BINARY2_MAGIC
        struct.pack_into("<H", header, 4, 2)
        struct.pack_into("<I", header, 6, len(events))
        struct.pack_into("<I", header, 10, session.capacity)
        struct.pack_into("<I", header, 14, session.tstates_per_frame)
        struct.pack_into("<H", header, 18, len(session.decode_rules))
        struct.pack_into("<Q", header, 20, len(frame))
        f.write(header)
        for rule in session.decode_rules:
            f.write(BINARY_RULE.pack(*rule))
        f.write(frame)


def write_summary(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    out.write("Port Access Trace Summary\n" + "=" * 30 + "\n")
    out.write(f"Model:    {session.model}\n")
    out.write(f"Filter:   {session.filter_desc}\n")
    out.write(f"Events:   {len(events)} shown, {session.total_evicted} evicted, "
              f"{session.total_filtered} filtered out at capture\n\n")

    if not events:
        return

    dirs = Counter(e.direction for e in events)
    out.write("By Direction:\n")
    for d, c in dirs.most_common():
        out.write(f"  {d:3}:  {c:>6} ({100*c/len(events):5.1f}%)\n")

    devs = Counter(e.device_name for e in events)
    out.write("\nBy Device:\n")
    bar_max = 20
    for d, c in devs.most_common():
        filled = int(bar_max * c / len(events))
        out.write(f"  {d:<18} {c:>6}  ({100*c/len(events):5.1f}%)   "
                  f"{'#' * filled}{'.' * (bar_max - filled)}\n")

    ports = Counter(f"0x{e.decoded_port:04X}" if e.decoded_port else "unmapped" for e in events)
    out.write("\nBy Decoded Port:\n")
    for p, c in ports.most_common(16):
        out.write(f"  {p:<8} {c:>6}  ({100*c/len(events):5.1f}%)\n")

    unmapped = [e for e in events if e.decoded_port == 0 and not e.beta128_gated]
    if unmapped:
        raw_unmapped = Counter(f"0x{e.raw_port:04X}" for e in unmapped)
        out.write("\nUnmapped Port Addresses (raw):\n  ")
        out.write("  ".join(f"{p} x{c}" for p, c in raw_unmapped.most_common(12)))
        out.write("\n")

    gated = sum(1 for e in events if e.beta128_gated)
    if gated:
        out.write(f"\nBeta128-gated accesses: {gated}\n")

    rules = Counter(e.decode_rule for e in events)
    out.write("\nDecode Rule Distribution:\n")
    for rule, c in sorted(rules.items()):
        if rule == RULE_NO_MATCH:
            name = "No match (unmapped)"
        elif rule == RULE_BDI_FALLBACK:
            name = "BDI fallback (#1F/#3F/#5F/#7F)"
        elif rule == RULE_NO_TABLE:
            name = "If-chain decoder (no table)"
        elif rule < len(session.decode_rules):
            mask, match, port = session.decode_rules[rule]
            name = f"Rule {rule} (mask=0x{mask:04X} match=0x{match:04X} -> 0x{port:04X})"
        else:
            name = f"Rule {rule}"
        out.write(f"  {name}: {c}\n")


def write_strictness(session: SessionInfo, events: List[PortTraceEvent], out) -> None:
    """Near-miss analysis for unmapped events: which decode rule would have
    matched if exactly one masked bit were ignored? Uses the decode-rule table
    embedded in the trace (use-case category 2: over-strict decode)."""
    unmapped = [e for e in events if e.decoded_port == 0 and not e.beta128_gated]
    out.write(f"Strictness Analysis: {len(unmapped)} unmapped events\n" + "=" * 45 + "\n")

    if not session.decode_rules:
        out.write("Trace carries no decode-rule table (if-chain decoder model) - "
                  "near-miss analysis unavailable.\n")
        return
    if not unmapped:
        out.write("No unmapped events - nothing to analyze.\n")
        return

    near_misses = {}  # raw_port -> (count, [(rule_index, wrong_bit, port), ...])
    genuinely_unmapped = Counter()
    for e in unmapped:
        candidates = []
        for idx, (mask, match, port) in enumerate(session.decode_rules):
            mismatch = (e.raw_port & mask) ^ match
            if mismatch and (mismatch & (mismatch - 1)) == 0:  # exactly one wrong bit
                candidates.append((idx, mismatch, port))
        if candidates:
            key = e.raw_port
            count = near_misses.get(key, (0, candidates))[0] + 1
            near_misses[key] = (count, candidates)
        else:
            genuinely_unmapped[e.raw_port] += 1

    if near_misses:
        out.write(f"WARNING: {sum(c for c, _ in near_misses.values())} unmapped events "
                  f"may be strict-decode rejects:\n\n")
        for raw, (count, candidates) in sorted(near_misses.items()):
            out.write(f"  rawPort=0x{raw:04X} (x{count}):\n")
            for idx, wrong_bit, port in candidates:
                bit = wrong_bit.bit_length() - 1
                mask, match, _ = session.decode_rules[idx]
                out.write(f"    near-miss for 0x{port:04X} (rule {idx}, mask 0x{mask:04X}): "
                          f"requires A{bit}={1 if (match >> bit) & 1 else 0}, "
                          f"bus had A{bit}={1 if (raw >> bit) & 1 else 0} "
                          f"-> would decode if A{bit} were dropped from the mask\n")
    if genuinely_unmapped:
        out.write(f"\n{sum(genuinely_unmapped.values())} events appear genuinely unmapped "
                  f"(no single-bit near-miss):\n  ")
        out.write("  ".join(f"0x{p:04X} x{c}" for p, c in genuinely_unmapped.most_common(12)))
        out.write("\n")


# ── Filters ────────────────────────────────────────────────────────────────

def apply_filters(events: List[PortTraceEvent], args) -> List[PortTraceEvent]:
    if args.filter_port:
        port = int(args.filter_port, 16)
        events = [e for e in events if e.decoded_port == port or e.raw_port == port]
    if args.filter_device:
        name = args.filter_device
        events = [e for e in events if e.device_name.lower() == name.lower()]
    if args.filter_direction:
        d = args.filter_direction.upper()
        events = [e for e in events if e.direction == d]
    if args.filter_pc:
        lo, hi = (int(x, 16) for x in args.filter_pc.split("-"))
        events = [e for e in events if lo <= e.pc <= hi]
    if args.filter_unmapped:
        events = [e for e in events if e.decoded_port == 0 and not e.beta128_gated]
    return events


# ── Self-test ──────────────────────────────────────────────────────────────

def selftest() -> int:
    """Round-trip a synthetic trace through binary -> json -> csv readers."""
    import tempfile

    session = SessionInfo(model="Pentagon", tstates_per_frame=71680,
                          decode_rules=[(0xC002, 0xC000, 0xFFFD), (0x8006, 0x0004, 0x7FFD)])
    events = [
        PortTraceEvent(1000, 1, 0xFEFD, 0xFFFD, 0x8000, 0xFE, 0, 0x04, FLAG_DIRECTION_OUT | FLAG_WAS_DECODED),
        PortTraceEvent(1011, 1, 0x7FF9, 0x0000, 0x8005, 0x10, RULE_NO_MATCH, 0x00, FLAG_DIRECTION_OUT),
        PortTraceEvent(1022, 1, 0x001F, 0x001F, 0x3D00, 0x88, RULE_BDI_FALLBACK, 0x06,
                       FLAG_DIRECTION_OUT | FLAG_WAS_DECODED | FLAG_HAD_HANDLER | FLAG_CF_TRDOS),
    ]

    with tempfile.TemporaryDirectory() as tmp:
        # Binary round-trip (write mirrors the C++ layout)
        bin_path = Path(tmp) / "t.bin"
        with open(bin_path, "wb") as f:
            header = bytearray(32)
            header[:4] = BINARY_MAGIC
            struct.pack_into("<H", header, 4, 1)
            struct.pack_into("<I", header, 6, len(events))
            struct.pack_into("<I", header, 10, 65536)
            struct.pack_into("<I", header, 14, session.tstates_per_frame)
            struct.pack_into("<H", header, 18, len(session.decode_rules))
            f.write(header)
            for rule in session.decode_rules:
                f.write(BINARY_RULE.pack(*rule))
            for e in events:
                f.write(BINARY_EVENT.pack(e.timestamp, e.frame, e.raw_port, e.decoded_port,
                                          e.pc, e.value, e.decode_rule, e.device_id, e.flags))
        s2, ev2 = read_binary(bin_path)
        assert ev2 == events, "binary round-trip mismatch"
        assert s2.decode_rules == session.decode_rules

        # JSON round-trip
        json_path = Path(tmp) / "t.json"
        with open(json_path, "w", encoding="utf-8") as f:
            write_json(session, events, f)
        s3, ev3 = read_json(json_path)
        assert ev3 == events, "json round-trip mismatch"
        assert s3.decode_rules == session.decode_rules

        # CSV round-trip
        csv_path = Path(tmp) / "t.csv"
        with open(csv_path, "w", encoding="utf-8", newline="") as f:
            write_csv(session, events, f)
        s4, ev4 = read_csv(csv_path)
        assert ev4 == events, "csv round-trip mismatch"
        assert s4.decode_rules == session.decode_rules

        # Strictness: 0x7FF9 must be reported as a near-miss for 0x7FFD (A2)
        buf = io.StringIO()
        write_strictness(session, events, buf)
        assert "near-miss for 0x7FFD" in buf.getvalue(), "strictness analysis failed"
        assert "A2" in buf.getvalue()

        # PTR2 v2 round-trip: transform is always testable; the zstd frame
        # needs a zstd source (stdlib 3.14+/zstandard/CLI)
        assert decode_v2_payload(encode_v2_payload(events), len(events)) == events, \
            "v2 delta/xor transform round-trip mismatch"
        try:
            binz_path = Path(tmp) / "t.binz"
            write_binz(session, events, binz_path)
            s5, ev5 = read_binary(binz_path)
            assert ev5 == events, "PTR2 v2 round-trip mismatch"
            assert s5.decode_rules == session.decode_rules
            v2_note = f"v2 OK ({binz_path.stat().st_size} bytes)"
        except RuntimeError as exc:
            v2_note = f"v2 zstd round-trip SKIPPED ({exc})"

    print(f"porttrace_convert selftest: OK ({v2_note})")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description="Unreal-NG Port Access Trace converter/analyzer")
    parser.add_argument("input", nargs="?", help="Input trace file (.json, .csv, or .bin)")
    parser.add_argument("--to", choices=["json", "csv", "markdown", "text", "binz"], default="text",
                        help="Output format; binz = compressed PTR2 v2 (needs -o and a zstd source)")
    parser.add_argument("--via-webapi", metavar="URL",
                        help="Read binary/compressed input through a running emulator's WebAPI "
                             "readfile endpoint (core-side decompression; no local zstd needed)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("--summary", action="store_true", help="Print summary statistics")
    parser.add_argument("--analyze-strictness", action="store_true",
                        help="Near-miss analysis of unmapped events vs the embedded decode rules")
    parser.add_argument("--filter-port", help="Filter by port (hex, matches decoded or raw)")
    parser.add_argument("--filter-device", help="Filter by device name (e.g. AY_FFFD)")
    parser.add_argument("--filter-direction", choices=["in", "out", "IN", "OUT"])
    parser.add_argument("--filter-pc", help="Filter by PC range (hex: 3D00-3FFF)")
    parser.add_argument("--filter-unmapped", action="store_true", help="Only unmapped events")
    parser.add_argument("--selftest", action="store_true", help="Run the built-in round-trip self-test")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if not args.input:
        parser.error("input trace file required (or --selftest)")

    path = Path(args.input)
    if not path.exists():
        print(f"No such file: {path}", file=sys.stderr)
        return 1

    try:
        session, events = read_any(path, via_webapi=args.via_webapi or "")
    except (ValueError, KeyError, json.JSONDecodeError, RuntimeError, OSError) as exc:
        print(f"Failed to parse {path}: {exc}", file=sys.stderr)
        return 1

    events = apply_filters(events, args)

    if args.to == "binz" and not (args.summary or args.analyze_strictness):
        if not args.output:
            print("--to binz requires -o OUTPUT (binary format)", file=sys.stderr)
            return 1
        try:
            write_binz(session, events, Path(args.output))
        except RuntimeError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        return 0

    out = open(args.output, "w", encoding="utf-8", newline="") if args.output else sys.stdout
    try:
        if args.summary:
            write_summary(session, events, out)
        elif args.analyze_strictness:
            write_strictness(session, events, out)
        elif args.to == "json":
            write_json(session, events, out)
        elif args.to == "csv":
            write_csv(session, events, out)
        elif args.to == "markdown":
            write_markdown(session, events, out)
        else:
            write_text(session, events, out)
    finally:
        if args.output:
            out.close()

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        # Output piped into head/less that closed early — not an error
        sys.exit(0)
