#!/usr/bin/env python3
"""Sector-table parity between the C++ disk loaders (unreal-disk, as the emulator loads an image) and the Python
converter (tools/diskconverter), for every disk fixture under testdata/ or the images given.

Per physical track both sides give the sectors in stream order with: C, H, R, raw N, data length (0 for an ID
without a data field), deleted mark, ID CRC (validity and the two bytes on the disk), data CRC (validity and bytes).
Any difference is a discrepancy; the script lists them and exits 1.

C++ side, two sources:
  webapi (default)  unreal-qt offscreen over the WebAPI (tools/verification/disk/disklib.py): each image is inserted
                    into drive A of a fresh, paused, muted Pentagon; per track GET /disk/A/track/{c}/{s} gives the
                    C++ parser's fields (CHRN, validities, deleted, data size, IDAM offset) and
                    GET /disk/A/track/{c}/{s}/raw the stream the loader built, from which the CRC bytes are read at
                    the offsets the C++ parser reported.
  --cpp-dump CMD    any command printing the table as JSON, `{image}` replaced by the image path. Meant for a future
                    `zxdisk sectors {image} --json` (zxdisk has no such verb yet). Expected shape:
                      {"cylinders": 80, "heads": 2, "tracks": [{"cylinder": 0, "head": 0, "sectors": [
                        {"c": 0, "h": 0, "r": 1, "n": 1, "length": 256, "deleted": false,
                         "id_crc_valid": true, "id_crc": "1234", "data_crc_valid": true, "data_crc": "5678"}]}]}
                    A field the tool does not give may be left out or null: it is then not compared.

Python side: CRC bytes are known for valid CRCs (computed) and for broken ones read from UDI (stored); FDI/TRD/TD0
keep only a flag, so a broken CRC's value is compared only when both sides know it.

    python3 tools/verification/disk/sector-parity.py --launch                    # every fixture under testdata/
    python3 tools/verification/disk/sector-parity.py --launch a.udi b.fdi --json
    python3 tools/verification/disk/sector-parity.py --cpp-dump 'zxdisk sectors {image} --json' testdata/loaders

Report: scratch/sector-parity/report.json (always) and a text summary. Exit 0 no discrepancies, 1 discrepancies or an
image one side could not read, 2 bad usage.
"""
from __future__ import annotations

import argparse
import base64
import concurrent.futures
import contextlib
import io
import json
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disklib  # noqa: E402

sys.path.insert(0, os.path.join(disklib.REPO, 'tools', 'diskconverter'))
from diskconverter import formats, mfm  # noqa: E402
from diskconverter.disk_image import DiskConversionError  # noqa: E402

EXTENSIONS = ('.trd', '.scl', '.fdi', '.udi', '.td0')
FIELDS = ('c', 'h', 'r', 'n', 'length', 'deleted', 'id_crc_valid', 'id_crc', 'data_crc_valid', 'data_crc')
DAM_SEARCH_WINDOW = 43  # bytes after the ID CRC in which the WD1793 accepts a data mark


def find_fixtures(paths):
    """Image files among paths (directories searched recursively), sorted"""
    found = []
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                dirs[:] = [d for d in dirs if not d.startswith('.')]
                found += [os.path.join(root, f) for f in files if f.lower().endswith(EXTENSIONS)]
        else:
            found.append(path)
    return sorted(found)


# --------------------------------------------------------------- Python side

def python_table(path):
    """{(cyl, head): [sector dict]}, geometry and the reader's warnings"""
    module = formats.resolve_input(path)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        disk = module.read(path)
    tracks = {}
    for (cyl, head), track in disk.tracks.items():
        rows = []
        for s in track.sectors:
            id_value = mfm.crc_bytes(mfm.crc_wd1793(bytes([mfm.IDAM, s.cylinder, s.head, s.number, s.size_code])))
            row = {'c': s.cylinder, 'h': s.head, 'r': s.number, 'n': s.size_code,
                   'length': 0 if s.no_data else s.data_size, 'deleted': bool(s.deleted) and not s.no_data,
                   'id_crc_valid': s.id_crc_valid,
                   'id_crc': id_value.hex().upper() if s.id_crc_valid else _hex(s.id_crc_stored),
                   'data_crc_valid': None, 'data_crc': None}
            if not s.no_data:
                dam = mfm.DAM_DELETED if s.deleted else mfm.DAM_NORMAL
                row['data_crc_valid'] = s.crc_valid
                row['data_crc'] = (mfm.crc_bytes(mfm.crc_wd1793(bytes([dam]) + s.data)).hex().upper() if s.crc_valid
                                   else _hex(s.data_crc_stored))
            rows.append(row)
        if rows:
            tracks[(cyl, head)] = rows
    warnings = [line for line in out.getvalue().splitlines() if line.strip()]
    return {'cylinders': disk.cylinders, 'heads': disk.heads, 'tracks': tracks, 'warnings': warnings}


def _hex(value):
    return None if value is None else bytes(value).hex().upper()


# --------------------------------------------------------------- C++ side

def _data_crc_from_raw(raw, id_offset, size):
    """(DAM, CRC bytes) of the data field after the ID at id_offset (offset of FE), as the WD1793 finds it"""
    start = id_offset + 7
    for j in range(start, min(start + DAM_SEARCH_WINDOW, len(raw) - 4)):
        if raw[j:j + 3] == b'\xA1\xA1\xA1' and raw[j + 3] in (mfm.DAM_NORMAL, mfm.DAM_DELETED):
            crc = raw[j + 4 + size:j + 6 + size]
            return raw[j + 3], (crc.hex().upper() if len(crc) == 2 else None)
    return None, None


def webapi_table(machine, path):
    ok, message = machine.insert(path)
    if not ok:
        raise RuntimeError(f'insert refused: {message}')
    drives = machine.call('GET', '/disk')[1]
    drive = next((d for d in drives.get('drives', []) if d.get('id') == 0), {}) if isinstance(drives, dict) else {}
    cylinders, heads = drive.get('cylinders'), drive.get('sides')
    if cylinders is None or heads is None:
        raise RuntimeError(f'no geometry for drive A: {drives}')
    def read_track(key):
        cyl, head = key
        code, summary = machine.call('GET', f'/disk/A/track/{cyl}/{head}')
        if code != 200 or not isinstance(summary, dict):
            return key, []
        sectors = summary.get('sectors') or []
        if not sectors:
            return key, []
        raw = b''
        code, raw_reply = machine.call('GET', f'/disk/A/track/{cyl}/{head}/raw')
        if code == 200 and isinstance(raw_reply, dict):
            raw = base64.b64decode(raw_reply.get('raw_base64', ''))
        fm = summary.get('encoding') == 'FM'
        rows = []
        for s in sectors:
            id_offset = s.get('id_offset')
            id_crc = None
            if raw and isinstance(id_offset, int) and id_offset + 7 <= len(raw) and raw[id_offset] == mfm.IDAM:
                id_crc = raw[id_offset + 5:id_offset + 7].hex().upper()
            has_data = bool(s.get('has_data'))
            row = {'c': s['id_cyl'], 'h': s['id_head'], 'r': s['id_sector'], 'n': s['id_size_code'],
                   'length': s.get('data_size', 0) if has_data else 0,
                   'deleted': bool(s.get('deleted')) and has_data,
                   'id_crc_valid': s.get('id_crc_valid'), 'id_crc': id_crc,
                   'data_crc_valid': s.get('data_crc_valid') if has_data else None, 'data_crc': None}
            if has_data and raw and isinstance(id_offset, int):
                _, row['data_crc'] = _data_crc_from_raw(raw, id_offset, s.get('data_size', 0))
            if fm:
                # FM CRCs have no A1 preset; the Python model has no encoding and computes MFM ones: the encoding
                # difference is reported once per track instead of every CRC value
                row['id_crc'] = row['data_crc'] = None
            row['encoding'] = 'FM' if fm else 'MFM'
            rows.append(row)
        return key, rows

    # Each request costs ~0.1 s of round trip: the tracks are read in parallel
    keys = [(cyl, head) for cyl in range(cylinders) for head in range(heads)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        tracks = {key: rows for key, rows in pool.map(read_track, keys) if rows}
    machine.call('POST', '/disk/A/eject', {})
    return {'cylinders': cylinders, 'heads': heads, 'tracks': tracks}


def command_table(template, path):
    command = [part.replace('{image}', path) for part in shlex.split(template)]
    proc = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise RuntimeError(f'{shlex.join(command)} exited {proc.returncode}: {proc.stderr.strip()[:300]}')
    doc = json.loads(proc.stdout)
    tracks = {}
    for track in doc.get('tracks', []):
        rows = [{field: s.get(field) for field in FIELDS} for s in track.get('sectors', [])]
        if rows:
            tracks[(track['cylinder'], track['head'])] = rows
    return {'cylinders': doc.get('cylinders'), 'heads': doc.get('heads'), 'tracks': tracks}


# --------------------------------------------------------------- comparison

def compare(cpp, py):
    """Discrepancy texts (C++ value first, Python second)"""
    diffs = []
    if (cpp['cylinders'], cpp['heads']) != (py['cylinders'], py['heads']):
        diffs.append(f"geometry cpp={cpp['cylinders']}x{cpp['heads']} python={py['cylinders']}x{py['heads']}")
    for key in sorted(set(cpp['tracks']) | set(py['tracks'])):
        a, b = cpp['tracks'].get(key, []), py['tracks'].get(key, [])
        where = f'track {key[0]}/{key[1]}'
        if len(a) != len(b):
            diffs.append(f'{where}: sector count cpp={len(a)} python={len(b)}')
        if a and a[0].get('encoding') == 'FM':
            diffs.append(f'{where}: encoding cpp=FM python=MFM (the Python model has no encoding; CRC values not compared)')
        for index, (x, y) in enumerate(zip(a, b), 1):
            for field in FIELDS:
                if x.get(field) is None or y.get(field) is None:
                    continue  # one side does not know it (a broken CRC from FDI/TRD/TD0, or a field the tool lacks)
                if x[field] != y[field]:
                    diffs.append(f'{where} sector {index} (R={y["r"]}): {field} cpp={x[field]} python={y[field]}')
    return diffs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('images', nargs='*', help='images or directories (default: testdata/)')
    parser.add_argument('--cpp-dump', metavar='CMD', help="command printing the C++ table as JSON, '{image}' = path")
    parser.add_argument('--json', action='store_true', help='print the full report as JSON')
    parser.add_argument('--report', default=os.path.join(disklib.REPO, 'scratch', 'sector-parity', 'report.json'))
    disklib.add_instance_args(parser)
    args = parser.parse_args()

    fixtures = find_fixtures(args.images or [os.path.join(disklib.REPO, 'testdata')])
    if not fixtures:
        print('error: no disk images found', file=sys.stderr)
        return 2

    process = api = None
    if not args.cpp_dump:
        api, process = disklib.open_instance(args)
    results = []
    try:
        for path in fixtures:
            rel = os.path.relpath(path, disklib.REPO)
            entry = {'image': rel, 'status': 'ok', 'discrepancies': []}
            try:
                py = python_table(path)
                entry['python_warnings'] = py['warnings']
            except (DiskConversionError, ValueError, OSError) as e:
                py = None
                entry['python_error'] = str(e)
            try:
                if args.cpp_dump:
                    cpp = command_table(args.cpp_dump, path)
                else:
                    machine = disklib.Machine(api, 'PENTAGON')
                    try:
                        machine.pause()
                        cpp = webapi_table(machine, path)
                    finally:
                        machine.close()
            except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as e:
                cpp = None
                entry['cpp_error'] = str(e)
            if py is None and cpp is None:
                entry['status'] = 'unreadable'  # both refuse it: agreement, not a discrepancy
            elif py is None or cpp is None:
                entry['status'] = 'one-side-error'
            else:
                entry['discrepancies'] = compare(cpp, py)
                entry['status'] = 'mismatch' if entry['discrepancies'] else 'ok'
            results.append(entry)
            if not args.json:
                note = entry.get('python_error') or entry.get('cpp_error') or ''
                print(f"{entry['status']:>14}  {rel}  {len(entry['discrepancies']) or ''} {note}".rstrip())
                for diff in entry['discrepancies'][:10]:
                    print(f'                {diff}')
                if len(entry['discrepancies']) > 10:
                    print(f"                ... {len(entry['discrepancies']) - 10} more")
    finally:
        disklib.stop(process)

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w') as f:
        json.dump(results, f, indent=2)
    bad = [r for r in results if r['status'] in ('mismatch', 'one-side-error')]
    if args.json:
        json.dump(results, sys.stdout, indent=2)
        print()
    else:
        counts = {}
        for r in results:
            counts[r['status']] = counts.get(r['status'], 0) + 1
        print('summary: ' + ', '.join(f'{v} {k}' for k, v in sorted(counts.items())) + f'; report {args.report}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
