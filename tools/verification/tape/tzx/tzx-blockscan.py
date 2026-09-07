#!/usr/bin/env python3
"""TZX block-structure scanner for vetting tape fixtures.

Walks .tzx files per the TZX 1.13/1.20 block layout and prints a per-block-type
histogram per file. Detects bad magic, truncated blocks, and unknown block ids.

Usage:
    tzx-blockscan.py <file.tzx | glob>...    # scan specific files/dirs
    tzx-blockscan.py                          # scan *.tzx in current dir

Exit status is 0 only when every scanned file walks cleanly to EOF — usable as
a fixture gate. Note: "UNSUPPORTED BLOCK" means this scanner lacks the layout
for that id (e.g. $19 generalized data); the emulator's LoaderTZX remains the
authoritative parser — this tool is a fast pre-vet, not a replacement.

Block layout reference: TZX revision 1.13/1.20 specification (World of Spectrum
archive), which is also the source for core's tzx-loader-design.md.
"""
import argparse
import glob
import sys

BLOCKS = {
    0x10: 'std-data', 0x11: 'turbo', 0x12: 'pure-tone', 0x13: 'pulses', 0x14: 'pure-data',
    0x15: 'direct-rec', 0x18: 'csw', 0x19: 'gen-data', 0x20: 'silence', 0x21: 'group',
    0x22: 'jump', 0x23: 'loop-start', 0x24: 'loop-end', 0x25: 'call', 0x26: 'return',
    0x27: 'select', 0x28: 'stop-48k', 0x2A: 'signal', 0x2B: 'comment',
    0x30: 'archive-info', 0x31: 'hw-type', 0x32: 'custom-info', 0x33: 'glue', 0x35: 'custom',
}


def u16(d, p):
    return int.from_bytes(d[p:p+2], 'little')


def u24(d, p):
    return int.from_bytes(d[p:p+3], 'little')


def u32(d, p):
    return int.from_bytes(d[p:p+4], 'little')


def skip(data, pos, bt):
    """Return payload end offset (exclusive) of block whose id was at pos-1."""
    if bt == 0x10: return pos + 4 + u16(data, pos + 2)
    if bt == 0x11: return pos + 18 + u24(data, pos + 15)
    if bt == 0x12: return pos + 4
    if bt == 0x13: return pos + 2 + 2 * u16(data, pos)
    if bt == 0x14: return pos + 10 + u24(data, pos + 7)
    if bt == 0x15: return pos + 9 + u24(data, pos + 6)
    if bt == 0x18: return pos + 16 + u32(data, pos + 12)
    if bt == 0x19: return None  # generalized data: symbol/sequence tables, not tabulated here
    if bt == 0x20: return pos + 2
    if bt == 0x21: return pos + 1 + data[pos]
    if bt == 0x22: return pos + 2
    if bt == 0x23: return pos + 2
    if bt == 0x24: return pos + 2
    if bt == 0x25: return pos + 2 + 2 * u16(data, pos)
    if bt == 0x26: return pos
    if bt == 0x27: return pos + 2 + 2 + 2 * u16(data, pos)
    if bt == 0x28: return pos + 2
    if bt == 0x2A: return pos + 4
    if bt == 0x2B: return pos + 1 + data[pos]
    if bt == 0x30: return skipArchiveInfo(data, pos)
    if bt == 0x31: return pos + 1 + 3 * data[pos]
    if bt == 0x32: return skipCustomInfo(data, pos)
    if bt == 0x33: return pos + 3
    if bt == 0x35: return pos + 4 + u32(data, pos)
    return None


def skipArchiveInfo(data, pos):
    """0x30: spec form [fullLen u8][count u8][strings...] with body 2+fullLen.

    Some tools (e.g. BASin) omit the count byte and write [fullLen][text] —
    detect by validating that count strings consume exactly fullLen bytes.
    """
    full_len = data[pos]
    count = data[pos + 1]
    walked = 2
    if 0 < count <= full_len:
        for _ in range(count):
            if walked >= 2 + full_len or walked >= len(data):
                walked = -1
                break
            walked += 1 + data[pos + walked]
    if walked == 2 + full_len:
        return pos + 2 + full_len  # spec form validated
    return pos + 1 + full_len  # no-count variant: [fullLen][text]


def skipCustomInfo(data, pos):
    """0x32: spec form [16-byte ASCII key][len u8][text] with body 17+len.

    Preservation archives also carry a variant starting with a u16 payload
    length instead of an ASCII key (non-printable first bytes expose it).
    """
    key = data[pos:pos + 16]
    if len(key) == 16 and all(32 <= b < 127 for b in key):
        return pos + 17 + data[pos + 16]
    payload = u16(data, pos)
    if pos + 2 + payload <= len(data):
        return pos + 2 + payload  # [payloadLen u16][key/value pairs] variant
    return None


def scan(path):
    """Return (status_string, ok). ok is True only for a clean walk to EOF."""
    try:
        data = open(path, 'rb').read()
    except OSError as e:
        return f"{path}: CANNOT READ {e}", False
    if data[:8] != b'ZXTape!\x1a':
        return f"{path}: BAD MAGIC {data[:8]!r}", False
    ver = f"{data[8]}.{data[9]}"
    pos, hist = 10, {}
    while pos < len(data):
        bt = data[pos]
        pos += 1
        end = skip(data, pos, bt)
        if end is None:
            return f"{path} v{ver}: UNSUPPORTED BLOCK {bt:#04x} at {pos-1:#x}", False
        if end > len(data):
            return f"{path} v{ver}: TRUNCATED BLOCK {bt:#04x} at {pos-1:#x}", False
        hist[bt] = hist.get(bt, 0) + 1
        pos = end
    desc = ', '.join(f"{BLOCKS.get(b, hex(b))}x{n}" for b, n in sorted(hist.items()))
    return f"{path} v{ver} blocks={sum(hist.values()):>3}: {desc}", True


def main():
    ap = argparse.ArgumentParser(description="TZX block-structure scanner (fixture pre-vet)")
    ap.add_argument('paths', nargs='*', help='.tzx files or globs (default: *.tzx in cwd)')
    args = ap.parse_args()

    files = []
    for p in args.paths:
        files.extend(glob.glob(p) if any(c in p for c in '*?[') else [p])
    if not files:
        files = glob.glob('*.tzx')
    if not files:
        print("no .tzx files found (pass paths or run inside a fixture dir)")
        return 2

    all_ok = True
    for path in files:
        line, ok = scan(path)
        print(line)
        all_ok &= ok
    return 0 if all_ok else 1


if __name__ == '__main__':
    sys.exit(main())
