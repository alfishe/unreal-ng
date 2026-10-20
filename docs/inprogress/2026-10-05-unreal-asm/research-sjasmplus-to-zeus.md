# unreal-asm: sjasmplus → ZEUS (part B, Z4)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/zeus/zeusbackend.cpp` (the `zeus` backend: 1983, `gg`, `pht` for v7.E, `1.1`), `ChainWriter`, `src/sync/reader.cpp` (`Zeus1983`: idle rule, the `Linear` inject with an end marker) |
| **Rules from** | research-zeus-to-sjasmplus.md §2 (the ZEUS manual, four ZEUS versions run in unreal-ng), ZEUS 1983 run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`ZeusBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, ZAsm, STORM, MASM, XAS and TASM 5.0 programs; `SyncInject_Test.Zeus1983TakesAFileAtItsPromptAsOLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py zeus1983` |

## 1. Example first

```text
sjasmplus                          ZEUS 1983
lab_x:  ld hl,lab_x+2+3            LABX LD HL,LABX+2+3     ; labels: letters and digits
        ld a,low lab_x              LD A,LABX&255
        ld a,high lab_x             LD A,96                ; no division: the value
        ld a,5 | 3                  LD A,5!3               ; ! is OR
        ld a,"A"                    LD A,"A                ; "c is a character
        ld a,ixh                    DEFB #DD,#7C           ; no undocumented instructions
        DISP #9000                  ORG #9000 / DISP #D00F ; DISP is an offset from ORG
        ENT                         DISP 0 / ORG $+#D00F
        db "text",0                 DEFM /text/ / DEFB 0
end:                               END EQU $              ; a label alone is an error
```

## 2. What the backend writes

| Source | ZEUS text |
|---|---|
| An expression | a chain left to right of `+ - & !` (`!` OR; v7.E also `*`; its `/` rounds, so it is not used), each operator taking an atom (`ChainWriter`); `low` as `&255`, `-x` as `0-x`, `~x` as `#FFFF-x`; anything else (a high byte, XOR, a product before v7.E, a comparison) as its value from the source's layout |
| A label | letters and digits, a letter first, up to 14 characters, case counts (manual 5.1.1; ZEUS+ 3.1: 6, a seventh is its error 1); a label alone on a line as `NAME EQU $` (ZEUS 1983 gives error 0 otherwise) |
| `IF`, `DUP`, `WHILE`, `MACRO`, `DEFL` | decided, unrolled, expanded, written as values (ZEUS has none of them) |
| `DB` / `DW` / `DS` | `DEFB`, `DEFM` with a delimiter the text lacks, `DEFW`; `DS` as `DEFB` (ZEUS 1983 / GG's `DEFS` leaves memory as it was; v7.E's writes zeros) |
| `DISP r` / `ENT` | `ORG r` / `DISP place-r` (an offset), `DISP 0` / `ORG $+offset`; a place known only when ZEUS assembles under names of its own (`ZPLACE EQU $`, `ZRUN EQU r`) |
| `INCLUDE` / `INCBIN` | v7.E `INCLUDE name` / `PLACE name`, GG `INCBIN "name"`; ZEUS 1983 has neither (a warning) |
| the undocumented instructions | bytes (the halves through PROMETHEUS' instruction table) |
| Lines | the label, a blank, the instruction; a line without a label starts with a blank |

## 3. Writing into ZEUS 1983 (the injection)

ZEUS keeps its records from a start word to an end word that holds the address of the last `FF` (`#FD00`, `#FD02`
for ZEUS 1983 at 57344; asm-synchronizer.md §7.14). `o addr` makes the text at addr current; the inject writes the
file (its `FF FF` included) from the start word and the end word at the last `FF` (`PlanInjectLinear` with the end
marker). The idle rule: at its prompt its line input returns through `#F6E5`, `#E0D5`; a line being typed stays out
of the text. ZEUS keeps no "changed" flag: a loaded text is replaced by force or when it is the caller's own.

## 4. Results

| Check | Result |
|---|---|
| `ZeusBackend_Test.ProgramsKeepTheirBytesThroughZeus` (host): every instruction form, ZAsm 3.15's ZXT1-3, STORM 1.3's STORMT2, MASM 1.1's MT1, XAS 7.447 `constrct`, TASM 5.0's T50PROG → sjasmplus → ZEUS 1983 → the ZEUS codec and back → the ZEUS frontend → sjasmplus | the original assemblers' bytes |
| ZEUS 1983 in unreal-ng, the converted files at 32768 (`o 32768`): `oracle1.asm` and every instruction form at `#6000` | 143 / 1173 bytes = sjasmplus' |
| `inject-oracle.py zeus1983`: the same written at its prompt, assembled with `A` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py zeus --template` in ZEUS 1983, `zeus7e` in ZEUS v7.E (code at #C000: v7.E lies low; seeds 76-85 too) (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' (found on the way: v7.E's `*` stops at the first carry as Primus' does, `35*#2000` is #1800: written as its value) |

## 4.1 Primus Assembler 2.9 (2026-10-11)

The same backend writes Primus 2.9 text (`--to-version primus`, codec version `primus`): `+ - * / ? & !` left to
right (`/` unsigned and truncating, so `high` is `/256`; `?` the remainder, `!` OR), characters as numbers (Primus
refuses `"A"`), labels of up to 16 characters, `DISK name` for an include. `?` after a name is part of the name to
Primus (`COUNT?7`: an undefined label, seen in unreal-ng): `+0` ends the name first. Primus takes one `ORG` (a second
is "Duplicate ORG": not converted) and has no `DISP`. It keeps its text from `#8254` with the start word at `#61B1`
and the end word at the last `FF` at `#61B3` (asm-synchronizer.md §7.15); the inject goes in at its prompt (its key
wait returns through `#6857` / `#6858`, then `#6836` / `#6851`; not while it assembles), by force over the manual it
loads at start (no "changed" flag). `A out` assembles and saves the code as `out`: Primus 2.9 built `oracle1.asm` and
every instruction form to sjasmplus' bytes (`inject-oracle.py primus`).

Its `*` (measured on 84 products, 2026-10-19) takes the right operand's bits from the top (`r*2`, `+a` at a 1) and
stops at the first carry out of 16 bits with the value so far: `3*#7FFF` is #7FFA, `2420*#FFFF` #1B98, `#8001*x` 2.
ZEUS v7.E multiplies the same way, ZAsm 3.15 with the operands the other way round (`z80::StopAtCarryProduct`). A
product that differs from the 16-bit one is written as its value; `expression-fuzz.py primus --template` (seeds 71-85,
`--media` its disk) then gave sjasmplus' words in all 1500 lines (93 of 100 before).

## 5. Limits

- ZEUS 1983, GG, 1.1, v7.E and Primus have inject rules; each assembled the injected `oracle1.asm` to sjasmplus'
  bytes (GG, 1.1, v7.E 2026-10-13). The backend's versions: `gg`, `pht` (v7.E) and `1.1` (v7.E's table without `*`:
  ZEUS 1.1 gives error 0 on it); GG and PHT's versions take `DB DW DM DS` only (`DEFB` is error 0 there). v7.E keeps
  its text from `#6000` up: give sources above it.
- A source with `INCBIN` / `INCLUDE` does not convert for ZEUS 1983.
- No idle template snapshots for ZEUS yet.
