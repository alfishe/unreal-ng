# Loader: SCP (SuperCard Pro flux image)

> Part of [2026-09-02-universal-track-model](README.md). Depends on [flux-decoder.md](flux-decoder.md).
> Spec already in the repo: `docs/file-formats/disk-images/scp/`. Status: design.

## 1. File layout (from the SCP spec, little-endian unless noted)

```
0x00  3   "SCP"
0x03  1   version (major.minor nibbles)
0x04  1   disk type (manufacturer/type)
0x05  1   number of revolutions per track
0x06  1   start track, 0x07 end track (track number = cylinder × 2 + side)
0x08  1   flags: bit0 index (flux starts at index), bit1 96 tpi, bit2 360 rpm, bit3 normalised, bit4 read/write, bit5 footer, bit6 extended, bit7 non-flux-only
0x09  1   bit-cell width (0 = 16 bits)
0x0A  1   heads (0 both, 1 side 0, 2 side 1)
0x0B  1   resolution (0 = 25 ns)
0x0C  4   checksum of everything after the header
0x10  4×168 track data header offsets (0 = track absent)
Track data header: "TRK", track number, then per revolution: index time (4), flux count (4), data offset (4)
Flux data: 16-bit big-endian intervals in 25 ns units (0 = overflow, add 65536)
```

## 2. Mapping to the model

Per (cylinder, side): take all revolutions (one tick = 25 ns × (resolution + 1)), `FluxPll` (FM autodetected:
DD MFM has 6 µs intervals, DD FM only 4 and 8 µs), decode each revolution to bytes + clock bitmap, align revolutions
at the index, majority-vote the bytes, mark disagreements as weak bits; each clock mark comes from the first
revolution that decoded the voted byte. An end track past the 168-entry table is cut to 167 with a warning. 96 tpi flag with 40-track images → every second
cylinder is skipped (double-stepping) with a note in `lastWarnings()`.

## 3. Save

Single revolution, synthetic timing (see flux-decoder.md §3), flags: index, normalised, 96 tpi for 80-track images.
Resolution 0 (25 ns), index time = the sum of the flux (200 ms for 6250 MFM bytes), end track = (cylinders − 1) × 2 +
(sides − 1). More than 84 cylinders do not fit the track table: the save is refused.
Lossless for the model, not for original flux jitter.

## 4. Tests

| Test | Assertion |
|------|-----------|
| `Detect_Signature` | "SCP" |
| `Load_TrdosScp_Validates` | fixture (to be added) → `validateTRDOSImage` passes |
| `Load_MultiRev_WeakBits` | synthetic 3-revolution track with one byte varying → weak bit set, majority value stored |
| `Load_Fm_Autodetect` | synthetic FM flux → `Encoding::FM` |
| `Save_RoundTrip_Model` | model → SCP → model identical |
| `Pll_Jitter` | ±20 % jittered synthetic flux decodes exactly |
