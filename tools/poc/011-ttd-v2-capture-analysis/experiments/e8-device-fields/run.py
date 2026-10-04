#!/usr/bin/env python3
"""E8 - which device-state bytes change from frame to frame, and why.

Input: v1 sessions recorded by the benchmark (UNREAL_TTD_BENCH_KEEP_SESSIONS).
For every device blob of every checkpoint the state is decoded and compared
with the previous checkpoint's. Per device it reports:

- in how many frames the state changed, and the bytes per frame it would cost
  stored three ways: the zstd-compressed XOR with the previous state, the
  changed byte ranges (3-byte header per range, ranges closer than 3 equal
  bytes merged), and the smaller of the two; plus 2 bytes of version
  reference per changed frame;
- the fields that change: runs of adjacent byte offsets that change in at
  least 10% of the frames, with the share of frames in which they change and
  their step from frame to frame read as a little-endian unsigned number
  (runs of up to 8 bytes). A run whose step is the same in most frames is a
  counter that only advances with time: it can be derived from the clock
  (an anchor in the device, or a time field in the engine) instead of stored.

With --derive the listed byte runs are treated as derived from the machine
time (a device anchor or an engine time field): they are left out of the
comparison, which gives the bytes per frame that remain.

Usage:
    python3 run.py <session.ttd> [...] [--json out.json] [--derive spec.json]
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

import numpy as np
import zstandard

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "verification", "ttd-analyzer"))
from src import ttd_format as tf  # noqa: E402

GS_PERIPHERAL_ID = 5          # PeripheralId::GeneralSound: RAM after the fixed state
GS_FIXED_STATE_BYTES = 95     # the engine keeps the RAM as a region
FIELD_SHARE = 0.10            # a field: changes in at least this share of frames
VERSION_REF_BYTES = 2


def peripheral_names() -> dict:
    """PeripheralId names from ttdserializable.h"""
    path = os.path.join(ROOT, "core", "src", "debugger", "ttd", "ttdserializable.h")
    names = {}
    with open(path) as f:
        for m in re.finditer(r"^\s*(\w+)\s*=\s*(\d+),", f.read(), re.M):
            names[int(m.group(2))] = m.group(1)
    return names


def changed_offsets(prev: bytes, cur: bytes) -> np.ndarray:
    a = np.frombuffer(prev, dtype=np.uint8)
    b = np.frombuffer(cur, dtype=np.uint8)
    return np.nonzero(a != b)[0]


def ranges_cost(offsets: np.ndarray) -> int:
    """Changed byte ranges: 3 B header + bytes per range, gaps of up to 3 equal bytes merged"""
    if offsets.size == 0:
        return 0
    gaps = np.diff(offsets)
    breaks = np.nonzero(gaps > 4)[0]          # more than 3 equal bytes between two changes
    starts = np.concatenate(([offsets[0]], offsets[breaks + 1]))
    ends = np.concatenate((offsets[breaks] + 1, [offsets[-1] + 1]))
    return int(3 * starts.size + (ends - starts).sum())


def xor_cost(prev: bytes, cur: bytes, cctx) -> int:
    x = (np.frombuffer(prev, dtype=np.uint8) ^ np.frombuffer(cur, dtype=np.uint8)).tobytes()
    return len(cctx.compress(x))


def apply_mask(prev: bytes, state: bytes, runs) -> bytes:
    """`state` with the derived runs copied from `prev` (so they compare equal)"""
    if not runs:
        return state
    b = bytearray(state)
    for off, length in runs:
        b[off:off + length] = prev[off:off + length]
    return bytes(b)


def analyze(path: str, names: dict, derive: dict = None) -> dict:
    dump = tf.parse_file(path)
    cps = dump.checkpoints
    frames = len(cps) - 1
    cctx = zstandard.ZstdCompressor(level=1)
    devices = {}
    prev_states = {}
    for i, cp in enumerate(cps):
        for pid, raw in cp.peripheral_blobs.items():
            state = tf.decode_peripheral_blob(pid, raw)
            if pid == GS_PERIPHERAL_ID:
                state = state[:GS_FIXED_STATE_BYTES]
            d = devices.setdefault(pid, {
                "name": names.get(pid, f"id{pid}"), "size": len(state), "changed": 0,
                "xor": 0, "ranges": 0, "best": 0, "offset_changes": collections.Counter(),
                "steps": collections.defaultdict(collections.Counter), "resized": 0, "v1": 0,
            })
            if i > 0:
                d["v1"] += len(raw)   # v1 stores the whole wrapped blob in every checkpoint
            prev = prev_states.get(pid)
            prev_states[pid] = state
            if prev is None or i == 0:
                continue
            if derive and pid in derive and len(prev) == len(state):
                state = apply_mask(prev, state, derive[pid])
                prev_states[pid] = state
            if len(prev) != len(state):
                d["resized"] += 1
                d["changed"] += 1
                d["xor"] += len(cctx.compress(state)) + VERSION_REF_BYTES
                d["ranges"] += 3 + len(state) + VERSION_REF_BYTES
                d["best"] += min(len(cctx.compress(state)), 3 + len(state)) + VERSION_REF_BYTES
                continue
            if prev == state:
                continue
            d["changed"] += 1
            offs = changed_offsets(prev, state)
            x = xor_cost(prev, state, cctx)
            r = ranges_cost(offs)
            d["xor"] += x + VERSION_REF_BYTES
            d["ranges"] += r + VERSION_REF_BYTES
            d["best"] += min(x, r) + VERSION_REF_BYTES
            d["offset_changes"].update(offs.tolist())
    # Fields: runs of offsets changing in >= FIELD_SHARE of frames
    runs_by_device = {}
    for pid, d in devices.items():
        hot = sorted(o for o, c in d["offset_changes"].items() if c >= FIELD_SHARE * frames)
        runs = []
        for o in hot:
            if runs and o == runs[-1][1]:
                runs[-1][1] = o + 1
            else:
                runs.append([o, o + 1])
        runs_by_device[pid] = runs
    # Second pass: each field's step from frame to frame
    steps_by = collections.defaultdict(collections.Counter)
    prev_states = {}
    for i, cp in enumerate(cps):
        for pid, raw in cp.peripheral_blobs.items():
            if not runs_by_device.get(pid):
                continue
            state = tf.decode_peripheral_blob(pid, raw)
            if pid == GS_PERIPHERAL_ID:
                state = state[:GS_FIXED_STATE_BYTES]
            prev = prev_states.get(pid)
            prev_states[pid] = state
            if prev is None or len(prev) != len(state) or prev == state:
                continue
            for start, end in runs_by_device[pid]:
                if end - start > 8:
                    continue
                mask = (1 << (8 * (end - start))) - 1
                a = int.from_bytes(prev[start:end], "little")
                b = int.from_bytes(state[start:end], "little")
                steps_by[(pid, start)][(b - a) & mask] += 1
    out = {"path": os.path.basename(path), "frames": frames, "devices": []}
    for pid, d in sorted(devices.items()):
        fields = []
        for start, end in runs_by_device[pid]:
            length = end - start
            share = max(d["offset_changes"][o] for o in range(start, end)) / frames
            steps = steps_by.get((pid, start), collections.Counter())
            top = steps.most_common(2)
            fields.append({
                "offset": start, "length": length, "share": round(share, 3),
                "top_steps": [[s, c] for s, c in top],
                "distinct_steps": len(steps),
            })
        out["devices"].append({
            "id": pid, "name": d["name"], "size": d["size"], "frames_changed": d["changed"],
            "bpf_v1": round(d["v1"] / frames, 1),
            "bpf_xor": round(d["xor"] / frames, 1), "bpf_ranges": round(d["ranges"] / frames, 1),
            "bpf_best": round(d["best"] / frames, 1), "resized": d["resized"], "fields": fields,
        })
    return out


def print_report(r: dict) -> None:
    print(f"\n## {r['path']} ({r['frames']} frames)\n")
    print("| Device | State bytes | Frames changed | v1 B/frame | XOR B/frame | Ranges B/frame | Best B/frame |")
    print("|---|---|---|---|---|---|---|")
    total = 0.0
    total_v1 = 0.0
    for d in r["devices"]:
        total += d["bpf_best"]
        total_v1 += d["bpf_v1"]
        print(f"| {d['name']} ({d['id']}) | {d['size']} | {d['frames_changed']} | {d['bpf_v1']} | {d['bpf_xor']} | "
              f"{d['bpf_ranges']} | {d['bpf_best']} |")
    print(f"| **All** | | | **{total_v1:.1f}** | | | **{total:.1f}** |")
    print("\nFields changing in at least 10% of frames (offset, length, share of frames, most common steps):\n")
    for d in r["devices"]:
        if not d["fields"]:
            continue
        print(f"- {d['name']} ({d['id']}):")
        for f in d["fields"]:
            steps = ", ".join(f"{s} x{c}" for s, c in f["top_steps"]) if f["top_steps"] else "-"
            kind = ""
            if f["top_steps"] and f["distinct_steps"] <= 3 and f["top_steps"][0][1] >= 0.8 * r["frames"] * f["share"]:
                kind = " - steady counter"
            print(f"  - @{f['offset']} len {f['length']}: {f['share']:.0%}, steps {steps} "
                  f"({f['distinct_steps']} distinct){kind}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--json", default=None)
    parser.add_argument("--derive", default=None,
                        help="JSON {peripheral id: [[offset, length], ...]} of fields derived from time")
    args = parser.parse_args()
    names = peripheral_names()
    derive = None
    if args.derive:
        with open(args.derive) as f:
            derive = {int(k): v for k, v in json.load(f).items()}
    results = []
    for path in args.sessions:
        r = analyze(path, names, derive)
        print_report(r)
        results.append(r)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
