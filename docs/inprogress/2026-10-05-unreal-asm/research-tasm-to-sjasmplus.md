# unreal-asm: TASM → sjasmplus (phase A5b)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Code** | `src/dialects/tasm/tasmfrontend.cpp` (the `tasm` frontend, versions 3 / 4.0 / 4.12), shared macro expansion `src/dialects/common/macros.cpp`, the sjasmplus backend |
| **Oracles** | the General Sound 1.04 ROM (the sources and the ROM TASM 4.0 built, disk `GS104SRC.TRD` in `GS104SRC.ZIP` from KLUG's BBS archive, [klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw); the ROM is also unreal-ng's `data/rom/gs104.rom`), and TASM 4.12's SINUS example assembled by TASM 4.12 in the emulator |
| **Result** | the converted ROM sources assemble to the 32 768 bytes of the ROM, every byte; SINUS's 256-byte table equal; 99 sources from 6 disks unchanged through the sjasmplus round trip |
| **Checks** | `tools/unreal-asm/` (round trip, emulator oracle) and `unreal-asm-tests` (`TasmFrontend_Test`) |

## 1. Example first

```text
TASM 4.0                          sjasmplus
        LD H,COMTAB{              LD H,high COMTAB         ; postfix { = high byte of the value so far
        ADD A,CHANLEN}+1          ADD A,low CHANLEN+1      ; sjasmplus' unary operators bind first: (low CHANLEN)+1
        DW #9C40^                 DW ((#9C40&#FF)<<8|(#9C40&#FFFF)>>8)
        PUSH AF,BC                PUSH AF / PUSH BC
        JP NV,LOOP                JP PO,LOOP
        ORG #8000                 (ENT if a PHASE is active) ORG #8000
        PHASE #0000               DISP #0000
BPMTAB  INCBIN BPM                INCBIN "BPM" + the rest of its last sector, address moved back
TASM 4.12
        .IF DEBUG                 IF (DEBUG)==0            ; .IF compiles its first part when the value is 0
KEY     =  [#5C08]                KEY={#5C08}              ; [address]: memory read while assembling
        DEFMAC MOVE ... /0 \1     MACRO MOVE _arg0,_arg1
```

## 2. TASM facts the conversion relies on

Sources: the TASM 4.0 description in ZX Format #3 (1996), the TASM 4.12 article in Scenergy #1 (1999), the keyword
tables in the binaries (research-tasm.md), and the oracles.

| Fact | Conversion |
|---|---|
| No operator priorities: left to right (4.0 description; `(Y)&#C0*32` in the 4.12 article is `((Y)&#C0)*32`) | parentheses by tree |
| 16-bit words, unsigned division (SINUS: equal to TASM 4.12 only with it) | `/` masked to 16 bits |
| Constants `30`, `#40`, `40H`, `%1101`, `"A"` (`""` inside a string is one quote, the GENS rule TASM 4.0 fixed) | numbers keep their spelling |
| `+ - * / & |`, `!` xor; postfix `^` swaps the bytes, `{` high byte, `}` low byte, in 4.0 `[` / `]` rotate a 16-bit word by one bit | the same operations; `SwapBytes` and the rotations spelled out |
| TASM 4.12: `[address]` reads the word at the address while assembling | `{address}`, with `DEVICE` |
| An operand that starts with `(` is memory (`0+((\0)+1)` makes it a value, 4.12 article) | memory up to the matching `)` |
| Directives: `ORG`, `EQU`, `DEFB`/`DB`, `DEFW`/`DW`, `DEFM`/`DM`, `DEFS`/`DS n,fill...` (the fill, strings too, repeats n times), `PHASE` / `UNPHASE`, `INCLUDE` / `INCBIN name` without quotes; 4.12 adds `.INCLUDE`, `.INCBIN`, `.PHASE`, `.UNPHASE`, `.IF` / `.ELSE` / `.ENDIF`, `.LOCAL`, `.PAGE`, `.RUN`, `DEFMAC` / `ENDMAC`, `DISPLAY` | the IR directive kinds; `.PAGE` / `.RUN` kept as text with a warning |
| `ORG` ends an active `PHASE`, so does another `PHASE`; `UNPHASE` without one is ignored; a `PHASE` continues into an `INCLUDE` (GS ROM: `MAIN` includes 20 files inside `PHASE`) | `ENT` before `ORG` / `DISP`; where a file starts, unknown: `IFDEF __UNREALASM_DISP` / `ENT` / `ENDIF`, the backend keeps that `DEFINE` with every `DISP` / `ENT` |
| `INCBIN` copies whole sectors: the bytes after the file in its last sector land in memory after it, the address moves by the file's length (GS ROM: 64 bytes after `BPM` are the sector slack of `BPM.C`) | `INCBIN "<file>"`, then `INCBIN "<file>.slack"` and the address moved back (`zxasm convert` extracts both) |
| `PUSH` / `POP` take several registers | one instruction each |
| `NV` / `V` are the conditions PO / PE; `LX HX LY HY`; `INF`; `SLI` | `PO` / `PE`, `IXL ...`, `IN F,(C)` |
| 4.12: `.IF expr` compiles its first part when the value is 0 (`USE_MULT8=0` selects the code) | `IF (expr)==0` |
| 4.12: `.LOCAL` starts a region; `...name` labels belong to it; inside a macro to each expansion | `__local<n>_name`; in a macro a LOCAL block (sjasmplus `.local_name`) |
| 4.12: macro parameters `\0`…`\9` or `/0`…`/9`; `\c \n \s \r` walk the parameter text like ALASM's `\C \N \S \R` | named parameters; walking or gluing macros expanded at their calls |
| 4.12: a label defined with `EQU` may be reassigned with `=` | every definition of such a name becomes `=` |
| 4.12: names like `HL*8`, `?ASKYN`, `@204` | renamed (`L_HL_8`, `L__ASKYN`, `L__204`) the same way in every file |
| A label may stand indented when it is referenced (`" ?ASKYN"`) | taken as a label |

## 3. Oracle results

| Source | Built by | Compared | Result |
|---|---|---|---|
| GS 1.04 ROM: `MAIN` + 20 included files, `INCBIN STUFF / BPM / SGEN` | TASM 4.0 (the disk's `GS.C` = `data/rom/gs104.rom`) | all 32 768 bytes; the two CRC bytes at `#0006` are written by the build's last step (`CRC.A`, run inside TASM): the sum of the words from `#0008`, computed the same way | equal |
| SINUS (4.12 example, `ORG #7000`): a macro calling itself 64 times, values carried from pass 1 to pass 2 (`.IF PASS`), 16-bit division | TASM 4.12 in unreal-ng | 256 bytes | equal |

Found on the way (fixed): an `ORG` inside `PHASE` moved only `$` in sjasmplus; the `PHASE` state crossing `INCLUDE`;
`INCBIN` sector slack; `UNPHASE` without `PHASE`; `@204`-style labels.

## 4. Round trip and the wider corpus

`roundtrip.py` over TASM 4.12's own disk, Legend of Kyrandia (27 TASM 3 / 4.12 sources), sobdemo, Adventurer #12,
Gold Station and the GS sources: 99 sources, 0 round-trip differences, 2 lines the converter keeps as text (a
`JR C,` without its operand in the GS disk's unused `EFX_H`). Assembled alone, 68 of 99 build without errors; the
others are parts of projects (labels from the other parts), a source whose `INCBIN` file is not on its disk
(`ODNO`) and SNAKE, a game TASM 4.12 plays while it assembles.

## 5. Limits and open items

| Item | Note |
|---|---|
| TASM 4.12 `.PAGE` / `.RUN` | kept as text: assembling into a page and running code at the start of pass 2 have no sjasmplus counterpart yet |
| TASM 4.12's manual | it sits compressed in `tasm.ovl` (HyperText); the facts above come from the articles, the binaries and the oracles |
| `INCBIN` slack of a file filling whole sectors | the `.slack` file is empty: sjasmplus warns, the bytes are right |
| TASM 2.0 | the codec reads it (plain text); its dialect is TASM 3's |
