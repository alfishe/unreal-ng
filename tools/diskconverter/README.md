# diskconvert.py

Standalone Python CLI to convert ZX-Spectrum/TR-DOS disk images between **SCL**, **TRD**,
**FDI**, **UDI**, and (read-only) **TD0**. No dependency on the C++ core — pure Python,
reimplementing each format from its spec in `docs/file-formats/disk-images/`.

```
python3 diskconvert.py <input> <output> [--from FORMAT] [--to FORMAT]
```

Format is guessed from each path's extension, falling back to content sniffing for the
input file. Override with `--from`/`--to` (`scl`, `trd`, `fdi`, `udi`, `td0`) when a path
has no/a wrong extension.

## Round-trip guarantee

Any chain through the sector-preserving formats (**TRD ↔ FDI ↔ UDI**, in any order, any
number of hops) preserves every sector's bytes and ID fields exactly. Converting **SCL →
(any chain of TRD/FDI/UDI) → SCL** reproduces a byte-identical SCL file, files and all —
verified against several real fixtures in `testdata/` (see below), including
`scl → trd → fdi → udi → trd → scl`.

This works because TRD/FDI/UDI are lossless containers for sector content (given standard
TR-DOS geometry for TRD), and SCL's own file table (the TR-DOS catalog) round-trips through
them as ordinary sectors.

## Supported conversions

| | read | write |
|---|---|---|
| **SCL** | yes | yes (requires a TR-DOS-formatted source disk) |
| **TRD** | yes | yes (requires standard 16×256-sector TR-DOS geometry) |
| **FDI** | yes | yes |
| **UDI** | yes | yes |
| **TD0** | yes, uncompressed ("TD") only | no |

### What's lossy, and why

- **TRD** can only hold standard 16×256-byte TR-DOS tracks. Writing a disk with any other
  geometry (weird sector sizes/numbers, deleted sectors, bad CRCs, ID-only sectors) is
  **refused with an error**, not silently corrupted — use FDI or UDI instead.
- **SCL** only stores catalogued, non-deleted files — a disk's raw/uncatalogued sectors,
  free space, and disk label are not represented. Writing SCL from a disk with no valid
  TR-DOS catalog is refused.
- **FDI** and **TRD** cannot express weak/floating ("flaky") sectors used by some
  copy-protection schemes at all — see
  `docs/WD1793/FlakySectorEmulator.md` and the case study at
  `docs/disasm/black-raven-voron-protection/README.md`. **UDI** cannot either (it has a
  reserved multi-revolution track type but no loader in this codebase implements it).
  Only SCP, HFE and extended DSK can, and this tool does not (yet) implement those two
  formats.
- **TD0**'s advanced ("td" signature) variant uses Teledisk's own whole-file LZSS +
  adaptive-Huffman compression (~300 lines of custom decoder in
  `core/src/loaders/disk/loader_td0.cpp`). That decoder was not ported here; reading an
  advanced TD0 fails with a clear error rather than guessing. Uncompressed ("TD") TD0 is
  fully supported for reading. Writing TD0 is not implemented at all (nothing in this
  project needs it as an output format) — convert to FDI or UDI instead.
- Description/comment text (FDI's free-form description field, TD0's comment block) is not
  preserved across conversions.

Every lossy-but-possible conversion prints a `warning: ...` line to stderr and proceeds;
every conversion that would actually corrupt data is a hard `error: ...` with a non-zero
exit code.

## Layout

```
diskconvert.py          # CLI entry point
diskconverter/
  disk_image.py          # format-agnostic Sector/Track/DiskImage model
  trdos.py                # TR-DOS catalog (used by scl.py; matches trdoscatalog.cpp exactly)
  mfm.py                   # byte-level MFM sector <-> raw track stream codec (used by udi.py)
  formats/
    fdi.py, trd.py, udi.py, scl.py, td0.py
    __init__.py            # format registry / extension+content resolution
```

## Manual verification performed

```
testdata/sound/tsfm/hny.SCL          -> trd -> fdi -> udi -> trd -> scl   (byte-identical)
testdata/machines/atm/2048.scl       -> fdi -> trd -> udi -> fdi -> scl   (byte-identical)
testdata/sound/tsfm/TFMWORKS.SCL     -> trd -> udi -> fdi -> trd -> scl   (byte-identical)
testdata/loaders/trd/EyeAche.trd     -> fdi -> udi -> trd                (byte-identical)
testdata/loaders/udi/Zvezdnoe Nasledie.udi (real copy-protected dump)     -> fdi           (parses; -> trd correctly refused, non-standard geometry)
testdata/loaders/td0/trdos-sample.td0     -> fdi -> udi -> trd            (reads correctly)
testdata/loaders/td0/trdos-sample-adv.td0 (compressed)                    -> correctly refused with a clear error
```

No automated test suite yet — a good next step would be a pytest file driving the same
matrix above.

### A bug this self-round-trip testing missed

The self-round-trip checks above (this tool's own encoder feeding its own decoder) all
passed even while `mfm.py`'s CRC-16 was written to disk **byte-swapped** — a consistent
bug round-trips through itself without ever showing up. It only surfaced when converting
`VORON1.FDI` to UDI and inserting the result into the real emulator: every sector's ID and
data CRC came back invalid (`crc_valid: false`, and the reported CRC bytes were reversed
versus the original FDI's), which is exactly what a real WD1793 sees as "Disk error" —
its ID search rejects every sector with a bad CRC. Caught and fixed by comparing
`GET /api/v1/emulator/{id}/disk/{drive}/sector/{cyl}/{side}/{sec}` between the FDI source
and the converted UDI on a live `unreal-ng` instance; both `testdata/loaders/udi/VORON1.UDI`
and `VORON2.UDI` were regenerated afterward and now match byte-for-byte (verified at
several tracks, including the protected one and the last cylinder).

**Lesson**: for any format whose bytes get *interpreted* by something other than this
tool's own decoder (which is UDI's whole reason to exist here), self-round-trip is not
enough verification — cross-check against the real consumer.
