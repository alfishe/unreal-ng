#!/usr/bin/env python3
"""Build a MAME rompath from ROM files found elsewhere, matched by CRC32 (see README.md).

    romset.py <mame binary> <rompath out> <search dir>... -- <driver>...

MAME itself says which files each driver needs (-listxml: name, size, CRC32, BIOS, region, offset). Every file
under the search directories is cut into slices of each needed size (at 16K steps: unreal-ng keeps a 128K
machine's two ROMs in one file), and a slice whose CRC32 matches is written under MAME's name to
<rompath>/<driver>/ and, for a device's ROM, also to <rompath>/<device>/.

The BIOS: the driver's default one if all its files are found, else the first BIOS (in MAME's order) whose files
are all found. Optional and undumped ROMs are never needed.

MAME refuses to start when a required file is missing, but only warns ("WRONG CHECKSUMS") when a file is there
with other contents. So a file whose contents MAME never uses is written as a placeholder (zeros, the right size)
when it is not found:
  - a ROM that later ROMs of the same set overwrite completely (same region, covering its whole range). MAME's
    Beta Disk device loads some 50 TR-DOS versions to offset 0 of one region, one after the other: only the last
    one, trd503.rom (TR-DOS 5.03), stays;
  - a ROM in a region the driver's source never reads (UNREAD_REGIONS below).

Prints one line per driver: "<driver> ok bios=<name> placeholders=<n>" or "<driver> missing <n> ROMs: <names>".
"""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib

STEP = 0x4000
MAX_FILE = 4 * 1024 * 1024

# (set, region) that MAME loads but its driver never reads (checked in the MAME 0.289 sources): the keyboard
# controller ROMs of the Scorpion (scorpion.cpp) and the ATM Turbo (atm.cpp), and the ZX-Evo's AVR firmware
# (evo/pentevo.cpp), are dumped, not emulated
UNREAD_REGIONS = {
    ('scorpio', 'keyboard'),
    ('atm', 'keyboard'),
    ('atmtb2', 'keyboard'),
    ('atmtb2plus', 'keyboard'),
    ('pentevo', 'fw'),
}


def bios_names(m):
    """The machine's BIOS names, the default one first"""
    names = [b.get('name') for b in m.findall('biosset')]
    default = [b.get('name') for b in m.findall('biosset') if b.get('default') == 'yes']
    if default:
        names.remove(default[0])
        names.insert(0, default[0])
    return names


def set_roms(m, bios):
    """(name, size, crc, owner, dead) for one set with the given BIOS (None: its default); dead = MAME never uses it"""
    name = m.get('name')
    if bios is None:
        names = bios_names(m)
        bios = names[0] if names else None
    roms = []
    for r in m.findall('rom'):
        if r.get('bios') is not None and r.get('bios') != bios:
            continue
        roms.append(r)
    out = []
    for i, r in enumerate(roms):
        if r.get('status') == 'nodump' or r.get('optional') == 'yes' or r.get('crc') is None:
            continue
        region, size = r.get('region'), int(r.get('size'))
        ofs = int(r.get('offset') or '0', 16)
        # Overwritten: the ranges of the later ROMs in this region cover [ofs, ofs + size)
        covered = False
        later = sorted((int(x.get('offset') or '0', 16), int(x.get('size'))) for x in roms[i + 1:]
                       if x.get('region') == region)
        pos = ofs
        for lo, sz in later:
            if lo <= pos < lo + sz:
                pos = lo + sz
            if pos >= ofs + size:
                covered = True
                break
        dead = covered or (name, region) in UNREAD_REGIONS
        out.append((r.get('name'), size, r.get('crc').lower(), name, dead))
    return out


def device_roms(machines, name, seen):
    """The ROMs of a machine's devices, each with its default BIOS"""
    out = []
    for d in machines[name].findall('device_ref'):
        dn = d.get('name')
        if dn in seen or dn not in machines:
            continue
        seen.add(dn)
        out += set_roms(machines[dn], None)
        out += device_roms(machines, dn, seen)
    return out


def main():
    args = sys.argv[1:]
    if '--' not in args or len(args) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    sep = args.index('--')
    mame, rompath, dirs, drivers = args[0], args[1], args[2:sep], args[sep + 1:]

    xml = subprocess.run([mame, '-listxml'] + drivers, capture_output=True, check=True).stdout
    machines = {m.get('name'): m for m in ET.fromstring(xml).findall('machine')}

    # Every candidate: per driver, per BIOS, the ROMs it needs (the devices' ones included)
    candidates = {}
    for d in drivers:
        devs = device_roms(machines, d, {d})
        names = bios_names(machines[d]) or [None]
        candidates[d] = [(b, set_roms(machines[d], b) + devs) for b in names]
    sizes = sorted({r[1] for c in candidates.values() for _, roms in c for r in roms})
    wanted = {r[2] for c in candidates.values() for _, roms in c for r in roms}

    found = {}  # crc -> bytes
    for top in dirs:
        for root, _, files in os.walk(top):
            for fn in sorted(files):
                path = os.path.join(root, fn)
                try:
                    if os.path.getsize(path) > MAX_FILE or os.path.getsize(path) < min(sizes + [STEP]):
                        continue
                    data = open(path, 'rb').read()
                except OSError:
                    continue
                for size in sizes:
                    for ofs in range(0, len(data) - size + 1, STEP):
                        crc = '%08x' % zlib.crc32(data[ofs:ofs + size])
                        if crc in wanted and crc not in found:
                            found[crc] = data[ofs:ofs + size]

    for d in drivers:
        chosen = None
        for bios, roms in candidates[d]:
            missing = [r[0] for r in roms if r[2] not in found and not r[4]]
            if not missing:
                chosen = (bios, roms)
                break
        if chosen is None:
            bios, roms = candidates[d][0]
            missing = [r[0] for r in roms if r[2] not in found and not r[4]]
            shown = ' '.join(missing[:6]) + (' ...' if len(missing) > 6 else '')
            print('%s missing %d ROMs: %s' % (d, len(missing), shown))
            continue
        bios, roms = chosen
        placeholders = 0
        for name, size, crc, owner, dead in roms:
            data = found.get(crc)
            if data is None:
                data = bytes(size)
                placeholders += 1
            for folder in {d, owner}:
                os.makedirs(os.path.join(rompath, folder), exist_ok=True)
                with open(os.path.join(rompath, folder, name), 'wb') as f:
                    f.write(data)
        print('%s ok bios=%s placeholders=%d' % (d, bios or '-', placeholders))
    return 0


if __name__ == '__main__':
    sys.exit(main())
