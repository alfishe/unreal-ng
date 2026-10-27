# unreal-asm: sjasmplus → PROMETHEUS (part B, Z4)

| | |
|---|---|
| **Date** | 2026-10-12 |
| **Code** | `src/dialects/prometheus/prometheusbackend.cpp` (the `prometheus` backend, on `ChainWriter`), `src/dialects/prometheus/prometheuscurrent.h` (where its `$` points, shared with the frontend), `src/sync/reader.cpp` (`Prometheus48`: idle rule, `PlanInjectPrometheus`) |
| **Rules from** | research-prometheus.md §5, the reconstructed source (oldcompcz/prometheus `src/prometheus.asm`), probes assembled by PROMETHEUS 48K in unreal-ng |
| **Checks** | `unreal-asm-tests` (`PrometheusBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, PROMETHEUS' own probe, ZAsm, STORM, MASM, XAS and TASM 5.0 programs; `SyncInject_Test.PrometheusTakesASaveInItsEditor`); `tools/verification/unreal-asm/emulator/inject-oracle.py prometheus` |

## 1. Example first

```text
sjasmplus                          PROMETHEUS 48K (as it shows the line)
        org 60000                           org  60000
longlabelname ld hl,(longlabelname+1)*2     LONGLABE ld   hl,LONGLABE+1*2   ; 8 characters, left to right
        ld a,low longlabelname              ld   a,LONGLABE?256             ; no AND: the remainder
        ld bc,2+3*4                         ld   bc,3*4+2                   ; the atom moved to the right
        jr $                                jr   $-1                        ; its $ is where the value goes
        in f,(c)                   ;IN F,(C)
                                            defb #ED,#70                    ; not in its table
        ds 3                                defb 0,0,0                      ; its DEFS leaves a hole
lab2    disp #9000                 LAB2
                                            org  #9000                      ; both addresses
                                            put  #601F                      ; the bytes back where they went
        ent                                 org  $-#9000+#601F
```

## 2. What the backend writes

| Source | PROMETHEUS text |
|---|---|
| An expression | a chain left to right with `+ - * / ?` (`ChainWriter`): `a+(b+c)` as `a+b+c`, an atom of a commuting operator moved to the right; the low byte as `?256`, the high byte as `/256`; what no chain gives (AND, OR, XOR, comparisons, an operation on two groups) as its value where known, else 0 with a warning |
| Numbers | decimal, `#` hex in capitals, `%` binary, a printable character in quotes |
| A name | up to 8 characters (a ninth is its syntax error), capitals, letters, digits and `_`, a letter first (`__x` becomes `X`); unique within those 8 |
| `$` in an instruction | `$-k`: PROMETHEUS' `$` is the address of the byte the operand's value goes into (§3) |
| `$` in `DB` / `DW` | one item a line (its `$` is the item's address) |
| `IF`, `DUP`, `WHILE`, `MACRO`, `DEFL` | decided, unrolled, expanded, written as values (it has no condition, repeat or macro) |
| `ORG` | `org x` (both addresses) |
| `DISP r` / `ENT` | `org r`, `put place` / `org $-r+place`; a place or a run address known only when PROMETHEUS assembles under names of their own (`ZPLACE equ $`, `ZRUN equ r`); a label of the line before the `org` on a line of its own |
| `DB` / `DW` / `DS` | `defb` with `defm "text"` for runs of three printable characters (the quote, control bytes and bytes above #7F as numbers), `defw`; `DS n` up to 256 bytes as `defb` zeros or its fill, larger as `defs n` with a warning (PROMETHEUS leaves those bytes as they were) |
| A comment | a line of its own before the statement (`;` in column 0; it has no comment after an instruction) |
| `IN F,(C)`, `OUT (C),0`, the undocumented copies (`RLC (IX+d),B`) | bytes (its table has the index halves and `SLIA`, not these) |
| `(IX)` | `(ix+0)` (its table has only the `(ix+d)` forms, `jp (ix)` aside) |
| `INCLUDE`, `INCBIN` | not converted (a warning): PROMETHEUS has neither |
| `END`, `SAVEBIN` | dropped (info) |

Every line goes through the codec's record encoder (`PrometheusCodec::EncodeLine`) and is written as PROMETHEUS shows
it once stored (label 9 columns, mnemonic 5, lower case); a line it cannot store (more than 63 bytes in its record)
is reported.

## 3. PROMETHEUS' `$` in an instruction

Typed into PROMETHEUS 48K and assembled at 60000 (2026-10-12):

| Line | Bytes | `$` |
|---|---|---|
| `ld hl,$` | `21 61 EA` | the instruction's address +1 |
| `ld ix,$` | `DD 21 65 EA` | +2 |
| `jr $` / `djnz $` | `18 FF` / `10 FF` | +1 |
| `ld a,$-60000` | `3E 0F` | +1 |
| `ld (ix+5),$-60000` | `DD 36 05 13` | +3 (the `n`) |
| `ld (iy+$-60000),7` | `FD 36 16 07` | +2 (the `d`) |
| `ld bc,($)` / `ld ($),a` | `ED 4B 7A EA` / `32 7D EA` | +2 / +1 |

So `$` is the address of the byte the operand's value goes into, as in a `DEFB` / `DEFW` item.
`prometheus::OperandField` gives that place: 1, or 2 after a prefix (DD / FD / ED), 2 for a displacement, and 3 for
the `n` of `ld (ix+d),n`. The backend writes `$-k`, and the frontend reads `$` as `$+k`.

The frontend had read `$` in an instruction as the instruction's address since 2026-10-08. The PROBE and the
library's label check use no `$` there, so nothing caught it. The instruction-form run below did (`DIFFER at #EEC1`,
`jr nc,$` built `30 FF`).

## 4. Writing into PROMETHEUS 48K (the injection)

The records run from the access line `#9C28` to twelve bytes below the symbol table (the word at `#87D7`), and the
table runs to the word at `#881A` (asm-synchronizer.md §7.17). The twelve bytes are six empty records (`00 30`).

The inject takes a save (records, the checksum, `FF`, the table) and writes:
- the records;
- the six empty records;
- the table;
- the two words;
- zeros over what the old text held above the new end (as its deletion does).

It also sets the active line (`varcSourceBufferActiveLine`, operand at `#826D`) and the block margins
(`varcSelectedBlockStart` / `End`, `#80E8` / `#80EB`) to `#9C28`, as CLEAR leaves them. A text whose end would reach
U-TOP (`varcUTop`'s operand, `#7946`) is refused.

The values in the table are 0 and no name is DEFINED. PROMETHEUS works them out when it assembles. The addresses
come from the reconstruction's listing (offsets from 24000) and were checked in memory.

The idle rule: its editor's key wait (`CALL #86AE` at `#8626`, called from `#7CFA`) leaves `#8629, #7CFD` on the stack.
A line or command being typed lives in the input line, out of the text. PROMETHEUS keeps no "changed" flag: a typed
text is replaced by force, or when it is the caller's own.

## 5. Results

| Check | Result |
|---|---|
| `PrometheusBackend_Test.ProgramsKeepTheirBytesThroughPrometheus` (host): every instruction form, the PROBE (60000, 60200 and the PUT at 62000), ZAsm 3.15's ZXT1-3, STORM 1.3's STORMT2, MASM 1.1's MT1, XAS 7.447 `constrct`, TASM 5.0's T50PROG → sjasmplus → PROMETHEUS (its records) → the PROMETHEUS frontend → sjasmplus | the original assemblers' bytes |
| `inject-oracle.py prometheus` (PROMETHEUS 48K in unreal-ng): `oracle1.asm`, every instruction form with its 599 labels and without, at 60000 | 143 / 1173 / 1173 bytes = sjasmplus' |
| The same saves on tape (`LOAD :name`, the tape in real time: `fasttape` / `turbotape` off), assembled with SS+A | 143 / 1173 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py prometheus --template` in PROMETHEUS 48K (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' |

The round trip also found two bugs in the PROMETHEUS frontend, now fixed:
- An `org` that ends a `put` took `$` after the `ENT`, so `$` was where the bytes went instead of the address.
- A `put` after a `put` took its address after the `ENT` as well.

An emulator bug showed on the way (not fixed here): with `fasttape` and `turbotape` on, PROMETHEUS' LOAD of a save of
2 KB or more stops with "Tape error". It reads with ROM LD-BYTES entered at `#0562`. The tape plays in real time and
the error comes at a different place each run (8.7 s, 12.3 s into a 14.9 s block). The ROM's own `LOAD "" CODE`
reads the same TAP correctly, fast or in real time. With both features off from the start, PROMETHEUS loads it.

## 6. Limits

- Only the 48K tape edition installed at 24000 has a descriptor; installed elsewhere, its addresses move.
- A source with `INCLUDE` or `INCBIN` does not convert (one text, no binaries).
- A `DS` of more than 256 bytes stays a hole.
- No idle template snapshots for PROMETHEUS yet.
