# Scroller by Demarche - Reverse Engineering & Crash Triage

## Diagnosis Notice

**Root cause found - it is demo/ROM-environment interaction, NOT an emulator bug.
The emulator was deliberately left unmodified.**

- **Problem:** Demo jumps to the entry point in RAM page 2 (`$8000`) that is still
  filled with zeroes - nothing was depacked there.
- **User-visible choice:** either "content from disk not depacked there" or
  "wrong RAM page selected" - it was the **wrong page selected** variant.
- **Why:** The demo's BASIC line 80 selects page 4 with `OUT VAL"32765",VAL"20"`
  before `LOAD *"SCROLL12" CODE`. When booted through the 128K editor menu, the
  editor's SWAP hook (`$5B00`) re-derives port #7FFD from the `BANK_M` shadow
  (`$5B5C`) at every statement boundary - and neither stock ROM half in the 64K
  `pentagon.rom` (a plain Amstrad pair, verified byte-identical to `128.rom` +
  `48.rom`) ever updates `BANK_M` on a BASIC OUT. The stale shadow reverts the
  page selection one statement later: SCROLL12 loads into page 0, the final
  depack reads an empty page 4 and overwrites page 2 with zeroes.
- **Why the demo nonetheless works on real machines:** booted the usual way
  (TR-DOS direct into 48K BASIC), the editor hooks are never installed and the
  demo runs fine - reproduced by `BootScrollerDemoTRD`. The 128K-safe idiom for
  the loader would have been `POKE VAL"23388"` (set `BANK_M`, let the editor do
  the OUT) instead of `OUT 32765`.
- **Regression tests:** `BootScrollerDemoTRD` asserts the working path;
  `BootScrollerDemoTRD_Via128KMenu` asserts the authentic stock-ROM failure
  signature (page 4 empty, SCROLL12 in page 0, page 2 zeroes, IM2 never set up).
- **Details:** See [TRIAGE.md](TRIAGE.md).

## Overview

- **Demo:** Scroller by Demarche (AAA Party)
- **Platform:** Pentagon 128K/512K, standard 128K pages 0-7
- **Sound:** Covox on port #FB
- **Disk:** `testdata/sound/covox/scroller_by_demarche.trd` (TR-DOS, 80 tracks x 16 sectors x 256 bytes)
- **Sources:** `testdata/sound/covox/source/` (original build tree)

## Files

| File | Contents |
|------|----------|
| `README.md` | This file - overview and index |
| `REVERSING.md` | Full static reverse engineering: disk image, BASIC loader, LOADER/LOADTBL/DEPACK, MegaLZ V4 depacker, demo runtime, 128K ROM environment |
| `SOURCE.md` | Analysis of the author's original build tree (`testdata/sound/covox/source/`): build pipeline, decoded `scroller.bas`, loader/menu/STARTDEMO/IM2 source, and the source-level confirmation of the root cause |
| `TRIAGE.md` | The crash investigation: symptom, hypotheses, bus-trace evidence chain, root cause, fix iterations, verification, mid-load split analysis |
| `trd_mlz.py` | Ground-truth tool: extracts the TRD catalog and runs the REAL MegaLZ depacker bytes from SCROLL00.C in a mini Z80 interpreter, producing the exact expected content of every demo RAM page |
| `trd_mlz_truncation.py` | Truncation matrix: replays entry 6 with only the first k sectors of SCROLL12 real - maps each split offset to surviving page-2 landmarks and the observable symptom |

## Boot Flow (short form)

```
TR-DOS BASIC loader (SCROLLER.B)
  LOAD "SCROLL00" CODE -> $6200        (LOADER + LOADTBL + MegaLZ depacker)
  LOAD "SCROLLnn" CODE -> $8000/$C000  (6 MegaLZ blocks)
    after each LOAD: RANDOMIZE USR 25094 ($6206)
      -> OUT (#7FFD),page ; CALL DEPACK ($6244)
  line 80: OUT VAL"32765",VAL"20"      (page 4 at $C000 for SCROLL12)
  line 90: USR 25094 (depack #6: $C000 -> $8000, page 2)
           USR 25088 ($6200: LD SP,$6200 ; JP $9B6B)
$9B6B main -> SPACE accepted $9CD6 -> fade $9D48 -> JP $8000
$8000: CALL INIT1 ($8593) ; EI ; HALT  (IM2 takes over)
IM2: I=$BE, vector table $BE00-$BEFF ($BF fill) -> handler $BFBF
```

The critical environmental detail (the entire crash story) is that every
`RANDOMIZE USR` statement boundary passes through the 128K editor's SWAP
trampoline at `$5B14 -> $5B00`, which XORs `BANK_M` with `$10` and re-OUTs it.
See [TRIAGE.md](TRIAGE.md) for why this silently reverted the page-4 selection
on the stock ROM pair.

## Supersedes

This folder supersedes `docs/disasm/scroller_by_demarche_analysis.md`, whose
"POKE 32765 bug - demo is buggy" conclusion was disproven by the bus trace:
line 80 is an `OUT` command (executed by the ROM OUT-command executor at
`$1E7D`), the port write **was** applied, and the demo works on real hardware
with a matched Pentagon ROM. The failure was emulator/ROM-firmware divergence,
not an authoring bug.

## Related Code

| Item | Path |
|------|------|
| Boot tests (both outcomes, with the bus-trace instrumentation) | `core/tests/emulator/io/fdc/scroller_boot_test.cpp` |
| Port write dispatch (m1_pc semantics) | `core/src/emulator/cpu/z80.cpp` (`Z80::out` -> `DecodePortOut(port, val, m1_pc)`) |
| Pentagon ROM | `data/rom/pentagon.rom` (64K, 4 x 16K quarters; verified a plain Amstrad pair) |
