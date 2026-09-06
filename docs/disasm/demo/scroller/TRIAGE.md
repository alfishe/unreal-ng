# Scroller by Demarche - Crash Triage

Investigation log for the boot failure of `scroller_by_demarche.trd`, condensed
to what was proven, in what order, and what fixed it. Static analysis context
lives in [REVERSING.md](REVERSING.md).

**Status: RESOLVED.** Both boot tests pass; the demo runs end to end.
Follow-up analysis of the second symptom report (covox menu shows, crash only
after SPACE) is in section 9 - same root cause, later split offset;
conclusion unchanged.

## 1. Symptom

> scroller demo still jumps to RAM2 page filled with zeroes. either content
> from disk not depacked there or wrong ram page selected

Observed after the demo's fade-out and `JP $8000`: the CPU executes zeroes in
RAM page 2 (`$8000-$BFFF`), i.e. the demo body was never depacked there.

## 2. Hypotheses

The report offered exactly two candidate mechanisms; both were kept on the
table until the trace could discriminate:

- **H1 - depack broken:** content loaded from disk, but the MegaLZ depacker
  produced nothing (or garbage) for the page-2 target (LOADTBL entry 6).
- **H2 - wrong page selected:** the raw `SCROLL12.C` never landed in page 4, so
  entry 6 depacked from an empty window; page 2 legitimately stayed zeroes.

Both produce an identical-looking crash at `$8000` - the discrimination had to
come from watching bank 3 (`$C000-$FFFF`) at load time, not from the crash site.

## 3. Instrumentation

All evidence came from the harness in
`core/tests/emulator/io/fdc/scroller_boot_test.cpp` (kept in-tree, it is the
regression test now):

- A **bus trace hook** logging every `OUT #7FFD` with the port value, `pc`, the
  resolved bank-3 RAM page, and the emulator state flags (`CF_TRDOS`).
- **Breakpoints** at the boot chain landmarks: `$6200` (START), `$6206`
  (LOADER), `$6206+RET` post-depack checkpoints, `$9B6B` (main), `$9CD6`
  (STARTDEMO), `$9D48` (pre-jump), `$8000` (entry), `$BF02` (IM2INI), `$BFBF`
  (IM2 handler, counter only).
- At each post-depack checkpoint: first bytes of the `$C000` source window per
  physical page and of the `$8000` destination window.
- Two test paths: direct 48K boot (`BootScrollerDemoTRD`) and the user's
  scenario - boot into the 128K menu, press SPACE, select 48 BASIC
  (`BootScrollerDemoTRD_Via128KMenu`). The failure reproduced only on the
  second path - the first big clue that the ROM environment mattered.

Log format: `[SEQ n][BUS] OUT #7FFD value=.. pc=.. -> p7FFD=.. bank3=page..`
and `[SEQ n][BP] <event>`.

## 4. Evidence Chain (pre-fix run, Via128KMenu path)

1. **Line 80 executes - and works.** `OUT VAL"32765",VAL"20"` reaches the port:
   `[SEQ 286][BUS] OUT #7FFD value=14 ... bank3=page4`. Page 4 is selected at
   `$C000`. H1's premise (statement never applied) is dead; the old "POKE bug"
   theory is dead with it.
2. **One statement later the selection is silently reverted.**
   `[SEQ 287][BUS] OUT #7FFD value=00 pc=5B10` - the 128K editor's
   statement-boundary trampoline (YOUNGER, `$5B14 -> CALL $5B00`) ran the SWAP
   hook: `LD A,($5B5C) ; XOR $10 ; OUT (C),A`. `BANK_M` still held the boot-time
   `0x10`; `0x10 XOR 0x10 = 0x00` -> bank 3 reverts to **page 0**, 128K ROM.
3. **TR-DOS loads into whatever is mapped.** Entering `$3D03` swaps in the
   TR-DOS ROM (CF_TRDOS trap), the DOS does its own `OUT` with the ROM bit
   (`$10`, page 0) and `LOAD *"SCROLL12" CODE` writes the 48 raw sectors to
   `$C000` -> **page 0**, not page 4.
4. **Entry 6 depacks the wrong window.** `USR 25094` OUTs `$14` correctly and
   reads the MegaLZ stream from `$C000` - now page 4, which is pristine zeroes.
   Ground truth: page 4 at that moment must read `CD FF 93 85 ...` (the packed
   header; see REVERSING.md section 1). The depacker consumes a zero stream and
   produces (almost) nothing at `$8000`.
5. **`JP $8000` executes zeroes.** Page 2 was never written except by earlier
   packed-block landings; the demo body is absent. Crash matches the report.

Verdict: **H2 - wrong RAM page selected**, with the depacker fully functional
(proven separately: `trd_mlz.py` reproduces every page bit-exactly from the
real depacker bytes, and the 48K-path test - which never runs the editor
trampoline - booted the demo fine).

## 5. Root Cause

A three-party protocol mismatch between the demo, the editor, and the ROM pair:

- The demo's `OUT 32765,20` sets the **port latch** (hardware state) but cannot
  set `BANK_M` (`$5B5C`) - that is a RAM variable owned by the 128K editor.
- The SWAP hook's contract is "toggle the ROM bit of whatever `BANK_M` says,
  re-OUT it". It is *supposed* to preserve the page/screen bits it finds there.
- For that contract to hold, **someone must keep `BANK_M` equal to the port**.
  The matched Pentagon firmware does this in the interpreter when an `OUT`
  command executes. The stock Amstrad halves bundled in the 64K `pentagon.rom`
  (an Amstrad 128K editor + Amstrad 48K ROM, neither with Pentagon extensions)
  **never write `BANK_M` from BASIC**.

So on the stock pair: port = `$14`, `BANK_M` = stale `$10`, hook re-derives
`$00`, and the demo's page-4 request evaporates between two statements. (An
earlier revision of this document claimed a "matched Pentagon firmware" would
have kept `BANK_M` in sync - retracted: no ROM in `data/rom/` exhibits that,
and `pentagon.rom` is a plain Amstrad pair. The demo instead survives on
boot paths where the hooks are never installed.)

## 6. The Demo Is Not Buggy (but its boot path matters)

This supersedes the earlier analysis (`docs/disasm/scroller_by_demarche_analysis.md`)
which concluded "POKE 32765 bug - demo is buggy":

- The statement is `OUT VAL"32765",VAL"20"`, an `OUT` command, **not** `POKE`.
  The trace shows the port write landing (SEQ 286). The earlier reading was a
  tokenization misread.
- The full chain (disk, depacker, LOADTBL, entry chain) is bit-correct:
  `trd_mlz.py` reproduces every page from the real depacker bytes, and the
  demo boots and runs end to end whenever the editor hooks are not installed
  (direct TR-DOS / 48K BASIC boot - the way the demo was historically run,
  and how `BootScrollerDemoTRD` exercises it).
- What the demo does NOT tolerate is being RUN from an editor environment on
  stock ROMs: `OUT 32765` sets the port latch but not `BANK_M`, so the SWAP
  hook reverts the selection. The 128K-safe idiom (`POKE 23388`) would have
  been immune. On any stock-ROM machine this is exactly what happens - the
  observed failure is authentic hardware behavior, which the emulator now
  reproduces faithfully and pins with a regression test.

## 7. Resolution: No Emulator Change

The failure is **authentic behavior of a stock-ROM machine**, not an emulator
defect, so the correct resolution is documentation plus regression tests - not
compensation in the core:

- Verified: `data/rom/pentagon.rom` (and `pentagon128k.rom`) are a plain
  Amstrad pair (Q2 == `128.rom`, Q3 == `48.rom`/`sos.rom`); **no ROM in the tree
  syncs `BANK_M` on a BASIC OUT** - the earlier "matched Pentagon firmware does
  this" premise was unverified and is retracted.
- Verified: the reference emulator (UnrealSpeccy, `other/unrealspeccy`) contains
  no `BANK_M`/`$5B5C`/`$1E7D` handling of any kind.
- The demo's own line 80 is the unsafe idiom (`OUT 32765` where 128K canon says
  `POKE 23388` = set `BANK_M`, let the editor OUT it). Any stock-ROM machine
  booted via the editor menu fails identically.

**History (kept for honesty):** mid-investigation a `BANK_M` mirror was
implemented in `PortDecoder_Pentagon128::Port_7FFD_Out` (gated on the OUT
executor pc `$1E7D` + a SWAP-hook signature at `$5B00`). It made the demo boot
via the menu path, but it emulates firmware behavior that nothing we ship
exhibits - a per-demo compatibility hack inside the hardware model layer. It
was **reverted in full** after review. Lessons that survive the revert:
`DecodePortOut` receives the M1 pc (instruction start, `$1E7D`), not the
post-increment `cpu->pc` (`$1E7F`) that trace hooks log; and a wide mirror
(any ROM-origin OUT) demonstrably corrupts the editor's own menu flow, whose
internal scratch OUTs (`$1C7B`/`$1F3E`) deliberately bypass `BANK_M`.

### Unrelated test fix kept

`KeyboardInjection_Integration_test::BootEmulator` now forces
`reset_rom = RM_SOS` + `Reset()` (same as `EmulatorTestHelper::CreateStandardEmulator`),
because `EmulatorManager` loads model-specific configs (`data/configs/pentagon128k/unreal.ini`,
`RESET=128`) while those tests assume a direct 48K BASIC boot. Test determinism
only - no emulator behavior involved.

## 8. Verification

With the emulator unmodified (no BANK_M mirror):

- `Scroller_Boot_Test.BootScrollerDemoTRD` (48K path, editor hooks never
  installed) - **PASS**: demo boots, `$8000` depacked (`CD 93 85 FB 76 3E`),
  `$9B6B` reached, IM2 live (I=$BE, handler `$BFBF` firing), page 4 filled with
  the SCROLL12 stream, page 2 15585/16384 nonzero, ground-truth bytes match
  `trd_mlz.py` at `$8000/$9CD6/$9D48/$BF02`.
- `Scroller_Boot_Test.BootScrollerDemoTRD_Via128KMenu` (user path) - **PASS by
  asserting the authentic failure**: all 6 depack calls run, page 4 stays
  empty, SCROLL12's packed header (`CD FF 93 85`) lands in page 0, page 2 is
  all zeroes (final depack read an empty page), IM2 never initialized. Note:
  a pc-crossing counter on `$BFBF` is NOT a liveness marker here - the
  post-crash NOP slide loops through it (141 crossings observed); the test
  uses `im == 2 && I == $BE` and page-2 content instead.
- KeyboardInjection suite - 9/9 PASS.
- Full `core-tests` - 1055/1057 at the time of the last full run; the two
  failures (`PortDecoder_Profi_Test.IsPort_7FFD`,
  `PortDecoder_Spectrum128_Test.IsPort_7FFD`) pre-exist at pristine HEAD and
  are unrelated.

## 9. Follow-up: the mid-load split, the "menu then slide" variant, and a retraction

Second user report, after the resolution above:

> demo loads only to covox selection screen. press space and it jumps to nopes
> and resets because of address overflow and wrap

This is the same root cause one race-length later. It also surfaced two new
facts and one of our own errors, all recorded here.

### 9.1 Where in the LOAD the flip lands (and why it varies per run)

TR-DOS 5.03 does not keep the whole LOAD interrupt-disabled. Byte-verified in
`data/rom/trdos503.rom`:

```asm
3F16  F3          DI            ; guards command issue + sector transfer only
3F1B  D3 5F       OUT ($5F),A   ; WD1793 read-sector command
3F2A  CC BA 3F    CALL Z,$3FBA  ; sector transfer loop (still DI'd)
3F32  FB          EI            ; interrupts back ON before...
3F33  DB 1F       IN A,($1F)    ; ...the status poll (runs EI'd)
```

Every sector leaves a short EI'd window, and the motor/seek helpers have
their own. A 50 Hz interrupt accepted in one of them runs the `$5B00` hook
and flips bank3 mid-file, so the raw `SCROLL12.C` stream splits: sectors
`1..k` land in page 4, sectors `k+1..48` in page 0. Which `k` you get depends
on interrupt phase vs. disk timing - repeated attempts fail differently
(observed in realtime repro runs: 171 B and 857 B reached page 4).

Our core gates interrupts correctly (`z80.int_pending && z80.iff1` with the
one-instruction EI delay, z80.cpp:662 / emulator.cpp:1898), so the mid-load
flip is authentic ROM behavior, not a forced interrupt.

### 9.2 What a split at sector k does (measured, `trd_mlz_truncation.py`)

The demo body is decoded strictly bottom-up from the stream, and the IM2 top
of page 2 lives in the LAST sectors. Measured valid-prefix boundaries:

| k (sectors in page 4) | page-2 landmarks valid | observable symptom |
|---|---|---|
| 0 | none | body zeroed; no menu; NOP slide -> wrap -> reset (harness instrumented runs) |
| 3 | INIT1 only | no covox screen (realtime repro run 4) |
| 24 | INIT1, menu `$9B6B`, STARTDEMO `$9CD6` - but IM2INI `$BF02` / handler `$BFBF` corrupt | **covox menu shows, SPACE works, init chain runs up into the corrupt top of page 2 -> NOP slide -> wrap past `$FFFF` -> ROM reset** - the user's report verbatim |
| 47 | all but IM2 handler | crash one interrupt later |
| 48 | all (clean terminator) | demo runs (48K-path test) |

### 9.3 Retraction: the "CPU vs. memory dual view" theory was wrong

A realtime probe once showed execution-breakpoint hits at `$BFBF` while
`DirectReadFromZ80Memory($BFBF)` returned `00`, and we briefly reported an
emulator-internal desync. False alarm: execution breakpoints fire on PC
match at fetch (z80.cpp:226, `HandlePCChange`) regardless of the byte
fetched - the probe was catching the post-crash NOP slide passing through
`$BFBF`. The CPU was executing exactly the zeros the reader reported
(per-frame sampler: `opcode=00`, `prev_pc = pc - 2`, sequential sweep).

### 9.4 Conclusions unchanged

- Emulator: no defect found at any layer examined - interrupt gating, hook
  bytes, TR-DOS EI windows, depacker (the demo's own code).
- Demo: line 80's `OUT 32765` is 128K-editor-unsafe. `POKE 23388` (set
  `BANK_M`, let the editor do the OUT), or entering TR-DOS without a prior
  128K-editor session (hooks never armed - the 48K/direct path), makes the
  boot deterministic.

## 10. References

| Item | Path |
|------|------|
| Tests / instrumentation (both outcomes) | `core/tests/emulator/io/fdc/scroller_boot_test.cpp` |
| Keyboard test boot determinism | `core/tests/debugger/keyboard_integration_test.cpp` (`BootEmulator`) |
| M1-pc port dispatch | `core/src/emulator/cpu/z80.cpp` (`Z80::out` -> `DecodePortOut(port, val, m1_pc)`) |
| Model configs staged for tests | `data/configs/pentagon128k/unreal.ini` (`RESET=128`) |
| ROM pair under discussion | `data/rom/pentagon.rom` (verified plain Amstrad pair) |
| Reference emulator (no BANK_M handling) | `other/unrealspeccy` |
| Ground-truth tool | `trd_mlz.py` (this folder) |
| Truncation matrix tool (split offset vs. surviving landmarks) | `trd_mlz_truncation.py` (this folder) |
