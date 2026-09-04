# TZX Loader Technical Design

| | |
|---|---|
| **Status** | Design, r0 |
| **Date** | 2026-09-01 |
| **Parent design** | [Tape Manager & Unified Tape Model](design.md) (r3) — this document expands parent §5.4 "TZX block-type mapping" into an implementation-complete loader specification |
| **Target phase** | Parent §13 P2 (loader rewrite), landing on the P1 unified model |
| **Affected code** | `core/src/loaders/tape/loader_tzx.h/.cpp` (rewrite), `core/src/loaders/tape/loader_tape.h` (contract, P1), `core/src/emulator/io/tape/tapecatalog.h` (profile types, P1) |

## Revision History

| Rev | Date | Notes |
|-----|------|-------|
| r0 | 2026-09-01 | Initial full specification: container framing, byte-level layouts for all block IDs $10–$5A, linearization engine, error policy, fixtures, test inventory, amendments A1/A2 to the parent design |
| r1 | 2026-09-01 | Aligned with parent r3: **A1 corrected** (widening the profile requires widening `generateBitstream()`'s own `uint16_t` parameters — the engine change A1 originally disclaimed); loader contract v2 (buffer-based `Load`, `TapeFormatInfo` probe on the `ZXTape!` magic, `TapeLoadStatus` error channel); fixtures moved to `testdata/loaders/tzx/` to match the tree; §8a fast-load eligibility contribution per parent §5.8 |

## 1. Scope and Compatibility Contract

This document specifies **the reader** for TZX (rev 1.20/1.21) producing the parent design's shared structures. It does not modify the unified model; where the TZX format demands more than the current model offers, a numbered **amendment** is proposed against `design.md` instead of a local workaround.

| Parent concept (design.md §5) | This document's obligation |
|---|---|
| `LoaderTapeBase::Load(bytes, sourceName) → TapeImage` | `LoaderTZX` implements exactly this — **buffer in, no filesystem access** (parent r3 §5.3); no extra public surface beyond it and `Format()` |
| `TapeFormatInfo::Probe` | `100` when the buffer opens with `"ZXTape!"` + `$1A`, else `0` — TZX is magic-bearing, so selection never depends on the `.tzx` extension |
| `TapeLoadStatus` / `errorText` | `Malformed` for the §7 fatal rows; `Warnings` whenever `parseWarnings` is non-empty; `Ok` otherwise |
| `TapeFastLoadEligibility` (parent §5.8) | Nothing to implement — see §8a; the loader's obligation is only to fill `rawFlag`, `checksumValid`, `timing` and `playable` honestly |
| `TapeBlock` (bytes + `timing`, or `edgePulseTimings` precomputed) | Every playable TZX block maps to one of the three representations (§4) |
| `TapeBlockDescriptor` v2 | Loader supplies the ground-truth fields (kind, timing, group labels, checksum) it uniquely knows; `TapeCatalogParser` derives the rest |
| `TapeImage { format, title, hardwareNote, controlFlowLinearized }` | `format = TZX`; title/hardware from $30/$32/$33 (§6); `controlFlowLinearized` set by the linearizer (§5.6) |
| `TAPE_LINEARIZE_MAX_BLOCKS` (default 4096) | The linearizer's expansion budget — enforced exactly once, at the expansion site (§5.6) |
| `TapVariantEnum` | Not applicable (TAP-family only) — left at `Standard` |

**Amendment A1 (parent §5.2), corrected in r1** — widen `TapeTimingProfile` period fields from `uint16_t` to `uint32_t` (`pilotHalfPeriod`, `sync1`, `sync2`, `zeroHalfPeriod`, `oneHalfPeriod`):
- TZX $11 stores the pilot half-period as a **u32** (rev 1.20 widened it from the 1.13 word — §2.3);
- CSW/PZX pulse trains already flow through `edgePulseTimings` (`std::vector<uint32_t>`, tape.h L98) where long silences exceed 65535 T;
- keeping the profile at u16 would silently truncate turbo pilots > 65535 T (real in protections).

**r0 claimed this needed "no engine change". That was wrong.** `Tape::generateBitstream()` declares
all five period parameters as `uint16_t` (tape.h L268-275), so a u32 profile field passed into it
truncates at exactly the boundary A1 exists to defend. The engine signature widens with the profile,
and because the profile type is a P1 deliverable, **so is the signature change** — it lands before any
TZX code exists to exercise it. Cost: 10 bytes per descriptor, one signature edit, no call-site
semantics change. Related P1 cleanup (parent §5.2): `pilotLength_periods` actually carries *pulses*
(tape.cpp L632), so it is renamed `pilotLength_pulses`; the `$11` pilot pulse count maps to it 1:1
with no ×2 conversion — a mistake here doubles every turbo pilot.

**Amendment A2 (parent §5.3)** — `TapeImage` gains `std::vector<std::string> parseWarnings;`:
- TZX files in the wild routinely contain quirks (skipped deprecated blocks, bound-limited linearization, malformed sub-blocks). The catalog/UI warning badge needs a data path, not just log lines. Loaders append human-readable one-liners; `Tape` exposes them next to the catalog; empty for clean TAP loads.

## 2. TZX Container Primer

### 2.1 File header (10 bytes)

| Offset | Size | Value |
|---|---|---|
| 0 | 7 | `"ZXTape!"` ASCII signature |
| 7 | 1 | `0x1A` end-of-file marker |
| 8 | 1 | major version = `1` |
| 9 | 1 | minor version (e.g. `0x13`, `0x20`) |

Acceptance policy (**D1**): major must be 1; minor `≤ 0x21` accepted silently; minor `> 0x21` accepted with an A2 warning (forward-compat: post-1.21 blocks would be unknown IDs and handled by §7). Minor `< 0x20` accepted **with a warning** — see D2.

### 2.2 The two framing classes (the single most important parsing rule)

Every block starts with a 1-byte ID, then one of two framings:

- **Class A — self-describing** (`$10, $11, $12, $13, $14, $15, $20, $21, $22, $23, $24, $25, $28, $31`): no length prefix; the payload size follows from the block's own fields. The parser must know each layout to find the next block.
- **Class B — length-prefixed** (`$18, $19, $26, $27, $2A, $2B, $30, $32, $33, $34, $35, $40, $5A`): `[u32 LE block length][payload]`; length counts the payload bytes after the length field itself.

All multi-byte integers are **little-endian**; jump/call offsets are **signed**.

Unknown ID policy (**D3**): stop parsing at that offset, keep everything parsed so far, append an A2 warning. Rationale: the spec's forward-compatibility promise ("new blocks carry a u32 length so old readers can skip") cannot be distinguished from a Class-A unknown — a wrong guess desynchronizes the stream. No known file in circulation uses IDs outside the tables below; revisit if one appears (risk R3).

### 2.3 Revision quirks (why some references disagree)

Public mirrors document **rev 1.13 layouts** for several blocks (Claus Jahn's otherwise excellent page shows $11 with an 18-byte header, metadata blocks without the u32 length, $13 with a 1-byte count). The 19 Dec 2006 rev 1.20 specification (the document referenced from `loader_tzx.h`) is authoritative and is what real-world files follow; this design implements **1.20/1.21 framing exclusively**. Pre-1.20 files are museum pieces — policy (**D2**): parse with 1.20 rules, warn, recommend re-conversion; no dual-layout support. Per-field doubts that survive this rule are recorded as checkpoints C1–C5 (§12) and settled by golden fixtures in T2, not by guesswork.

## 3. Loader Architecture

### 3.1 Position in the loader family

```
LoaderTapeBase (loader_tape.h, P1)          ← TapeLoaderRegistry::Select() routes here on the
                                               "ZXTape!" magic, not on the file extension
    └── LoaderTZX (loader_tzx.h/.cpp, rewritten by this design)
          ├── validateFile()      10-byte header + version policy D1   (kept from stub)
          ├── Pass 1 — scan       raw block index: {id, fileOffset, payloadSpan}
          ├── Pass 2 — decode     playable blocks → TapeBlock + descriptor hints
          ├── Pass 3 — linearize  control-flow evaluation (§5.6) → final flat order
          └── BuildTapeImage()    blocks + descriptors + metadata + warnings
LoaderCSW core (P3)                          ← shared pulse decoder; $18 delegates (§4.7)
```

- **Whole-file I/O** stays as today (`_buffer`, one read, one parse) — TZX files are small; streaming adds nothing.
- The `EmulatorContext*`/`ModuleLogger*` constructor shape and the CUT wrapper (`LoaderTZXCUT`, `#ifdef _CODE_UNDER_TEST`) are preserved — the existing test fixture keeps compiling against the rewrite.
- `parseTZX()` splits into the three passes above; `parseHardware(uint8_t*)` becomes `ParseHardwareTriples()` (§6.4) with the same logging intent.

### 3.2 Internal records

```cpp
struct TzxRawBlock                     // Pass 1 output — one per physical block
{
    uint8_t id;
    size_t fileOffset;                 // of the ID byte
    size_t payloadOffset;              // first byte after ID (+u32 length for Class B)
    size_t payloadSize;                // for Class A: derived, exact
};

struct TzxLinearContext                // Pass 3 state
{
    std::vector<size_t> output;        // indices into the raw block list, in play order
    size_t expansions = 0;             // budget consumed (≤ TAPE_LINEARIZE_MAX_BLOCKS)
    bool bounded = false;              // set when the budget is exhausted
};
```

## 4. Playable Blocks — Byte Layouts and Mappings

Conventions below: offsets are from the first byte **after** the ID; `u16/u32/u24` = little-endian; `s16/s32` = signed. Payloads of $10/$11/$14 carry the TAP byte format (flag + data + XOR checksum — parent §5.5), so `checksumValid`, `rawFlag`, `payloadPreview` and header interpretation work identically to the TAP path.

### 4.1 $10 — Standard speed data block

| Off | Size | Field |
|---|---|---|
| 0 | u16 | pause after block, ms |
| 2 | u16 | data length |
| 4 | … | data (flag + payload + checksum) |

**Mapping**: `bytes + StandardRom` — identical to a TAP block including the pause. `TapeBlock::timing` = nullopt (parent §5.2: nullopt *is* the ROM-standard profile, so existing TAP code paths serve TZX $10 unchanged, including the fast-load trap). Pilot counts come from the spec defaults (flag < $80 → 8063 pilot pulses, ≥ $80 → 3223); the engine's own ROM constants (tape.h L19-20) govern waveform generation — the 8063/8064 ±1 discrepancy between spec prose and engine constants never materializes because $10 carries no counts to honor (engine behavior is bit-identical to the TAP path by construction; see checkpoint C5).

Kind classification: same flag rule as TAP — $00 + 19 bytes → `Header` (parse per parent §5.5); ≥ $80 → `Data`; anything else → `Custom`.

### 4.2 $11 — Turbo speed data block (20-byte header)

| Off | Size | Field | ROM default |
|---|---|---|---|
| 0 | u32 | pilot half-period | 2168 |
| 4 | u16 | sync 1 | 667 |
| 6 | u16 | sync 2 | 735 |
| 8 | u16 | zero-bit half-period | 855 |
| 10 | u16 | one-bit half-period | 1710 |
| 12 | u16 | pilot pulse count | 8063 / 3223 |
| 14 | u8 | used bits in last byte (1–8, MSB first: `6` = `++++++00`) | 8 |
| 15 | u16 | pause after block, ms | 1000 |
| 17 | u24 | data length | — |
| 20 | … | data | — |

> The u32 pilot and 20-byte header are rev 1.20; the 1.13-era 18-byte word-pilot layout is deliberately not supported (D2). Corroboration: the FreeBSD `file` magic database locates $11 data at offset 21 (ID + 20).

**Mapping**: `bytes + Custom` — `TapeTimingProfile { profile=Custom, pilotPulses, pilotHalfPeriod, sync1, sync2, zeroHalfPeriod, oneHalfPeriod, pauseMs, bitsInLastByte }` filled verbatim (A1 widths). Defensive: `bitsInLastByte == 0` → 8. `baudEstimate = 3500000 / (zero + one)`. Kind: $00 flag + 19 bytes may still classify `Header` (legal in TZX); ≥ $80 → `Data`; else `Custom`. Header+data pairing and headerless classification (parent §5.5) work across mixed $10/$11 — the *preceding valid Header* rule is profile-agnostic by design.

If every timing field equals the ROM defaults the profile stays `Custom` (source-verbatim — "custom that happens to equal ROM" is honest metadata and keeps the trap disabled; only $10/nullopt is trappable, §8).

### 4.3 $12 — Pure tone

| Off | Size | Field |
|---|---|---|
| 0 | u16 | pulse length (T-states) |
| 2 | u16 | pulse count |

**Mapping**: `PulseStream`, kind `Tone`. `edgePulseTimings` = `count` repetitions of the period; `data` empty; `timing.profile = PulseStream`. No pause field — pauses only via $20. `estimatedSeconds = count × period / 3.5 MHz` (pulse-sum rule, parent §5.5).

### 4.4 $13 — Pulse sequence

| Off | Size | Field |
|---|---|---|
| 0 | u16 | pulse count N |
| 2 | u16 × N | pulse lengths |

**Mapping**: `PulseStream`, kind `PulseStream`; `edgePulseTimings` copied verbatim. Checkpoint **C1**: Jahn's mirror documents the count as u8 (1–255); the 1.20 spec reads u16 — implement u16, and let the golden fixture in §9 decide once and for all (a u8-count file would mis-parse loudly, not silently, under u16 rules when N is large).

### 4.5 $14 — Pure data block

| Off | Size | Field | ROM default |
|---|---|---|---|
| 0 | u16 | zero-bit half-period | 855 |
| 2 | u16 | one-bit half-period | 1710 |
| 4 | u8 | used bits in last byte | 8 |
| 5 | u16 | pause after block, ms | 1000 |
| 7 | u24 | data length | — |
| 10 | … | data | — |

**Mapping**: `bytes + Custom` with `pilotPulses = 0`, `sync1 = sync2 = 0` — no pilot, no sync, data bits start immediately. Frequently follows $12/$13 blocks that hand-built the pilot (custom loaders); the descriptor's `groupLabel` (§5.2) usually names the group they belong to.

### 4.6 $15 — Direct recording block

| Off | Size | Field |
|---|---|---|
| 0 | u16 | T-states per sample (158 ≈ 22050 Hz, 79 = 44100 Hz) |
| 2 | u16 | pause after block, ms |
| 4 | u8 | used bits (samples) of last byte |
| 5 | u24 | data length |
| 8 | … | bit stream — each bit = absolute EAR level (0 = low, 1 = high), MSB first |

**Mapping**: `PulseStream`, kind `PulseStream`. Bits are **levels, not edges** — the expansion packs runs of equal bits into single hold-pulses:

```text
samples → pulses:  level_i run of n samples  ⇒  one edgePulseTimings entry = n × t-states-per-sample
                      (a toggle follows each entry; the entry carries the duration AT the current level)
initial level = $2B-derived level state (§4.9), else the level the previous block ended on, else low
```

`pauseMs` rides in the profile (played as level-low hold after the last sample); `bitsInLastByte` = used bits. Checksum: n/a. `estimatedSeconds` = total samples × t-states-per-sample / 3.5 MHz.

### 4.7 $18 — CSW recording block (Class B)

| Off | Size | Field |
|---|---|---|
| 0 | u32 | block length |
| 4 | u16 | pause after block, ms |
| 6 | u24 | sample rate (Hz) |
| 9 | u8 | compression type (see C4) |
| 10 | u32 | pulse count after decompression (validation only) |
| 14 | … | CSW-encoded pulse data |

**Mapping**: decompress (RLE per CSW v1 encoding or zlib per CSW v2, selected by the compression byte — checkpoint **C4** fixes the exact constants against the CSW spec in T3; the $18 payload is CSW v2-family either way) → scale each pulse `× 3.5 MHz / sampleRate` → `PulseStream`, kind `PulseStream`, one block (long pauses ≥ 2 s split into pseudo-blocks exactly like standalone `.csw` files, parent §5.4 — shared code path with `LoaderCSW`, no divergence). The u32 pulse count is validated against the decoded array; mismatch → A2 warning, proceed with the decoded array (it is the ground truth).

### 4.8 $19 — Generalized data block (Class B) — parse, catalog, defer playback

Full layout (one place in the ecosystem documents it cleanly — reproduced here for T2/T5):

| Off | Size | Field |
|---|---|---|
| 0 | u32 | block length |
| 4 | u16 | pause after block, ms |
| 6 | u32 | `TOTP` — symbols in pilot/sync stream (may be 0) |
| 10 | u8 | `NPP` — max pulses per pilot symbol |
| 11 | u8 | `ASP` — pilot alphabet size (0 = 256) |
| 12 | u32 | `TOTD` — symbols in data stream (may be 0) |
| 16 | u8 | `NPD` — max pulses per data symbol |
| 17 | u8 | `ASD` — data alphabet size (0 = 256) |
| 18 | (2·NPP+1)·ASP | pilot SYMDEF table |
| … | 3·TOTP | pilot stream: `[symbol index u8][repeat count u16]` records |
| … | (2·NPD+1)·ASD | data SYMDEF table |
| … | ceil(BITS·TOTD/8) | data bit stream, `BITS = ceil(log2(ASD))`, MSB-first groups |

SYMDEF = `[flags u8][pulse lengths u16 × NPP]` where flags select the level before the symbol's pulses (`00` opposite / `01` same / `10` force low / `11` force high); zero-filled trailing pulses are ignored.

**v1 mapping** (parent R6 stands): catalog-only descriptor — kind `Custom`, `playable = false`, `rawSize` = block length, `estimatedSeconds` = pause only, A2 warning `"generalized data block N not playable in this version"`. **v2 sketch** (deferred, no commitment): SYMDEF tables + PRLE streams expand to `PulseStream` mechanically — the layout above is complete enough that no format research remains, only implementation.

### 4.9 $2B — Set signal level (Class B)

`[u32 length = 1][u8 level]` — 0 = low, 1 = high. **Mapping**: not a block; sets the initial EAR level for the **next playable block** — stored as that block's `timing.invertedLevel = (level == 1)` (parent §5.4 row). Consumed by $15 expansion (§4.6) and pulse playback start polarity; harmless (already the default) for byte blocks. A $2B with no following playable block is dropped with a debug log.

## 5. Control Blocks and the Linearization Engine

### 5.1 $20 — Pause / stop the tape

`[u16 milliseconds]` — `0` means STOP. **Mapping** (parent §5.4 pause policy):

| Pause | Result |
|---|---|
| `0` | terminal `Control` stop marker (end of tape) — Speedlock-style terminators |
| `1–5` ms | merged into the previous playable block's `timing.pauseMs` (no catalog entry) |
| `> 5` ms | `Control` pseudo-block with `estimatedSeconds = ms/1000`; seek granularity preserved |

A $20 at the very start of the tape (no previous block) with ≤ 5 ms is dropped; > 5 ms becomes a leading `Control` pause block.

### 5.2 $21/$22 — Group start / end

`$21`: `[u8 nameLen][name]`; `$22`: empty. **Mapping**: no blocks — the open group's name becomes `groupLabel` of every playable block decoded until the matching $22 (nesting is not allowed by the spec; a stray $22 logs a warning and clears nothing). Group labels flow into the Tape Manager's tree grouping (parent §9.1) and WebAPI block rows.

### 5.3 $23 — Jump to block

`[s16 relative offset]` — relative to the **jump block itself**: `1` = next block (no-op), `2` = skip one, `−1` = previous, `0` = endless loop. `0` maps to a terminal stop marker + A2 warning ("protection loop — tape ends here"); any backward jump or forward jump beyond EOF saturates: forward → clamp to EOF (tape end), backward → honored (the linearizer budget guards abuse, §5.6).

### 5.4 $24/$25 — Loop start / end

`$24`: `[u16 repeat count]`; `$25`: empty. Body = blocks strictly between $24 and its matching $25; nesting not allowed (unbalanced $25 → warning, treated as loop end of the innermost open loop; unclosed $24 → warning, body extends to EOF). Semantics (**C3**): body plays `count` times **in total** (defensive: `count < 2` → 1); the golden loop fixture in §9 pins this against a reference emulator capture before merge.

### 5.5 $26 — Select block (Class B) / $27 — Call sequence (Class B) / $28 — Return

`$26` layout per 1.20: `[u32 blockLen][u16 count][u16 stringLen × count][s32 offset × count][strings]`; offsets relative to the $26 block. **v1 policy** (parent §5.4): interactive selection is out of scope — treat as **jump to `offset[0]`** (the first/typically-default branch), log once, expose the full selection list in the A2 warning string ("select block: 3 branches — taking 1: 'START GAME'"). Checkpoint **C2** guards the field widths (Jahn documents a u16-offset/u8-count variant) — the fixture decides; on any parse doubt: degrade to plain continuation (no jump) + warning, never fatal.

`$27`: `[u32 blockLen][u16 callCount][s16 offset × callCount]` — offsets relative to the $27 block; each target sequence **must** end with `$28`. Mapping: each called sub-sequence is inlined once, in list order, then execution continues after $27. A called target without a terminating $28 → warning; sequence runs to the next control boundary.

### 5.6 Linearization algorithm

Two logical passes over the Pass-1 raw index (in-memory, no re-reading):

```text
Linearize(rawBlocks, budget = TAPE_LINEARIZE_MAX_BLOCKS):
  pc = 0
  loopStack = []                      // (bodyStart, remaining)
  callStack = []                      // (returnPc, remainingOffsets)
  out = []                            // flat play order (raw indices)
  while pc < rawBlocks.size:
      if out.size >= budget:                   // bound hit → linear tail mode (below)
          break
      b = rawBlocks[pc]
      switch b.id:
        $20..$2B-playable → out.push(pc); pc++
        $23 jump   → pc += offset              // 0 handled as stop (§5.3)
        $24 loop   → loopStack.push(pc + 1, count - 1); pc++
        $25 loopend→ if loopStack.empty: warn; pc++
                     else (bodyStart, remaining) = pop;
                          if remaining > 0: pc = bodyStart; loopStack.push(bodyStart, remaining - 1)
        $26 select → pc += firstOffset          // v1 policy
        $27 call   → callStack.push(pc + 1, offsets[1..]); pc = pc + offsets[0]
        $28 return → if callStack.empty: warn; pc++
                     else (returnPc, rest) = pop
                          if rest empty: pc = returnPc
                          else: callStack.push(returnPc, rest[1..]); pc = returnPc + rest[0]
        skipped ids (§6) → pc++                  // metadata/deprecated: not in output
  if out.size >= budget:                        // linear tail per parent §5.4: control blocks remain
      bounded = true                            //   as Control entries, playback proceeds linearly
      while pc < rawBlocks.size:                // emit the remaining blocks in file order,
          b = rawBlocks[pc]                     //   NO control-op execution (jumps/loops inert),
          if b.id in playableIds: out.push(pc)  //   $20 > 5 ms pauses as Control pause markers,
          elif b.id == $20 and ms > 5: out.push(pc)   //   other control ids as Control entries
          elif b.id in controlIds: out.push(pc) //   metadata still skipped
          pc++
  return out
```

Pseudocode is normative for **state transitions**, not literal C++; the real implementation is a flat switch with the same stack discipline. Properties:

- **Budget accounting is `out.size`** — emitted playable blocks, so memory is bounded by the same constant; the raw index itself is bounded by file size.
- **`bounded = true` → `TapeImage.controlFlowLinearized = false`** + A2 warning; everything decoded before the bound is played in expanded order, everything after it plays in raw file order with control blocks surfaced as inert `Control` entries — exactly the parent §5.4 degradation contract (the catalog warning badge explains the seam).
- Backward jumps and loops with protection-grade counts (e.g. $24 count = 65535) degrade exactly like over-long expansions — bounded expansion, warning, linear tail to EOF.
- $2B state (§4.9) and $20 ≤ 5 ms merges (§5.1) are resolved **during emission**, after linearization ordering is final, so a block duplicated by a loop body inherits its own $2B/$20 context per iteration.

Post-linearization: `output` order drives the final `TapeImage.blocks` + `descriptors` arrays (reindexed 0…N−1); catalog indices are cursor indices — parent §6 seek semantics need no awareness that linearization happened.

## 6. Metadata and Skipped Blocks

| ID | Layout | Disposition |
|---|---|---|
| $30 | `[u32 len][text]` | candidate `TapeImage.title` (first occurrence; $32 wins — precedence below) |
| $31 | `[u8 seconds][u8 len][text]` | display-only; logged; recorded for a future Tape Manager banner (no v1 UI) |
| $32 | `[u32 len][u8 count]{ [u8 type][u8 len][text] }` | archive info — title assembly (below) |
| $33 | `[u32 len][u8 count]{ [u8 hwType][u8 hwId][u8 runs] }` | hardware info → `hardwareNote` (below); `ParseHardwareTriples()` keeps the stub's logging intent |
| $34 | `[u32 len][flags…]` (deprecated emulation directives) | skipped via length — **never honored**: emulator configuration belongs to the user, not the tape |
| $35 | `[u32 len][16-byte ASCII id][data]` | skipped via length; id string logged once per file |
| $40 | `[u8 snapType][u24 len][snapshot]` (deprecated SCREEN$) | skipped via length + warning (suggests snapshot-in-tape; out of scope) |
| $16/$17 | `[u32 len][C64 layout]` (deprecated) | skipped via length + warning (Commodore-era vestige) |
| $5A | `[u32 len = 9]["XTape!" 0x1A major minor]` glue | skipped silently; if embedded version ≠ file-header version → warning (concatenation of mixed-version files) |

**Title precedence**: $32 type `00` (full title) > first $30 text > first $32 type `01` (publisher) > file name. Assembled `title` stays a single line (embedded newlines → spaces).

**`hardwareNote`**: decoded as `count` human-readable triples joined with `", "` where the type table renders (v1: type name from the spec's hardware table — `Z80/ROM/RAM/keyboard/…`; id rendered as hex when the v1 table lacks it); `runs` suffix (`"(used)"`, `"(works)"`, `"(incompatible)"`). Example: `"RAM 128K (used), keyboard issue 3 (works)"`.

## 7. Error Handling and Malformed Input

Policy: **TZX anomalies are recoverable by default** — the loader never throws for content it can frame; it degrades, warns (A2 + ModuleLogger), and continues. Only the file-level failures below are fatal (empty `TapeImage`, error return via the P1 contract's error path).

| Condition | Class | Action |
|---|---|---|
| Bad signature / missing 0x1A / major ≠ 1 | fatal | `Load` fails — same UX as today's stub rejection |
| Trailing garbage (< minimum block framing, e.g. lone ID byte at EOF) | recoverable | stop scan, warn `"truncated block at offset N"` |
| Class-B length runs past EOF | recoverable | clamp payload to EOF, warn, decode what fits |
| Class-A field reads past EOF (e.g. $11 header truncated) | recoverable | drop block, stop scan, warn |
| `$11`/`$14` `len` × 3 bytes inconsistent with remaining file | recoverable | clamp to available, `checksumValid = false`, warn |
| `$13`/`$18` pulse count field disagrees with decoded array | recoverable | trust the array, warn |
| `$18` unknown compression byte | recoverable | catalog-only entry `playable = false`, warn (C4) |
| $24 without $25 / $25 without $24 / $27 target without $28 | recoverable | §5.4/§5.5 rules; warn |
| Jump/loop beyond EOF / backward past start | recoverable | §5.3 saturation rules; warn |
| Unknown block ID (D3) | recoverable | stop scan at that point; warn with the ID and offset |
| Empty tape (no playable blocks at all) | recoverable | `TapeImage` with zero blocks + warnings; `Tape` treats as unloaded |

Logging contract: one `ModuleLogger` line per anomaly **plus** the A2 `parseWarnings` entry; duplicates of the same anomaly type collapse to a counter (`"3 truncated blocks"`) to keep logs readable on damaged files.

## 8. Playback Integration (compatibility matrix)

| Concern | Behavior |
|---|---|
| `Tape::EnsureImageLoaded` | P1 dispatch — `.tzx` reaches `LoaderTZX` for the first time; the accepted-but-broken mis-parse through `LoaderTAP` (parent §1.2) disappears |
| Fast-load trap (`tapefastload`) | armed **only** for `timing == nullopt` blocks ($10) — identical matrix to TAP; every $11/$14/$15/$18/$19 block is signal-path only. No trap changes needed: the decline condition "non-vanilla block" already keys on the same property the profile encodes |
| Turbo/pulse signal path | `getTapeStreamBit()` plays `edgePulseTimings` / profile-parameterized `generateBitstream()` — no new engine surface; `TAPE_EAR_POLL_RESUME_THRESHOLD` watchdog interplay is unchanged (turbo loaders poll EAR in tight loops, which the existing resume machinery already serves) |
| Pause/resume & seek | `SeekToBlock` lands on any playable block incl. `Tone`/`PulseStream`; Turbo and DR blocks resume mid-block exactly like TAP data blocks (cursor semantics are representation-agnostic, parent §6) |
| TTD | seek/rewind invalidate the session for TZX tapes exactly as for TAP (parent §6.2 rule 3); no TZX-specific behavior |
| Control-plane parity | `GET /tape` JSON gains nothing TZX-specific — `format: "tzx"`, per-block `kind`/`speed`/`playable`/warnings flow through the unified catalog (parent §7.2); `controlFlowLinearized = false` surfaces as a top-level warning |

## 8a. Fast-Load Eligibility Contribution (parent §5.8)

TZX is the format that makes whole-image fast-load analysis worth having: a TAP is always `Full` and
a CSW is always `None`, but a TZX can be either, or anything in between, and the user cannot tell by
looking. **This loader implements no analysis code.** It fills four descriptor fields honestly and the
shared analyzer does the rest — that is the §5.7 seam working as designed.

| TZX block | `timing` | `rawFlag` / `checksumValid` | Resulting per-block verdict |
|---|---|---|---|
| $10 standard | `nullopt` | from payload | **eligible** when flag ∈ {$00,$FF} and XOR == 0 — a $10-only TZX scores `Full` and traps exactly like the equivalent TAP |
| $11 turbo | `Custom` | from payload | ineligible — `NonStandardTiming` |
| $14 pure data | `Custom` (`pilotPulses = 0`) | from payload | ineligible — `NonStandardTiming` |
| $12/$13/$15/$18 | `PulseStream` | n/a | ineligible — `PulseStream` |
| $19 generalized | — | n/a | ineligible — `Unplayable` (`playable = false`, §4.8) |
| $20 pause, other `Control` | — | n/a | not byte-payload → ineligible; a `Control` block **before** the first data block sets the horizon to 0 |
| linearization bounded | — | — | `controlFlowLinearized = false` → whole image scores `None` (`ControlFlowInert`) |

Two consequences worth stating because they are counter-intuitive and will otherwise be re-discovered
as bugs:

1. **A $11 block whose timings happen to equal the ROM defaults is still ineligible.** §4.2 keeps such
   a block `Custom` (source-verbatim), and §8's matrix arms the trap only for `timing == nullopt`.
   This is deliberate and correct — the trap's own decline matrix keys on the same property — but it
   means a re-encoded "turbo" TZX of an ordinary tape loads at real speed. The fast-load badge's
   reason string makes that visible rather than mysterious.
2. **The `$20` pause placement matters to the horizon.** A leading `> 5 ms` pause becomes a `Control`
   pseudo-block at index 0 (§5.1), which under the parent's prefix rule drops the horizon to zero even
   on an otherwise-vanilla tape. Control blocks are therefore **skipped, not rejected**, when computing
   the horizon — they carry no bytes for the ROM loader to consume and the trap never sees them.
   Parent §5.8's per-block rule is read with this exemption; the `LeadingPauseDoesNotKillHorizon` case
   in the parent's `TapeFastLoadEligibility_Test` pins it.

## 9. Fixtures

All fixtures live in `testdata/loaders/tzx/` — the root the tape tests already use (`insult.tap` and
the scl/sna/fdi sets live there; parent §8.0 standardises on it). **Synthesized by a test builder,
committed as golden binaries** so the bytes are reviewable and the tests don't depend on builder correctness drift:

```text
core/tests/_helpers/tapetzxbuilder.h/.cpp   // byte-exact synthesis helpers (CUT-visible)
    WriteHeader(), AddStandard(), AddTurbo(), AddTone(), AddPulseSequence(),
    AddPureData(), AddDirectRecording(), AddCsw(), AddGeneralized(), AddPause(),
    AddGroup(), AddJump(), AddLoop(), AddCall(), AddSelect(), AddSignalLevel(),
    AddMetadata()                          // $30/$31/$32/$33

testdata/loaders/tzx/
    minimal.tzx          // header + $10 header/data pair only
    turbo.tzx            // $10 pair + $11 turbo pair (non-ROM timings)
    pulses.tzx           // $12/$13/$15 sequence with $2B polarity flips
    loop3.tzx            // $24×3 loop around a pair — pins C3
    callseq.tzx          // $27 with two subroutine targets + $28s
    select.tzx           // $26 with 3 branches — pins C2
    jumpedge.tzx         // $23 0-loop, backward jump, EOF-overflow saturations
    linearize-max.tzx    // loop sized to breach TAPE_LINEARIZE_MAX_BLOCKS (builder takes target size)
    malformed-*.tzx      // one per §7 row (truncated header, overlong Class-B length, …)
    metadata.tzx         // $30/$31/$32/$33/$5A + deprecated $34/$35 skips
    standardonly.tzx     // $10 pairs only — must score fast-load Full (§8a)
    mixedspeed.tzx       // $10 pair, $10 pair, $11 turbo, $10 pair — scores Partial, horizon 4
    leadingpause.tzx     // $20 (500 ms) then $10 pairs — horizon must survive the Control block (§8a.2)
```

Real-world validation files (not committed): a handful of freely redistributable TZXs from public archives — one Speedlock-protected title (loop/jump stress), one turbo-loader title, one $18-embedding file if locatable; run through the manual checklist (§10.3) before release. Licensing keeps them out of the repo.

## 10. Test Inventory

### 10.1 Unit — `LoaderTZX_Test` (extends `core/tests/loaders/loader_tzx_test.cpp`)

| Case | Fixture | Asserts |
|---|---|---|
| HeaderRejectsBadMagic / BadEof / Major2 | synthetic | `Load` fails, message names the offset |
| ScanFramingClassA / ClassB | minimal + metadata | raw index: ids, offsets, payload spans byte-exact |
| StandardBlockDecodesAsTap | minimal | same `TapeBlock` bytes/pause as equivalent TAP fixture; `timing == nullopt`; kind Header/Data; checksum |
| TurboBlockVerbatimProfile | turbo | every §4.2 field lands in the profile; `baudEstimate`; kind; `bitsInLastByte` |
| TurboZeroBitsLastByte | synthetic | `0` → 8 defensive rule |
| ToneAndPulseSequence | pulses | `edgePulseTimings` exact vectors; kinds Tone/PulseStream; estimated seconds |
| DirectRecordingRunPacking | pulses | run-length expansion incl. initial-level and $2B polarity (C-adjacent) |
| PureDataNoPilot | turbo | `pilotPulses == 0`, syncs 0, custom bits decode |
| CswEmbeddedDelegates | synthetic | pulse array == hand-computed; pseudo-block split at ≥ 2 s pause |
| GeneralizedCatalogOnly | synthetic | `playable == false`, warning present, rawSize honest |
| PauseMergeThreshold | synthetic | 5 ms merges / 6 ms becomes Control / 0 becomes stop |
| GroupLabelPropagation | metadata | labels on contained blocks; stray $22 tolerated |
| JumpEdgeSemantics | jumpedge | offset 0 → stop; backward honored; EOF clamp |
| LoopTotalCount | loop3 | body × exactly 3 — pins C3 |
| CallSequenceInline | callseq | sub-sequences in order; execution resumes after $27 |
| SelectJumpsToFirst | select | branch 1 taken; warning lists all branches — pins C2 |
| LinearizationBound | linearize-max | `controlFlowLinearized == false`, playable prefix, single warning |
| SignalLevelAppliedToNext | pulses | next block `invertedLevel` per §4.9 |
| MetadataAssembly | metadata | title precedence, hardwareNote string, $5A version-mismatch warning, deprecated skips logged |
| Malformed* (one per §7 row) | malformed-* | recoverable: partial image + the exact warning; no throw |
| UnknownIdStopsScan | synthetic | D3 behavior |
| FastLoadFullOnStandardOnly | standardonly.tzx | plan `Full`, horizon == blockCount (§8a) |
| FastLoadPartialAtTurbo | mixedspeed.tzx | plan `Partial`, horizon 4, reason `NonStandardTiming` |
| FastLoadRomTimedTurboStillIneligible | synthetic $11 with ROM-default fields | ineligible — §8a.1 |
| LeadingPauseDoesNotKillHorizon | leadingpause.tzx | Control block skipped, not rejected — §8a.2 |

### 10.2 Integration (extend `core/tests/emulator/io/tape/` per parent §8.2)

| Case | Asserts |
|---|---|
| TzxStandardLoadsViaTrap | $10-only file fast-loads through the ROM trap like TAP |
| TzxTurboLoadsViaSignal | turbo.tzx block decodes under `LOAD ""` on 48K (signal path, no trap) |
| TzxSeekBackReplaysHeader | seek to header block replays pair correctly through mixed $10/$11 |
| TzxCatalogMatchesWebAPI | `GET /tape` rows == catalog (kind/speed/playable/warnings) |
| TzxLinearizedLoopLoads | loop3.tzx: the loader ROM sees a coherent stream across expanded iterations |
| TzxFastLoadPredictionMatchesReality | standardonly.tzx and mixedspeed.tzx under a real ROM `LOAD ""`: blocks actually trapped == `plan.stickinessHorizon` (parent §8.2 `FastLoadPlanMatchesObservedTrapping`) |

### 10.3 Manual checklist (real-world files, pre-release)

- [ ] Speedlock title: reaches main menu; warnings explain any degradation
- [ ] Turbo title: loads via signal path at sensible wall-clock speed
- [ ] Tape Manager: tree grouping from $21 labels; seek across Control/pause blocks; details pane shows turbo timings
- [ ] CLI `tape info` on each fixture: block table matches expectations

## 11. Implementation Phases (slots into parent P2)

| Phase | Scope | Exit criteria |
|---|---|---|
| T1 | Framing: header policy, Pass-1 scan, Class A/B tables, $10 decode, minimal.tzx | scan tests + StandardBlockDecodesAsTap green |
| T2 | Playable blocks: $11–$15, $2B, fixtures turbo/pulses | §10.1 block tests green; C1 settled |
| T3 | Control flow: $20–$28, linearizer, jumpedge/loop3/callseq/select/linearize-max | linearization tests green; C2/C3 settled |
| T4 | Metadata + $18/$19 handling + $5A/skips + A1/A2 amendments folded into parent | metadata/malformed suites green; zero warnings build |

T2 and T3 are independent after T1 (playable decode vs control machinery) — parallelizable.

## 12. Risks, Checkpoints and Open Questions

| # | Item | Disposition |
|---|---|---|
| R1 | Reference disagreement (Jahn mirror = 1.13 layouts) | Mitigated by D2 + checkpoints; golden fixtures are the arbiter, not mirrors |
| R2 | Linearizer complexity vs exotic files (unbalanced constructs, cross jumps) | Every malformation path has a defined recoverable behavior (§5, §7); fuzz-lite: mutated fixtures in CI |
| R3 | A future spec rev introduces a new Class-A block | D3 stops cleanly with a warning; revisiting costs one constant table row |
| R4 | zlib dependency for $18/CSW v2 | Shared with parent R7 (CSW decision) — one spike resolves both; $18-RLE works without it |
| R5 | $19 remains unplayable in v1 | Cataloged honestly (`playable=false` + warning); layout fully documented for v2 (§4.8) |
| C1 | $13 count width: u16 (spec 1.20) vs u8 (Jahn) | Implement u16; `PulseSequence` fixture settles it |
| C2 | $26 field widths (u32 offsets vs u16/u8 variant) | Implement 1.20 widths; select.tzx settles it; doubt → no-jump degradation |
| C3 | $24 loop `count` = total vs +1 | Implement total; loop3.tzx + reference emulator capture settles it |
| C4 | $18 compression-type constants (RLE vs zlib encoding) | Fix against CSW spec during T3 alongside LoaderCSW |
| C5 | Engine pilot constants vs spec prose (8063/8064) for $10 | No-op: $10 carries no counts; engine constants define the waveform, trap parity with TAP proves equivalence. Verified in tree: `tape.h` L19-20 declares `PILOT_DURATION_HEADER = 8064` / `PILOT_DURATION_DATA = 3220`, while the comment block at L102-104 says 8063/3223 — the constants win, the comment is stale, and no $10 path reads either. Worth a drive-by comment fix in P1, not a design question |
| Q1 | $31 message display in Tape Manager (toast/banner)? | Open — data captured in v1, UI deferred with parent Q3 |
| Q2 | Upgrade $19 to playable in v2? | Open — mechanical expansion sketched (§4.8); gated on a real-world file demand |

## 13. References

- TZX format specification rev 1.20 (19 Dec 2006), Tomaz Kac / Martijn van der Heide / Ramsoft — `worldofspectrum.net/TZXformat.html` (frame-walled; field tables cross-checked below), mirrored at `k1.spdns.de/Develop/Projects/zasm/Info/TZX format.html` (linked from `loader_tzx.h`)
- Claus Jahn's TZX reference (zx-modules mirror, `worldofspectrum.net/zx-modules/fileformats/tzxformat.html`) — most complete public field tables; **rev-1.13 layouts** for $11/$13/metadata noted where divergent (D2, C1, C2)
- libspectrum supported-format list and `tzx.c` reader — behavioral reference for checkpoint settlements (`fuse-emulator.sourceforge.net/libspectrum.php`)
- FreeBSD `file` magic database TZX entries — independent corroboration of the $11 20-byte header (data at offset 21)
- ZOT emulator `tzx.c` notes (github.com/antirez/ZOT) — block-semantics corroboration, skip policy
- Parent design: [design.md](design.md) — §5 unified model, §5.4 TZX mapping, §6 seek semantics, §8 test conventions, §13 phase plan
- Fast-tape loading design: `docs/inprogress/2026-08-30-fast-tape-loading/design.md` — cursor model, trap decline matrix, pause/resume machinery this loader must stay compatible with
