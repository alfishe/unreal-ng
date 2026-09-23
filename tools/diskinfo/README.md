# diskinfo.py

Standalone Python CLI to inspect ZX-Spectrum/TR-DOS disk images: a brief geometry
summary plus full dumps (sector data, sectors with ID fields, raw MFM tracks).
Reads every format the sibling `tools/diskconverter` reads — **SCL, TRD, FDI, UDI
and uncompressed TD0** — by importing its `diskconverter` package directly, so the
two tools always agree on parsing.

```
python3 diskinfo.py <image> [--from FORMAT] [--tracks]
python3 diskinfo.py <image> --dump sectors [--track SPEC] [--side SPEC] [--sector SPEC]
python3 diskinfo.py <image> --dump idam   [...same selection options...]
python3 diskinfo.py <image> --dump raw    [--track SPEC] [--side SPEC] [--out FILE]
```

Format is guessed from the extension, falling back to content sniffing; override
with `--from` (`scl`, `trd`, `fdi`, `udi`, `td0`).

## Brief mode (default)

Number of tracks (cylinders x heads), sectors per track and sector sizes —
grouped into runs when the disk is uniform — followed by two analysis blocks.
`--tracks` adds a one-line-per-physical-track table that also shows the TR-DOS
logical track number (`logical = cylinder * heads + head`).

Per-track notes flag anything unusual: `id-mismatch` (an ID field whose C/H does
not match the sector's physical track/head — the classic copy-protection trick,
e.g. every side-1 track of `Zvezdnoe Nasledie.udi` carries IDs claiming `H=0`),
`deleted`, `data-crc errors`, `id-only` (ID without a data field).

### `filesystems` block

| Filesystem | How it is recognized |
|---|---|
| TR-DOS | valid catalog + disk-info sector (`diskconverter.trdos`); reports file count, label, disk type, free sectors |
| CP/M | a directory block of valid 32-byte FCB entries found anywhere on the disk (user number + 8.3 name + extent checks; TR-DOS catalogs fail the user-number check by construction), plus `CP/M` banner locations. Without a directory, banner-in-system-tracks or data-area traces are reported with lower confidence |
| MS-DOS FAT | boot sector at track 0/side 0: validated BPB (bytes/sector, sectors/cluster, FAT count, media byte) + `0x55AA`; reports OEM, cluster size, label. Catches Profi's PQDOS FAT12 disks |
| iS-DOS, NedoOS | signature scan of the whole disk (`iS-DOS`, `NedoOS`, `NEO-DOS`, `NEOFS`); `likely` when in the boot track, `traces` otherwise |

Verified against real fixtures: `testdata/machines/atm/cpm/prince.trd` (CP/M
directory `TITLE1.DAT … PRINCE1.OVL` at track 1/0), `testdata/machines/profi/
software/CPM.UDI` (Profi CP/M system disk, `BOOTK.COM`/`BDOS.BIN`/`AUTOEXEC.BAT`),
`testdata/machines/profi/software/pqdos1.fdi` (MS-DOS FAT12, OEM `PQDOS2.1`).

### `protection analysis` block

Collects WD1793-visible protection signals and prints a verdict:

- ID/position mismatch (CHRN does not match the physical track/head)
- partial non-TR-DOS reformatting of data tracks (VORON1 keeps its system track
  standard and reformats the rest — the reverse of a machine-native format, which
  is uniform across the whole disk and reported only as an informational note)
- sector numbers `R >= 0xC0` (standard TR-DOS numbers are 1-16)
- duplicate sector IDs within a track, deleted (F8) marks, data-CRC errors,
  ID-only sectors
- over-length geometry (more than 80 cylinders), unformatted holes inside the
  used area (weak signals — protection or damage)

Strong signals yield `likely copy-protected`; the verdict derives only from
signal counts — the tool never names or assumes a title or a known protection
scheme. (The signal taxonomy was validated against the fixtures analyzed in
`docs/disasm/black-raven-voron-protection`.) Verified: VORON1.UDI and
`protected-sample.td0` light up, Zvezdnoe Nasledie flags the all-side-1 ID
mismatch, plain TR-DOS/SCL/TD0 disks come out clean.

## Dump modes

| Mode | Output |
|---|---|
| `sectors` | Sector data only, hexdumped in track stream order |
| `idam` | Each sector preceded by its decoded ID field (CHRN, size, the CRC-16 bytes the ID/data fields carry, deleted/no-data flags) |
| `raw` | The raw MFM byte stream of each track with IDAM/DAM marker offsets listed ahead of the hexdump |

`raw` uses the **original** per-track byte stream when the input is UDI (the only
format that stores one); for every other format it synthesizes the stream with
`diskconverter.mfm.encode_track` — gaps rebuilt to the nominal 6250-byte
revolution — and says so in the header line. Note that sector gaps, interleave
timing and weak bits of the *original* floppy only survive in UDI input.

## Selection

`--track` (physical cylinders), `--side` (heads) and `--sector` (sector ID
number R) each take comma-separated numbers and inclusive ranges — `0`, `0-3`,
`0,2,5-7` — and combine freely:

```
diskinfo.py game.trd --dump idam --track 0 --sector 9        # disk-info sector
diskinfo.py game.trd --dump sectors --track 0-2 --side 1     # first three side-1 tracks
diskinfo.py dump.udi --dump raw --track 79 --out t79.bin     # one raw track, binary
```

`--sector` cannot be combined with `--dump raw` (a raw stream is whole-track).
Selections outside the image produce a `note: ...` on stderr and are ignored.

## Binary output

`--out FILE` skips the hexdump and writes the payload as binary instead:
concatenated sector data for `--dump sectors`, concatenated track streams for
`--dump raw`. `--out -` writes binary to stdout. Text-only modes (`brief`,
`idam`) reject `--out`.

## Exit codes

`0` success; `1` the image could not be read; `2` bad usage (unknown format,
malformed selection, conflicting options).
