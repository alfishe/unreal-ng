# Scroller by Demarche - Static Reverse Engineering

Everything in this document was derived from `testdata/sound/covox/scroller_by_demarche.trd`
(catalog dump, opcode-level disassembly) and cross-checked against the original
sources in `testdata/sound/covox/source/`. Byte values are exact; symbolic names
follow the source labels where available. Runtime confirmation came from the
instrumented boot test (`core/tests/emulator/io/fdc/scroller_boot_test.cpp`).

## 1. Disk Image

TR-DOS geometry: 80 tracks x 16 sectors x 256 bytes = 4096 bytes per track.
Catalog at sector 8 of track 0, 128 entries x 16 bytes:
`name[8] | ext | params u16 | length u16 | sizeInSectors | startSector | startTrack`
(see `core/src/emulator/io/fdc/trdos.h`).

Catalog dump (via `trd_mlz.py`):

| File | Params (load addr) | Length | Sectors | Start (trk,sec) | First bytes |
|------|--------------------|--------|---------|-----------------|-------------|
| `SCROLLER.B` | $014D | 333 | 2 | 1, 0 | `00 0A 16 00 D9 C3 A7 3A` |
| `SCROLL00.C` | $6200 | 178 ($B2) | 1 | 1, 2 | `31 00 62 C3 6B 9B F3 FD` |
| `SCROLL15.C` | $8000 | 1708 | 7 | 1, 3 | `01 E7 FD BF D9 FC FF 2A` |
| `SCROLL10.C` | $8000 | 4601 | 18 | 1, 10 | `00 FF 1A 25 29 C5 2F 34` |
| `SCROLL11.C` | $8000 | 5342 | 21 | 2, 12 | `09 9E 1B FF 12 32 36 94` |
| `SCROLL13.C` | $8000 | 2962 | 12 | 4, 1 | `00 60 F8 FF C7 01 FF 01` |
| `SCROLL17.C` | $8000 | 4371 | 18 | 4, 13 | `80 1F 6F 68 FF 61 5E 85` |
| `SCROLL12.C` | $C000 | 12155 | 48 | 5, 15 | `CD FF 93 85 FB 76 3E 01` |

Notes:

- `SCROLL00.C` is 178 bytes = exactly `$B2`, so it occupies `$6200-$62B1`.
  `LOADER_END = $62B2` - the destination of depack entry 1 below.
- Every `SCROLLnn.C` except `SCROLL12.C` loads to the fixed page-2 window at
  `$8000`. `SCROLL12.C` is the odd one: it loads raw to `$C000`, i.e. directly
  into whatever RAM page is currently mapped in bank 3 - which is what makes the
  page selection around it load-bearing (see TRIAGE.md). If that selection is
  flipped mid-LOAD, the stream splits at sector k and the depacker produces
  only the valid prefix - the surviving-landmarks-per-k matrix is in TRIAGE.md
  section 9 and reproducible via `trd_mlz_truncation.py` (this folder).
- The first bytes of `SCROLL12.C` (`CD FF 93 85`) and the first bytes of its
  depacked output (`CD 93 85 FB 76`) differ only by the inserted `FF` flag byte -
  a useful sanity marker when grepping traces: `p4=CDFF9385` (packed, read from
  page 4 at `$C000`) vs `@8000=CD9385FB` (depacked into page 2).

## 2. BASIC Loader (`SCROLLER.B`, tokenized, 333 bytes)

Memory map during loading:

```
$0000-$3FFF  ROM          (128K editor / 48 half, #7FFD bit 4; TR-DOS ROM when in DOS)
$4000-$7FFF  RAM page 5   (screen; SCROLL15 also depacks here above the loader)
$8000-$BFFF  RAM page 2   (fixed on 128K; raw landing zone for packed blocks)
$C000-$FFFF  RAM page 0-7 (switchable via #7FFD bits 0-2; depack destinations)
```

Reconstructed listing (line 80 corrected - see note):

```basic
10 RANDOMIZE USR VAL "25088"        : REM $6200 LD SP,$6200 : JP SETUP
20 RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL00" CODE
30 RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL15" CODE
40 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL10" CODE
50 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL11" CODE
60 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL13" CODE
70 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL17" CODE
80 RANDOMIZE USR VAL "25094": OUT VAL "32765",VAL "20": RANDOMIZE USR VAL "15619": CODE: LOAD *"SCROLL12" CODE
90 RANDOMIZE USR VAL "25094": RANDOMIZE USR VAL "25088"
```

- `15619` = `$3D03` (TR-DOS command entry), `25088` = `$6200` (START), `25094` = `$6206` (LOADER).
- **Line 80 note:** this is an `OUT` command (BASIC tokenized command executed by
  the 48-half OUT-command executor, `OUT (C),A` at `$1E7D`), **not** `POKE`.
  An early analysis pass misread the tokenized line as `POKE 32765,20` and
  concluded the demo was buggy; the bus trace disproved this - the port write
  lands (`#7FFD <- $14`, page 4 at `$C000`). See TRIAGE.md, section "The demo is
  not buggy".

## 3. `SCROLL00.C` - LOADER / LOADTBL / DEPACK ($6200-$62B1)

### START ($6200)

```asm
6200  31 00 62      LD SP,$6200
6203  C3 6B 9B      JP $9B6B        ; -> demo main (page 2)
```

`RANDOMIZE USR 25088` therefore: sets the stack below the loader area and jumps
into the demo. Called twice - once at line 10 (harmless early visit to $9B6B,
which expects a keypress), once at line 90 (the real start).

### LOADER ($6206) - depack dispatcher

```asm
6206  F3            DI
6207  E5            PUSH IY
6208  2A 0A 62      LD HL,(LDTBLA)  ; self-mod pointer to current LOADTBL entry
620B  7E            LD A,(HL)       ; page value for #7FFD
620C  23            INC HL
620D  01 FD 7F      LD BC,$7FFD
6210  ED 79         OUT (C),A       ; select RAM page in bank 3 ($C000-$FFFF)
6212  5E            LD E,(HL)       ; dest lo
6213  23            INC HL
6214  56            LD D,(HL)       ; dest hi
6215  23            INC HL
6216  4E            LD C,(HL)       ; src lo
6217  23            INC HL
6218  46            LD B,(HL)       ; src hi
6219  23            INC HL
621A  22 0A 62      LD (LDTBLA),HL  ; advance pointer for next call
621D  60            LD H,B
621E  69            LD L,C          ; HL = source
621F  CD 44 62      CALL DEPACK     ; $6244, MegaLZ V4: HL=src, DE=dest
6222  E1            POP IY
6223  FB            EI
6224  C9            RET
```

### LOADTBL ($6226) - 6 entries x 5 bytes

Entry format: `[1 byte PAGE] [2 bytes DEST] [2 bytes SOURCE]`

| # | PAGE | Dest | Src | Meaning |
|---|------|------|-----|---------|
| 1 | $15 | $62B2 | $8000 | page 5 at $C000 during depack; output to LOADER_END (page 5 code extension) |
| 2 | $10 | $C000 | $8000 | page 0 <- packed at $8000 |
| 3 | $11 | $C000 | $8000 | page 1 |
| 4 | $13 | $C000 | $8000 | page 3 |
| 5 | $17 | $DB00 | $8000 | page 7, dest $DB00 |
| 6 | $14 | $8000 | $C000 | page 4 at $C000 (source!); output to $8000 = page 2 |

Entry 6 is inverted relative to 1-5: with page 4 mapped at `$C000`, the MegaLZ
stream is **read** from `$C000` (where line 80's `OUT` made `LOAD *"SCROLL12" CODE`
land the raw sectors) and **written** to `$8000-$BFFF` (fixed page 2, the demo
body). This is why line 80's page selection must still be in effect at line 90's
first `USR 25094` - the depacker re-OUTs `$14` itself at entry, but if the raw
data never reached page 4, it re-OUTs into an empty page and depacks zeroes.

All LOADTBL page values have bit 4 set (48K ROM half selected) - deliberate, so
depack never trips over a 128K editor ROM bank at `$C000` mid-stream.

## 4. DEPACK ($6244) - MegaLZ V4

Calling convention: `HL` = packed source, `DE` = destination, returns on stream
end marker. Located at `$6206 + code-to-$6226 + 6*5 = $6244` (verified by
executing the very bytes from the disk image - see `trd_mlz.py`, `dec40 = 0x6244`).

Head disassembly:

```asm
6244  3E 80         LD A,$80        ; prime bit counter (8 bits)
6246  08            EX AF,AF'       ; A' = bit budget, flags side kept for buffer
6247  ED A0         LDI             ; first literal always copied
6249  01 FF 02      LD BC,$02FF     ; offset register pair default
624C  08            EX AF,AF'
624D  87            ADD A,A         ; shift next bit into carry
624E  20 03         JR NZ,+3        ; budget left -> dispatch on bit
6250' 7E            LD A,(HL)       ; budget empty: reload from stream
6251' 23            INC HL
6252' 17            RLA             ; (shifts new byte in, feeds carry)
6253  CB 11         RL C
6255  30 F6         JR NC,-10       ; bit==0: short match path
...
6259  10 0F         DJNZ ...
625B  3E 02         LD A,$02
625D  CB 29         SRA C           ; long-offset decode
...
```

The instruction set the whole depacker is built from (complete inventory,
verified by running the real bytes in `trd_mlz.py`'s closed-set interpreter -
any opcode outside this set raises):

| Group | Instructions |
|-------|--------------|
| Bit reader | `ADD A,A` `RLA` `RLC C`(`CB 11`) `RL B`(`CB 10`) `SRA C` `SRL C` `RR C` `EX AF,AF'` |
| Literals | `LD A,(HL)` `LDI` `LDIR` `LD BC,nn` `LD A,n` `LD B,n` |
| Match/offset arithmetic | `ADD A,B` `ADD A,C` `ADD HL,DE` `INC/DEC B` `INC C` `INC A` `INC HL` `LD B,C` `LD C,A` `LD C,(HL)` `LD H,B` `LD L,C` |
| Control flow | `JR NZ/NZ/C/NC` `DJNZ` `JR` `RET` `RET C` `PUSH/POP HL` |

The stream is read MSB-first through the `A`/`A'` bit-buffer with refill from
`(HL)`; match offsets are decoded through the `C` register rotate chain
(`SRA C`/`SRL C`/`RR C`), with `B`/`C` doubling as offset-high and `DE` the
moving destination. For the container format spec see MegaLZ V4
(fyrex^mhm); for this demo the exact end-to-end behavior is captured by the
tool below, which is the authoritative oracle used by the boot test.

## 5. Depacked Output Map (ground truth from `trd_mlz.py`)

| Block | Dest window | Coverage | Last nonzero | Notable content |
|-------|-------------|----------|--------------|-----------------|
| SCROLL15.C | page 5 `$62B2-$7E14` | 3910/7502 (52%) | `$7E14` | loader/code extension in page 5 above the screen |
| SCROLL10.C | page 0 `$C000-` | 13454/16384 (82%) | `$FFB3` | data |
| SCROLL11.C | page 1 `$C000-` | 14731/16384 (89%) | `$FFFF` | data |
| SCROLL13.C | page 3 `$C000-` | 12936/16384 (78%) | `$FF52` | scroll text area starts zeroed; 2x3 font at `$F600` (`01 00 01 00 ...`) |
| SCROLL17.C | page 7 `$DB00-` | 8358/9472 (88%) | `$FC1E` | data |
| SCROLL12.C | page 2 `$8000-` | 15175/16384 (92%) | `$BFEE` | the demo body (code + tables) |

(The emulator run measures 15585/16384 nonzero in page 2 - slightly more than
the tool's 15175 because runtime data also lands there after depack.)

## 6. Demo Runtime (page 2)

Entry chain, confirmed by breakpoints in the boot test:

| Address | Event | Source evidence |
|---------|-------|-----------------|
| `$6200` | `LD SP,$6200 ; JP $9B6B` - `USR 25088` | `31 00 62 C3 6B 9B` at file head |
| `$9B6B` | demo main; waits for SPACE | breakpoint hit |
| `$9CD6` | SPACE accepted -> STARTDEMO | breakpoint hit |
| `$9D48` | fade done + covox-port patch loops complete -> `JP $8000` imminent | breakpoint hit |
| `$8000` | demo entry: `CALL INIT1` (`$8593`) ; `EI` ; `HALT` | depacked bytes `CD 93 85 FB 76 3E 01 ...` |

The `$8000` head is the classic wait-for-IM2 idiom: call init, enable
interrupts, halt until the raster service takes over.

### Interrupt mode 2

- `IM2INI` at `$BF02` (breakpoint-verified); its tail is visible in the page-2
  dump: `F3 3E BE ED 47 ED 5E FB C9` = `DI ; LD A,$BE ; LD I,A ; IM 2 ; EI ; RET`.
- Interrupt register `I = $BE`, vector table `$BE00-$BEFF` filled with `$BF`
  bytes -> every vector resolves to `$BFBF`.
- Handler `$BFBF`:

```asm
BFBF  F5          PUSH AF
BFC0  3E 00       LD A,$00        ; demo-active flag
BFC2  B7          OR A
BFC3  28 27       JR Z,$BFEC      ; not active -> skip work, restore & out
BFC5  C5 D5 E5    PUSH BC,DE,HL
BFC8  DD E5       PUSH IX
BFCA  FD E5       PUSH IY
BFCC  D9          EXX
BFCD  E5 D5 C5    PUSH HL',DE',BC'
...
      01 FD 7F    LD BC,$7FFD
      3E 10       LD A,$10        ; page 0, 48K ROM
      ED 79       OUT (C),A
      CD B2 62    CALL $62B2       ; page-5 service code (SCROLL15 depack dest)
      01 FD 7F    LD BC,$7FFD
      3E 17       LD A,$17        ; page 7
      ED 79       OUT (C),A
BFEC  C1 D1 E1    POP BC',DE',HL'   ; epilogue ($BFEC)
      D9          EXX
      FD E1 DD E1 POP IY,IX
      E1 D1 C1    POP HL,DE,BC
      F1          POP AF
      FB          EI
      C9          RET
```

The handler page-flips bank 3 between `$10` (page 0) and `$17` (page 7) on
every interrupt to stream data from paged RAM - this is why the post-boot bus
trace shows continuous `#7FFD` chatter once the demo is alive.

### Sound

Covox (PWM DAC) on port `#FB` - hence the demo's home under
`testdata/sound/covox/`.

## 7. 128K ROM Environment (the part that broke the boot)

The demo is BASIC-driven, so the ROM around it is load-bearing. Facts
established during the triage:

- `data/rom/pentagon.rom` is a 64K image = 4 x 16K quarters. On a Pentagon the
  active pair is the 128K editor (bit 4 = 0) or the 48K/sos half (bit 4 = 1).
- The 128K editor copies a SWAP hook template from ROM (`$006B` in the editor
  quarter) to RAM `$5B00`:

```asm
5B00  F5          PUSH AF
5B01  C5          PUSH BC
5B02  01 FD 7F    LD BC,$7FFD
5B05  3A 5C 5B    LD A,($5B5C)    ; BANK_M - editor's paging shadow
5B08  EE 10       XOR $10         ; toggle ROM bit only
5B0A  F3          DI
5B0B  32 5C 5B    LD ($5B5C),A
5B0E  ED 79       OUT (C),A       ; re-OUT shadow (with ROM bit toggled)
5B10  FB          EI
5B11  C1          POP BC
5B12  F1          POP AF
5B13  C9          RET
```

- The interpreter's statement-boundary trampoline ("YOUNGER", `$5B14` region)
  CALLs `$5B00` between BASIC statements. Signature of the resident copy:
  `F5 C5 01 FD 7F 3A 5C 5B ...` at `$5B00`.
- `BANK_M` (`$5B5C`) is the editor's **logical** paging state, not a port
  shadow: the editor's own internal OUTs (e.g. menu-screen scratch mappings
  `OUT (C),$07` from `$1C7B`/`$1F3E`) deliberately bypass it.
- The 48 half executes BASIC `OUT` commands through a small executor whose tail
  is `CALL $1E85 ; OUT (C),A ; RET` - the `OUT (C),A` opcode pair sits at
  `$1E7D`, the `RET` at `$1E7F`. This executor exists **only** in the 48 half;
  the editor quarter has no copy.
- Neither ROM half updates `BANK_M` when the interpreter executes an `OUT`
  command (verified across every ROM in `data/rom/`; `pentagon.rom` is a plain
  Amstrad pair). In an editor environment this makes a BASIC `OUT` to `#7FFD`
  ephemeral - reverted one statement later by the hook. That is the entire
  crash, and it is authentic stock-ROM behavior (see TRIAGE.md).

## 8. Tool: `trd_mlz.py`

Ground-truth oracle. What it does:

1. Parses the TRD catalog and extracts every file (raw sector walk, track x 16
   sectors x 256 bytes).
2. Takes the **real** MegaLZ depacker bytes from `SCROLL00.C` at `$6244`
   (rest of the sector, zero-padded).
3. Runs them inside a ~200-line Z80 interpreter implementing exactly the closed
   opcode set of section 4 (any other opcode aborts with `opcode XX at YYYY` -
   a useful tripwire if the depacker is ever mis-located).
4. Invokes the depacker once per LOADTBL entry with the real `HL`/`DE`, and
   reports first bytes, coverage and last-nonzero offset per target page.

Usage:

```sh
python3 docs/disasm/demo/scroller/trd_mlz.py
```

The `TRD`/`OUT` constants at the top are absolute paths into this checkout
(`testdata/...` and `build/trd_files/`); adjust if running from elsewhere.
Output: catalog table (section 1) plus the per-block stats of section 5, and
`.depacked` binaries under `build/trd_files/` for byte-diffing against emulator
memory dumps.
