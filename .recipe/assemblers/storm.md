# Recipe: STORM in unreal-ng

Run STORM Turbo Assembler (X-Trade, 1997) in the emulator: start it, load a source, assemble, save the object code,
export plain text, get the code back in BASIC, and move sources between STORM and the host. Every step was run on
unreal-ng (2026-10-07, `PENTAGON` model) with STORM 1.3; 1.3i and 1.0beta were started. The STORM language
(priorities, implied commands, packed labels) is in
[research-storm-to-sjasmplus.md](../../docs/inprogress/2026-10-05-unreal-asm/research-storm-to-sjasmplus.md), the
file format in [research-storm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-storm.md); STORM's own help
is the `STORMhlp` file on its disk (a UTF-8 copy in the collection: `software/programming/storm/docs/`). Shared steps:
[README.md](README.md).

## Versions and where they are

| Version | Image (collection: `software/programming/storm/unpacked/`) | Loader | Status here |
|---|---|---|---|
| 1.3 | `StormAssemblerV1.3/STORM_13.TRD` ([zxart](https://zxart.ee/releasefile/id:155558/StormAssemblerV1.3.trd.zip)); `STORM_13/STORM_13.SCL` ([zxart](https://zxart.ee/releasefile/id:249295/STORM_13.ZIP)) | `STORM1.3` | load, assemble, save object, export, quit to BASIC: all run |
| 1.3i | `STORM13I/STORM13I.SCL` ([zxart](https://zxart.ee/releasefile/id:249297/STORM13I.ZIP)) | `STORMimp` | starts (the 1.3 editor) |
| 1.0beta | `STORM_1B/STORM.$B` (hobeta; put it on a disk with `zxdisk.py add`) | `STORM` | starts; the editor commands listed on the right |

STORM needs 128K and TR-DOS 5.03 / 5.04. It keeps banks 0, 2 and 5 for the compiled code and swaps its own code into
48K memory while it runs: the code is in place only after **Quit** to BASIC. It compiles to `#6000-#FFFF`; `#4000-#5FFF`
is unreachable from the environment (assemble elsewhere with a displaced ORG and copy it). Return to STORM from
BASIC: `RANDOMIZE USR 23600` (RAMTOP must be 24575).

## MCP (preferred)

```text
media             {"action":"insert","slot":"A","path":"/abs/scratch/st13.trd","discard":true}
emulator_manage   {"action":"reset"}
type_input        {"action":"tap","key":"down"}                 # x4
type_input        {"action":"tap","key":"enter"}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"STORM1.3\""}}
capture_media     {"action":"screenshot","filename":"scratch/storm-started.png"}   # status line LAT BIG INS SET Line 0 Col 0 Mem 16367
```

STORM opens in its editor. **BREAK** prefixes an external command (the status line shows `ALT`), **Extend** (CS+SS)
an editor command (`EXT`). External commands (help; L, A, O, X and Q were run here): **L**oad, **S**ave, **C**at, **M**erge, save **B**lk, save **O**bj,
**Z**ap text, **Q**uit, impor**T**, e**X**port, **A**ssemble, **D**ebug (STS), **R**un. The keyboard is in BIG mode:
`type` lower case and capitals arrive.

```text
type_input {"action":"tap","key":"break"}
type_input {"action":"tap","key":"l"}                         # "Load source"
type_input {"action":"type","text":"sttest","delay_frames":6} # file STTEST
type_input {"action":"tap","key":"enter"}
type_input {"action":"tap","key":"break"}
type_input {"action":"tap","key":"a"}                         # Assemble
capture_media {"action":"screenshot","filename":"scratch/storm-assembled.png"}
```

A small program without errors assembles without a word ("you will not notice anything", the help). Errors come 20
at a time with a key between (SPACE leaves the compiler); the second pass runs only after an error-free first.
Border colours report editor errors: blue a line over 40 characters, red syntax, yellow no memory, green internal.

Save the object code (the bytes from the lowest to the highest address written) and export the text:

```text
type_input {"action":"tap","key":"break"}
type_input {"action":"tap","key":"o"}                         # "Save OBJ"
type_input {"action":"type","text":"stobj","delay_frames":6}
type_input {"action":"tap","key":"enter"}                     # STOBJ.C, start #8000, 8 bytes in the check
type_input {"action":"tap","key":"break"}
type_input {"action":"tap","key":"x"}                         # "Export text"
type_input {"action":"type","text":"stexp","delay_frames":6}
type_input {"action":"tap","key":"enter"}                     # STEXP.C: text, CR after every line
```

Quit to BASIC and read the code:

```text
type_input    {"action":"tap","key":"break"}
type_input    {"action":"tap","key":"q"}                      # "0 OK"
inspect_state {"aspects":["memory"],"address":32768,"length":8}
```

Editor keys (help): CS+1 / CS+2 scroll, CS+3 / CS+4 page, CS+5..8 cursor, CS+9 delete, CS+0 backspace, SS+W
insert / overwrite, SS+A caps lock, SS+Q / SS+E line start / end, SS+I next search, CS+ENTER mark a line, SS+ENTER
delete it, SS+SPACE Rus / Lat. Extend commands: Begin, End, blK mode, Copy, Delete, Move, uNmark, Search, neXt,
Replace, Undo, Assemble, Jump, Inspect (calculator).

A source from the host: `zxasm encode prog.txt --codec storm --version 1.3 -o prog.bin`, then
`zxdisk.py hobeta prog.bin PROG.C 0xC00B PROG.$C` (a STORM file is type `C`, start `#C00B`) and `zxdisk.py add`.
STORM's own `imporT` reads plain text from the disk (unknown lines are marked as a block and commented).

## WebAPI

`tools/verification/unreal-asm/emulator/assemble-in-emulator.py storm13 <STORM_13.trd> <source.$C> <address> <length> <out.bin> [--extra
FILE.$T ...]` runs the whole sequence (BREAK L, BREAK A, BREAK Q, the bytes read in BASIC). The calls are those of
[tasm.md](tasm.md#webapi) with `RUN "STORM1.3"` and the keys above.

## A host source into STORM (1.3, 1.3i)

Write the source while STORM's editor waits for a key (nothing typed in the cursor line); BREAK `A` then assembles it:

```text
asm_source  {"action":"sync_inject","path":"/abs/main.asm","assembler":"storm-1.3"}   # sjasmplus, converted by the STORM backend
type_input  {"action":"tap","key":"break","frames":4}
type_input  {"action":"tap","key":"a","frames":4}                                      # Assemble
```

The source is written as STORM text (STORM's priorities and postfix operators, 7-character names, conditions decided
while converting, macros expanded, a one-line DUP as `.n`:
[research-sjasmplus-to-storm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-sjasmplus-to-storm.md)) into page 6,
and STORM's own PGUP is pressed through its variables: the screen shows the new text from its first line. Refused
without force when STORM's text changed since its LOAD / SAVE (the `*` in its status) or the cursor line has typing
not yet in the text. The push watch (`sync_push_watch`) waits the same way.
`tools/verification/unreal-asm/emulator/inject-oracle.py storm <STORM_13.trd> <source.asm> 0x6000` runs the whole
check (`storm13i --boot STORMimp` for 1.3i: its Quit restarts the machine, so the code is read from its pages,
#6000-#6FFF page 7, #7000-#7FFF page 3, #8000-#BFFF page 4, #C000-#FFFF page 0).

## The text in memory

While STORM edits, the text is in RAM page 6 at `#C000`: the end pointer, the name at `#C002`, an `#FF` at `#C00A`,
the file from `#C00B` up to the `#FF` before the end pointer. BREAK `S` (Enter keeps the name) saves exactly those
bytes; the asm-synchronizer reads them without a save
([asm-sources.md](../analysis/asm-sources.md#the-source-an-assembler-holds-in-ram-asm-synchronizer)).

## Pitfalls

- **STORM 1.3 holds a file of 16369 bytes** (RAM page 6 from `#C00B`; `Mem 16367` on an empty text): a longer one is
  `File too big` and the text stays empty (measured 2026-10-19). `zxasm convert --project --to storm` cuts a longer file
  into INCLUDEd parts.
- **The code is not in memory while STORM runs**: read it after BREAK Q.
- **The export's start field** holds the text length (82 for an 82-byte text), not an address.
- A line of more than 40 characters cannot be stored (blue border): the format's limit. The backend puts the longest
  expression of a wider line under a name of its own (`S EQU (VALUE_E-VALUE_4-2)*256+#18`, `LD HL,S`); STORM's EQU
  takes no label defined further down ("Label not defined"), an instruction takes a later EQU, so the EQU goes after
  the line of the last name it holds.
- **In parentheses STORM 1.3 computes only `+ - * / \ << >>` and `~ [ ]`**: `&` `!` `|` lose their right operand
  (`1+(6&3)` = 7), `@` is dropped (`1+(0@)` = 1), a comparison is a syntax error that derails the whole pass, `(6!1)`
  stops STORM with `3 Subscript wrong`. The same in an index's `(IX+5!2)`; a memory operand's `LD HL,(5!2)` is right,
  and so are all of them outside parentheses. Checked 2026-10-19 with the lines typed in STORM's own editor (its tokens
  are the codec's byte for byte). The backend writes such an expression as its value.
- **`DB "A"`**: STORM 1.3's first pass counts it as two bytes, so the labels after it come out too high (its help
  lists it among the bugs); write the character's number.
- **STORM 1.3i** loses bytes of a large assembly (about 500 lines and more): the code read after BREAK Q is wrong
  after the first few bytes, whether the text came from its LOAD or an inject. STORM 1.3 builds the same text right.
