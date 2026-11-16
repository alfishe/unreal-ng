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

v3 (`"HXCHFEV3"`) adds an opcode stream inside the track data; bytes are bit-reversed compared with v1. The opcode
semantics follow Greaseweazle (`greaseweazle/image/hfe.py`), the reference both HxC and Greaseweazle files agree with:

| Opcode | Arguments | Cells |
|--------|-----------|-------|
| 0xF0 NOP | none | none (padding) |
| 0xF1 SETINDEX | none | none (index here) |
| 0xF2 SETBITRATE | 1 byte: 36 MHz / cell rate (72 for 2 µs cells) | none |
| 0xF3 SKIPBITS | n, x | the low 8 − n bits of x; x = 0xF4: 8 − n weak cells |
| 0xF4 RAND | none | 8 weak cells |

The LUT length is twice the longer side's actual bytes: the padding of the last 512-byte block is not counted.
FM is stored at double rate, each FM cell as two (`0c`), so a file at 250 kbit/s holds FM and MFM tracks alike.

## 2. Mapping to the model

Per track: de-interleave the 256-byte side blocks (LUT length / 2 bytes a side), convert to bit cells (v3: run the
opcodes, a weak flag per cell), then decode with `MfmDecoder` and with `FmDecoder` on both phases of the doubled
stream (and on the cells as they are when the header says FM at ≤150 kbit/s). The decode with the most sectors wins,
the header's encoding byte first on a tie (a disk-wide byte cannot describe a mixed disk). Byte stream + clock
bitmap → `setRaw / setClockBitmap`; a byte is weak when any of its cells is (`weakBytes()` of the decoders).

## 3. Save

v3 by default; v1 on request (weak bits dropped, with a warning). Byte stream + clock bitmap → `MfmEncoder` /
`FmEncoder` (FM doubled) → bit cells. v3 starts each side with SETBITRATE 72, writes weak bytes as RAND, sends a
cell byte that would read as an opcode (high nibble F) as SKIPBITS 1 + seven cells, pads with NOP. Header: bit rate
250, encoding FM only when every track is FM, unused bytes 0xFF. Lossless for model content: a 6250-byte track
reads back as 6250 bytes.

## 4. Tests

| Test | Assertion |
|------|-----------|
| `Detect_v1_v3` | signatures |
| `Load_TrdosHfe_Validates` | fixture → `validateTRDOSImage` passes, every track 16 sectors |
| `Load_Fm_Encoding` | encoding 2 → `Encoding::FM` |
| `Load_v3_Weak` | RAND run → weak bitmap set |
| `Save_RoundTrip_Model` | model → HFE → model: raw + clock bitmap identical |
| `Save_FromTrd_Load_ValidatesTrdos` | TRD → HFE → TRD equals original |
