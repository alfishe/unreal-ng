# TFMcom — TFM Music Compiler Module

## Overview

Music module format of the native ZX Spectrum tracker **TFM Music Compiler 1.1x**
for the TurboSound FM board (2 × YM2203, six FM channels). A module is a
header plus six independent per-channel event streams; a small position-fixed
Z80 player (`_tsfmplaye`, 1883 bytes at `0x61A8`) interprets them at 50 Hz and
drives both chips through the `0xFFFD`/`0xBFFD` port pair.

- **Signature**: `TFMcom1.1` + version digit (`TFMcom1.11` = v1, `TFMcom1.12` = v2)
- **Load address**: `0x8000` (32768) — the player hardcodes the tune base
- **Reference corpus**: 106 modules inside [`testdata/sound/tsfm/TSFM-EL.TAP`](../../../testdata/sound/tsfm/TSFM-EL.TAP)
- **Format status**: fully reverse engineered (no published sources exist — see
  the provenance section of the
  [player disassembly README](../../disasm/software/tfmplayer/README.md));
  every rule below was validated statically over all 636 channel streams of
  the corpus and dynamically against emulator port traces

Not to be confused with Shiru's *TFM Music Maker* (PC tracker, `TF0`/`TFE`
modules, YM2612) — a different toolchain despite the similar name.

## Container and load context

In `TSFM-EL.TAP` each module is one standard ROM-tape header/data block pair:

| TAP header field | value |
|---|---|
| type | 3 (CODE) |
| name | 10 chars, e.g. `03 DJ Tepp` |
| start | 32768 (`0x8000`) |
| length | 389–21885 bytes (corpus range) |

The same tape carries the player block (`_tsfmplaye`, type 3, start 25000) and
a loader-graphics block (`lnxdata`, start 31000) that the player never
references. Memory after a hardware-order load:

```
0x61A8  player (1883 bytes, position-dependent)
0x7918  lnxdata (overwritten by the tune from 0x8000)
0x8000  TFMcom module
```

## Detection

1. Block starts with the 9 ASCII bytes `TFMcom1.1`; byte `+9` is `1` or `2`.
2. Six little-endian u16 values at `+10..+21` are strictly increasing, the
   first is ≥ 34 and the last ≤ block length.
3. Bytes `+22..+33` are all zero (reserved).
4. In the corpus all 106 modules pass 1–3 with no false positives or exceptions.

## File Layout

```
        +0 ┌────────────────────────────────────────────┐
           │ "TFMcom1.1"            9 bytes ASCII       │
        +9 │ version digit          '1' or '2'          │
       +10 │ stream offset[1..6]    6 × u16 LE, rel. to │
           │                        module start        │
       +22 │ reserved               12 zero bytes       │
       +34 │ info strings           0–4 NUL-terminated  │
           │                        ASCII strings       │
           │ [optional 3-byte residual, see below]      │
    off[0] └────────────────────────────────────────────┘
          ┌────────────────────────────────────────────┐
          │ channel 1 event stream  (ends with 0x7F)   │
   off[1] ├────────────────────────────────────────────┤
          │ channel 2 event stream  (ends with 0x7F)   │
          │ …                                          │
   off[5] ├────────────────────────────────────────────┤
          │ channel 6 event stream  (ends with 0x7F)   │
  l ength └────────────────────────────────────────────┘
```

### Header fields

| Offset | Size | Field |
|---|---|---|
| `+0` | 9 | signature `TFMcom1.1` |
| `+9` | 1 | version digit: `'2'` = one event per interrupt (50 Hz); `'1'` = player patches its divider to run one frame per 6th interrupt (≈ 8.33 Hz event rate) |
| `+10` | 12 | six u16 **little-endian** stream offsets, relative to module start (the player's init adds `0x8000`) |
| `+22` | 12 | reserved, always zero |
| `+34` | … | info strings (below) |

### Info strings

Zero to four NUL-terminated ASCII strings, terminated by the last NUL
(corpus distribution: 0 strings × 9, 1 × 6, 2 × 25, 3 × 49, 4 × 17 modules).
Observed roles — presumed, from content: title, author, free comments; `\r\n`
line breaks occur inside comments. The player never reads this area.

**Residual trailer.** In 64 of 106 modules exactly three extra bytes sit
between the last string NUL and `off[0]`; in the other 42 the gap is absent
(strings end flush at `off[0]`). Non-zero values observed:
`FA FF 28` (54 modules) and a small variant set (`FF 28 01`, `FE 10 28`,
`FE B4 28`, …). The bytes are not a checksum of the strings, the player
ignores them, and their meaning is unknown (likely a compiler/editor
artifact). Writers may emit three zero bytes or omit the trailer.

### Channel regions

Stream *i* occupies exactly `[off[i], off[i+1])` for *i* = 1..5 and
`[off[6], length)` for channel 6 — the regions tile the whole module with no
gaps. Every one of the 636 corpus regions ends precisely on its `0x7F` loop
restart (636/636, no exceptions): there is no data after the loop end inside
a region, and calls never cross region boundaries (see
[Compression structure](#compression-structure-and-corpus-statistics)).

## Channel-to-hardware mapping

Derived from the six per-channel handler clones in the player
(`0x6365/0x6465/0x6566/0x6667/0x6767/0x6868`, ~0x100-byte near-clones; the
frame routine selects the chip, then calls the three stubs in order):

| stream | handler | chip | YM2203 FM slot | F-num regs hi/lo | key-off / key-on value (reg `0x28`) |
|---|---|---|---|---|---|
| 1 | `0x6365` | A (`0xFFFD ← 0xF8`) | ch 1 | `A4` / `A0` | `0x00` / `0xF0` |
| 2 | `0x6465` | A | ch 2 | `A5` / `A1` | `0x01` / `0xF1` |
| 3 | `0x6566` | A | ch 3 | `A6` / `A2` | `0x02` / `0xF2` |
| 4 | `0x6667` | B (`0xFFFD ← 0xF9`) | ch 1 | `A4` / `A0` | `0x00` / `0xF0` |
| 5 | `0x6767` | B | ch 2 | `A5` / `A1` | `0x01` / `0xF1` |
| 6 | `0x6868` | B | ch 3 | `A6` / `A2` | `0x02` / `0xF2` |

Register writes go to port `0xFFFD` (register select), data to `0xBFFD`, both
preceded by a busy-poll of the status word — see the
[player README](../../disasm/software/tfmplayer/README.md) for the port
protocol and the legacy-TurboSound hang.

## Playback model

- One **frame** = one interrupt (IM1, 50 Hz). The frame routine selects chip A,
  runs channels 1–3, selects chip B, runs channels 4–6. (v1 modules run a
  frame only every 6th interrupt.)
- Each channel dispatches **exactly one stream event per frame**. Waits defer
  the channel without consuming events; the loop ops re-dispatch *within the
  same frame*.
- Per-channel player state (kept in self-modified code, one clone each):
  - *resume pointer* — stream position for the next frame;
  - *wait counter* — counts the stored wait opcode up to `0xFF`;
  - *instrument counter + return slot* — while an instrument plays, its own
    events hijack the resume pointer; the saved main-stream position is
    restored when the counter reaches zero.
- Channel caches: the last absolute frequency bytes are remembered
  (`hi` byte and `lo`-derived delta base) and reused by relative notes.

## Opcode reference

All multi-byte operands belong to their opcode; stream interpretation is
deterministic (no alignment ambiguities — every corpus target lands on an
event boundary, see validation).

| op | size | operands | meaning |
|---|---|---|---|
| `0x00–0x7D` | 1 + [2] + 2N | see below | **event** (no key pulse; only `0x7E`/`0x7F` are reserved) |
| `0x7E` | 1 | — | **loop mark**: record restart position (byte after this op) |
| `0x7F` | 1 | — | **loop restart**: jump to the last mark; both loop ops continue dispatching in the same frame |
| `0x80–0xBE` | 1 + [2] + 2N | see below | **key-on event**: as event, plus reg `0x28` key pulses around it |
| `0xBF` | 3 | `off_hi off_lo` | **call16**: signed 16-bit **big-endian** offset from the byte after the operand; execute one event from the target this frame, resume after the operand next frame |
| `0xC0–0xDF`, ≠`0xD0` | 1 | — | **relative note**: `flo' = flo + (op − 0xD0)` (mod 256, cumulative) |
| `0xD0` | 4 | `len off_hi off_lo` | **instrument**: run `len` frames of one event each from target = after-operand + signed BE offset, then resume the main stream after the operand |
| `0xE0–0xFE` | 1 | — | **wait**: `N = 255 − op` silent frames (1–31) |
| `0xFF` | 2 | `off8` | **call8**: unsigned 8-bit offset; target = after-operand − 256 + `off8` (backward window of 256) |

### Event encoding (`0x00–0x7D`, `0x80–0xBE`)

```
opcode  [freq_hi freq_lo]  [reg data] × N      ← every field after the opcode is optional
```

- **bit 0** = 1 → two absolute frequency bytes follow the opcode
  (written before the pairs, see
  [Frequency encoding](#register-writes-and-frequency-encoding));
- **N = (op >> 1) & 0x1F** — count of inline *(register, data)* pairs that
  follow (register byte first — confirmed against runtime port traces);
  observed range 0–29, maximum encodable 31;
- **bit 6** is masked out by the count extraction — `0x40–0x7D` duplicate the
  `0x00–0x3D` encodings; both halves occur in real modules. Only `0x3E`/`0x3F`
  (and their key-on forms `0xBE`, outside this range) reach the maximum
  pair count N = 31 unambiguously;
- **bit 7** (the `0x80–0xBE` range) adds the key pulse: reg `0x28 ← slot`
  *before* the event (key off) and reg `0x28 ← 0xF0|slot` *after* it
  (key on), restarting the envelope;
- an event with no frequency bytes and N = 0 writes nothing — a pure frame
  tick used for spacing.

### Key opcodes in context

- **`0x7E` / `0x7F`** — the stream loops forever between them. Multiple `0x7E`
  marks may appear (964 marks vs 636 restarts in the corpus); the last mark
  before the `0x7F` wins. Code before the first mark plays once as an intro.
- **`0xBF` / `0xFF` calls** are one-shot: exactly one event is executed from
  the target, then the calling stream continues after the operand. The
  target's own continuation is never followed — called events are reusable
  pool material. Corpus reality: **all** 92,555 call/instrument offsets are
  negative (backward references only, up to −8867 bytes).
- **`0xD0` instrument** — the player stores the return position, seeds a frame
  counter with `len`, and dispatches one event per frame from the sub-stream.
  Wait opcodes inside an instrument stall the channel (counter and pointer
  both freeze). Observed lengths: 15–235 frames. Note the player keeps a
  single return slot per channel, so an instrument nested inside a called
  event would overwrite the pending return — an edge case the corpus never
  exercises.
- **`0xE0–0xFE` wait** — the stored opcode value counts *up* to `0xFF`, so
  the silent-frame count is `255 − op`. One wait opcode spans at most 31
  frames (≈ 0.62 s at 50 Hz); longer rests chain multiple waits.

## Register writes and frequency encoding

### Inline pairs

Each pair is `(register, data)`, written register-select-then-data to the
currently selected chip. Observed register usage across the corpus
(83,845 pair writes):

| registers | YM2203 meaning | writes |
|---|---|---|
| `0x30–0x3F` | DT / MULT | 12,576 |
| `0x40–0x4F` | total level | 31,183 |
| `0x50–0x5F` | KS / attack rate | 7,480 |
| `0x60–0x6F` | AM / decay rate | 8,503 |
| `0x70–0x7F` | sustain rate | 7,867 |
| `0x80–0x8F` | SL / release rate | 8,768 |
| `0xB0–0xB2` | feedback / algorithm | 3,233 |
| `0x90–0x9E`, `0xA8–0xAE`, `0x10–0x1C` | **undefined on YM2203** (OPNA-only or unused) | 4,235 |

The out-of-spec groups (≈ 5 % of writes) are ignored by real YM2203
hardware; they appear to be compiler artifacts. SSG registers (`0x00–0x0D`)
never appear in pairs — the format drives FM voices only. Voice writes use
the operator register grid (`reg = group + 4·operator + channel`), so
envelope setups appear as stride-4 register ladders.

### Frequency encoding

Absolute (event bit 0 = 1): the two bytes go to the channel's F-num register
pair using the YM2203 convention — `hi` register (`A4`-class) carries
`(block << 3) | (fnum >> 8)`, `lo` register (`A0`-class) carries
`fnum & 0xFF`. The player also latches both bytes into per-channel caches
(self-modified immediates in the handler clone).

Relative (`0xC0–0xDF`): the `lo` byte is recomputed as
`flo' = flo_cached + (op − 0xD0)` (mod 256) and written to the `lo`
register; the `hi` register is rewritten from the cached `hi` byte. The
result *replaces* the cache, so consecutive relative notes form cumulative
ladders (glissando/vibrato); the offset range is −16…+15 per step. Relative
notes are only meaningful after an absolute write has seeded the caches.

## Worked example

Opening of `03 DJ Tepp` stream 1 (module offset `0x5A`):

```
bytes              decode
00                 event: no freq, N=0            — silent tick
7E                 loop mark                      — loop body starts at +0002
00                 event: silent tick
40                 event: silent tick             — bit 6 masked by N
F7                 wait 8 frames                  — 255 − 0xF7
20 | 50 1F 54 1F 58 12 5C 1F  60 00 64 00 68 00 6C 00
   | 70 00 74 00 78 01 7C 01  80 0F 84 0F 88 3F 8C 3F
                   event, N=(0x20>>1)&0x1F=16 pairs — full voice setup:
                   KS/AR ← 1F 1F 12 1F, AM/DR ← 00 00 00 00,
                   SR ← 00 00 01 01, SL/RR ← 0F 0F 3F 3F
9B | 1C 62 | 30 74 34 34 38 74 3C 34  40 19 44 19 48 19 4C 06
   | 90 00 94 00 98 00 9C 00  B0 2C
                   key-on event, freq hi=1C lo=62, N=13 pairs
                   (DT/MULT, TL, undefined 0x9x, feedback)
EB                 wait 20 frames
...
```

Channel 6 of the same tune opens with an instrument rest:

```
00 7E              silent tick, loop mark
E0 × 16            sixteen 31-frame waits (intro rest)
D0 10 FF EC        instrument: len=16 frames, offset 0xFFEC = −20
                   → target = +0x16 − 20 = +0x0002 (back onto the wait run)
D0 10 FF E8        next instrument, offset −24, …
```

The first 32 port writes captured from a live emulator port trace of this
tune are exactly these pair sequences (e.g. `51←10 55←50 … 8D←17` for
channel 2, `50←1F 54←1F … 8C←3F` for channel 1) — register byte first,
byte-for-byte identical to the stream.

## Compression structure and corpus statistics

The format is a per-channel bytecode with aggressive factoring: earlier
events double as reusable pool data, and all references are backward.

| metric | value |
|---|---|
| modules / channel streams | 106 / 636 |
| module sizes | 389–21,885 bytes |
| versions | all `'2'` (50 Hz) |
| regions ending exactly on `0x7F` | 636 / 636 |
| total opcodes decoded | 266,405 |
| waits / call8 / call16 | 78,420 / 45,968 / 22,674 |
| relative notes / instruments | 37,553 / 23,913 |
| events (key-off / key-on) | 30,890 / 25,387 |
| loop marks / restarts | 964 / 636 |
| call+instrument targets | 92,555 → 18,382 distinct (≈ 5× reuse) |
| targets inside own region / on event boundaries / backward | 100 % / 100 % / 100 % (max −8,867) |
| events with N = 0 pairs | 38,101 of 56,277 (68 %) |
| instrument lengths | 15–235 frames |

Structural consequences for tooling: a single linear walk per region with the
opcode table above parses every stream unambiguously; call/instrument targets
resolve only backward, so one pass in forward order has already seen every
target; and the loop-closing `0x7F` is always the region's final byte, so
`off[i+1]` can serve as the stop sentinel.

## Validation methodology

Three independent methods, all agreeing:

1. **Static disassembly** of the player (1,883 bytes,
   [`docs/disasm/software/tfmplayer/`](../../disasm/software/tfmplayer/README.md)):
   header parsing at init, the opcode dispatch tree at `0x637A` and its five
   clones, the SMC slot map. The pair-count formula `N = (op >> 1) & 0x1F`
   comes from the `rra` before `and 01fh` on the no-frequency path; the
   big-endian signed call offsets from `ld b,(hl)` / `ld c,(hl)` feeding
   `add hl,bc`.
2. **Corpus walker**: applying the grammar to all 636 streams yields the
   statistics above with zero structural violations (every walk terminates on
   its own `0x7F`, every target is in-region and on an event boundary).
3. **Runtime port traces**: the player running in a PENTAGON-model emulator
   instance (TSFM device) with the port-trace profiler filtered to
   `0xFFFD`/`0xBFFD` outputs; captured reg/data bursts match the decoded
   stream bytes exactly, in order, register-first.

## Open questions

- **String roles** — title/author/comment ordering is inferred from content,
  not from code (the player ignores the area).
- **3-byte residual trailer** — purpose unknown; see header section.
- **v1 modules** — no `'1'`-version module exists in the corpus; the 1-in-6
  frame divider behaviour is derived from the player code only.
- **Undefined register writes** (`0x90–0x9E`, `0xA8–0xAE`, `0x10–0x1C`) —
  presumably compiler output that YM2203 hardware discards; a faithful
  emulator should mirror the ignore.

## References

- [`docs/disasm/software/tfmplayer/`](../../disasm/software/tfmplayer/README.md) —
  annotated player disassembly: entry points, SMC map, port protocol, provenance
- [`docs/disasm/software/tfmplayer/tsfm-sna-guide.md`](../../disasm/software/tfmplayer/tsfm-sna-guide.md) — wrapping
  player + module into an instant-play `.sna`
- `docs/inprogress/2026-09-10-turbosound-fm/verification/player-entry-points.md` —
  harness-level player contract
- [`tools/porttrace/`](../../../tools/porttrace/README.md) — the port-trace
  profiler used for runtime validation
- `core/tests/_helpers/tsfmplayerharness.{h,cpp}` — in-process test harness
