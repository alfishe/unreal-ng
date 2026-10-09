# unreal-asm: XAS → sjasmplus

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Code** | `src/dialects/xas/xasfrontend.cpp` (the `xas` frontend), the sjasmplus backend (16-bit unsigned words, `DS` patterns) |
| **Syntax source** | the help files of XAS 4.18 (`Xas help`, the same file in ZX Format #4's appendix) and 7.43 (`Read Me`), the editor's token table (research-xas.md §4), and XAS itself: every fact marked *run* below was assembled by XAS 7.447 (and 4.18 where noted) in unreal-ng. The full description the help files mention was not found |
| **Oracles** | three programs written for the test (`testdata/dialects/xas7447/constrct`, `proj` with the files it loads, `testdata/dialects/xas418/cons418`), assembled by XAS 7.447 / 4.18 over memory filled with #AA; their sjasmplus conversions build the same bytes |
| **Checks** | `unreal-asm-tests` (`XasFrontend_Test`; with `UNREAL_ASM_SJASMPLUS` the oracle programs too); `tools/verification/unreal-asm/checks/roundtrip.py --assemble` on the collection's XAS disks; how to run XAS: `.recipe/assemblers/xas.md` |

## 1. Example first

```text
XAS 7.447                          sjasmplus
        DW    2+3*4                DW (2+3)*4                    ; left to right: 20
        DW    1+LAB&H              DW high (1+LAB)               ; postfix: on the value so far
        DW    LAB'L                DW (((LAB&#FFFF)<<1|...)      ; rotate the word left by one bit
        EX    AF,AF                EX AF,AF'
        OUT   PORT,A               OUT (PORT),A
        PUSH  HL,IX,AF             PUSH HL / PUSH IX / PUSH AF
        DS    5,#AB12              DUP 2 / DB low #AB12,high #AB12 / EDUP / DB low #AB12
        "XY"                       DB 'XY'                       ; a string in the command place
        WORK  #9000                __xas_work=#9000-$ / DISP #9000
        ORG   #6100                ENT / ORG #6100 / DISP #6100+__xas_work   ; ORG keeps the offset
        !ASSM 3 ... !CONT          DUP 3 ... EDUP
        IFZ   X ... !CONT          IF (X)==0 ... ENDIF
        LTEXT "inc"                INCLUDE "inc.asm"
        LCODE "dat2.Z"             INCBIN "dat2.Z"
```

## 2. XAS facts the conversion relies on

| Fact | Conversion |
|---|---|
| One statement per line; the label in column 0 (text before the command), the command a keyword token (research-xas.md §5) | the decoded line's fields |
| Labels compare on their **first 7 characters**, without case (*run*, 4.18 and 7.447: `abcdefgh` and `abcdefgx` are the same label, `Label Exists`) | every name is replaced by the spelling of the first definition with the same 7 characters |
| Numbers: decimal, `#FF` / `.FF` hex (the editor stores `.` as `#`), `%101` binary, `"A"` a character, `"AB"` = #4142; no `H` suffix (`12H`: `Syntax In Number`), 65536 is 0 (*run*) | numbers masked to 16 bits |
| Operators `+ - * / !` (XOR), **left to right without priorities**, on unsigned 16-bit words (`2+3*4` = 20, `0-7/2` = #7FFC, `65535+2` = 1, `#10*#10` = #100; *run*) | `expressionBits = 16`, unsigned; the backend writes the parentheses |
| Postfix `&L` / `&H` (low / high byte) and `'L` / `'R` (rotate the word by one bit) act on the value so far: `lab+six&L` = low(lab+six), `lab'L'L` rotates twice (*run*; 4.18 has none of them) | `low`, `high`, the backend's 16-bit rotation |
| No parentheses in expressions, no unary minus, no `&` other than `&L` / `&H`, no `?` (*run*: `Syntax`, the value 0) | the line stays text with a warning |
| A division by 0 gives 0 without an error (*run*) | `x/0` written as 0 |
| `$` in a `DB` / `DW` list is the address of that item (`DW six-lab,lab/six,lab/0,$` gives #604D, the fourth word's address; *run*) | a list using `$` becomes one statement per item |
| `DB` takes strings from 7.432 (help); a one-character string is a byte; `DB """` is the quote; a string in the command place is `DM` (help, *run*) | strings of any other length are `DB` strings |
| `DS n` writes n zeros; `DS n,w` repeats the **word** w (low, high) and cuts at n bytes (`DS 5,#AB12` = 12 AB 12 AB 12); a third operand is ignored (*run*) | the cyclic `DS` pattern `low w, high w` |
| `WORK address`: the labels count from the address while the code stays where it is; `WORK` without an address ends it; `ENT` does not; `ORG` under `WORK` moves both addresses and keeps the offset (`ORG #6010` after `WORK #9000` at #6000 gives #9010; *run*) | `DISP`; the offset in `__xas_work`, so an `ORG` under `WORK` becomes `ENT` / `ORG` / `DISP org+__xas_work` |
| `ENT` names the start address for Run (help: no parameter) | a comment |
| `!ASSM n` ... `!CONT` repeats n times, `!ASSM !ON` assembles once, `!ASSM !OFF` skips; 7.43 / 7.447 add `IFNZ x` / `IFZ x` ... `!CONT` (*run*) | `DUP` / nothing / `IF 0`, `IF (x)!=0` / `IF (x)==0` |
| **One block level**: a block opened inside another is ignored and the next `!CONT` ends the outer one (`!ASSM 2` / `IFNZ 0` / `DB 6` / `!CONT` / `DB 7` / `!CONT` gives 06 06 07; *run*) | the inner opening becomes a comment with a warning. XAS's own object pointer then runs one byte behind (seen only through a later `WORK` + `ORG`): the oracle programs leave the construct out |
| `PUSH` / `POP` with several registers (5.05 on), `EX AF,AF` = `EX AF,AF'`, `(IX)` = `(IX+0)`, `HX` / `LX` / `HY` / `LY`, `IN A,port` / `OUT port,A` without parentheses (7.43 help; 4.18: `Fatal Error`) | spelled out |
| `LTEXT "file"` (`LOADTEXT` in 4.x / 5.x) assembles another source, `LCODE "file"` loads a code file (type C, or `"name.T"`, `"B:name"` another drive; 7.22 help) (*run*) | `INCLUDE` / `INCBIN` with the name as `zxasm convert` writes the files |
| `USEL "library"` `:proc` ... (7.x) and `MAKE` (7.43 / 7.447) | kept as text with a warning (meaning not checked) |

## 3. Results

| Check | Result |
|---|---|
| `constrct` (XAS 7.447): every construct of §2 but the nested block, #110 bytes | equal |
| `cons418` (XAS 4.18): the constructs 4.18 has (DEFB / DEFW / DEFM / DEFS, `!ASSM !ON` / `!OFF`, `WORK` + `ORG`) | equal |
| `proj` (XAS 7.447): `LTEXT`, `LCODE "dat"`, `LCODE "dat2.Z"` | equal |
| Labels of `constrct` laid out by `SymbolsFromProject` against sjasmplus 1.24's `--sym` of the conversion | 8 of 8 equal |
| `roundtrip.py --assemble` on the collection's XAS disks (Chaos Constructions 2000 intros, Oberon #5, SNG #2 appendix, ZX Navigator 1.3, ZXN): 9 sources | 0 round-trip differences; 4 assemble; the others need what their disks lack: `LCODE "SIN_CUT"` and `LCODE "DEB13"` are not on them, and three ZX Navigator modules use the kernel's labels without loading the kernel |

## 4. XAS 9.10

Run in unreal-ng (2026-10-07, `XAS9_10.SCL`, `RUN "XAS9.10"`):

- **The command line** opens with Caps Shift + Symbol Shift held longer than 7.447 needs: 12 frames
  (`keyboard/combo` with `frames: 12`; the 4 frames 7.447 takes type letters into the text). The line shows `>Edit`;
  `A` assembles ("Label Table Made, Last Address", "Object Length").
- **The file list** at start shows only files whose catalog start field holds `AS` (and 0 in the length field), as
  XAS writes them; a file without it is not listed. `zxasm encode --codec xas` now writes that header. A Pentagon
  reset does not clear RAM and XAS then reopens the text it finds there instead of the list: start a fresh machine.
- **Strings in expressions**: a string of several characters in an operand of a command or a `DW` item puts all
  but its last character into the code as bytes before it and stands for its last character: `LD HL,"AB"` is `41 21
  42 00`, `DW "AB"` is `41 42 00`, `LD BC,"XY"+1` is `58 01 5A 00`; one character and numbers behave as in 7.447
  (testdata `dialects/xas910/s9`, equal through the conversion). Its first pass does not count those bytes: labels
  after such a line are that many bytes lower than the code (seen on `constrct` re-encoded for 9.10: `w2` under
  `WORK` one higher than the layout). The frontend (codec version `9.10`) writes the bytes and warns; the label
  offset is not reproduced.
- Apart from that, `constrct` re-encoded for 9.10 (without `IFNZ` / `IFZ`, which 9.10's table lacks) assembles in
  9.10 to the bytes of its conversion.

## 5. Open

- XAS 9.07m (its tables match 9.10 apart from the dot words; not run).
- `USEL` libraries and `MAKE` (not established: no source uses them, no help text found unpacked; 9.10's "XAS help"
  is packed inside its BASIC file).
- How ZX Navigator's modules got the kernel's labels (XAS's two texts, `aNother`?).
