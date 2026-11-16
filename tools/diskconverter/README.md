# diskconvert.py

Standalone Python CLI to convert ZX-Spectrum/TR-DOS disk images between **SCL**, **TRD**,
**FDI**, **UDI** and **TD0**. No dependency on the C++ core: pure Python, but a port of
unreal-disk (`core/src/3rdparty/unreal-disk`): the model, every reader and every writer
behave byte for byte as the C++ library does, checked against `zxdisk dump` / `zxdisk
convert` by [`tools/verification/disk/parity.py`](../verification/disk/parity.py).

```
python3 diskconvert.py <input> <output> [--from FORMAT] [--to FORMAT]
python3 -m diskconverter dump <image>                 # run from tools/diskconverter
python3 -m diskconverter convert <image> <out-image>
```

`diskconvert.py`: the format is guessed from each path's extension, falling back to
content sniffing for the input file. Override with `--from`/`--to` (`scl`, `trd`, `fdi`,
`udi`, `td0`) when a path has no/a wrong extension. Loader and writer warnings are printed
as `warning: ...`; a refusal is `error: ...` with exit code 1.

`python3 -m diskconverter` is the `zxdisk`-compatible interface:

- `dump <image>` prints the floppy as the model holds it, as JSON, byte for byte what
  `zxdisk dump <image>` prints: `format`, `cylinders`, `sides`, the loader's `warnings`,
  then one line per track with `encoding` (`mfm` / `fm`), `length`, `raw` (the stream,
  hex), `clock` (the clock-mark bitmap, hex, bit i = byte i), `weak` (the weak-byte bitmap,
  "" when the track has none) and `sectors` (`c h r n` as in the ID field, `idam` / `dam` /
  `data` stream offsets or -1, `size`, `has_data`, `deleted`, `id_crc_valid`,
  `data_crc_valid`, the stored `id_crc` / `data_crc` as little-endian words).
- `convert <image> <out-image>` probes the input the way `FloppyFormats::Probe` does and
  saves in the format the output extension names (`FloppyFormats::Save` without
  retargeting): the same file, the same refusal message (`zxdisk: ...`, exit 1), the same
  first warning (`zxdisk: warning: ...`).

TRD, SCL, FDI, UDI and TD0 are read and written; so are DSK / Extended DSK (`formats/dsk.py`,
always written as EDSK), raw PC floppies (`.img` / `.ima`, 720 KB / 1.44 MB, `formats/rawpc.py`),
MGT (`.mgt` cylinder-major, `.img` side-major, `formats/mgt.py`: a +D disk saved as `.img` stays
MGT, anything else goes to the raw PC writer) and Profi PRO (`formats/pro.py`). Formats this
package does not port are probed and named but refused.

## The model

`diskconverter/disk_image.py` mirrors `DiskImage` / `DiskImage::Track` / `DiskImage::Sector`:

- a **track** is a raw byte stream (64..12500 bytes) with a clock-mark bitmap (the A1 / C2
  sync bytes of MFM, the marks of FM), an optional weak-byte bitmap and an encoding;
- its **sector index** is rebuilt by `Track.reindex`, a port of `Track::reindex` (MFM marks
  need A1 A1 A1 before them, with clock marks when the track has any; FM marks need a
  clock mark, or two 00 bytes when the track has none; the data mark is searched 43 / 30
  bytes after the ID; the size is `128 << (N & 3)`);
- `Track.format_track(spec)` is `formatTrack` with `TrackFormatSpec` (`trdos`, `ibm`,
  `ibm3740`), and `fit_track_spec` the loaders' gap-3 shrink / track grow;
- `track.index` gives live views (`SectorIndex`) over the stream, with the cached flags C++
  keeps; `track.sectors` gives value snapshots (`Sector`) for the older sector-level API, and
  assigning `track.sectors` lays the track out as the FDI loader does (MFM) or the TD0 loader
  for FM tracks. A sector built with a broken CRC flag gets the computed CRC inverted, as the
  FDI / TD0 loaders write one.

Readers return a `DiskImage` with `format` and `warnings` (the loader's report); writers return
their warnings; both raise `DiskConversionError(message, warnings)` where C++ refuses.
`formats.load(path)` / `formats.save(disk, path)` are `FloppyFormats::Load` / `Save`.

## Supported conversions

| | read | write |
|---|---|---|
| **SCL** | yes | yes (track 0 must hold a TR-DOS catalogue; files whose chain leaves the disk are skipped with a warning) |
| **TRD** | yes | yes (every track exactly sectors 1..16 with 256-byte data fields) |
| **FDI** | yes | yes |
| **UDI** | yes (the weak-byte `UDIW` chunk too; a wrong file CRC refuses the load) | yes |
| **TD0** | yes, "TD" and advanced "td" (Teledisk LZHUF, `diskconverter/td0lzh.py`), FM and MFM tracks | yes (uncompressed "TD") |

### What's lossy, and why

- **TRD** holds only standard 16 x 256-byte TR-DOS tracks; any other layout is refused with
  the C++ message (`TRD save refused: ...`). Like the C++ writer, it does not check CRCs: a
  bad ID or data CRC is not stored.
- **SCL** stores catalogued, non-deleted files only.
- **FDI** and **TD0** store sector lists: gaps, clock marks, exact broken-CRC values and
  the track length are rebuilt by the reader (FDI also drops FM density, and cannot flag a
  broken ID CRC). The writers say so in a warning, as C++ does.
- **UDI** is lossless for everything the model holds (stream, marks, weak bytes).
- Description/comment text (FDI's description, TD0's comment block) is not written.

## Layout

```
diskconvert.py          # CLI entry point
diskconverter/
  __main__.py             # python3 -m diskconverter dump|convert (zxdisk-compatible)
  disk_image.py           # DiskImage / Track / SectorIndex / Sector, CRCs, TrackFormatSpec
  dump.py                 # the zxdisk dump JSON
  td0lzh.py               # Teledisk LZHUF decoder (advanced TD0)
  trdos.py                # TR-DOS catalogue helpers
  mfm.py                  # compatibility helpers over the stream model (CRC, encode / decode a track)
  formats/
    fdi.py, trd.py, udi.py, scl.py, td0.py
    __init__.py           # probe / load / save as FloppyFormats, extension+content resolution
```

## Manual verification performed

```
testdata/sound/tsfm/hny.SCL          -> trd -> fdi -> udi -> trd -> scl   (byte-identical)
testdata/machines/atm/2048.scl       -> fdi -> trd -> udi -> fdi -> scl   (byte-identical)
testdata/sound/tsfm/TFMWORKS.SCL     -> trd -> udi -> fdi -> trd -> scl   (byte-identical)
testdata/loaders/trd/EyeAche.trd     -> fdi -> udi -> trd                (byte-identical)
testdata/loaders/udi/Zvezdnoe Nasledie.udi (real copy-protected dump)     -> fdi           (parses; -> trd correctly refused, non-standard geometry)
testdata/loaders/td0/trdos-sample.td0     -> fdi -> udi -> trd            (reads correctly)
```

Since the port to the unreal-disk model, the reference is `zxdisk` itself:
`python3 tools/verification/disk/parity.py` compares dumps and conversions to every format
for every fixture under `testdata/` (and any other images given).

To compare an image with a reference dump track by track, see [`tools/diskcompare`](../diskcompare/README.md).

Automated tests: `python3 -m pytest tools/diskconverter/tests` (synthetic images built in the tests; artifacts go to
`scratch/diskconverter-tests/`). The size code is kept raw: an ID field with N = #F7 (a protection trick) stays #F7
in every format that can carry it, the data length is `128 << (N & 3)`.

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
