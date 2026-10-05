# unreal-asm: dialect conversion

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | IR v1 and the first plugins implemented in A5 (ALASM frontend, sjasmplus frontend and backend: [research-alasm-to-sjasmplus.md](research-alasm-to-sjasmplus.md)); the other dialects' columns stay to be researched |
| **Order** | after the codecs (decision D-4) |

## 1. Why an IR

Converting ALASM to sjasmplus, TASM to sjasmplus, STORM to pasmo and so on directly needs one converter per pair.
With a common **intermediate representation** each dialect needs one **frontend** (dialect → IR) and one **backend**
(IR → dialect); every pair then works. Shared passes (label renaming, macro expansion, number spelling) are written
once, on the IR.

## 2. The IR

The IR is **neutral** (decision D-7): it is designed from every dialect of the catalog at once and copies no
dialect's spelling. Directive kinds, label scopes, number forms and expression operators are the union of what the
dialects have; what only one dialect has gets its own node kind (or `Other` with the dialect named) rather than
being forced into another dialect's construct. The construct matrix (§4) is filled for **all** dialects before the
IR node set is frozen (phase A5).

**As built (A5):** `include/unrealasm/ir.h`. A `Program` (dialect, lines, the source's arithmetic: `expressionBits`,
`unsignedArithmetic`) holds `Line`s (label, `labelGlobal`, statements, comment, source line). A `Statement` is an
`Instruction` (lower-case mnemonic, operands), a `Directive` (a kind, arguments, operands, text, parameters), a
`MacroCall` (name, argument texts) or `Raw`. An `Operand` is a register, condition, indirect, indexed, memory,
immediate or string. An `Expr` is a number (value + spelling + digits), symbol, `$`, `$$`, unary, binary, group (the
parentheses the source wrote), memory read `{..}`, ALASM's `?label` or raw text. The node set is the union of ALASM and
sjasmplus so far; directive kinds without a counterpart in a dialect (`DEVICE`, `MODULE`, ...) are `Other` with their
text, which a backend of the same dialect writes back.

A `Program` is a list of `IrLine`s; each keeps its source position.

| Node | Holds | Example (any dialect) |
|---|---|---|
| `Label` | name, scope (global / local / temporary / module), parent for locals | `PLAY`, `.loop`, `1` (temporary) |
| `Instruction` | mnemonic (a closed Z80 set, incl. undocumented `SLI` / `SLL`, `IXH` / `IXL` / `LX` / `HX` forms normalized), operands | `LD A,(IX+5)` |
| `Operand` | register, register pair, indirect, indexed with displacement, condition, immediate `Expr` | `(IX+OFFS)` |
| `Directive` | normalized kind + args: `Org`, `Equ`, `Defl` (redefinable), `Db`, `Dw`, `Ds` (with fill), `Include`, `Incbin`, `If` / `Else` / `EndIf`, `Macro` / `EndM`, `Rept` / `EndR`, `Disp` / `Ent` (assemble for another address), `Module` / `EndModule`, `Align`, `Display` (print at assembly), `End`, `Other` (kept by name) | `ORG #8000`, `DEFB 1,2,"AB"` |
| `MacroCall` | name, argument texts | `PUSHALL` |
| `Expr` | tree: number (value + **spelling**: `#C000`, `$C000`, `0xC000`, `C000h`, `%1010`, `'A'`), symbol, current address, unary, binary (with the dialect's operator), function (`HIGH`, `LOW`), string, parentheses | `(TABLE+2)*256` |
| `Comment` | text, position (own line / end of line) | `; plays one frame` |
| `Raw` | the original line text when the frontend cannot parse it | — |

Operators with different precedence between dialects are parsed into the tree by the **frontend's** precedence and
written by the **backend** with parentheses where the target's precedence would differ.

## 3. Plugins

```mermaid
flowchart LR
    subgraph Front["frontends (dialect → IR)"]
        FA["alasm"] & FT["tasm"] & FS["storm"] & FZ["zxasm"] & FG["gens"] & FJ["sjasmplus"] & FP["pasmo"]
    end
    subgraph Pass["transforms (IR → IR)"]
        T1["labels → target rules"] --> T2["macro / repeat expansion (opt)"] --> T3["number / string spelling"] --> T4["current-address forms"]
    end
    subgraph Back["backends (IR → dialect)"]
        BJ["sjasmplus"] & BP["pasmo"] & BZ["z88dk"] & BA["alasm"] & BT["tasm"]
    end
    Front --> Pass --> Back
```

A plugin is a folder with its grammar / writer and its tests, registered in one table (open question Q-2 proposes
compiled-in modules). A backend declares **capabilities**: which IR nodes and directive kinds it writes, its label
rules (characters, length, local syntax, case), its number spellings, its reserved words.

## 4. Construct matrix (to be filled from each dialect's research)

`●` native, `R` by a rewrite rule, `X` by expansion, `–` not expressible (comment / refuse), `?` to verify.

| Construct | sjasmplus | pasmo | z88dk | ALASM | TASM | STORM | ZX-ASM | GENS |
|---|---|---|---|---|---|---|---|---|
| global labels | ● | ● | ● | ● | ● | ● | ● | ● |
| local labels | ● (`.name`) | ? | ? | ● (`LOCAL` / `ENDL` blocks, `@name` global) → R (`name__L<n>`, `.name` in macros) | ? | ? | ? | ? |
| temporary labels | ● (`1`, `1B` / `1F`) | ? | ? | ? | ? | ? | ? | ? |
| `EQU` / `DEFL` | ● / ● | ● / ? | ? | ● / ● (`label=expr`) | ● / ? | ? | ? | ● / ? |
| `DB` / `DW` / `DS` | ● | ● | ● | ● | ● (`defb`, `defw`, `defs`, `db`, `dw`, `ds` in L1's table) | ? | ? | ● |
| `INCLUDE` / `INCBIN` | ● / ● | ● / ● | ? | ● / ● | ● / ● (L1) | ? | ? | ? |
| conditionals | ● | ● | ● | ● (`IF`, `IF0`, `IFN`; `?label` → R `exist`) | ? | ? | ? | ? |
| macros | ● | ● | ● | ● (`\0`…`\9` → R named; glued or `\P \R \C \N \S` → X) | TASM 4 (`DEFMAC` / `ENDMAC`, P1) | ? | ? | ? |
| repeat blocks | ● (`DUP` / `REPT`, `WHILE`) | ● (`REPT`) | ? | ● (`DUP` / `EDUP`; `REPEAT` / `UNTIL0` → R `WHILE`) | ? | ? | ? | ? |
| assemble for another address | ● (`DISP` / `ENT`) | ? | ? | ● (`DISP`, L8) | ● (`PHASE` / `UNPHASE`, L1) | ? | ? | ? |
| modules / name spaces | ● | – | ? | ? | – | – | – | – |
| print at assembly | ● (`DISPLAY`) | ? | ? | ● (`DISPLAY`, L8) | TASM 4 (`DISPLAY`, P1) | ? | ? | ? |
| undocumented instructions | ● | ● | ● | ● (`LX` / `HX` forms) | ● (`lx`, `hx`, `ly`, `hy`, `sli`, L1) | ? | ? | ? |
| expression priorities | C-like, 32-bit signed | ? | ? | none (left to right), 16-bit unsigned → R parentheses, masks | ? | ? | ? | ? |

The `?` cells are filled by the research of each dialect (the probe source of [source-formats.md](source-formats.md)
§3 shows what the assembler accepts and what binary it builds).

## 5. Worked example (illustrative; real spellings come from the ALASM research)

An ALASM fragment, decoded:

```text
        ORG #8000
PLAY    LD HL,TABLE
.loop   LD A,(HL)
        INC HL
        DJNZ .loop
        DUP 4
        NOP
        EDUP
        DISPLAY "end: ",$
TABLE   DB 1,2,3
```

Into IR (simplified): `Org(#8000)`; `Label PLAY` + `LD HL,TABLE`; `Label .loop (parent PLAY)` + `LD A,(HL)`; ...;
`Rept(4){NOP}`; `Display("end: ", Current)`; `Label TABLE` + `Db(1,2,3)`.

Out of the sjasmplus backend: the same lines with sjasmplus spellings (`DUP 4` / `EDUP` are native there, the local
label keeps its `.` form, numbers keep `#8000` unless the option asks for `0x8000`). Out of a backend without repeat
blocks the `DUP` is expanded into four `NOP` lines and the report says so.

**The test** for this pair: ALASM in the emulator assembles the original → binary A; sjasmplus assembles the
converted text → binary B; A == B.

## 6. What cannot be converted, and how it is reported

| Case | Default | Options |
|---|---|---|
| a directive the target lacks and no rule covers | the line as a comment + `; unreal-asm: not converted (<kind>)`, reported | `--unsupported refuse` / `expand` |
| macro systems that differ (argument syntax, local labels in macros) | expand the macro at its call sites when the target cannot express it | `--macros keep` (write as is, report) |
| expressions the target evaluates differently (precedence, 16-bit vs 32-bit) | parentheses added; a value check by assembling both (test) | — |
| a line the frontend could not parse (`Raw`) | copied as a comment, reported | `--raw keep` |
| a label invalid in the target | renamed by the target's rules (collision-free), rename list in the report and the file header | — |
