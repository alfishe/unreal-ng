# unreal-asm: sjasmplus → Laser Genius (part B, Z4)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/lasergenius/lasergeniusbackend.cpp` (the `lasergenius` backend, on `RetroWriter`), `src/sync/reader.cpp` (`LaserGenius`: idle rule, the `Linear` inject of the paragraphs and `FF FF`) |
| **Rules from** | research-laser-genius.md §4 (the manual, appendix D, probes assembled by Laser Genius 1.04 in unreal-ng) |
| **Checks** | `unreal-asm-tests` (`LaserGeniusBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, LG's own expression probe, ZAsm, STORM, MASM, XAS and TASM 5.0 programs; `SyncInject_Test.LaserGeniusTakesItsParagraphsInItsEditor`); `tools/verification/unreal-asm/emulator/inject-oracle.py lasergenius` |

## 1. Example first

```text
sjasmplus                          Laser Genius 1.04
        org #6900                          ORG #6900
                                           PUT #6900          ; without PUT nothing is stored
lab:    ld hl,(lab+1)*2            LAB: LD HL,[LAB+1]*2      ; [ ] group, C-like priorities
        ld a,high lab                      LD A,LAB>>8
        ld hl,(1<2)+lab                    LD HL,-[1<2]+LAB   ; its true is 1, sjasmplus' -1
        ld a,~5                            LD A,^5
        ld a,ixh                           DB #DD,#7C         ; no undocumented instructions
        if ext == 5                        COND -[EXT?=5]
        disp #9000                         ORG #9000          ; the address only, the bytes go on
        ent                                ORG $-#9000+#6912
```

## 2. What the backend writes

| Source | Laser Genius text |
|---|---|
| An expression | its own priorities (`&& \|\|` 1, `& \| ^` 2, `?= !=` 3, `< > <= >=` 4, `<< >> @< @>` 5, `+ -` 6, `* / %` 7, prefix 8) with `[ ]` where the source's order needs them; `~` as `^`, `==` as `?=`; a comparison or logical operation negated (true 1, sjasmplus -1) unless the source keeps LG's own truth; a second prefix sign in `[ ]` (`--` / `++` are Phoenix tokens); a division only by a known divisor below `#8000` (LG divides wrong by `#8000` and up), else the value |
| Numbers | decimal, `#` with 2 or 4 hex digits (binary as hex), a printable character in quotes |
| A label | up to 16 significant characters, `LABEL: instruction` (a colon: without it LG takes a command) |
| `IF` | `COND` / `ELSE` / `ENDC` when the condition is known only when LG assembles, else decided |
| `DUP`, `WHILE`, `MACRO`, `DEFL` | unrolled, expanded, written as values |
| `ORG` | `ORG x` + `PUT x` (ORG sets the address, PUT where the bytes go) |
| `DISP r` / `ENT` | `ORG r` / `ORG $-r+place`; a place or a run address known only when LG assembles under names of its own (`ZPLACE: EQU $`, `ZRUN: EQU r`) |
| `DB` / `DW` / `DS` | `DB` with printable runs quoted (quote, `\`, control bytes as numbers), `DW`, `DS n` (a fill as `DB` items) |
| `INCLUDE` / `INCBIN` | `*INCLUDE "name"`; no INCBIN (a warning) |
| the undocumented instructions, `IN F,(C)`, `OUT (C),0`, `SLL` | bytes (the halves through PROMETHEUS' instruction table) |
| `END`, `SAVEBIN` | dropped (info) |

The round trip back (the LG frontend) showed three bugs, now fixed: a comparison inside `[ ]` lost its sjasmplus truth
(`[1<2&#FFFF]<3`: the sjasmplus backend froze binary operands in the source's truth, not groups); `ORG $-r+place`
under a PUT took `$` after the ENT that ends the displacement; and a PUT took the address after it ended the PUT
before (`$` then is where the bytes went, not the address).

## 3. Writing into Laser Genius 1.04 (the injection)

The text in memory is the paragraphs and `FF FF` from the start word `(#A975)` (`#7F2E`; `set space` moves it) to the
end word `(#A973)` (asm-synchronizer.md §7.16). Right after it the editor keeps its screen lines: `FF`, the lines,
`FF` at `(#A9DC)`; the words `#A9D4`-`#A9DE` point into them, and the space ends at `(#A971)` (`buffer` in `stats`).
LOAD moves the screen lines behind the text it reads. The inject does the same: it drops a disk file's `#AF` header,
writes the paragraphs and `FF FF` from `(#A975)` and the end word after them, moves the screen lines behind them with
their six pointers (`LinearParams::followLastAt` / `followPointers`), and refuses a text that does not fit up to
`(#A971)` (`limitAt`). Without the move the editor took every later line as a statement ("opcode expected" for
`list`). The idle rule: its key wait (`CALL #B071`) returns to `#AB04`, then to the ROM's `#1303`; an assembler error
waits under `#AB0C`, so assembling is not idle. A paragraph being typed stays out of the text until ENTER. LG keeps no
"changed" flag: a loaded text is replaced by force or when it is the caller's own.

### 3.1 A Laser Genius bug: (IX- / (IY- operands

Laser Genius 1.04 builds its operand tokens `(IX-` / `(IY-` with a plus: `RLC (IY-4)` gives `FD CB 04 06`, `LD
A,(IX-4)` `DD 7E 04`. `LD (IY-3),A` is right: LD and its first operand are one token. `(IY+-4)` is right too (typed into LG 1.04 and assembled,
2026-10-12). The backend writes every displacement below 0 as `(IX+-d)`. The frontend gives a source that uses
`(IX-d)` elsewhere the bytes LG builds (`+d`, with a warning).

## 4. Results

| Check | Result |
|---|---|
| `LaserGeniusBackend_Test.ProgramsKeepTheirBytesThroughLaserGenius` (host): every instruction form, LG's EXPR probe, ZAsm 3.15's ZXT1-3, STORM 1.3's STORMT2, MASM 1.1's MT1, XAS 7.447 `constrct`, TASM 5.0's T50PROG → sjasmplus → Laser Genius → the LG frontend → sjasmplus | the original assemblers' bytes |
| Laser Genius 1.04 in unreal-ng, the converted files loaded from disk (`load "NAME"`), assembled with `assem`: `oracle1.asm` and every instruction form at `#6900` (without the labels: its name table does not hold 599 names, nor its 8K space the labelled text) | 143 / 1173 bytes = sjasmplus' |
| `inject-oracle.py lasergenius`: the same written in its editor, assembled with `assem` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py lasergenius --template` in Laser Genius 1.04 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' (found on the way, each measured: its binary `+ - *` stop the assembly with `** overflow **` outside 0..#FFFF, `2-3`, `#FFFF+1`, `256*256`; a shift by 0 gives 0, `4<<0`; `3-(-5)` was written `3--5`, Phoenix' decrement, now `3-[-5]`; those operations are written as their values) |

## 5. Limits

- Laser Genius 1.04 with the hash extensions takes the same rule (2026-10-17): only the start word differs (`#737E`), the end word is `(#A973)` in both.
- LG stores code between `#68D2` and `#7F22` (its `stats`). Its text space is 8K by default (`set space` gives more
  text space and less for names), and its name table holds a few hundred names: larger sources do not fit.
- A source with `INCBIN` does not convert.
- No idle template snapshots for Laser Genius yet.
