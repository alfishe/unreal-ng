# Scroller by Demarche - Author's Source Code Analysis

Analysis of the original build tree at
`testdata/sound/covox/source/` (`scroller_by_demarche_src.zip` ->
`extracted_src/Scroller_AAA_party/`). The sources confirm the binary-level
findings in [REVERSING.md](REVERSING.md) and [TRIAGE.md](TRIAGE.md)
byte-for-byte, add the author's intent (his own comments about the load plan),
and pinpoint the exact line that makes the demo fragile on 128K-editor
machines. Russian comments are translated; originals kept in parentheses.

## 1. Archive Contents and Build Pipeline

| Path (under `Scroller_AAA_party/`) | Role |
|------|------|
| `scroller.asm` | master source, 3671 lines (SJAsmPlus dialect, cp1251 comments) |
| `scroller.bas` | the tokenized BASIC loader, **byte-identical to the disk's `SCROLLER.B`** |
| `linker.asm` | second-pass script that builds `scroller.trd` |
| `scroller.bat` | build driver |
| `depacker.asm` | MegaLZ V4 depacker source - **empty (0 bytes) in this archive** |
| `sjasmplus.exe`, `MegaLZ.exe` | third-party tools (readme.txt credits them) |
| `gfx/`, `music/`, `dots_figure/`, `line_traektory/`, `text_convertor/` | assets + Delphi (Pascal) asset converters |
| `effect_plan2.txt` | author's per-frame cycle budget notes (frame = 17920T) |

Build chain (`scroller.bat`):

```
sjasmplus scroller.asm          ; -> bin/SCROLL00.BIN (raw loader, $6200)
                                ;    bin/SCROLL10/11/12/13/15/17.BIN (raw parts)
megalz.exe bin/scrollNN.bin bin/scrollNN.mlz    ; x6, MegaLZ V4 pack
sjasmplus linker.asm            ; EMPTYTRD + SAVETRD -> scroller.trd
```

`linker.asm` maps sources to disk 1:1 (`DEVICE ZXSPECTRUM128`):

| SAVETRD entry | ORG | becomes |
|---|---|---|
| `INCBIN scroller.bas` | #C000 | `SCROLLER.B` (333 B) |
| `bin/SCROLL00.BIN` | #6200 | `SCROLL00.C` (178 B, **raw**, not packed) |
| `bin/SCROLL15.MLZ` | #8000 | `SCROLL15.C` (1708 B) |
| `bin/SCROLL10.MLZ` | #8000 | `SCROLL10.C` |
| `bin/SCROLL11.MLZ` | #8000 | `SCROLL11.C` |
| `bin/SCROLL13.MLZ` | #8000 | `SCROLL13.C` |
| `bin/SCROLL17.MLZ` | #8000 | `SCROLL17.C` |
| `bin/SCROLL12.MLZ` | **#C000** | `SCROLL12.C` (48 sectors - the only `$C000` load) |

This is the origin of the asymmetry that the whole crash story hangs on: every
part is a **raw .mlz staged through the fixed `$8000` window** except
`SCROLL12.C`, which is staged through `$C000` - directly into whatever RAM
page bank 3 holds at LOAD time.

`depacker.asm` being empty means the released archive cannot rebuild the
depacker from text - but the binary `SCROLL00.C` contains it (at `$6244`),
and `trd_mlz.py` in this folder already executes those exact bytes as ground
truth.

## 2. The BASIC Loader (`scroller.bas`), Authoritatively Decoded

The file is tokenized with the 128K/TR-DOS extended token table (the same map
as our `BasicEncoder`: `RANDOMIZE`=F9, `USR`=C0, `VAL`=B0, `OUT`=DF,
`REM`=EA, `LOAD`=EF, `CODE`=AF - see
`core/src/debugger/analyzers/basic-lang/basicencoder.cpp`). Full listing:

```basic
10 INK NOT PI: PAPER NOT PI: BORDER NOT PI: CLEAR VAL "25088"
20 RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL00" CODE
30 RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL15" CODE
40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": REM: LOAD *"SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"
```

Decoded facts worth noting:

- `NOT PI` is the classic 2-byte spelling of 0 (cheaper than the 7-byte
  numeric-constant encoding); line 10 paints black and **`CLEAR $6200`** -
  RAMTOP pulled below the machine-code loader so BASIC never allocates over
  it. This is a hand-tuned loader, not editor-entered text.
- `RANDOMIZE USR 15619` (`$3D03`) enters TR-DOS's command interpreter, which
  parses the `REM:`-prefixed tail as its own `LOAD *"file" CODE` syntax.
- `25094` = `$6206` = LOADER; `25088` = `$6200` = START.
- **Line interleaving is the design:** each of lines 40-80 first runs
  `USR 25094` (depack the *previous* LOADTBL entry out of the `$8000`
  staging window into its final page), then loads the next .mlz into the
  now-free `$8000` window. Nothing is depacked until its successor's data
  needs the window.
- Line 80 is the sole page-sensitive statement in the entire loader:
  `OUT VAL"32765",VAL "20"` = select RAM page 4 at `$C000`, immediately
  followed by the only `LOAD` whose address is `$C000`.

## 3. Loader Source (`scroller.asm` at $6200)

Assembled output = `SCROLL00.C`, whose first bytes `31 00 62 C3 6B 9B`
(`LD SP,$6200 ; JP $9B6B`) double as proof that `SETUP = $9B6B`.

```asm
START   EQU     #6200
        ORG     START
        LD      SP,START
        JP      SETUP
LOADER  DI
        PUSH    IY
        LD      HL,LOADTBL
LDTBLA  EQU     $-2              ; self-mod: pointer to next entry
        LD      A,(HL)           ; [1] page value for #7FFD
        INC     HL
        LD      BC,#7FFD
        OUT     (C),A            ; bank3 <- entry's page (bit4=48K ROM)
        ...                       ; [2] DEST, [2] SRC -> DE (out), HL (in)
        LD      (LDTBLA),HL      ; advance pointer for next USR 25094
        CALL    DEPACK
        POP     IY
        RET
```

The author documents the plan himself (translated):

```
; load picture:
; 1) loader #START (size=LOADER_END-#START)
; 2) #LOADER_END..#7FFF (PAGE #15)
; 3) PAGE #10 C000 ; 4) PAGE #17 DB00 ; 5) PAGE #13 C000 ; 6) PAGE #11 C000
; 7) #8000 (PAGE #12)
; 2-6 are loaded at #8000 and depacked into their pages
; 7 - loaded at #C000 into page 14 and depacked to address 8000
```

LOADTBL in source matches the binary at `$6226` entry-for-entry:
`[PAGE][DEST][SRC]` = `#15/$62B2/$8000`, `#10/$C000/$8000`,
`#11/$C000/$8000`, `#13/$C000/$8000`, `#17/$DB00/$8000`, `#14/$8000/$C000`.

Two structural points, both visible only in source:

- **The depacker re-OUTs its page itself** (`LOADER: OUT (C),A` from the
  table entry). So the LOADER does not depend on the BASIC-level page
  selection surviving - but it *reads* whatever the earlier `LOAD` happened
  to leave in the bank-3 page named by the entry.
- All six LOADTBL page values carry bit 4 (48K ROM half) - deliberate, so a
  depack never fetches stream bytes from a 128K editor ROM bank at `$C000`.

## 4. The Covox Selection Screen (`SETUP` = $9B6B)

Drawn from `gfx/setup/*.SPR` sprites (SELECT/USE CURSORS/SPACE/credits + the
author's group mark `DEMARCHE.SPR`, `PENT.SPR`). Then a display-synced poll:

```asm
KEYLP   EI
        HALT                       ; one frame per iteration
        LD      A,(COVOXH) ... NUMOUT  ; show port digits F b
        ...                       ; blinking cursor over the digit (CURBLNK)
        LD      A,#7F
        IN      A,(#FE)
        BIT     0,A
        JP      Z,STARTDEMO        ; SPACE -> go
        ...                       ; cursor keys -> KEY_UP / KEY_DW
```

Port selection state (defaults in source):

```asm
COVOXH  DB      15                 ; high digit  ($F)
COVOXL  DB      11                 ; low digit   ($B) -> port #FB
COVOXS  DB      0                  ; 0 = covox mode, 1 = NeoGS mode
```

`KEY_UP` selects covox mode (`COVOXS=0`), `KEY_DW` selects NeoGS
(`COVOXS=1`). Number/letter keys (`SETCVX`, digits C=0..15) edit either
digit; `COVOX EQU #FB` is the assembled-in default everywhere else.

## 5. STARTDEMO ($9CD6) - Fade, NeoGS Init, the Port Patcher, Launch

```asm
STARTDEMO
        EI : HALT : EI : HALT       ; 2-frame sync
        ; fade: LDIR over attributes, self-mod FADE counter, 8 -> 0
        LD      HL,COVOXH
        LD      A,(HL) : INC HL
        ADD     A,A x4 : ADD A,(HL)  ; A = H*16 + L = selected port
        LD      LX,A                ; IX high byte holds the port
        LD      A,(COVOXS) : OR A
        JP      Z,SEEKLP            ; covox: skip GS init
        LD      A,#F4 : CALL GSCOMM ; NeoGS: cold restart
        LD      A,#0E : CALL GSCOMM ; NeoGS: enter covox mode
        LD      LX,179              ; GS data register
SEEKLP  LD      A,50 ...             ; ~1 s pause (50 frames)
        LD      HL,START : LD BC,#8000-START
SEEK1   ... CALL SEEK (DE=#237E)     ; patch pass 1
SEEK2   ... CALL SEEK (DE=#030A)     ; patch pass 2
        JP      #8000               ; enter the demo body (INIT1)
```

`SEEK` is a **code patcher, not a disk seek** - it scans `$6200-$7FFF`
(LOADER + page 5, where the sample players live) for two playback idioms and
rewrites the immediate:

```asm
SEEK    LD      A,(HL) : CP E       ; pass 1: 7E = LD A,(HL)
        ... CP D                   ;           23 = INC HL
        ... CP #D3                 ;           D3 = OUT (n),A
        ... CP #FB                 ;           n  = #FB (default)
        LD      A,LX : LD (HL),A   ; -> selected port
```

Pass 2 patches the `0A 03 D3 FB` variant (`LD A,(BC); INC BC`). Everything in
`$6200-$7FFF` is page-5 code, permanently visible at `$4000-$7FFF` - the
patcher itself is immune to bank-3 state. The NeoGS command port is 187
(`$BB`).

## 6. Demo Body (`SCROLL12` depack output, $8000-$C000 = RAM page 2)

`SAVEBIN "bin/SCROLL12.BIN",#8000,#C000-#8000` - the whole body is ONE file,
which is why page 2 is all-or-nothing. Layout from source ORGs:

| Region | Content |
|---|---|
| `$8000` | entry: `CALL INIT1` then `BLOOP` main loop |
| `$9B6B` | `SETUP` (selection screen, section 4) |
| `$9CD6` | `STARTDEMO` (section 5) |
| `$BE00` | IM2 vector table: `DS 258,#BF` -> every vector `$BFBF` |
| `$BF02` | `IM2INI`: `DI; LD A,#BE; LD I,A; IM 2; EI; RET` |
| `$BFBF` | `IM2B` interrupt handler |

`INIT1` silences covox (`LD A,#80; OUT (COVOX),A`), clears screens, switches
bank3 (`#17`, `#10`) for clears and `AYINIT`, builds effect tables, then
`CALL IM2INI` - from here on every `EI/HALT` lands in `IM2B`:

```asm
IM2B    PUSH AF : LD A,0 : OR A : JR Z,IM2R   ; MUSFLAG gate ($BFBF+2)
        PUSH ... EXX ...                       ; full context save
        LD BC,#7FFD : LD A,#10 : OUT (C),A     ; bank3 = page 0
        CALL AYPLAY                            ; AY player (page 5)
        LD BC,#7FFD : LD A,#17 : OUT (C),A     ; bank3 = page 7
        POP ... : EI : RET
```

`BLOOP` (main loop) is one frame per iteration: `EI/HALT`, `BTRACKER`
(effect sequencer over the `FXTRACK` table), then page-switched rendering
(`#15+#08`/`#17`/`#13` at bank 3, `ROLL_L`/`ROLL_R` scroller, dots,
textures). `effect_plan2.txt` is the author's timing budget proving the
frame is fully hand-scheduled (~17920T).

## 7. Deduction: Why the Demo Does Not Load / Decrunch Properly

The source makes the failure mode airtight - every link is now author-
attested, not just binary-observed:

1. **Everything except SCROLL12 is page-safe by construction.** Six of seven
   loads stage through the fixed `$8000` window (page 2, immune to #7FFD),
   and the depacker re-OUTs its target page from LOADTBL itself (section 3).
   Any boot path loads and depacks parts 1-5 (pages 5/0/1/3/7) correctly.

2. **SCROLL12 is the one deliberate exception - and line 80 is its only
   protection.** The author's own comment says it: "7 - loaded at #C000 into
   page 14 and depacked to address 8000". The mechanism that must make that
   true is a single BASIC statement, `OUT VAL"32765",VAL "20"`, executed
   one statement before `USR 15619` triggers the TR-DOS `LOAD` into `$C000`.
   Nothing in the machine code re-selects page 4 *during* the LOAD - LOADER's
   own `OUT (C),A` happens only at the *depack* (line 90), long after the
   sectors have landed somewhere.

3. **On a 128K-editor machine the protection does not survive to the LOAD.**
   The `OUT` command sets the port latch, not `BANK_M` ($5B5C) - and the
   editor's SWAP hook at `$5B00` re-derives #7FFD from that stale shadow at
   every statement boundary (proof: bus trace in TRIAGE.md section 4).
   Worse, TR-DOS 5.03's sector loop re-enables interrupts per sector
   (`DI` $3F16 ... `EI` $3F32, byte-verified), so the hook can fire *inside*
   the LOAD: sectors 1..k reach page 4, sectors k+1..48 land in page 0 -
   a run-dependent split (TRIAGE.md section 9).

4. **Line 90 then faithfully depacks the wrong page.** `USR 25094` OUTs `#14`
   (page 4 at `$C000` - from LOADTBL, exactly as designed) and MegaLZ-decodes
   whatever partial stream is there into `$8000`. The depacker is not broken;
   it is fed a truncated source. `trd_mlz_truncation.py` maps every split k:
   low k -> page 2 dead (no menu, NOP slide -> wrap -> reset); k >= ~22 ->
   INIT1/menu/STARTDEMO valid but the IM2 top ($BE00/$BF02/$BFBF - the last
   bytes of the stream, section 6) corrupt -> **covox menu shows, SPACE
   starts, INIT1's `CALL IM2INI` and the handler are garbage, one EI/HALT
   later the CPU slides through zeroes, wraps past $FFFF and resets.**

5. **Why the author never saw it:** booted the way such demos were run -
   reset into 48K/TR-DOS directly, where the 128K editor's SWAP hooks are
   never installed - line 80's latch write is final, all 48 sectors reach
   page 4, and the demo runs (our `BootScrollerDemoTRD` reproduces this).
   The loader is 48K-idiomatic; on 128K editors the safe spelling is
   `POKE VAL"23388",VAL"20"` (write `BANK_M`, let the editor perform the
   OUT) - exactly what TRIAGE.md concluded from the binary side.

**Verdict, source-confirmed:** not a depacker bug, not a disk bug, not an
emulator bug. A single 128K-unsafe `OUT 32765,20` whose page selection the
editor environment reverts (at a statement boundary or mid-LOAD), turning
the demo body's only load into a truncated stream. The emulator reproduces
authentic hardware behavior; both outcomes are pinned by the two regression
tests.

## 8. Source <-> Binary Cross-Reference

| Source item | Binary evidence | Documented in |
|---|---|---|
| `linker.asm` SAVETRD map | TRD catalog (8 files, sizes match) | REVERSING.md sec. 1 |
| `scroller.bas` lines 10-90 | `SCROLLER.B` bytes (identical file) | REVERSING.md sec. 2 |
| `LOADER`/`LDTBLA` self-mod | bytes at `$6206-$6225` | REVERSING.md sec. 3 |
| LOADTBL + author comments | table at `$6226` | REVERSING.md sec. 3 |
| `depacker.asm` (empty) | depacker bytes at `$6244` executed by `trd_mlz.py` | REVERSING.md sec. 4 |
| `SETUP` sprites/keys | code at `$9B6B` | REVERSING.md sec. 6 |
| `STARTDEMO`/`SEEK`/NeoGS | code at `$9CD6` | REVERSING.md sec. 6 |
| `ORG #BE00: DS 258,#BF` | `$BE00-$BF01` all `$BF` | REVERSING.md sec. 7 |
| `IM2INI` / `IM2B` | `$BF02` / `$BFBF` ground-truth bytes | REVERSING.md sec. 7 |
| line 80 `OUT 32765,20` | SEQ 286/287 bus trace (apply + revert) | TRIAGE.md sec. 4 |
