# Tools

Reproduction and analysis tooling for the VORON1 protection research (see the
[package README](../README.md); tools are referenced from its §9). Same convention
as the other `docs/disasm/` packages: the scripts that produced the evidence stay
with the evidence.

## Prerequisites

- A built emulator serving the WebAPI on `localhost:8090`
  (`./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`, Linux
  `bin/unreal-qt`, Windows `bin/unreal-qt.exe`). Only **one** instance of the app
  can own port 8090 — kill stale ones first (`pkill -9 unreal-qt`).
- Fixtures: `testdata/loaders/udi/VORON1.UDI` (live runs — richer capture than the
  FDI, and the one whose track-0 fails `IsTrdos`), `testdata/loaders/fdi/VORON1.FDI`
  (offline damage analysis), `data/rom/trdos504t.rom` (TR-DOS 5.04T strings).
- Python 3, stdlib only — except `voron1-damage-map.py`, which imports the
  project's `tools/diskconverter` package (resolved from the repo root
  automatically, wherever the script is run from).
- All transient outputs (`.json`, `.log`, screen `.gif`) go to `scratch/` per the
  project test-artifact rule. Scratch is git-ignored: anything worth keeping must
  be distilled into `artifacts/` or the package README.

## The deterministic boot (why these scripts work)

Earlier attempts drove the 128K menu with synthetic arrow keys and typed
`RUN"boot"` — timing-fragile (the modern `/basic/run` encoder injects tokenized
lines cleanly in either editor; the old "mangles lines" note referred to the
raw keystroke approach). Every tool here instead uses the sequence proven by
`voron1-detboot.py`:

1. create **PENTAGON**, `fastdisk` off + `trdos_traps` off (real FDC) — except
   `voron1-trap-chain.py`, which deliberately runs with traps **on**;
2. `/reset` → `/basic/state` reports `menu128k` → `POST /basic/mode {"mode":"128k"}`
   — **never `48k`**: the menu's 48K transition ends with `OUT 0x7FFD,0x30`,
   latching the paging-lock bit (D5), after which every loader bank write is
   silently ignored until the next reset (this exact trap produced the
   2026-09-22 "silent derailment" below);
3. plain `/disk/0/insert` — **not** insert+autostart: VORON1's track 0 fails
   `TrdosCatalog::IsTrdos`, so autostart refuses with
   *"Disk is not TR-DOS formatted - mounted only"*;
4. `POST /basic/run "RANDOMIZE USR 15616"` — the `$3D00` hook pages TR-DOS in
   directly (the `udi_zvezdnoe_boot_test` pattern), then
   `POST /basic/run 'RUN"boot"'` once OCR sees the `A>` / `TR-DOS` banner.

## Tool reference

### voron1-detboot.py — canonical deterministic boot + TTD capture

```
python3 tools/voron1-detboot.py [stale-instance-id ...]
```

Creates its own PENTAGON instance (ids of instances to delete first may be passed
on argv), boots via the deterministic sequence, starts TTD **after** reset
(reset stops recording), watches the load via OCR + `/analyzer/trdos/events`,
captures the screen (the endpoint returns a base64 **GIF**), stops recording,
then stamps `find-last` markers for the TR-DOS error/FDC blocks
(`0x1D29`, `0x1D1A`, `0x3F17`, `0x3F22`). Prints the new instance id — keep it for
the walk tools. Outputs: `scratch/voron1-detboot.json`, `scratch/voron1-detboot-screen.gif`.

Known first-run outcome (2026-09-22 journal): loader runs, screen clears ~f2700,
check stub at `0x5D8C–0x5E30` polls at `0x5DE0`, the `ret` at `0x5DE5` pops
`0xF5C9`, a NOP march runs to `0xFFFF`, TR-DOS silently re-initializes, blank
screen. The error entries never execute — the derailment is silent.

> **Correction (2026-09-23)** — that derailment was the harness's own 48K-mode
> paging lock (see step 2 above), not a protection check. With the 128K boot,
`voron1-detboot.py` runs the game to completion: 149 real-FDC reads, full
eight-page depack, animated intro. Re-interpret any 2026-09-22 journal
derailment evidence accordingly.

### voron1-damage-map.py — offline FDI damage census

```
python3 tools/voron1-damage-map.py [fdi-path]   # default VORON1.FDI
```

No emulator needed. Aggregates per-sector anomalies (bad data CRC / no-data /
deleted-DAM / ID-field C-H mismatch) across the whole image, grouped by sector
number R, plus a track-shape histogram — this is what established the 138-track
reformatted band (5×1024B, `R=192..196`) and the cylinder-59 CRC-bad + lying-C
trap (README §3/§7).

### voron1-revwalk2.py — backward TTD execution analysis

```
python3 tools/voron1-revwalk2.py <emu-id-from-detboot>
```

Three passes: (A) last execution per TR-DOS ROM/loader region with the search
origin re-seeked to the journal end after **every** query (`find-last` moves the
current TTD position to each hit — without the re-seek, later queries silently
search from the previous hit); (B) deepest FDC progress before a given moment;
(C) step-by-step instruction walk-back with inclusive-semantics stall detection.
The `ERR_TIME`/`REGIONS` constants are examples from the 2026-09-22 journal —
adjust per journal. This is the tool that reconstructed the NOP-march crash path.

### voron1-fwdwalk2.py — time-exact disassembly + forward walk

```
python3 tools/voron1-fwdwalk2.py <emu-id-from-detboot>
```

`/disasm` decodes linearly from the given address, so mid-instruction starts
produce garbage: this tool seeks to the exact `(frame, tin)` a region executed
first, then disassembles — guaranteeing instruction alignment — and afterwards
runs a long forward instruction walk compressing consecutive same-PC repeats
(the loop-run counters). The `TIMES` constants are journal examples.

### voron1-catch-error.py — error-path breakpoint harness (real FDC)

```
python3 tools/voron1-catch-error.py
```

Traps **off**. Execution breakpoints on `0x1D29` (error print), `0x1E36`
(read-address callers), `0x1E5A` (readonly branch); on each hit dumps registers,
a stack window (through the correct Pentagon physical-page mapping:
`0x4000-0x7FFF`→page 5, `0x8000-0xBFFF`→page 2, `0xC000-0xFFFF`→page 0), the
message string at HL straight from `trdos504t.rom` when HL < `0x4000`, and the
live FDC state. Exists because the journal showed the failure is silent — if a
future build makes the loader take the error path, this catches it with context.

### voron1-trap-chain.py — working-path service/stage trace

```
python3 tools/voron1-trap-chain.py
```

Traps **on** (fastdisk + `trdos_traps`). Breakpoints on `0x3D13` (ROM service
trap: A = service code, BC/DE/HL = params), `0x5F10`/`0x6000`/`0x6200` (loader
stages) — the §8.3 "breakpoint on `0x6000-0x60FF`" next step, answering *which*
ROM service calls VORON1's own code makes and where control lands.

### voron1-author-weak.py — offline UDIW authoring (no emulator)

```
python3 tools/voron1-author-weak.py IN.udi OUT.udi        # default mark: M1 (cyl 59/h1 R=192 data)
python3 tools/voron1-author-weak.py IMAGE.udi --list      # sector census incl. CRC / lying-C anomalies
python3 tools/voron1-author-weak.py IMAGE.udi --verify    # dump the UDIW chunk records
```

Writes the `UDIW` weak-map chunk into the UDI trailer (byte-exact `CRCHelper::crcUDI`
port; the input's stored CRC is verified before anything is touched), so the stock
emulator's `FlakySectorEmulator` sees the marked bytes as physically weak — no
re-dump of the original media needed. Outputs go to `scratch/` (test-artifact rule).
`voron1-weak-probe.asm` (same directory) is the matching live probe: through the real
Beta-128 port path it reads the marked spot (`59:1:192`) twice plus the solid
neighbour `R=193`, proving the mark is applied on load, the weak data varies per
revolution, both weak reads end CRC-error (not RNF), and the lying-C ID still
matches the track register. Run on the Pentagon 128 config only.

## TTD pitfalls baked into these scripts

- **Scrub only while stopped**: `/ttd/seek`/`find-last` return 409 while
  recording; `/reset` silently stops recording ("must never append to a recorded
  timeline"). Order: record → `/ttd/stop` → scrub.
- **`/registers` is position-aware**: after a seek, it reports the journal state
  at that moment, not the live machine. A "stuck PC" seen through the journal is
  usually just the seek position. Check `/ttd/position` before concluding.
- **Journal addresses are decimal** in JSON bodies (`0x3F17` → `16151`) — compute
  them in code, never hand-convert (three separate decimal/hex slips produced
  bogus pages during this research).
- **Pentagon pages ≠ bank numbers**: bank1 is physical RAM page 5, etc. — see
  the mapping in `voron1-catch-error.py`'s `page_for()`.
