#!/usr/bin/env python3
"""tap-audit — independent TAP framing audit.

Walks .tap dumps with a parser independent of the emulator (u16 length +
body blocks, ROM header decode, XOR parity) and prints one line per block
plus any findings. Used to vet the Tape Manager catalog against real-world
dumps without trusting the loader to check itself.

Findings come in two severities:
  !! anomaly  — the framing walk is broken (truncated block, trailing byte,
                zero-length block). Exit code 1.
  ~  note     — valid-but-noteworthy content the catalog handles by design
                (non-standard flag bytes, invalid parity). Exit code 0.

Exit codes: 0 = clean walk, 1 = anomalies found, 2 = usage/environment error.
"""

import os
import struct
import sys

TYPES = {0: "Program", 1: "Number array", 2: "Character array", 3: "Code"}


def audit_file(path):
    """Walk one TAP file. Returns (blocks, anomalies, notes)."""
    with open(path, "rb") as fh:
        data = fh.read()

    blocks = []
    anomalies = []
    notes = []
    offset = 0
    index = 0

    while offset < len(data):
        if offset + 2 > len(data):
            anomalies.append(f"block {index}: trailing byte at {offset:#x} (no u16 length)")
            break
        (block_len,) = struct.unpack_from("<H", data, offset)
        body = data[offset + 2 : offset + 2 + block_len]
        if len(body) < block_len:
            anomalies.append(
                f"block {index}: declared {block_len} bytes but only {len(body)} remain (truncated)"
            )
        offset += 2 + block_len

        block = {
            "index": index,
            "len": block_len,
            "flag": body[0] if body else None,
            "parity_ok": None,
            "type": None,
            "name": None,
            "declared": None,
        }

        if not body:
            anomalies.append(f"block {index}: zero-length block")
        else:
            parity = 0
            for byte in body:
                parity ^= byte
            block["parity_ok"] = parity == 0
            if parity != 0:
                notes.append(f"block {index}: XOR parity != 0 (invalid checksum)")

            if block_len == 19 and body[0] == 0x00:
                type_byte = body[1]
                block["type"] = TYPES.get(type_byte, f"UNKNOWN({type_byte})")
                if type_byte > 3:
                    anomalies.append(f"block {index}: header type {type_byte} > 3 (invalid)")
                raw_name = bytes(b & 0x7F for b in body[2:12]).split(b"\0")[0]
                block["name"] = raw_name.decode("ascii", "replace").rstrip(" ")
                block["declared"] = struct.unpack_from("<H", body, 12)[0]
            elif body[0] not in (0x00, 0xFF):
                notes.append(f"block {index}: non-standard flag byte {body[0]:#04x}")

        blocks.append(block)
        index += 1

    return blocks, anomalies, notes


def main(argv):
    if len(argv) < 2:
        print("usage: tap-audit.py <file.tap> [more.tap ...]", file=sys.stderr)
        return 2

    total_anomalies = 0
    for path in argv[1:]:
        if not os.path.isfile(path):
            print(f"MISSING: {path}", file=sys.stderr)
            return 2
        blocks, anomalies, notes = audit_file(path)
        print(f"=== {os.path.relpath(path)} — {len(blocks)} block(s)")
        pair_pending = None
        for b in blocks:
            note = ""
            if b["flag"] == 0x00 and b["len"] == 19 and b["type"] in TYPES.values():
                pair_pending = b["index"]
                note = f"  {b['type']} '{b['name']}' declared={b['declared']}"
            elif b["flag"] == 0xFF:
                if pair_pending is not None:
                    note = f"  pairs with header #{pair_pending}"
                    pair_pending = None
                else:
                    note = "  headerless data"
            print(
                f"  [{b['index']:3d}] len={b['len']:6d} flag={b['flag']:#04x} "
                f"parity={'ok' if b['parity_ok'] else 'BAD' if b['parity_ok'] is not None else '-'}{note}"
            )
        for n in notes:
            print(f"  ~  {n}")
        for a in anomalies:
            print(f"  !! {a}")
            total_anomalies += 1

    if total_anomalies:
        print(f"\naudit: {total_anomalies} anomaly/anomalies across {len(argv) - 1} file(s)")
        return 1
    print(f"\naudit: clean walk across {len(argv) - 1} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
