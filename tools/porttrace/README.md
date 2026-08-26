# Port Trace Tools

Offline and remote-control tooling for the **Port Diagnostic Recorder (PDR)** — the
structured port I/O event recorder built into the Unreal-NG core for diagnosing
port-decode and peripheral-emulation bugs (TurboSound chip-select drops, Beta128
TR-DOS gating, mask collisions, over-strict decoding, ghost reads).

| Tool | Purpose |
|---|---|
| [`porttrace_capture.py`](porttrace_capture.py) | One-command capture session over the WebAPI: enable → filter → capture → save → convert |
| [`porttrace_convert.py`](porttrace_convert.py) | Standalone converter/analyzer for saved traces (json/csv/bin → json/csv/markdown/text, summary, strictness analysis) |

Both are Python 3.8+, standard library only — no pip installs.

Design documents: [`docs/inprogress/2026-08-24-diagnostic-observability/`](../../docs/inprogress/2026-08-24-diagnostic-observability/)

---

## 1. Prerequisites

### The `porttrace` runtime feature

The recorder is gated **at runtime** by the FeatureManager feature `porttrace`
(alias `pt`, persisted in `features.ini`, **off by default**). There is no
compile-time flag — the code is always built in; while the feature is off the
recorder is not even instantiated and the hot-path cost is a single cached
boolean test.

Enable it any of these ways:

```text
CLI (telnet):        feature porttrace on
features.ini:        [porttrace]  state = on
WebAPI:              PUT /api/v1/emulator/{id}/feature/porttrace   {"enabled": true}
Python bindings:     emu.feature_set("porttrace", True)
porttrace_capture:   (enables it automatically unless --no-enable)
```

### A running emulator with the WebAPI (for `porttrace_capture.py` only)

`porttrace_capture.py` talks to the WebAPI on `http://localhost:8090` (the
automation binary, or unreal-qt with automation enabled). It must run on the
**same machine** as the emulator: the trace is saved server-side and then read
locally for conversion.

`porttrace_convert.py` needs nothing running — it works on saved trace files.

---

## 2. Quick start

```bash
# Capture everything for 5 seconds; produce trace.json, trace.csv, trace.txt
tools/porttrace/porttrace_capture.py --duration 5 -o /tmp/trace --to json,csv,text --summary

# Interactive: capture AY/TurboSound writes with a live counter, ANY key stops
tools/porttrace/porttrace_capture.py \
    --include port=FFFD,direction=out \
    --include port=BFFD,direction=out \
    --wait-key -o /tmp/ay-session --to json,markdown

# Boot debugging: keep the START of the run in a big buffer
tools/porttrace/porttrace_capture.py --capacity 262144 --overflow stop \
    --duration 10 -o /tmp/boot --to json

# Analyze a saved trace offline
tools/porttrace/porttrace_convert.py /tmp/trace.json --summary
tools/porttrace/porttrace_convert.py /tmp/trace.json --filter-unmapped --to text
tools/porttrace/porttrace_convert.py /tmp/trace.json --analyze-strictness
```

---

## 3. `porttrace_capture.py` — capture driver

Runs a complete session against a live emulator:

1. resolves the target instance (`--emulator ID-or-prefix`, default: the running one),
2. enables the `porttrace` feature (skip with `--no-enable`),
3. applies buffer configuration and filter,
4. captures for `--duration SECONDS`, or interactively until a keypress (`--wait-key`),
5. saves the canonical **JSON** trace server-side (it embeds the decode-rule
   table, making the file self-describing),
6. converts locally to every format in `--to`.

### Options

| Option | Meaning |
|---|---|
| `--url URL` | WebAPI base URL (default `http://localhost:8090`) |
| `--emulator ID` | Emulator id or unique prefix (default: the single/running instance) |
| `--duration N` | Capture length in seconds (float) |
| `--wait-key` | Interactive mode: capture until **any key** is pressed, with a live event counter updating twice a second (alternative to `--duration`; falls back to Enter-terminated when stdin is not a terminal) |
| `--preset NAME` | Filter preset: `all` `ay-only` `fdc-only` `no-fdc` `outs-only` `ins-only` `unmapped` |
| `--include RULE` | Compound include rule (repeatable — rules OR together) |
| `--exclude RULE` | Compound exclude rule (repeatable — exclude always wins) |
| `--capacity N` | Ring buffer capacity in events. Default: **auto-sized** — `duration × 250k events/s × 1.5` headroom for `--duration`, 4M for `--wait-key`, clamped to 1M–8M (24 bytes/event: 1M = 24 MB, 8M = 192 MB) |
| `--overflow ring\|stop` | `ring` = evict oldest (default); `stop` = auto-stop when full, keeping the **start** of the run |
| `-o BASE` | Output base path; the extension is added per format (default `porttrace`) |
| `--to LIST` | Comma-separated formats: `json,csv,text,markdown,bin` (default `json`) |
| `--summary` | Print the trace summary after conversion |
| `--no-enable` | Do not auto-enable the feature (fail if it is off) |

### Interactive mode (`--wait-key`)

The workflow for reproducing a bug by hand: start the capture, play with the
emulator until the glitch happens, tap a key, read the trace.

```text
$ tools/porttrace/porttrace_capture.py --wait-key --preset ay-only -o /tmp/glitch --to json,text
Feature 'porttrace' enabled
Filter: include: {port=0xFFFD}; {port=0xBFFD}
Capturing... 1994 events  - press any key to stop
Captured 2254 events (produced 2254, evicted 0, filtered out 6410)
Saved: /tmp/glitch.json
Saved: /tmp/glitch.txt
```

- The counter line refreshes twice a second with the live event count, plus
  evicted/filtered counts when non-zero.
- **Any single key** stops the capture — no Enter needed (raw terminal mode on
  macOS/Linux, `msvcrt` on Windows). The terminal is restored even on Ctrl-C.
- With `--overflow stop`, the ticker shows `[BUFFER FULL - auto-stopped]` the
  moment the buffer fills, so you know further waiting is pointless.
- When stdin is not a terminal (piped/scripted runs), the mode degrades to a
  plain Enter-terminated wait so scripts keep working.

### Filter rule syntax

A rule is a comma-separated list of conditions; **all conditions in one rule
must match (AND)**, separate `--include` flags **OR** together, and any matching
`--exclude` rule rejects the event regardless of includes.

```text
port=FFFD              decoded port (hex)
raw=FEFD               raw bus address (hex)
device=AY_FFFD         device attribution (see table in §6)
direction=in|out
pc=3D00-3FFF           PC range (hex, inclusive)
unmapped               only events that decoded to nothing
```

Examples:

```bash
--include port=FFFD,direction=out            # AY register-select/chip-select writes only
--include unmapped,pc=0000-3FFF              # unmapped accesses from ROM code
--exclude device=WD1793_Data                 # drop FDC data-register noise
```

### Capacity planning

Unfiltered, a busy program produces roughly **50,000–200,000 port events per
second** (~3,500/frame at 50 fps, more with heavy AY/FDC activity). At 24
bytes/event:

| Capacity | Memory | Unfiltered window (~150k ev/s) |
|---|---|---|
| 1M events (core default) | 24 MB | ~7 s |
| 4M events (`--wait-key` auto-default) | 96 MB | ~28 s |
| 8M events (tool auto-size cap) | 192 MB | ~56 s |

`porttrace_capture.py` auto-sizes the buffer (see `--capacity` above) and the
`--wait-key` ticker shows evictions live, so a wrapped ring is visible
immediately. For longer sessions the effective answers are, in order:
**filter at capture time** (cheapest — a preset or a couple of `--include`
rules typically cuts the rate by 10–100×), raise `--capacity` explicitly, or
use `--overflow stop` when the interesting part is the *beginning* of the run.

---

## 4. `porttrace_convert.py` — converter / analyzer

Works entirely offline on files produced by any save path (CLI `port-trace save`,
Python/Lua `porttrace_save`, WebAPI save endpoint, or `porttrace_capture.py`).

```bash
porttrace_convert.py TRACE [--to json|csv|markdown|text] [-o OUT]
porttrace_convert.py TRACE --summary
porttrace_convert.py TRACE --analyze-strictness
porttrace_convert.py --selftest
```

The input format is auto-detected: `PTRC` magic → binary, `.csv` extension →
CSV, otherwise JSON.

### Filters (applied before any output)

| Option | Meaning |
|---|---|
| `--filter-port HEX` | Keep events whose decoded **or** raw port equals HEX |
| `--filter-device NAME` | Keep one device (case-insensitive) |
| `--filter-direction in\|out` | Keep one direction |
| `--filter-pc LO-HI` | Keep events with PC in the hex range |
| `--filter-unmapped` | Keep only unmapped events (decoded=0x0000, not gated) |

### `--summary`

Direction and device histograms, decoded-port ranking, unmapped raw addresses,
Beta128-gated count, and the decode-rule distribution (rule names resolved from
the table embedded in the trace).

### `--analyze-strictness`

The use-case-Category-2 detector ("software works on real hardware but not in
the emulator"). For every unmapped event it tests each decode rule from the
embedded table and reports **single-bit near-misses** — raw addresses that would
have decoded if exactly one masked address line were ignored:

```text
  rawPort=0x7FF9 (x4):
    near-miss for 0x7FFD (rule 2, mask 0x8006): requires A2=1, bus had A2=0
      -> would decode if A2 were dropped from the mask
```

That is the signature of an emulator mask that checks more address lines than
the real partially-decoded hardware does. Events with no single-bit candidate
are listed as genuinely unmapped.

### `--selftest`

Round-trips a synthetic trace through the binary, JSON, and CSV writers/readers
and asserts the strictness analysis. Pair it in CI with the C++ side
(`PortTrace_Test.ExportAllFormats`), which produces real artifacts these readers
are verified against.

---

## 5. Trace file formats

All three formats carry the same events; JSON and binary also embed the session
metadata and the model's **decode-rule table** (`{mask, match, port}` per rule),
so `decodeRuleIndex` values resolve offline without hardcoding per-model masks.

### JSON (`unreal-ng-porttrace-v1`)

```json
{
  "format": "unreal-ng-porttrace-v1",
  "session": { "emulator_id": "...", "model": "Pentagon", "tstates_per_frame": 71680,
               "filter": "All ports", "capacity": 1048576,
               "total_captured": 1984, "total_evicted": 0, "total_filtered": 0 },
  "decode_rules": [ {"index": 0, "mask": 49154, "match": 49152, "port": 65533}, ... ],
  "device_map": { "4": "AY_FFFD", ... },
  "events": [ {"ts": 73830874, "frame": 1030, "raw": 65533, "dec": 65533,
               "rule": 0, "val": 7, "pc": 14472, "dev": 4, "flags": 7}, ... ]
}
```

### CSV

`#`-prefixed metadata comments (model, tStates/frame, filter, decode rules)
followed by one row per event with hex ports/values and unpacked flag columns.

### Binary (`PTRC` v1, little-endian)

```text
[Header: 32 bytes]
  0   magic     "PTRC"
  4   version   u16 = 1
  6   count     u32
  10  capacity  u32
  14  tpf       u32   (tStatesPerFrame)
  18  ruleCount u16
  20  reserved  12 bytes
[Decode rules: ruleCount x 6 bytes]   u16 mask, u16 match, u16 port
[Events: count x 24 bytes]            Python struct "<QIHHHBBBBxx":
  u64 timestamp, u32 frame, u16 rawPort, u16 decodedPort, u16 pc,
  u8 value, u8 decodeRuleIndex, u8 deviceId, u8 flags, 2 pad
```

The C++ side `static_assert`s this exact layout
(`core/src/emulator/ports/portdiagrecorder.cpp`), so the formats cannot drift
silently. Binary carries no model/emulator-id strings — use JSON when you need
them.

---

## 6. Event reference

### Per-event fields

| Field | Meaning |
|---|---|
| `timestamp` | Absolute T-state: `frame * tStatesPerFrame + t-in-frame` |
| `frame` | Emulator frame counter |
| `raw_port` | Full 16-bit address the Z80 put on the bus |
| `decoded_port` | Canonical port after model-specific decoding; `0x0000` = unmapped |
| `decode_rule` | Which rule resolved it: table index, `0xFE` = BDI `#1F/#3F/#5F/#7F` fallback, `0xFD` = if-chain decoder model (no table), `0xFF` = no match |
| `value` | Data byte read (IN) or written (OUT) |
| `pc` | PC of the IN/OUT instruction |
| `device` | Device attribution (below) |
| `flags` | Disposition bits (below) |

### Flags (text/markdown letter → meaning)

| Bit | Letter | Meaning |
|---|---|---|
| 0 | (Dir) | 0 = IN, 1 = OUT |
| 1 | `D` | decoded — a hardware device actually responded |
| 2 | `H` | hadHandler — a `PortDevice` is registered on the decoded port |
| 3 | `G` | beta128Gated — FDC port dropped because `CF_TRDOS` was clear |
| 4 | `I` | handledInline — handled by the decoder switch, not a peripheral handler |
| 5 | `T` | cfTrdos — `CF_TRDOS` was set at event time (judge `G` with this) |
| 6 | `L` | legacyPath — captured on the legacy base-class dispatch path (Ghost-Byte visibility) |

### Devices

`None` `ULA_FE` `Memory_7FFD` `Memory_1FFD` `Memory_DFFD` `AY_FFFD` `AY_BFFD`
`WD1793_Status` `WD1793_Track` `WD1793_Sector` `WD1793_Data` `Beta128_System`
`Covox` `Custom`

(`#1F` is attributed to `WD1793_Status`; Kempston shares that port.)

---

## 7. Diagnostic recipes

**"Why is TurboSound AY1 silent?"**
```bash
porttrace_capture.py --include port=FFFD,direction=out --include port=BFFD,direction=out \
    --wait-key -o /tmp/ts --to json,text
```
In the text output look for the chip-select `(FFFD, FE)` before the `(BFFD, data)`
writes. Missing select → the player never sent it. Present with `H` flag but the
wrong chip sounds → the bug is inside TurboSound routing, not the port layer.

**"BetaDisk stopped working"**
```bash
porttrace_capture.py --duration 5 -o /tmp/fdc --to json
porttrace_convert.py /tmp/fdc.json --filter-device WD1793_Status --to text
```
All events showing `G` without `T` → the TR-DOS gate is (correctly) blocking
because `CF_TRDOS` never got set. Events with `D` but no `H` → the FDC never
registered its port handlers.

**"Program works on real hardware, not in the emulator"**
```bash
porttrace_capture.py --duration 10 -o /tmp/full --to json
porttrace_convert.py /tmp/full.json --analyze-strictness
```
Single-bit near-misses point at the exact mask bit the emulator checks but the
real partially-decoded hardware ignores.

**"What is hammering the ports every frame?"**
```bash
porttrace_convert.py /tmp/full.json --summary
```

---

## 8. Other ways to reach the same recorder

Every transport drives the same core (`PortDiagnosticRecorder`); files saved by
any of them are interchangeable inputs for `porttrace_convert.py`:

| Transport | Entry point |
|---|---|
| CLI (telnet) | `port-trace start/stop/dump/save/include/exclude/preset/config/status` |
| WebAPI | `/api/v1/emulator/{id}/profiler/porttrace/...` (spec: `/api/v1/openapi.json`) |
| Python bindings | `emu.porttrace_start()`, `emu.porttrace_include(port=0xFFFD, direction="out")`, `emu.porttrace_events()`, `emu.porttrace_save(path, "json")` |
| Lua bindings | `porttrace_start()`, `porttrace_include({port=0xFFFD, direction="out"})`, `porttrace_save(path, "json")` |
| Debugger UI | frame-scoped `PortActivitySummary` counters (in/out/unmapped/gated per frame) |
