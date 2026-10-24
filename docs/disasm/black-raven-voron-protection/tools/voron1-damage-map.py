#!/usr/bin/env python3
"""Aggregate per-sector anomalies (bad CRC / no-data / deleted / ID mismatch)
across the whole VORON1.FDI, grouped by sector number R, to locate the
physically damaged (acetone-wiped) spots described by the title's author.
Ground-truth tool for the README 3/7 damage discussion (voron-3 work).
usage: voron1-damage-map.py [fdi-path]   (default testdata/loaders/fdi/VORON1.FDI)
"""
import sys
from collections import defaultdict
from pathlib import Path

# repo root = walk up from docs/disasm/black-raven-voron-protection/tools/
root = Path(__file__).resolve().parent
while not (root / "CMakeLists.txt").exists() and root.parent != root:
    root = root.parent

sys.path.insert(0, str(root / "tools" / "diskconverter"))
from diskconverter import formats  # noqa: E402

fdi = sys.argv[1] if len(sys.argv) > 1 else "testdata/loaders/fdi/VORON1.FDI"
fdi = str(root / fdi) if not fdi.startswith("/") else fdi

module = formats.resolve_input(fdi, None)
disk = module.read(fdi)

by_r = defaultdict(list)        # R -> list of (cyl, head, what)
id_mismatch = defaultdict(int)  # R -> count where ID C/H != physical
unformatted = []

for (cyl, head), track in sorted(disk.tracks.items()):
    if track is None or not track.sectors:
        unformatted.append((cyl, head))
        continue
    for s in track.sectors:
        if not s.crc_valid:
            by_r[s.number].append((cyl, head, "bad-data-crc"))
        if s.no_data:
            by_r[s.number].append((cyl, head, "no-data"))
        if s.deleted:
            by_r[s.number].append((cyl, head, "deleted"))
        if s.cylinder != cyl or s.head != head:
            by_r[s.number].append((cyl, head, f"id-claims C={s.cylinder} H={s.head}"))
            id_mismatch[s.number] += 1

print(f"unformatted tracks ({len(unformatted)}):", unformatted[:20])
print()
print("anomalies grouped by sector number R:")
for r in sorted(by_r):
    entries = by_r[r]
    # split kinds
    kinds = defaultdict(int)
    for _, _, what in entries:
        kinds[what] += 1
    print(f"  R={r:3}: {len(entries):3} sectors: " + ", ".join(f"{k}x{v}" for k, v in sorted(kinds.items())))
    if len(entries) <= 25:
        for c, h, what in entries:
            print(f"        cyl={c:2} head={h} {what}")
print()
print("ID-mismatch counts by R:", dict(id_mismatch))

# per-track sector-count histogram of the reformatted band
sizes = defaultdict(int)
for (cyl, head), track in disk.tracks.items():
    if track and track.sectors:
        key = (len(track.sectors), track.sectors[0].size_code if track.sectors else -1)
        sizes[key] += 1
print("track shape histogram (sector_count, size_code): count ->", dict(sizes))
