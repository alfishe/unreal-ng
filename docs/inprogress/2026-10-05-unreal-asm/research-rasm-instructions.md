# rasm: the Z80 instruction layer

What rasm (`rasm.c`, version in `scratch/wt-rasm/scratch/rasm`) does with one instruction statement, as ported to
`core/src/3rdparty/unreal-asm/src/dialects/rasm/engine/rasminstructions.{h,cpp}` (namespace `unrealasm::rasm`).
The port is byte-exact with the rasm binary; the tables the tests replay are written by rasm itself
(`tools/verification/unreal-asm/checks/rasminstrtable.py`).

## Interface

| Function | rasm |
|----------|------|
| `IsInstruction(m)` | the Z80 entries of the `instruction[]` keyword table (75 names, aliases included; `API_SEND` is a directive and excluded) |
| `EncodeInstruction(m, words, options, evaluateNow)` | `_LD`, `_ADD` ... `_SET`: bytes, deferred expressions, `ae->nop` / `ae->tick`, the first error, warnings |
| `ApplyFixup(bytes, fixup, value, address, options)` | the write rule of one expression in `PopAllExpression` |
| `InstructionTiming(m, words)` | `Encoded::nops` / `ticks` / size (TICKER, COUNTNOPS) |
| `GetNop`, `GetTick`, `GetSize`, `Duration` | `__GETNOP`, `__GETTICK`, `__GETSIZE`, `__DURATION` (a separate string parser, not the encoder) |

Additions to the interface of the brief (all additive, listed at the top of the header): `Encoded::nops` and `ticks`,
`EncoderOptions { rough, noWarning, insideLoopOrMacro }`, `ApplyFixup` with `FixupResult`, `Duration`.

## Operand words

An instruction function sees the words of rasm's preprocessor (`EarlyPrepSrc`):

- upper case outside quotes, every blank outside quotes removed: `ld a , ( ix + 5 )` gives `LD` `A` `(IX+5)`;
- split at the commas outside parentheses and quotes;
- `AF'` after a comma becomes `AF` (`EX AF,AF'` = `EX AF,AF`);
- `<<` / `>>` become `[` / `]`, quotes stay (`'a'` is 97).

rasm compares words by `GetCRC`. For up to four characters the CRC is the characters packed, so a register test is an
exact string comparison: `(HL)` matches, `( HL)` cannot occur, `(HL+0)` is not `(HL)`.

Classes the functions use:

- **(IX+d)**: `strncmp(w,"(IX",3)==0` and `w[3]` is `+`, `-` or `)`; same for `(IY`. `(IX)` is `(IX+0)`.
- **StringIsMem**: the word is one parenthesised expression: `(5)` yes, `(5)+1` no (`\` skips a character, `'`
  skips the next one unless it is `\`).
- **EnforceNoAddressingMode**: a StringIsMem word where an immediate is expected (`LD B,(5)`, `JP (5)`,
  `ADD A,(5)`, `LD (IX+1),(5)`) is an error, "Ambiguous and potentially erroneous mnemonic entry. Expecting immediate
  value instead of immediate addressing value", but the bytes are still produced. `LD A,(5)` is not affected (it is
  `LD A,(nn)`).
- **Index halves**: `IXH HX XH`, `IXL LX XL`, `IYH HY YH`, `IYL LY YL`.

Words left over after an instruction function took what it needs are skipped silently (`LD A,(IX+5),B` is not
accepted by `_LD`, which wants exactly two, but `RLC (IX+5),B,C` and `SRL8 BC,DE` are quiet: the latter emits nothing).

## Fixups

`PushExpression` reserves the operand bytes (0) and remembers where they go and what `$` is for the expression.
`Fixup::offset` is the byte index, `Fixup::instructionOffset` is `$` relative to byte 0 (rasm's codeadr after its
"$ hack": the start of the instruction).

| Kind | rasm | Bytes | `$` | Write rule (value `r = floor(v + rough)`) |
|------|------|-------|-----|-------------------------------------------|
| J8 | E_EXPRESSION_J8 | 1 | start | `r -= $ + 2`; outside -128..127: error "relative offset %d too far [%s]" (byte written anyway) |
| V8, V8At0, V8At3 | V8, 0V8, 3V8 | 1 | start | outside -128..255: warning "truncating value #%X to #%X"; low byte |
| J16, V16, V16At0, IV16 | J16, V16, 0V16, IV16 | 2 | start | outside -32768..65535: warning "truncating value #%X to #%X"; little endian |
| V32At0 | 0V32 | 4 | start | warning "truncating value" outside -2^31..2^32-1 (on the unrounded value) |
| Float24 | F24 | 3 | - | from the float bits b: `((b>>23&FF)-64)|(b>>24&80)`, `b>>7`, `b>>15` |
| IV8 | IV8 | 1 | start | `r > 32767`: `r -= 65536`; outside -128..127: error "offset modifier (IX+d) must be between -128 and 127 inclusive : current value = %d" |
| IV81 | IV81 | 1 | see below | `r + 1`, then the IV8 rule |
| Rst | RST | 1 (the opcode) | start | `#00 #08 ... #38` -> `#C7 + r`; else error "RST #0,#8,#10,#18,#20,#28,#30,#38 only", byte 0 |
| RstConditional | RSTC | 1 (the JR displacement) | start + 1 | `#38` -> `#FF`; else error "RST condition,#38 only" |
| Im | IM | 1 (after ED) | start | 0/1/2 -> `#46/#56/#5E`; else error "IM 0,1 or 2 only", byte 0 |
| BitResSet | BRS | 0 (adds to the opcode byte) | the opcode byte | 0..7: `opcode += r*8`; else error "SET,RES,BIT shift value from 0 to 7 only" |
| RealAmsdos, RealMicrosoft | 0VR, 0VRMike | 5 | - | data directives; `ApplyFixup` refuses them |

- **IV8 / IV81 expression**: rasm overwrites `IX` / `IY` in the word with `%0` (binary 0) and evaluates the word:
  `(IX+5)` is `(%0+5)`; the port gives `(0+5)`. The error text puts the register back (`Fixup::extra` holds the
  prefix `#DD` / `#FD`).
- **`$` of the second half of a pair** (`LD HL,(IX+n)` = `LD H,(IX+n+1) : LD L,(IX+n)`, also `LD (IX+n),rr`): outside
  REPEAT / WHILE / macros rasm translates the word in place at the first push, so the second expression keeps the
  first `$` (`instructionOffset` 0). Inside them (`EncoderOptions::insideLoopOrMacro`) the second gets its own `$`
  (start + 3). Only matters for `$` inside the displacement.
- **Error texts quote the translated word**: before evaluating, rasm rewrites the word in place (`$` becomes the
  decimal address, `'a'` becomes `#61`), and the J8 / IV8 messages quote that text: "relative offset 128 too far
  [256+130]", "offset modifier (IX+256) must be ...". `ApplyFixup` takes the translated text as `shownExpression`
  (the engine has it); without it the messages quote `Fixup::expression`.
- **Rounding**: `rough` is 0.5 (`floor(v+0.5)`), 0 with `-m`. `-w` (`noWarning`) drops the warnings.

## Instructions

Opcodes, the accepted operand forms and the timing counters follow `_LD` ... `_SET`; the reference is
`instructions.tsv`. Points that differ from a textbook Z80 assembler:

- **Accumulator ALU**: `ADD ADC SUB SBC CP` take `A,x` or `x`; `AND OR XOR` take only `x` (`AND A,B` is an error).
  `ADD`/`ADC`/`SBC`/`SUB` reject `IX` / `IY` as `x` ("Use ADD with A,B,C,D,E,H,L,XH,XL,YH,YL,(HL),(IX+n),(IY+n)");
  `CP AND OR XOR` treat it as an expression.
- **LD H / LD L** do not take index halves (`LD H,XH` is `LD H,n` with an unknown symbol); `LD XH,r` takes
  `B C D E A` and the halves of the same register.
- **IN / OUT**: `IN 0,(C)` and `IN F,(C)` are `ED 70`, `OUT (C),0` is `ED 71`; `IN A,n` / `OUT n,A` without
  parentheses are errors.
- **JP**: `JP HL`, `JP (HL)`, `JP IX`, `JP (IX)` are the same; `JP cc,(IX)` is an error.
- **RST cc,#38** = `JR cc,$+1` (`28 FF` for `Z`): only `NZ Z NC C`.
- **Bare RLC / RRC ...**: without an operand rasm looks at the word after the statement. RL ... SRL end in their
  final message ("syntax is RL (IX+n),reg8 or RL reg8/(HL)/(IX+n)/(IY+n)", "syntax is SLA reg8/(HL)/(IX+n)/(IY+n) or
  SLA (IX+n),reg8"); RLC / RRC have no final branch and take the next statement's mnemonic as their operand: an
  error when a statement follows, nothing at all as the last statement of a file. The port always reports "syntax is
  RLC reg8/(HL)/(IX+n)/(IY+n)". Bare `SRL8` is the same case ("syntax is SRL8 BC/DE/HL/IX/IY"). These three are the only forms where the port differs from the table.
- **Aliases**: `EXA` = `EX AF,AF'`, `SL1` = `SLL`, `SHL` = `SLA`, `SHR` = `SRL`, `OTD` = `OUTD`, `OTI` = `OUTI` (with
  the messages of the long name).

### Super-instructions

| Form | Expansion | NOPs / T |
|------|-----------|----------|
| `PUSH r1,r2...`, `POP ...`, `INC ...`, `DEC ...` | one instruction per operand | sum |
| `NOP n`, `HALT n`, `RRCA n` (n >= 0); `RLCA n`, `LDI n`, `LDD n`, `OUTI n`, `OUTD n`, `INI n`, `IND n` (n >= 1) | n copies | n times |
| `LD BC,DE` `LD BC,HL` `LD DE,BC` `LD DE,HL` `LD HL,BC` `LD HL,DE` `LD HL,HL` | two LD r,r | 2 / 8 |
| `LD BC,IX` `LD DE,IY` ... | `LD B,XH : LD C,XL` | 4 / 16 |
| `LD IX,BC` `LD IY,DE` | `LD XH,B : LD XL,C` | 4 / 16 |
| `LD HL,SP` | `LD HL,0 : ADD HL,SP` | 6 / 21 |
| `LD rr,(IX+n)` | `LD hi,(IX+n+1) : LD lo,(IX+n)` | 10 / 38 |
| `LD (IX+n),rr` | `LD (IX+n+1),hi : LD (IX+n),lo` | 10 / 38 |
| `RL rr` `RR rr` `SLA rr` `SRA rr` `SLL rr` `SRL rr` | the two CB shifts in carry order | 4 / 16 |
| `RLC rr`, `RRC rr` | four CB shifts (8-bit rotation of a 16-bit pair through carry) | 8 / 32 |
| `SRL8 BC/DE/HL` | `LD C,B : LD B,0` (and so on) | 3 / 11 |
| `SRL8 IX/IY` | `LD XL,XH : LD XH,0` | 5 / 19 |
| `RST cc,#38` | `JR cc,$+1` | 2 / 7 |
| `RES/SET n,(IX+d),r`, `RLC (IX+d),r` ... | the undocumented copy to a register | 7 / 23 |

Repetition counts are evaluated at once (`evaluateNow`, `$` = the statement); errors "RLCA <repetition> allows only
non zero positive values" (also for `RRCA -1`: rasm's copy), "LDI minimum count is 1", "HALT <repetition> must use
posivite value", "NOP <repetition> must use positive value".

## Timing

- **TICKER / COUNTNOPS** use the counters the instruction function adds: CPC NOPs (`ae->nop`) and Z80 T-states
  (`ae->tick`) per instruction, summed over super-instructions and repetitions. Conditional instructions count the
  not-taken case for RET cc (2/5), JR cc (2/7), the taken one for CALL cc / JP cc (3/10).
- **GETNOP / GETTICK / GETSIZE / DURATION** are a separate simplified parser of a string: upper case, tabs and double
  blanks collapsed, `IY` -> `IX`, every index half -> `XL`, split at `:`, the opcode up to the first blank, the
  rest without blanks. Then a table per opcode with substring tests (`EX` with `AF` or `DE` anywhere is 1 NOP ...),
  so the values differ from the encoder's: `CALL` is 5 NOPs unless the argument contains `Z` or `C` (minus 2 each),
  `DJNZ` 4, `JR` 3. `EXA` and `SRL8` are known to GETNOP only, `HALT` to GETSIZE only. Errors: "unsupported opcode
  [X] for GETNOP, see documentation about this directive", "unsupported LD A,B for GETTICK, see documentation",
  "unsupported opcode LD for GETNOP, need 2 arguments [x]" (GETTICK: "requires 2 arguments").
  rasm dereferences a missing argument of `BIT RES SET INC DEC` (and the second LD argument in GETSIZE); the port
  reports the "unsupported opcode" / "missing LD argument" error instead.
- **DURATION** is GETNOP, or GETTICK once BUILDZX set the ZX output.

## Tests

`tests/rasminstructions_test.cpp` (`RasmInstructions.*`):

- `EveryFormMatchesRasm`: every row of `testdata/rasm/instructions.tsv` (forms built from a pool of 8-bit, index-half,
  16-bit, memory, immediate and condition operands, for every mnemonic with zero, one and two operands, the LD cross
  product, three-operand BIT/RES/SET, the super-instructions, the forms of AUTOTEST_INSTRMUSTFAILED), assembled by
  rasm alone at `org #100`. Accepted forms: same bytes after the fixups, same first warning. Rejected forms: an
  error, and its text is one of rasm's (unless an operand is a name the test's small evaluator cannot compute).
- `TimingsMatchRasm`: `testdata/rasm/timings.tsv`, the TICKER counts of every accepted form and the GETNOP / GETTICK
  / GETSIZE values or errors of every form.
- Hand-written cases: super-instructions, rejected forms with their messages, fixup offsets and expressions,
  `InstructionTiming`, `Duration`.

Regenerate the tables: `tools/verification/unreal-asm/checks/rasminstrtable.py --rasm <rasm binary>`.

## Results

- `instructions.tsv`: 16728 forms, 2596 accepted by rasm, 14132 rejected. All match (bytes, first warning, error
  text), apart from the bare `RLC` / `RRC` / `SRL8` above.
- `timings.tsv`: 2699 forms. TICKER NOPs / T-states of every accepted form and GETNOP / GETTICK / GETSIZE of every
  form (values and error texts) match. Two rows where rasm crashes (`GETNOP("INC")`, `GETNOP("DEC")`: it dereferences
  the missing argument) are marked `?` and skipped.
- Forms with a tab do not fit a TSV cell; `GETNOP("ld\ta,b")` is a hand-written case.

## Open points for the engine

- Proximity labels `_+` / `_-` and every name in an expression are the engine's: the port passes the word through.
- REAL formats (Amsdos, Microsoft) belong to the data directives; `ApplyFixup` refuses them.
- `insideLoopOrMacro` must be set by the engine for statements inside REPEAT / WHILE / macro expansions.
- `Encoded::error` holds rasm's first error only (rasm may report a second one for the same statement, e.g. both
  halves of `LD HL,(IX+200)`).
