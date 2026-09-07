# Loader: HFE (HxC Floppy Emulator, v1 and v3)

> Part of [2026-09-02-universal-track-model](README.md). Depends on [flux-decoder.md](flux-decoder.md).
> Status: design. Fixture to add: an HFE of a TR-DOS disk written by HxC tools.

## 1. File layout (v1, little-endian)

```
0x000  8   "HXCPICFE"
0x008  1   format revision (0)
0x009  1   number of tracks (cylinders)
0x00A  1   number of sides
0x00B  1   track encoding: 0 ISOIBM_MFM, 1 AMIGA_MFM, 2 ISOIBM_FM, 3 EMU_FM, 0xFF unknown
0x00C  2   bit rate (kbit/s), 250 for DD
0x00E  2   RPM (0 = unknown, 300)
0x010  1   interface mode (7 = GENERIC_SHUGART_DD)
0x011  1   reserved
0x012  2   offset of track LUT in 512-byte blocks (usually 1)
0x014  1   write allowed
0x015  1   single step
0x016  1   track0s0 alt encoding, 0x017 track0s0 encoding, 0x018/0x019 for side 1
LUT    per track: offset (2, in 512-byte blocks), length (2, bytes) — the track block interleaves sides: 256 bytes side 0, 256 bytes side 1, …
Track data: bit cells, LSB first, MFM cells at the bit rate (1 = flux transition); one revolution
```

v3 (`"HXCHFEV3"`) adds an opcode stream inside the track data (0xF0 NOP, 0xF1 SETINDEX, 0xF2 SETBITRATE,
0xF3 SKIPBITS, 0xF4 RAND — weak bits); bytes are bit-reversed compared with v1.

## 2. Mapping to the model

Per track: de-interleave the 256-byte side blocks, convert to a bit stream, run `MfmDecoder` (or `FmDecoder` for
encoding 2/3) → byte stream + clock bitmap → `setRaw / setClockBitmap / reindex`. v3 `SETINDEX` fixes offset 0;
`RAND` runs mark the weak bitmap; `SETBITRATE` changes the cell length mid-track (handled by the PLL). Track length is
whatever the bit count divides to (≈6250 for DD).

## 3. Save

v3 by default (weak bits, exact index); v1 on request. Byte stream + clock bitmap → `MfmEncoder` → bit cells →
side-interleaved 512-byte blocks → LUT. Lossless for model content.

## 4. Tests

| Test | Assertion |
|------|-----------|
| `Detect_v1_v3` | signatures |
| `Load_TrdosHfe_Validates` | fixture → `validateTRDOSImage` passes, every track 16 sectors |
| `Load_Fm_Encoding` | encoding 2 → `Encoding::FM` |
| `Load_v3_Weak` | RAND run → weak bitmap set |
| `Save_RoundTrip_Model` | model → HFE → model: raw + clock bitmap identical |
| `Save_FromTrd_Load_ValidatesTrdos` | TRD → HFE → TRD equals original |
