# unreal-asm: sjasmplus → Power Assembler 3.0 (part B, Z4)

| | |
|---|---|
| **Date** | 2026-10-12 |
| **Code** | `src/dialects/pasm/pasmbackend.cpp` (the `pasm` backend, on `ChainWriter`), `src/dialects/pasm/pasmcurrent.h` (what its `$` is in an instruction, shared with the frontend), `src/sync/reader.cpp` (`Pasm30`: idle rule, `PlanInjectPasm`) |
| **Rules from** | research-power-assembler.md §3, PASM 3.0's code (ZAP at `#7301`, GET at `#6E5E`, the editor's entry at `#73AE`), probes compiled by PASM 3.0 in unreal-ng |
| **Checks** | `unreal-asm-tests` (`PasmBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, PASM's own probes, ZAsm, STORM, MASM, XAS and TASM 5.0 programs; `SyncInject_Test.PasmTakesATextInItsEditorAsGetLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py pasm` |

## 1. Example first

```text
sjasmplus                                   PASM 3.0
        org #6000                                   ORG #6000
verylonglabelname ld hl,(verylonglabelname+1)*2     VERYLONGLABELN LD HL,VERYLONGLABELN+1*2   ; 14 characters, left to right
        ld bc,2+3*4                                 LD BC,3*4+2         ; the atom moved to the right
        ld a,-1                                     LD A,255            ; a byte in 0..255
        db "Hello",13,'A'                           DB 'Hello',13,65    ; a character in DB as its number
        ds 4,#e5                                    DB 229 DUP 4        ; DUP: the only item of its line
        jr $                                        JR $-1              ; its $ is what it has put
        ld (ix),a                                   LD (IX+0),A
        in f,(c)                                    DB #ED,#70 ;IN F,(C)
        disp #9000                                  ; (no DISP in PASM)
run     ld hl,$                                     RUN EQU #9000
                                                    LD HL,36864
        djnz run                                    DJNZ $-4            ; the distance at the run address
        ent
        org #6100                                   DB 0 DUP n          ; one ORG: a gap forward as zeros
```

## 2. What the backend writes

| Source | PASM text |
|---|---|
| An expression | a chain left to right with `+ - * /` (`ChainWriter`); the high byte as `/256`; what no chain gives (the low byte, AND, OR, XOR, comparisons) as its value where known, else 0 with a warning |
| Numbers | decimal, `#` hex in capitals, `%` binary, one printable character in apostrophes (`'AB'` is its syntax error) |
| A name | up to 14 characters (a fifteenth is "Bad label"), capitals, a letter first (`_X` is refused: `__x` becomes `X`); PASM keeps the case of names, the backend writes all in capitals |
| Mnemonics, registers | capitals (`ld a,1` is its syntax error) |
| A byte operand below 0 | its byte (`LD A,-1` is "Value out of range": PASM takes a byte only in 0..255) |
| `(IX+d)` with a computed `d` | the signed value (`(IX-3)`) |
| `$` in an instruction | `$-k` where PASM's `$` is ahead (`pasmcurrent.h`: `JR $`, `LD HL,$` +1; `LD IX,$`, `LD BC,($)` +2; the first of two operands is read before the opcode); in a byte operand its value (PASM refuses a byte made from `$` by subtraction) |
| `$` in `DB` / `DW` | one item a line (its `$` is the item's address); in `DB` the value |
| `EQU` | `EQU` with names defined above it only (a later name: the value); `$` there as the value (PASM's `$` in EQU is 0, `$+1` is refused) |
| `IF`, `DUP`, `WHILE`, `MACRO`, `DEFL` | decided, unrolled, expanded, written as values |
| `ORG` | the first as `ORG x`; a later one forward as `DB 0 DUP n` (PASM: "Bad ORG" after code); backward: not converted |
| `DISP r` / `ENT` | the block's labels as `NAME EQU run address`, its `$` as values, its `JR` / `DJNZ` as `$+k` by the distance at the run address (PASM has no displacement) |
| `DB` / `DW` / `DS` | strings as `'text'` (an apostrophe, `;`, control bytes and bytes above #7F as numbers); characters as numbers (an item that starts with an apostrophe is a string for PASM); `DS n` as `DB v DUP n` |
| A comment | after the statement (` ;`), cut to 64 columns |
| A line | at most 64 columns (PASM reads 64 columns: a longer `DB` line failed with "Value missing" at column 63), a warning otherwise |
| `IN F,(C)`, `OUT (C),0`, the index halves, the undocumented copies | bytes (PASM has none of them); `SLL` as `SLI` |
| `(IX)` | `(IX+0)` (PASM refuses `(IX)`), `JP (IX)` kept |
| `INCLUDE`, `INCBIN` | `ITXT name`, `IBIN name` (8 characters of the file's name) |
| `END`, `SAVEBIN` | dropped (info) |

The text has CR LF line ends, as PASM saves it.

## 3. Writing into PASM 3.0 (the injection)

The text runs from `#79AF` to a `#1A` byte. The inject leaves the editor as ZAP (`#7301`) and then GET (`#6E5E`) do:

- the text and its `#1A`;
- the text's length at `#7747` (without the `#1A`), the number of its last line at `#7752`;
- the cursor on the first line: its address in `#74E9` and `#76E9` (the first line on the screen), its length in
  `#7802`, its text in the 64-byte line buffer `#7342` with the TABs spread out;
- what ZAP clears: the words `#775B` (the cursor's line), `#7383` (its column and row), `#6525`, `#63CA`, `#6C54`,
  `#6DFE`, `#6E00` (the block marks) and the bytes `#6CD0`, `#6D31`, `#6B48`;
- the redraw flag `#73B6` (the operand of `LD A,0` at `#73B5`) to `#FF`: the editor redraws the screen the next time
  it is entered (after a menu command, or BREAK in the menu);
- the work name at `#7391` (8 characters, the inject's title).

The first line goes in packed as PASM packs it when the cursor leaves it (`PackPasmLine`, the routine at `#7881`), so
the text read back is the file written. The text stays below `#C000`, where the reader reads.

The idle rule: the editor's key wait (`CALL #65F7` at `#73CC`, inside the editor entered from `#61A3`) leaves `#73CF`,
`#61A6` on the stack. With the menu line open the stack holds neither. PASM keeps no "changed" flag: a typed text is
replaced by force, or when it is the caller's own.

## 4. Results

| Check | Result |
|---|---|
| `PasmBackend_Test.ProgramsKeepTheirBytesThroughPasm` (host): every instruction form, PASM's PROBE and CURRENT probes, ZAsm 3.15's ZXT1-3, STORM 1.3's STORMT2, MASM 1.1's MT1, XAS 7.447 `constrct`, TASM 5.0's T50PROG → sjasmplus → PASM (its text file) → the PASM frontend → sjasmplus | the original assemblers' bytes (`constrct`: the gap before its second ORG zeros, see §2) |
| `inject-oracle.py pasm` (PASM 3.0 on a Pentagon 128 in unreal-ng): `oracle1.asm` at `#6000`, every instruction form with its 599 labels and without at `#8000` | 143 / 1173 / 1173 bytes = sjasmplus' |
| The instruction forms with labels as a TR-DOS file `INSTR.C`, loaded with GET, compiled with L | 1173 bytes = sjasmplus' |
| `expression-fuzz.py pasm --template` in PASM 3.0 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' |

PASM puts the code it compiles in other RAM pages: code from `#8000` up in an image of the 48K memory (`#8000-#BFFF`
in page 3, `#C000-#FFFF` in page 0), code below `#8000` from `#2000` of page 4 whatever its ORG (`#6000` and `#5010`
both landed there). The oracle reads the bytes there.

The work found one bug in the PASM frontend, now fixed: `K EQU $` was read as the line's address. PASM gives 0 there
(and refuses `$+1`); the frontend now reads `$` in EQU as 0, with a warning.

## 5. Limits

- One ORG: a later ORG backward does not convert.
- `INCLUDE` / `INCBIN` become `ITXT` / `IBIN` of the disk's files; the inject writes one text.
- The text in memory stays below `#C000` (PASM itself takes it to `#FFFF`).
- No idle template snapshots for PASM yet.
