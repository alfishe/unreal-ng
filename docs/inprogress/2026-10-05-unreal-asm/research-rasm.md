# unreal-asm: rasm language specification (engine, IR, backend)

| | |
|---|---|
| **Date** | 2026-11-17 |
| **Tool** | rasm v3.3.3 (Roudoudou / Edouard Berge), `rasm.c` 35768 lines, commit `a5796b9` |
| **Source of truth** | `/Volumes/TB4-4Tb/Projects/emulators/rasm/rasm.c` (function names and line numbers below refer to it) |
| **Probes** | `scratch/wt-rasm/scratch/rasm/probe/` (`p.sh name [options] < src` assembles, prints messages and `xxd` of `name.bin`); every rule marked "probe" was run on the built binary |
| **Docs** | `rasm/resources/md/*.md` (French) and `github/rasm-doc/src/*.tex`; where they disagree with the binary the binary wins (e.g. `REPEAT_COUNTER`/`WHILE_COUNTER` are documented but do not exist in 3.3.3, §6.3) |
| **Purpose** | implementation-grade spec for a C++ rasm engine (byte for byte, labels equal), the rasm to IR front end and the IR to rasm backend |

Conventions: `w` = a *word* of rasm's word list (§1.4); hex is written `#XX` like rasm; "CLI" = the `rasm` command line,
"flux" = the library entry points `RasmAssemble*` used by the autotests (they differ in rounding and precedence, §3.2, §12).

## 0. Architecture of rasm in one page

rasm is **one pass over a pre-tokenised word list** plus a **deferred expression list**:

1. `PreProcessing` (l. 28012) reads the file (`_internal_readtextfile`), runs `EarlyPrepSrc` (l. 27725: upper case, comments,
   line joins, `<<`/`>>`), expands `INCLUDE`/`INC*` textually and in place, then splits everything into a flat array of
   words (`struct s_wordlist`: `w` text, `t` = 0 word followed by more words of the same statement / 1 last word of a
   statement / 2 end of list, `e` = offset+1 of the first `=`,`<`,`>`,`!` or `~` inside the word, `l` line, `ifile`).
2. `Assemble` (l. 24848) walks the word list once (`ae->idx`). Statement = first word + its operand words. The first
   word is (in this order) a directive/mnemonic from `instruction[]` (l. 24467), a macro name, or else a label; a
   first word with `e!=0` is an expression statement (`x=1`, `x equ 1`, `x+=1`).
3. Instruction operands, data, `RUN`, ... are **not evaluated** when met: `PushExpression` (l. 9497) records
   (word, output position, `$`, bank, ORG zone, crunched section, module) and leaves the bytes; variables (`=`) and `$`
   are **substituted textually at push time** (`ExpressionFastTranslate`, l. 8975). Labels and EQU aliases are looked
   up at the very end by `PopAllExpression` (l. 10407), after crunched sections were closed (§8).
4. Directives that need a value now (`ORG`, `IF`, `REPEAT n`, `DEFS n`, `ALIGN`, `=`, `PRINT`, `ASSERT`, ...) evaluate
   immediately with whatever is known: **a label defined later is an error there** (probe `f1`, table §3.8).
5. After the walk: ORG overlap check, crunch the LZ sections and relocate, pop all expressions, run delayed
   `SUMMEM`/`XORMEM`/`DELAYED_PRINT`/`SAVE`, then write outputs (binary / snapshot / tape / cartridge / disk).

Consequences a re-implementation must keep: one pass (no second pass, no fixpoint), textual substitution of variables
(with `%lf` formatting, §3.5), `$` captured at push time, alias (EQU) = text macro wrapped in parentheses.

## 1. Reading, preprocessing, lexing

### 1.1 File reading (`_internal_readtextfile`, l. 1849)

| Rule | Detail |
|---|---|
| Bytes | read raw, no decoding; no BOM handling (a UTF-8 BOM is an invalid char, §1.4) |
| Lines | split on LF (`#0A`); the LF stays at the end of the line and later becomes `:` (statement end) |
| CR | every `#0D` anywhere is replaced by `:` (so CRLF ends with `::`, and an old-Mac CR-only file is one line whose statements are separated by `:`; line numbers in messages are then all 1) |
| Last line | a `:` is appended to the last line (l. 28546) |
| Tabs | turned into a blank by `EarlyPrepSrc` outside quotes; inside quotes a tab stays a tab (`#09`) |
| Inline | `-inline 'text'` assembles a string (one line, file name `inline string`) |
| Main file not found | tries extensions `.asm .z80 .inc .src .dam .mxm .txt` (both cases), `.o .s .O .S`, then the `-I` paths |

### 1.2 `EarlyPrepSrc` (l. 27725) — per file, before includes are expanded

Left to right over each line, outside quotes:

| Input | Effect |
|---|---|
| `a`-`z` | upper-cased. **Everything outside quotes is upper case**: labels, macro names, parameters, directives are case-insensitive (`-ec` only restores the original case in exports, §10) |
| `'...'` `"..."` | skipped verbatim; `\x` inside skips the next char (so `"\""` is one string). Unclosed quote at end of line: error *Quote opened but not closed before the end of the line* |
| `\` outside quotes | skips the next char; if it is the last non-blank char of the line (trailing blanks/CR ignored) the next line is **joined** (its leading blanks/tabs removed). If both the line end (before `\`) and the next line start with the same quote char, the two strings are fused (`"abc"\`+`"def"` = `"abcdef"`) |
| `;` | comment to end of line: the chars are overwritten with `:` (harmless empty statements) |
| `//` | same as `;` |
| `/* ... */` | comment, may span lines; replaced by blanks (same line) or the lines are emptied. Unclosed: error *opened comment to the end of the file*. Not nestable |
| `<<` | becomes `[ ` (one token `[` = shift left) |
| `>>` | becomes `] ` (`]` = shift right). So `[` and `]` **are** the shift operators in expressions too (`1[2` = 4) |
| `AF'` | if the `'` follows `AF` (or `F` directly after `A`) and a `,` precedes `AF` (blanks allowed), the `'` is replaced by a blank: `ex af,af'` works, `ex af',af` opens a quote (probe `l3`: three errors) |

Case: quoted text keeps its case (`defb "a"` = `#61`). `ld a,'a'` = `#61` too (the quote is evaluated, §3.3).

### 1.3 Include expansion and quote scan (`PreProcessing` l. 28556-29020)

A second scan over the joined listing (still outside quotes, with `\` skipping a char):

* LF/CR become `:`.
* `&` becomes `#` when `-amper` is given (so `&` is hex prefix and the AND operator must be written `and`); inside quotes
  `&` is untouched (autotest `AUTOTEST_AMPER`).
* `INCLUDE "f"` / `READ "f"` and the binary includes (`INCBIN`, `INCL48`, `INCL49`, `INCLZ4`, `INCZX7`, `INCZX0`, `INCZX0B`,
  `INCEXO`, `INCAPU`, `INCLZSA1`, `INCLZSA2`, `INCWAV`) are recognised **only at the start of a word** (`ival==0`) and
  only if followed by blank or quote and a quoted file name on the same line. Text includes are spliced in place
  (file name resolution §9.6); binary includes are rewritten to the internal directive `HEXBIN #n` (index into
  `ae->hexbin`). A missing text include becomes `ERRRD n` (error raised when executed, so a missing include inside a
  false `IF` is no error). Include happens **before** assembly: an `INCLUDE` inside `IF 0` is still read (its text
  is then skipped), and an include inside a `REPEAT` is expanded once and repeated.
* `FREEQUOTES` (a word) switches the `-fq` behaviour on for the rest of the source.
* `MACRO name p1 p2` and `STRUCT name p` without a comma after the name get a comma inserted (so both
  `macro name,p1,p2` and `macro name p1 p2` work). The macro name is also stored in a "fast macro" list used by the
  word splitter below (macro names are known lexically for the whole file, even before their definition).

### 1.4 Word splitter (`PreProcessing` l. 29021-29540)

The character classes (`Automate[]`, l. 28490):

| Class | Chars | Meaning |
|---|---|---|
| regular | `@A-Z0-9.=_($)][+-*/^%#|&'"\m}{` and NUL | part of a word |
| separator | blank, tab, `,` | ends a word |
| end of statement | `:` (and the former LF/CR) | ends word and statement (`t=1` on the last word) |
| operator | `= < > !` | part of the word, marks it as an expression (`e`) |
| invalid | anything else (`~`, `?`, `` ` ``, bytes >= 128 outside quotes, ...) | fatal error *invalid char*, rasm **exits** |

Rules, in order:

1. After the first word of a statement is a **known directive/mnemonic or a macro name** (fast tables), blanks and tabs
   stop being separators for the rest of the statement: `ld a , ( ix + 5 )` gives the words `LD`, `A`, `(IX+5)`
   (blanks are dropped, probe `l2`). Only commas then separate operands.
2. Inside parentheses the comma is a regular char: `defb (1,2)` is one word.
3. A word that is not a keyword (a label) keeps blanks as separators: `lab nop` = label `LAB` + statement `NOP`.
4. Operator assignment with blanks `x + = 1` is glued back into `X+=1`; `x = 5` becomes the single word `X=5`
   (the previous word is merged when the operator starts a word and the previous word is not a keyword).
   An expression statement cannot start with `=`,`<`,`>`,`!` (error).
5. ` EQU ` after a word: `name equ expr` becomes the single word `NAME~EXPR` (alias definition, §2.7).
6. After a keyword, the Maxam word operators are rewritten when delimited by blanks: ` MOD ` -> `m`, ` AND ` -> `&`,
   ` OR ` -> `|`, ` XOR ` -> `^`, and `%` standing alone -> `m` (modulo). They are recognised only as blank-delimited
   words after a keyword (`defb 4 mod 3` = 1; `x=4 mod 3` also works because the word started with `=`, probe `e1`).
7. Quotes: the quote is copied with its delimiters into the word. A `\` escapes the next char. Empty quote: warning
   *Quote is empty*. A quote cannot span lines. Without `-fq`/`FREEQUOTES`/`-utf8`, a char outside `#20-#7E` (tab
   allowed) inside quotes is an error *Invalid char for quoted string ... use -fq*. With `-fq` bytes are kept raw
   (probe `u1`: `"été"` -> `C3 A9 74 C3 A9`). With `-utf8` a fixed table maps 2-byte UTF-8 to CPC chars
   (`ù`=124 `é`=123 `è`=125 `à`=64 `Ñ`=161 `ñ`=171 `¿`=174 `¡`=175, `ç` -> `\\`); others are errors (probe: `"été"` -> `7B 74 7B`).
8. Word `SAVE` followed directly by a quote: the quote starts a new word.
9. Empty parameter `defb 5,,5` is an error *empty parameter right after word* (unless `-mml`).

Two extra words are injected at the start: `BEGIN` and the expressions `SYNCHRO~256` and `RASM_VERSION~"3.3.3"`-ish
(`RASM_VERSION` = `PROGRAM_VERSION_FLOAT`) — i.e. `RASM_VERSION` and `SYNCHRO` are predefined aliases.

### 1.5 Statement classification (`Assemble` main loop l. 25093-25323)

For each statement start word `w`:

1. Conditional skipping (§6.1) first.
2. If `w.e` (it contains `=`/`<`/`>`/`!`/`~`): expression statement -> `ComputeExpression` (variable assignment or
   alias definition; a pure comparison as a statement is evaluated and discarded).
3. Else if `w` is in `instruction[]` (exact, case already upper): run it. **Mnemonics and directives are reserved**:
   a label named `NOP` is impossible; `nop` in column 0 is the instruction.
4. Else if `w` is a macro (defined *at this point of the assembly*) -> expand (§6.6).
5. Else: `PushLabel` -> `w` is a label definition, wherever it is (column 0 or indented). The rest of the statement
   (next words) is a new statement: `lab nop`, `lab: nop`, `lab:: nop` are equivalent (`:` is just a separator, probe `l4`).

There is no "column 0 means label" rule and no colon rule. A misspelt mnemonic becomes a label followed by
operands-as-statements (often an error *Invalid char in label declaration* or *... not found*).

Macro use before definition: the lexer knows the name (rule 1.4.1 applies) but at assembly time step 4 fails and the
word becomes a label.

## 2. Symbols

Four kinds live in four tables, and a name may be in only one of them (errors on collision):

| Kind | Created by | Value | Lookup in expressions |
|---|---|---|---|
| variable ("dico") | `x=expr`, `x+=...`, `REPEAT n,x`, `WHILE c,x`, `TICKER`, `LET`, `-Dx=v`, `ENUM`? (no: ENUM makes aliases) | double, changes over time | **substituted textually when the expression is pushed** (`%lf`, §3.5) |
| label | a non-keyword statement word, `STRUCT` fields, `-l` import | int address (+ bank, ORG zone, LZ section) | at final evaluation (deferred) |
| alias | `name equ expr` | the text `(expr)` with variables and `$` substituted at definition | textual replacement, recursive |
| macro | `MACRO` | word list | statement head only |

Lookup order in `ComputeExpressionCore` (l. 6960-7340): number literal, `$`/`$$`, function name (only if followed by
`(`), variable, label (module-prefixed first, then plain), alias (module-prefixed first), `STRUCTNAME`+digits array
index, else error *keyword [X] not found in variables, labels or aliases*.
Registers and directive names in an expression give *cannot use register/directive ... in this context*.

### 2.1 Label syntax (`PushLabel` l. 10723)

* First char `.A-Z_@`, then `0-9.A-Z_@{}` (`{...}` = generated part, §2.5). Upper-cased, so `Main` = `MAIN`.
* Reserved, error *Cannot use reserved word*: one-letter `A B C D E F H L I R`; `AF BC DE HL IX IY SP LX HX XL XH LY HY
  YL YH`; `IXL IYL IXH IYH`. (`IXH` etc. are reserved but `IX.HIGH`... is handled in macros, §6.6.)
* A label whose name is hex digits followed by `H` (`ABH`, `FFH`, `BEH`, `0CH`) can be defined but is **never
  reachable**: in an expression the number interpretation wins (probe `l1`: `ABH: ld a,ABH` = `3E AB`).
* Duplicate label: error *Duplicate label [X] - previously defined in [file:line]*. A label colliding with a variable
  or alias is an error, and vice versa.
* Value = `codeadr` (logical `$`) at the definition; bank = current bank; LZ section = current crunched section.

### 2.2 Global labels and proximity labels (`.x`)

`ae->lastgloballabel` holds the last global label name (without module prefix).

* Global definition `NAME` sets `lastgloballabel=NAME`.
* `.X` defined -> label `NAME.X` (pure concatenation: `lastgloballabel` + `.X`); with no previous global label the
  label is just `.X`. A proximity label does **not** change `lastgloballabel`.
* `.X` used in an expression is rewritten to `lastgloballabel.X` **at push time** (FastTranslate), so it refers to the
  global label current at the point of use, not at final evaluation (`START.LOOP`, probe `l1`).
* Full name access: `NAME.X` from anywhere.
* `lastgloballabel` is saved on entry to `REPEAT`/`WHILE`/each macro expansion and restored on exit
  (`PushGlobal`/`PopGlobal`, l. 19881): a global label defined inside a loop or macro does not stay the proximity
  reference after it (probe `r2`: `.after` after a loop -> `GLOB.AFTER`).
* A local label `@x` (inside loop/macro) **does** become the reference: `.p` after `@a` is `@AR0R2.P` (probe `r2`).
* `-dams`: a leading `.` is removed instead (`.grouik` defines `GROUIK`).
* `{BANK}.x` etc. work (FastTranslate l. 9330).

### 2.3 Local labels `@x` (`MakeLocalLabel` l. 4460)

Only inside `REPEAT`/`WHILE` or a macro expansion (`ae->ir || ae->iw || ae->imacro`; note `imacro` = number of
*defined* macros, so once any macro exists an `@x` outside loops goes through `MakeLocalLabel` and gets no suffix, i.e.
stays `@X`). Outside, `@X` is an ordinary global label (and sets `lastgloballabel`).

Name = `@X` + suffix, suffix built as:

```
if in a REPEAT:  "R" hex(id of the innermost REPEAT)        id = global REPEAT counter, 0,1,2... per REPEAT *execution*
for each REPEAT level outer->inner: "R" hex(cpt)            cpt = remaining iterations (counts DOWN: n, n-1, ... 1);
                                                            REPEAT..UNTIL: -1,-2,... printed as FFFFFFFF, FFFFFFFE
if in a WHILE:   "W" hex(id of the innermost WHILE)
for each WHILE level outer->inner: "W" hex(cpt)             cpt = 0,1,2... (counts UP)
if in a macro expansion: "M" hex(macro invocation counter)  counter increments on every macro call (global)
then the ".suffix" part of "@X.Y" if any
```

Probe `r2` (outer `repeat 2`, inner `repeat 2`, then an empty `repeat 2`, then `repeat ... until`, `while`, macro):

```
@AR0R2 @AR0R2.P @BR1R2R2 @BR1R2R1 @AR0R1 @AR0R1.P @BR2R1R2 @BR2R1R1
@UR4RFFFFFFFF @UR4RFFFFFFFE @WW0W0 @WW0W1 @MM0 @MM1
```

Each executed `REPEAT`/`WHILE` statement takes a new id, also when it is nested and re-executed (inner repeat got id 1
then 2); a zero-count `REPEAT 0` is skipped without taking an id. An `@x` *use* is resolved with the same suffix at
push time, so a use refers to the label of the same iteration/expansion. `@x` in a struct: error.

### 2.4 Anonymous labels `_`

* A statement word that is exactly `_` defines `LPR<n>OX`, `n` = 0,1,2,... (`ae->curProxIndex`).
* `_+` / `_-` are allowed **only as the whole operand of a jump** (`JR`, `DJNZ`, `JP`, `CALL`: expression types J8/J16):
  `_-` = last `_` defined before, `_+` = next one. `_-` before any `_`: error. In `ld hl,_-` it is not supported (it
  would be parsed as an expression).

Probe `l1`: `_ nop / jr _- / jr _+ / _ nop` -> `00 18 FD 18 00 00`.

### 2.5 Generated names `{expr}` (`TranslateTag` l. 4592)

In a label definition, an alias name, a variable name or a symbol reference, `{expr}` is replaced by the value of
`expr` (evaluated now, integer, decimal, `%d` of the rounded value):
`repeat 3,i : lab{i} nop : rend` defines `LAB1 LAB2 LAB3`. Inside `PRINT` strings too. Nested braces allowed. In
expressions a word starting with `{` must be one of the prefixes below.

### 2.6 Prefixes `{BANK}` `{PAGE}` `{PAGESET}` `{SIZEOF}` (l. 7000-7300)

| Prefix | Value |
|---|---|
| `{BANK}label` | bank number of the label (`ibank`); in snapshot mode for a ROM-space bank > 260: the ROM number it was mapped to |
| `{PAGE}label` | CPC gate array value `ae->bankgate[ibank]` (`#7FC4+...`, banks 0-3 -> `#7FC0`); with `BANKSET` the value for `(bank&~3)+(ptr>>14)`; error in cartridge mode |
| `{PAGESET}label` | `ae->setgate[ibank]` (`#7FC2`-style RAM config of the 64K set); error in cartridge mode |
| `{SIZEOF}name` | size of a struct, a struct field (`STRUCT.FIELD`) or a struct instance |
| `{BANK}$` `{PAGE}$` `{PAGESET}$` | current bank (cartridge: `activebank` < 32; snapshot: `activebank` or `activebank+(codeadr>>14)` in a bankset); in a plain binary build `{BANK}$` gives **-1** (tagvalue left at -1) |

Labels in the temporary (default) space have bank `BANK_MAX_NUMBER` (= 260); `{PAGE}` on them is an error.
ZX usage (§7.6): banks are the 128K pages 0-7.

### 2.7 EQU aliases vs `=` variables vs labels

| | `x equ e` (alias) | `x = e` (variable) | `x` label |
|---|---|---|---|
| when evaluated | each use, textually `(e)` | now, double | address now |
| forward refs inside `e` | allowed (text) | **error** | — |
| redefinition | error *Duplicate alias* | allowed, any number | error |
| contains `$` | `$` replaced by the address **at the EQU** | `$` now | — |
| contains variables | substituted **at the EQU** (probe `l2`: `al equ v1+1` then `v1=100`: `al`=15 still) | — | — |
| self reference | error *Alias are not variables, use = instead of EQU* | `x=x+1` fine | — |
| can be math fn/register name | error | first char must be `A-Z_` | — |

`x+=e`, `-=`, `*=`, `/=` (double), `|=`, `&=`, `^=`, `%=` (int `(int)` truncation of both sides), `<<=`, `>>=` (as `[=`/`]=`,
written without blanks `x<<=1`: probe `l2`). Operator assignment on a non-existing variable: error.

`UNDEF name` removes a variable (`__UNDEF` l. 21867 -> `DelDico`); it does not remove labels/aliases.
`LET x=e` = `x=e` (`__LET` l. 20451).

### 2.8 Modules (`__MODULE` l. 24223)

`MODULE name` / `ENDMODULE`: every label and alias defined afterwards that does not start with `@` is named
`NAME` + separator + label, separator `_` by default (`-msep c` sets another char). Proximity labels: `MOD_GLOB.X`
(the global part used for `.x` is the unprefixed name). Lookup from code inside a module tries `MODULE_name` first,
then `name` (so code inside a module can reach globals). Variables are not prefixed. `MODULE` without name = end of
module.

### 2.9 Export control

`NOEXPORT` / `ENOEXPORT` (l. 18440-18445) and `LABEL LOCAL|GLOBAL ...` (`__LABEL`, `__internal_LOCAL`/`EXPORT`) set
`autorise_export`/`local_export` flags used only by the symbol export (§10). `NOEXPORT` alone = stop exporting until
`ENOEXPORT`; with names = those names are not exported.

## 3. Expressions

Evaluator: `ComputeExpressionCore` (l. 6290-8970, shunting-yard over `s_compute_element`), entry points
`ComputeExpression` / `RoundComputeExpression(Core)`; textual pre-substitution `ExpressionFastTranslate` (l. 8975).
Deferred evaluation: `PushExpression` (l. 9497) stores the word plus the current `codeadr`/`outputadr`/`nop`/bank
context; `PopAllExpression` (l. 10407) evaluates all of them once the source is fully read.

### 3.1 When a value is known (one pass!)

rasm has a single pass. What is "known" at evaluation time:

| Context | Evaluated | Forward label |
|---------|-----------|---------------|
| instruction operands, `DEFB/DEFW/DEFI/DEFR/DEFF/DF24/STR`, `DEFS` fill value | at end (`PopAllExpression`) | OK |
| `EQU` (alias) | text captured at definition, `$` and variables substituted then, labels resolved at use | OK |
| `DELAYED_PRINT` | at end | OK |
| `x = e`, `IF`, `REPEAT n`, `WHILE`, `UNTIL`, `ORG`, `DEFS n` (count), `ALIGN`, `ASSERT`, `PRINT`, `BANK n`, `CHARSET`, `INCBIN` offsets, `SAVE` args, `STRUCT` sizes | immediately | **error** "expression [...] cannot use undefined label" (probe `f1`) |

At push time the expression text is translated with `fullreplace=1`: **variables and `$` are replaced by their
current textual value**, labels stay symbolic. So `v=1 : defb v : v=2` emits `01`, and a deferred operand never
sees later variable values.

Variables are substituted with `sprintf("%lf")` + `TrimFloatingPointString`: **6 decimal digits**. `v=1/3 : defb v*300`
gives `0.333333*300 = 99.9999` -> CLI rounding -> `64` (100). Same for `{EVAL}` macro args.

### 3.2 Final integer conversion (rounding mode)

Expressions compute in `double`. The final integer is `(long)floor(v + rough)`:

| Mode | `rough` | `ld a,2.5` | `ld a,-1.5` | `ld a,-2.5` |
|------|---------|------------|-------------|-------------|
| CLI default (`param.rough=0.5`) | 0.5 | `3E 03` | `3E FF` | `3E FE` |
| CLI `-m` (Maxam; also sets `ae->maxam=1`) | 0.0 | `3E 02` | `3E FE` | |
| library / flux `RasmAssemble(...,NULL param)` and **all built-in autotests** | 0.0 | `3E 02` | | |

Range checks after rounding (`PopAllExpression`):

| Expression type | Used by | Accepted | Out of range |
|-----------------|---------|----------|--------------|
| `E_EXPRESSION_V8` / `0V8` | `ld a,n`, `DEFB` | -128..255 silently; otherwise low byte | warning "truncating value #X to #Y" (`ld a,256` -> `00`, `ld a,-129` -> `7F`) |
| `V16` / `0V16` | `ld bc,nn`, `DEFW`, `jp nn` | -32768..65535 | warning, low 16 bits (`ld bc,65536` -> `00 00`) |
| `0V32` | `DEFI` | +-2^32 | warning |
| `J8` | `jr`, `djnz` | `r-ptr-2` in -128..127 | **error** "relative offset N too far" (`jr $-127` error, `jr $+129` = `18 7F`) |
| `IV8` / `IV81` | `(ix+d)` / second byte of 16-bit `(ix+d)` pairs (d+1) | value > 32767 is reduced by 65536 first (`(ix+#FF80)` = -128), then -128..127 | **error** "offset modifier ... must be between -128 and 127" |
| `RST` | `rst n` | 0,8,#10..#38 -> `C7|n` | error |
| `RSTC` | `rst cc,#38` | only #38 -> `FF` (after `20/28/30/38` prefix byte) | error |
| `IM` | `im n` | 0/1/2 -> `46/56/5E` after `ED` | error "IM 0,1 or 2 only" |
| `BRS` | `bit/set/res n` | 0..7, ORed into opcode | error |
| `0VR` / `0VRMike` / `F24` | `DEFR` / `DEFF` / `DF24` | float encoders (§5.3) | |

### 3.3 Operators and precedence

`ComputeExpressionCore` priorities (smaller binds tighter; an operator pops while top priority <= its own, so all
binary operators are **left associative**):

| Prio (rasm) | Operators | Semantics (rasm mode) |
|-------------|-----------|-----------------------|
| 0 | unary `-`, unary `+`, functions, `( )` | |
| 1 | `!` (unary logical not) | `!x` = `floor(x+0.5)==0` -> `!0.4`=1, `!0.6`=0 |
| 2 | `*` `/` `\` `MOD` `%` | `*`,`/` double; `/0` = error, result 0; `\` = `(int)a/(int)b` (C truncation): `-7\2`=-3; `MOD`/`%` on `floor(x+.5)` operands (C `%`: `-7 mod 3` = -1) |
| 3 | `+` `-` | double |
| 4 | `<<` `>>` (lexed into `[` `]`, §1.2) | operands rounded `floor(x+.5)`; count >31 or < -31 -> warning, result 0 |
| 5 | `<` `>` `<=` `>=` `==` `!=` `<>` | doubles; `==` true when `fabs(a-b)<1E-6` (`1.0000001==1` and `0.1+0.2==0.3` are true), `!=`/`<>` exact; result 1/0 |
| 6 | `&` `AND` | rounded ints |
| 7 | `^` `XOR` | rounded ints |
| 8 | `|` `OR` | rounded ints |
| 9 | `&&` | |
| 10 | `||` | |

Word operators `AND OR XOR MOD` are only operators when blank-delimited (the splitter rewrites ` AND ` to `&`, §1.4).
Note `<<` binds looser than `+` (C rules): `1 + 2 << 1` = 6. `3>2>1` = `(1)>1` = 0. `3--2` = 5. `-(-3)` = 3.

Probe `t1` bytes: `defb 7/2,7\2,-7/2,-7\2,7.6\2,7 mod 3,-7 mod 3,7.5%2,2.5&3,2.5|0,1.5<<1,5.5>>1,!0.4,!0.6`
-> `04 03 FD FD 03 01 FF 00 03 03 04 03 01 00` (CLI rounding of the final `/` result: 3.5 -> 4, -3.5 -> -3 = FD).

Single `=` inside an expression: rasm mode -> error "meaningless use of an expression" (`IF a=1`) / "cannot set
variable inside an expression" (`defb a=1`). Use `==`.

**Maxam mode** (`-m`, or `ae->maxam`, `-ass`/`-dams` style options do not change it): every arithmetic/bitwise operator
has the same priority 464 (strict left to right: `1+2*3` = 9), comparisons 664, `&&`/`||` 6128; single `=` is a
comparison; `!` -> error "invalid computing state"; every operator works on `(int)` values masked `& 0xFFFF`
(`ae->as80` mode: `0xFFFFFFFF`), so `7.9+0` = 7; divide/modulo mask operands first (`-7/2` -> `#7FFC` + truncation
warning, `-3 mod 256` = `FD`); shifts are not masked (`-1>>1` = `FF` in DEFB).

### 3.4 Primary terms

Lookup order for an identifier-like token (`ComputeExpressionCore`, l. 7000-7600):

1. number literal (table below) — **before** symbol lookup, so a label `ABH` is unreachable: `ABH` is hex #AB
2. `$` = `ae->codeadr` at push time after the "`$` hack" of `PushExpression` (l. 9540-9556): the operand's own
   address minus the assumed opcode length of its type (V8/V16/J8/J16/IM -1, IV8/IV81/IV16 -2, 3V8 -3), i.e. the
   **instruction start** for instructions (`jp $` = `C3 10 00` at #10, `ld (ix+$),$` = `DD 36 10 10`), but the
   **address of each item** in data lists (types 0V8/0V16/0V32: `org #10 : defb $,$,$+0 : defw $,$` =
   `10 11 12 13 00 15 00`). `DEFS n,$`: the fill text is translated once at the DEFS start in plain code (all bytes
   equal) but re-translated per byte inside REPEAT/WHILE/macro bodies (`ae->ir||ae->iw||ae->imacro` branch):
   `org #10 : defs 3,$` = `10 10 10`; the same inside `repeat 1` at #13 = `13 14 15` (probes `d2`, `d3`).
   Inside the `ORG` expression itself `$` = `outputadr`. `$$` = `outputadr` (physical write address): after
   `org #8000,#100` -> `defw $,$$` = `00 80 02 01`.
3. function name followed by `(` (§3.5)
4. variable (dictionary)
5. label: `MODULE_name` first when in a module, then `name`; proximity `.x` already expanded at push
6. EQU alias (expanded textually as `(expr)`, recursively)
7. `STRUCTNAME` + digits suffix of a struct array: `ptr + size*idx` (warning if idx >= count)
8. otherwise error "expression [...] keyword [...] not found in variables, labels or aliases"

| Literal | Form | Notes |
|---------|------|-------|
| decimal | `123`, `1.5` | `atof`; **no exponent**: `1e2` is an error; `.5` is a proximity label, not a number |
| hex | `#FF`, `$FF`, `0xFF`, `FFh`/`0FFh` | letter-first `FFH` accepted when all chars are hex |
| `&FF` | only with `-amper` (else `&` = AND) | |
| binary | `%1010`, `0b1010`, `1010b` | `%` is binary only if followed by 0/1 and then operator/end, otherwise modulo; `12B` is an error |
| octal | `@17`, `0o17` | `17o`/`17q` are errors |
| char | `'A'`, `"A"` | one char -> current CHARSET mapped value; escapes `\n \t \r \0 \b \v \f`; in DEFB a quoted `'\x'` is the `\xHH` escape (needs two hex digits) |
| string | `"AB"` in an expression | **silently 0** (`ld hl,"AB"` = `21 00 00`, `defb "AB"+1` = `01`); strings only produce bytes as whole DEFB/STR items |

Predefined aliases: `SYNCHRO` (256, CPC), `RASM_VERSION`. Built-in variables: none (`REPEAT_COUNTER`,
`WHILE_COUNTER` from the docs **do not exist** in v3.3.3, probe `r1` errors).

### 3.5 Functions (`math_keyword[]` l. 1484, `__*` helpers)

Arguments are comma separated inside parentheses. Trigonometry in **degrees** (internal pi = 3.1415926545).
Probe `fn1` results (CLI rounding):

| Function | Result | Probe |
|----------|--------|-------|
| `HI(x)` / `LO(x)` | `(round(x)&0xFF00)>>8` / `round(x)&0xFF` | `hi(#1234)`=12, `lo`=34 |
| `INT(x)` | `floor(x+0.5)` (round, not truncate!) | `int(2.5)`=3 |
| `FLOOR CEIL FRAC ABS SQRT LN LOG10 EXP` | C libm | `floor(-.5)`=-1, `ceil(1.1)`=2, `frac(2.75)`=.75 |
| `SIN COS ASIN ACOS ATAN ATAN2(y,x)` | degrees | `sin(90)*10`=10 |
| `POW(a,b) POW2(x)=2^x FMOD(a,b)=FREMAIN HYPOT LDEXP(a,n) FDIM(a,b)=max(a-b,0) FMAX FMIN` | libm | `pow2(4)`=16, `fmod(7,3)`=1 |
| `FSTEP(a,b)` | `a<=b ? 1 : 0` | `fstep(1,2)`=1 |
| `CLAMP(v,lo,hi)`, `LERP(a,b,t)`=a+t*(b-a) | | `clamp(5,1,3)`=3, `lerp(0,10,.5)`=5 |
| `ISGREATER(a,b)` `ISLESS(a,b)` | 1/0 | |
| `RND(n)` | `rand()%n` (not reproducible across libc) | |
| `PEEK(addr)` | byte already emitted at `addr` in the current memory space, at evaluation time (end of pass for deferred) | |
| `FILESIZE("f")`, `FILEBYTE("f",offset)` | file size / byte (§9.4) | `filesize`=6, `filebyte(,2)`=`43` |
| `IS_REGISTER("hl")` | 1/0 (`IsRegister`) | |
| `GETNOP("ops")` `GETTICK("ops")` `DURATION("ops")` | CPC NOPs / Z80 T-states of a `:`-separated instruction string; `DURATION` = GETTICK with `-zx`/`BUILDZX`, else GETNOP (l. 6138) | `getnop("ld a,(ix+1):nop")`=6, `gettick(...)`=23, `getnop("ldir")`=5, `gettick("ldir")`=16 |
| `GETSIZE("ops")` | **approximate** byte size (simplified parser, l. 5741): `getsize("ld bc,de")`=3 (real 2), `getsize("ld hl,(ix+1)")`=3 (real 6) | |
| `COUNTNOPS("lbl1","lbl2"|"$")` | `lbl2.nop - lbl1.nop` using the running NOP counter; `"$"` = counter **at evaluation time**: in `DEFB` (deferred) that is the end of the source (probe: 2 NOPs between, result 5) | |
| `GETR GETV/GETG GETB(#GRB)`, `SETR SETV/SETG SETB(n)` | CPC+ palette nibbles; SET clamps to 15 | `getr(#123)`=2, `setr(3)`=#30 |
| `SOFT2HARD_INK/S2H_INK`, `HARD2SOFT_INK/H2S_INK`, `PSGVALUE` | CPC tables | `s2h_ink(0)`=#54 |

In Maxam mode most functions are `(int)` + mask variants and `INT` is the identity.

Mode switches recap (`RasmAssemble` l. 28096-28134): `ae->rough=param->rough`; **`ae->maxam = (rough==0)`**. CLI
default rough 0.5 -> rasm precedence + round-half-up; `-m` rough 0 -> Maxam precedence + floor. A library call with a
zeroed `s_parameter` (`RasmAssembleInfoParam`, rough 0) is also Maxam mode, but `RasmAssemble`/`RasmAssembleInfo`
with a `NULL` param leave `ae` calloc-zeroed: **floor rounding with rasm precedence** — the mode of nearly all
built-in autotests (§12). `-ass` = `as80=1` (AS80 compatibility: 32-bit masks in Maxam mode, macro params without
braces), `-uz` = `as80=2` (UZ80). `-dams` is parsed but does nothing in 3.3.3 (`.label` handling still keyed on
`ae->dams`).

## 4. Instruction set

Every mnemonic is a C function `_XXX` dispatched by CRC from `instruction[]` (l. 24467); operands are words already
split on commas (`wl[idx+1..]`, `t==1` marks the last word of the statement). Register names are matched textually
after upper-casing; `IXH/HX/XH`, `IXL/LX/XL`, `IYH/HY/YH`, `IYL/LY/YL` are synonyms. Operand classes:

- memory operand = the whole word is wrapped in one pair of parentheses (`StringIsMem`): `ld a,(n)` = `3A 12 00`,
  `ld a,(n)+1` = `3E 13` (immediate), `ld a,[n]` = shift operator, never memory.
- `(IX)` = `(IX+0)`; `(IX+e)`/`(IX-e)` any expression after the sign; offset checked as `IV8` (§3.2).
- each instruction adds its CPC NOP count to `ae->nop` and its T-states to `ae->tick` inline in its `_XXX` function
  (used by `TICKER`, `GETNOP`, `COUNTNOPS`).

### 4.1 Standard Z80 (byte exact, probes `each.sh`, `n equ #12`, `nn equ #1234`)

All documented Z80 instructions with the Zilog encodings. Forms worth pinning:

| Source | Bytes | Note |
|--------|-------|------|
| `ld a,(ix)` | `DD 7E 00` | implicit 0 |
| `ld (ix),n` | `DD 36 00 12` | |
| `ld (ix+n),n` | `DD 36 12 12` | |
| `ld a,(ix+#FF80)` | `DD 7E 80` | >32767 -> -65536 |
| `ld a,(ix+128)` | error | |
| `ld a,r` / `ld r,a` / `ld a,i` / `ld i,a` | `ED 5F` / `ED 4F` / `ED 57` / `ED 47` | |
| `ld sp,(nn)` / `ld (nn),bc` / `ld (nn),ix` | `ED 7B 34 12` / `ED 43 34 12` / `DD 22 34 12` | `ld (nn),hl` uses `22` (not `ED 63`) |
| `ex af,af` / `ex af,af'` / `exa` | `08` | `ex af',af` error (§1.2) |
| `ex hl,de` / `ex de,hl` | `EB` | `ex de,ix` error |
| `ex (sp),hl` / `ex hl,(sp)` / `ex ix,(sp)` | `E3` / `E3` / `DD E3` | both orders |
| `jp hl` / `jp (hl)` / `jp ix` / `jp (iy)` | `E9` / `E9` / `DD E9` / `FD E9` | `jp (ix+0)` error |
| `jr $` / `djnz $` | `18 FE` / `10 FE` | `jr pe,x` error |
| `rst 56` / `rst #38` / `rst 8` | `FF` / `FF` / `CF` | `rst 7` error |
| `im 0/1/2` | `ED 46/56/5E` | |
| `in a,(n)` / `in a,(c)` / `in b,(c)` | `DB 12` / `ED 78` / `ED 40` | |
| `in f,(c)` / `in 0,(c)` | `ED 70` | `in (c)` error |
| `out (c),0` | `ED 71` | `out (n),b` error |
| `sll c` / `sl1 c` | `CB 31` | `sli`, `slia` not recognized |
| `add ix,hl` | error | `ADD IX,[BC,DE,IX,SP]` |

ALU with explicit `a,`: only `ADD ADC SUB SBC CP` accept `x a,src` (`sub a,b` = `90`, `cp a,n` = `FE 12`);
**`and a,b`, `or a,b`, `xor a,n` are errors**. `sub hl,bc` is an error (no 16-bit SUB). `adc hl,de` = `ED 5A`,
`sbc hl,sp` = `ED 72`.

### 4.2 Undocumented Z80 (always enabled, no option to disable)

| Source | Bytes |
|--------|-------|
| `ld a,ixh` / `ld xh,xl` | `DD 7C` / `DD 65` |
| `ld xh,yl`, `ld h,ixh`, `ld ixh,h`, `ld xl,(ix+1)`, `ld (ix+n),xh` | errors |
| `in f,(c)`, `out (c),0` | `ED 70`, `ED 71` |
| `sll r` / `sl1 r` | `CB 30+r` |
| `rlc (ix+n),b` (result copied to reg) | `DD CB 12 00` |
| `rr (iy+n),a` | `FD CB 12 1F` |
| `sll (ix+n),e` / `srl (ix+n),l` | `DD CB 12 33` / `DD CB 12 3D` |
| `set 1,(ix+n),c` / `res 0,(iy+n),a` | `DD CB 12 C9` / `FD CB 12 87` |
| `bit 7,(ix+n),b` | error "there is no syntax BIT n,(IX+n),reg8" |

### 4.3 rasm "super instructions" (pseudo ops expanding to several opcodes)

| Source | Expansion | Bytes |
|--------|-----------|-------|
| `ld bc,de` (any of BC/DE/HL pairs) | `ld b,d : ld c,e` | `42 4B` |
| `ld bc,ix` / `ld de,ix` / `ld ix,bc` / `ld iy,de` | via IXH/IXL | `DD 44 DD 4D` / `DD 54 DD 5D` / `DD 60 DD 69` / `FD 62 FD 6B` |
| `ld hl,ix`, `ld iy,ix`, `ld bc,sp` | errors | |
| `ld hl,sp` | `ld hl,0 : add hl,sp` | `21 00 00 39` (flags!) |
| `ld hl,(ix+n)` / `ld hl,(ix)` | `ld h,(ix+n+1) : ld l,(ix+n)` — **high byte first** | `DD 66 13 DD 6E 12` / `DD 66 01 DD 6E 00` |
| `ld (ix+n),de` | `ld (ix+n+1),d : ld (ix+n),e` | `DD 72 13 DD 73 12` |
| `ld bc,(iy+n)`, `ld (iy+n),bc` | same pattern | |
| `ld (hl),bc`, `ld bc,(hl)` | errors | |
| `push bc,de,hl` / `pop af,ix` | one opcode per item, left to right | `C5 D5 E5` / `F1 DD E1` |
| `inc bc,de` / `inc a,b,c` / `dec ...` | list | `03 13` / `3C 04 0C` |
| `rl de` / `rr bc` | `rl e : rl d` / `rr b : rr c` | `CB 13 CB 12` / `CB 18 CB 19` |
| `sla hl` / `sll bc` | `sla l : rl h` / `sll c : rl b` | `CB 25 CB 14` / `CB 31 CB 10` |
| `sra de` / `srl hl` | `sra d : rr e` / `srl h : rr l` | `CB 2A CB 1B` / `CB 3C CB 1D` |
| `rlc bc` (16-bit rotate) | `sla b : rl c : rr b : rlc b` | `CB 20 CB 11 CB 18 CB 00` |
| `rrc hl` | `srl h : rr l : rl h : rrc h` | `CB 3C CB 1D CB 14 CB 0C` |
| `srl8 bc` / `srl8 ix` | `ld c,b : ld b,0` / `ld xl,xh : ld xh,0` | `48 06 00` / `DD 6C DD 26 00` |
| `shl r` / `shr r` | = `sla r` / `srl r` | `CB 20` / `CB 3F` |
| `rst z,#38` / `rst nz,#38` (also `c`, `nc`) | `jr cc,$+1` into the `FF` byte itself | `28 FF` / `20 FF` |
| `nop n` | n x `00`, `nop 0` = nothing, negative error | |
| `ldi n` `ldd n` `outi n` `outd n` `ini n` `ind n` `rlca n` `rrca n` `halt n` | repeated n times | `ldi 2` = `ED A0 ED A0` |
| `ldir 2`, `neg 2`, `rla 2`, `di 2`, `ei 2`, `exx 2`, `cpl 2`, `daa 2`, `scf 2` | errors "does not need parameter" | |
| `oti` / `otd` / `exa` | `outi` / `outd` / `ex af,af` | `ED A3` / `ED AB` / `08` |

No Z80N, R800, GBZ80 or 8080 instruction sets exist in rasm 3.3.3 (grep finds none).

## 5. Data directives

Keywords (instruction table l. 24467): `DEFB DB DEFM DM` = `_DEFB`; `DEFW DW`; `DEFI` (32-bit); `DEFS DS`; `STR`;
`DEFR DR` (Amstrad REAL); `DEFF DF` (Microsoft 40-bit float); `DF24`. **Not keywords**: `BYTE`, `WORD`, `DD`,
`DEFD`, `DEFL`, `BLOCK` — they parse as labels and the operand then fails ("Invalid first char in label
declaration"). `END` is accepted as a plain label named `END` (no effect).

Every item of DEFB/DEFW/DEFI/DEFR/DEFF/DF24 is pushed as a deferred expression (§3.1); forward labels are fine;
`$` is the item's own address. Inside a STRUCT definition (`ae->getstruct`) a directive without operands emits zeros
of its size (DEFB 1, DEFW 2, DEFI 4, DEFR/DEFF 5, DF24 3).

### 5.1 DEFB strings (`_DEFB` l. 13914)

An item that is a quote (`'...'` or `"..."`, `StringIsQuote`) emits one byte per char, others are `0V8` expressions.

| In quote | Byte |
|----------|------|
| plain char | `charset[c]` (CHARSET mapping), or UTF8REMAP code when `UTF8REMAP` entries exist and c > 126 (warning if no entry matches) |
| `\\` | `charset['\\']` (the only escape that goes through CHARSET) |
| `\e` `\a` `\b` `\v` `\f` `\0` `\r` `\n` `\t` | 1B 07 08 0B 0C 00 0D 0A 09 (no CHARSET) |
| `\xHH` | the byte HH; exactly 2 hex digits else error "DEFB invalid escaped hexadecimal value" |
| `\` + other | that char literally (no CHARSET): `'\''` = `27` |

Probe `d1`: `charset 'a','z' : charset 'bc',1 : defb "abc",'a'+0,"\\"` -> `7A 01 02 7A 5C` (`'a'+0` is an
expression: single-char quote values are CHARSET mapped too); `ld a,'b'` -> `3E 01`; numeric items are never
mapped (`charset 1,3,#41 : defb 1,2,3` -> `01 02 03`).

### 5.2 STR (`_STR` l. 13714)

Like DEFB (escapes `\b \v \f \0 \r \n \t \xHH`, no `\e`/`\a`; escaped chars and `\xHH` bypass CHARSET) but the
**last char of a quote gets bit 7 set when the quote is the last item or is followed by another quote item**:
`str "ab","c",'d'` -> `61 E2 E3 E4`; `str "a",2` -> `61 02` (no bit 7: next item is a number); `str 1,"ab"` ->
`01 61 E2`.

### 5.3 Numbers: DEFW, DEFI, floats

| Directive | Encoding | Probe |
|-----------|----------|-------|
| `DEFW x` | 16-bit LE, rounded (§3.2) | `dw 1.5,-1` -> `02 00 FF FF` |
| `DEFI x` | 32-bit LE | `defi #12345678,-1` -> `78 56 34 12 FF FF FF FF`; `defi 2.5` -> `03 00 00 00` |
| `DEFR x` | Amstrad BASIC REAL, 5 bytes: 4 mantissa bytes LE (bit 7 of byte 3 = sign, implicit leading 1), then exponent `+128` (`__internal_MakeAmsdosREAL` l. 3064) | `1`=`00 00 00 00 81`, `0.5`=`00 00 00 00 80`, `-1`=`00 00 00 80 81`, `0`=`00 00 00 00 00`, `3.14159`=`81 CF 0F 49 82`, `100`=`00 00 00 48 87` |
| `DEFF x` | Microsoft 40-bit: exponent first, then mantissa big-endian with sign in bit 7 of the first mantissa byte (`__internal_MakeRosoftREAL` l. 2927) | `1`=`81 00 00 00 00`, `0.5`=`80 00 00 00 00`, `-1`=`81 80 00 00 00`, `0`=`00 00 00 00 00`, `3.14159`=`82 49 0F CF 81`, `100`=`87 48 00 00 00` |
| `DF24 x` | float32 cut to 3 bytes: `[(e8-64) | sign<<7, (bits>>7)&FF, (bits>>15)&FF]` (top mantissa bit implicit, low 7 bits dropped, no rounding) (`_DF24` l. 13775) | `1`=`3F 00 00`, `0.5`=`3E 00 00`, `-1`=`BF 00 00`, `0`=`C0 00 00`, `3.14159`=`40 1F 92`, `100`=`45 00 90` |

The REAL encoders take the double from the expression **without** rounding; `DEFR` = CPC firmware format, `DEFF`
= MSX/other Microsoft BASIC format.

### 5.4 DEFS (`_DEFS` l. 13643)

`DEFS n[,fill][,n2[,fill2]...]` — pairs repeat (`defs 2,#AA,3,#BB` -> `AA AA BB BB BB`). Count is evaluated
immediately (`RoundComputeExpressionCore`, so `defs 1.5` = 2 bytes; forward label = error; negative = error;
quote with more than one char = error). Fill omitted = `00` written directly; fill given = one deferred `0V8`
expression per byte (forward labels OK; `$` rule in §3.4). Trailing comma accepted (`defs 2,1,` = `01 01`).

### 5.5 CHARSET / UTF8REMAP (`__CHARSET` l. 19810, `__UTF8REMAP` l. 19788)

| Form | Effect |
|------|--------|
| `CHARSET` | reset identity |
| `CHARSET "src","dst"` | `charset[src[i]] = dst[i]`, strings must have the same length (error otherwise); `\` skips to the next char |
| `CHARSET "str",v` | consecutive values from v: `charset[str[i]] = v+i` |
| `CHARSET code,v` | one entry, code 0..255 |
| `CHARSET start,end,v` | `charset[start..end] = v, v+1, ...` (error unless 0<=start<=end<=255) |
| `UTF8REMAP` | clear the UTF-8 table |
| `UTF8REMAP "é",v` | multi-byte sequence -> byte v (only used for bytes > 126 in DEFB quotes) |

CHARSET applies to DEFB/STR quote chars and single-char quote values in expressions, not to numbers.

## 6. Control directives

All conditions below are evaluated **immediately** with `RoundComputeExpression` (CLI: `floor(x+0.5)`, so
`if 0.4` is false and `if 0.6` true; flux: floor). Forward labels are errors there (§3.1).

### 6.1 Conditional assembly (`__IF` l. 21631 ... `__ENDIF` l. 22145)

`IF e`, `IFNOT e`, `ELSEIF e`, `ELSEIFNOT e`, `ELSE`, `ENDIF`; `IFDEF s`, `IFNDEF s`, `IFUSED s`, `IFNUSED s`.
A stack of `s_ifthen`; inside a false branch the statements are skipped by the `_light` variants (they only keep
nesting, no evaluation). `ELSEIF` after a taken branch is not evaluated.

| Test | True when |
|------|-----------|
| `IFDEF s` | `s` is (at that point) a variable, `MODULE_s`/`s` label, alias, or **macro** name; `{tag}` in the name is translated |
| `IFNDEF s` | none of the above |
| `IFUSED s` | the variable/label/alias has `used` set (referenced earlier in the source), or the name was referenced while still unknown (`SearchUsed`, forward references) |
| `IFNUSED s` | not IFUSED |

Probe `c1`: `jp yy : ifused yy` true (forward ref counts), `ifused zz` false, `ifndef yy` before `yy` true, `v=1 : ifused
v` false until `defb v`. There is no `IF` on undefined symbols: `if undefined_label` is an error, not false.

### 6.2 SWITCH (`__SWITCH` l. 21880)

`SWITCH e` / `CASE e` / `DEFAULT` / `BREAK` / `ENDSWITCH`, integer compare after rounding (`switch 2.6 : case 3`
matches). C-like fall-through: a matching `CASE` turns execution on, it stays on through later `CASE`/`DEFAULT` lines
until `BREAK`; a later `CASE` with the same value turns it on again; `DEFAULT` turns it on only if no CASE matched
**before it** (position-dependent).

Probe `c1`: `switch 2 / case 1 #11 / case 2 #12 / case 3 #13 / break / case 2 #22 / default #D0` -> `12 13 22 D0`;
`switch 9 / case 1 #31 / default #D1 / case 9 #39` -> `D1 39`.

### 6.3 Loops

| Directive | Rules |
|-----------|-------|
| `REPEAT n[,var[,start[,step]]]` ... `REND` (`__REPEAT` l. 21445) | n evaluated once; 0 = skip the block; 1..65536, otherwise error. `var` is a variable set to `start` (default from `STARTINGINDEX`, default 1) and incremented by `step` (default 1) at each `REND`: `repeat 3,k,10,5 : defb k : rend` -> `0A 0F 14` |
| `STARTINGINDEX start[,step]` | default start/step of following REPEATs |
| `REPEAT` ... `UNTIL cond` | body runs, then `cond` evaluated; > 65536 iterations error |
| `WHILE cond[,var]` ... `WEND` | cond evaluated before each iteration; optional `var` counter (starts at the REPEAT start index, +1 per WEND); > 65536 iterations -> error "infinite loop" |
| `REPEAT_COUNTER`, `WHILE_COUNTER` | **do not exist** in 3.3.3 (documented, but probe `r1` = errors) |

Loop bodies re-execute the word list (no re-lexing); local labels `@x` get unique suffixes (§2.3), proximity
labels are saved/restored around the loop (`PushGlobal`/`PopGlobal`). In loop and macro bodies expressions are
translated on a copy, so `$` inside `DEFS n,$` is per byte (§3.4).

### 6.4 Macros (`__MACRO` l. 19923, `__MACRO_EXECUTE` l. 20096)

Definition: `MACRO name[,p1,p2,...]` (params separated by commas; with `-ass` params can be used without braces),
body until `ENDM` or `MEND` (an `ENUM` inside the body consumes one `ENDM`/`MEND`). Defining a macro inside a macro
is an error (and STOPs assembly). A macro name may not be a register; a macro must be defined before use (one
pass); macro names are case-insensitive (all upper-cased).

Invocation `name a1,a2,...`: argument words are split like operands (top-level commas; commas inside `( )` or quotes
do not split: `two "a,b",4` -> `61 2C 62 04`, `two (1,2),3` gives `defb (1,2)` = error "Missing operator").
Wrong argument count = error "MACRO [X] was defined with N parameters {A} {B}". A macro without parameters may be
called as `name` or `name (void)`; `-void` makes `(void)` mandatory.

Substitution is **textual on words that contain `{`** (`TxtReplace` of `{P}` with the argument text) at expansion
time. Special forms:

| Form | Meaning |
|------|---------|
| `{P}` | argument text |
| `{EVAL}expr` (at the **call site**) | argument evaluated now and passed as `%lf` text (`ev {eval}x` with x=3 -> `3.000000`) |
| `...` as last param | variadic; `{#}` = number of args, `{n}` = n-th argument, **0-based over all args**, `{n|=default}` default if missing (probe `mc3`: `va 9,7,8` with `defb {first},{#},{1},{2|=#EE},{3|=#DD}` -> `09 03 07 08 DD`); too many args -> warning |
| `REG.HIGH` / `REG.LOW` (AF BC DE HL IX IY) | meant to become `A`/`F`, `B`/`C`, ..., `XH`/`XL`; **dead in 3.3.3**: the `CRC_DE_LOW`-style constants (l. 1580) are computed for `DE_LOW` while the code compares `"DE.LOW"`, so the rewrite never fires (`ld de.low,1` = "Unknown LD format") |

Each expansion increments `ae->macrocounter` (all macros share one counter, starting at 0). Local labels in the body
get `M<counter>` (`@l` -> `@LM4` in the 5th expansion, probe `mc3`), and a local label becomes the proximity base
for `.x` inside the body (`@LM4.P`); the proximity base is restored after the expansion. `-mml` allows a macro call
whose arguments continue on the following lines.

### 6.5 ENUM (`__ENUM` l. 22347)

```
 enum col,10,2      ; prefix COL_, start 10, increment 2 (both optional; no name = no prefix, start 0, inc 1)
 red                ; COL_RED = 10   (one name per statement, no operands)
 green              ; COL_GREEN = 12
 blue=20.7          ; COL_BLUE = 20  (double truncated to int, then the sequence continues)
 cyan               ; COL_CYAN = 22
 mend               ; or ENDM
```

Members are EQU aliases `"(%d)"`; clash with an existing label/variable/alias is an error.

### 6.6 STRUCT (`__STRUCT` l. 22526, `__ENDSTRUCT` l. 22873)

Definition `STRUCT name` ... `ENDSTRUCT`/`ENDS`: code-less ORG 0 (no output, not part of LZ); labels inside are
field offsets, data directives give sizes (DEFB 1, DEFW 2, DEFI 4, DEFR/DEFF 5, DF24 3, DEFS n) and their operands
are the default contents. Nested: `STRUCT other fieldname` inside the definition (`tl struct pt` is wrong: it opens a new definition ->
error "STRUCT cannot be declared inside previous opened STRUCT"); no self insertion.

Instance: `STRUCT type name[,count[,init1,init2,...]]` emits `count` copies of the default bytes (or the given inits
per field, quotes allowed for DEFS/DEFB fields, the rest of a DEFS field keeps its default fill).

Generated symbols (probe `s1`, `pt` = `x defb 1 / y defw #2233 / nm defs 3,#EE`; `rect` = `struct pt tl / w defb 0`):

| Expression | Value |
|------------|-------|
| `pt.y`, `pt.nm` | field offsets 1, 3 (`ld a,(ix+pt.nm)` = `DD 7E 03`) |
| `{sizeof}pt`, `{sizeof}rect` | 6, 7 |
| `rect.tl.y`, `rect.w` | 1, 6 |
| `p1`, `p1.x`, `p1.y`, `p1.nm` | labels #100, #100, #101, #103 (exported with `-s`) |
| `pa` (2 elements at #106), `pa.y` | #106, #107 |
| `{sizeof}pa` | **12** (whole array) |
| `pa1` | `#106 + 6*1` = #10C; `pa5` warns "index out of array size" |
| `r1.tl.y` | #119 |

Instance bytes: `struct pt p3,1,5,#4455,"ab"` -> `05 55 44 61 62 EE`. Array indexing (l. 7376) strips **all**
trailing digits of the unknown identifier, so an instance named `p2` cannot be indexed (`p21` looks up struct `P`).
Numeric init of a DEFS field: warning "struct field description is an expression [7] but size is 3 bytes".

### 6.7 Assertions, messages, timing

| Directive | Behaviour |
|-----------|-----------|
| `ASSERT e[,print-args]` (l. 21605) | immediate; false -> error "ASSERT ... failed with <translated expr>" + the PRINT of the extra args + **STOP** |
| `PRINT a,b,...` (l. 21058) | immediate; quote -> text with `{tags}` translated and `\b\v\f\r\n\t`; numbers `%.2lf`; prefixes `{HEX}` (2/4/8 digits by magnitude) `{HEX2}` `{HEX4}` `{HEX8}` `{BIN}` `{BIN8}` `{BIN16}` `{BIN32}` `{INT}` `{INT2}`..`{INT5}` (zero padded); items separated by blanks, quotes followed by two blanks |
| `DELAYED_PRINT ...` | same, after the end of assembly (forward labels OK) |
| `FAIL ...` | PRINT + STOP + error |
| `STOP` | stop assembling; **no output file**, exit 1 |
| `TICKER START,v` / `TICKER STOP,v` / `TICKER STOPZ,v` | v = NOPs (CPC) / T-states (`STOPZ`) of the code between: `ld a,(ix+1) : nop` = 6 NOPs, `ld a,(ix+1)` = 19 T |
| `TIMESTAMP ["fmt"]` (l. 24388) | emits the local build time as bytes through CHARSET; `Y`/`YY`/`YYYY` `M` `D` `h` `m` `s`, others literal; default `[Y-M-D h:m]`. Non-reproducible output |
| `BRK` | emits `ED FF` (WinApe), or the bytes set by `REDEFINE_BRK b1,b2,...` |
| `BREAKPOINT [addr]` | no bytes; recorded for `-eb`/snapshot breakpoint export (CPC) |
| `LIST`, `NOLIST`, `LET x=e` | WinApe compatibility no-ops (`LET` = assignment) |
| `ALIGN n[,fill]` (l. 21227) | on `codeadr`; 1..65535; **without fill: skips** (`outputadr` and `codeadr` jump, gap stays whatever memory holds, 0 by default); with fill: writes fill bytes |
| `CONFINE n[,fill][,WARNING|ENFORCE]` (l. 21179) | if `[$, $+n-1]` crosses a 256-byte page, pad (physically, fill default 0) to the next page, warning (error with ENFORCE); n 2..256 |
| `PROTECT start,end` | later ORG/writes into the range are errors |
| `LIMIT addr[,EXTENDED]` (l. 19308) | writing at or beyond `addr` is an error; EXTENDED allows >64K |

## 7. Memory and output model

### 7.1 Address spaces

- Two counters: `codeadr` (logical `$`, always 16-bit) and `outputadr` (physical write offset in the current memory
  space, `$$`). `ORG a` sets both; `ORG a,b` sets `codeadr=a`, `outputadr=b` (`__ORG` l. 22238; `$` inside the
  second expression is the old `outputadr`). Bytes go to `ae->mem[activebank][outputadr++]`.
- A memory space is 64K (`LIMIT n,EXTENDED` grows it). Default space = bank 0 of the plain-binary mode; `BANK` with
  no argument opens a fresh anonymous temporary space (`___new_memory_space`); `BANK n` / `BANK NEXT` select numbered banks (and switch
  to cartridge mode unless BUILDSNA/BUILDZX/BUILDROM was given); `BANKSET n` maps 4 consecutive banks as one 64K
  space (snapshot modes).
- Every ORG/BANK/NOCODE opens an `s_orgzone {memstart, memend, ibank, nocode, protect, inplace}`. Opening a new ORG
  whose `outputadr` falls inside an earlier zone of the same bank that already holds bytes is an error ("ORG
  (output at #0100) located in a previous ORG section"); writing over existing bytes is an error "Assembling
  overwrite" (`OverWriteCheck` l. 19330). INCBIN `,OFF` disables the overwrite check for that include.
- `NOCODE` ... `CODE`: addresses advance, nothing is written, the zone is ignored by the output. `CODE SKIP`
  restores `codeadr`/`outputadr` to the values at `NOCODE`. Labels defined in NOCODE are normal (struct-like maps).
- `codeadr` past #FFFF: warning (once per ORG), wraps.

### 7.2 Plain binary output (`Assemble` l. 26962)

Written only when there is no `SAVE` at all and no BUILDSNA/BUILDZX/BUILDCPR/BUILDROM/BUILDTAPE mode
(`if (ae->nbsave==0 || force...)`, l. 26107). File `<out>.bin` (`-o name`, default `rasmoutput`; `-ob file` sets
the full name). Contents: the memory space of the **last** ORG zone that holds bytes (`lastspaceid`), from the lowest
`memstart` to the highest `memend` among the written, non-NOCODE, non-PROTECT zones of that space; gaps are zero
(or whatever was written there). Order of ORGs does not matter. Probe `o1`: `org #200 : defb 2 : org #100 : defb 1 :
nocode : org #300 : defb 3 : code : org #180 : align 4 : defb $ : align 8,#FF : defb $` -> 257-byte file starting at
#100: `01 00..00` / at +#80 `80 FF FF FF FF FF FF FF 88` / ... / +#100 `02` (ALIGN 4 at #180 does nothing, ALIGN 8
at #181 fills 7 bytes). No byte written: warning "Not a single byte to output", no file.

### 7.3 SAVE (`__SAVE` l. 24122, executed in `PopAllSave` l. 10289 after everything else)

`SAVE "file",offset,size[,AMSDOS|HOBETA|TAPE,"tape.cdt"|DSK,"disk.dsk"[,side][,PROT|HIDDEN]]`

- offset/size are evaluated at the **end** (forward labels OK, `ExpressionFastTranslate` at the directive freezes
  variables), against the memory space active **at the SAVE line**, after all fixups, crunching and pokers.
- `run` for AMSDOS/tape/DSK = the RUN expression **defined before this SAVE**, otherwise `offset`.
- Any SAVE suppresses the default `.bin` (§7.2). Filename `{tags}` are translated at the directive.
- `size` 1..65536 (plain SAVE up to the space size); offset+size bounds checked -> error "cannot save ...".

| Format | Bytes |
|--------|-------|
| plain | `mem[offset .. offset+size-1]` |
| `HOBETA` (`MakeHobetaHeader` l. 3730) | 17-byte header + data. `[0..7]` name, `[8]`=`'C'`, `[9..A]` = **size** (not the start address!), `[B..C]` = size, `[D]` = `(size+255)>>8` sectors, `[E]` = 0, `[F..10]` = CRC = `sum(i=0..14) (hdr[i]*257 + i)` & #FFFF, LE. Name = the 8.3-normalised filename (`MakeAMSDOS_name`: upper case, base padded with blanks to 8; the extension is dropped, type is always `C`). Probe `sv1` (302 bytes, "hob1.bin"): `48 4F 42 31 20 20 20 20 43 2E 01 2E 01 02 00 96 2F` |
| `AMSDOS` (`_internal_MakeAMSDOSHeader` l. 3668) | 128-byte CPC header: user, 8+3 name, type 2, length, load, exec, checksum of bytes 0..66 at 67..68, plus "created by RASM" filler (CPC only) |
| `TAPE` | CPC CDT/TZX blocks (`__output_CDT`), not a ZX `.tap` |
| `DSK` | CPC EDSK image (`EDSK_addfile`) |

There is **no ZX `.tap` writer and no `.trd`/`.scl`** in rasm 3.3.3; HOBETA is the only TR-DOS format.

### 7.4 BUILDZX (`__BUILDZX` l. 18488, writer l. 26512)

`BUILDZX` = `forcesnapshot=1`, `forcezx=1` (error when another build mode is active; warning if code/ORG came
before). Also makes `DURATION()` return T-states. Banks 0..7 (`BANK 8` = fatal). Output `<out>.sna` (`-oi`
overrides): **48K SNA only**, 27-byte header + 48K:

| Header byte | Value |
|-------------|-------|
| `0x00..0x12` | 0 (I, HL', DE', BC', AF', HL, DE, BC, IY, IX) |
| `0x13` | 0 (IFF2: DI) |
| `0x14..0x16` | 0 (R, AF) |
| `0x17..0x18` | SP = RUN's second argument |
| `0x19` | 1 (IM 1) |
| `0x1A` | border: **read one byte past the 26-byte header array** (`zxsnapheader[0x1A]` written with length 27): undefined, 0 in practice |

`RUN pc,sp` is mandatory in ZX mode (deferred `ZXRUN`/`ZXSTACK` expressions); PC is pushed into bank 0 memory at
`mem[0][sp]`, `mem[0][sp+1]` (no PC field in 48K SNA; `RETN` pops it).

Memory image: with `BANKSET 0` the 48K = `mem[0][#4000..#FFFF]` (code assembled at its real addresses; this is the
only coherent mode: probe `zx3`, `org #4000/#8000/#C000` bytes land at file `0x1B`, `0x401B`, `0x801B`, PC #C000
pushed at #FF00). Without BANKSET: `mem[5][0..#3FFF]`, `mem[2][0..#3FFF]`, `mem[0][0..#3FFF]` — i.e. each bank is
assembled at **offset 0** (`bank 5 : org 0`), and the PC push at `mem[0][sp]` only lands in the file if `sp` <
#4000 (probe `zx2`). Code assembled without BANK/BANKSET (e.g. `buildzx : org #8000`) is **silently absent** from
the snapshot (probe `zx1`: only the header has data). No 128K SNA, no `.z80`, no `.tap`.

### 7.5 Other build modes (CPC; identify only)

`BUILDSNA [V2]` (Amstrad SNA v3/v2, `SNASET`, `SETCPC`, `SETCRTC`, `BANKSET`), `BUILDCPR`/`BANK` (GX4000 cartridge
`.cpr`/`.xpr`), `BUILDROM [CONCAT]` (`.rom`), `BUILDTAPE ["f.cdt"]` (CDT of the binary, run = RUN or start),
`BUILDOBJ` (text `.obj` with PROCEDURE/EXTERNAL lines), `EDSK ...`/`HFE ...` disk image actions, `AMSDOS` directive
(header on the binary), `ROMBANK`, `NAMEBANK`, `NAMEROM`, `REBANK`, `SNAPINIT`, `CPRINIT`, `RUN addr[,ppi]`.

### 7.6 Pokers: SUMMEM, SUM16, XORMEM, CIPHERMEM (l. 24264-24387, executed l. 25958)

Executed **after** all expression fixups and crunching, in source order, on the memory space active at the
directive; `start,end` evaluated at the end (half-open range `[start,end)`).

| Directive | Emits at the directive | Effect at end |
|-----------|------------------------|---------------|
| `SUMMEM s,e` | 1 byte | byte = sum of `mem[s..e-1]` mod 256 |
| `SUM16 s,e` | 2 bytes | LE 16-bit sum |
| `XORMEM s,e` | 1 byte | XOR of `mem[s..e-1]` |
| `CIPHERMEM s,e[,m[,"key"]]` | nothing | in place: m=0/1 (default): `x=mem[s]; for j in s+1..e-1: mem[j]^=x; x=mem[j]` (chained, first byte is the key); m=2: same chain seeded with `s&#FF`; m=3: `mem[j]^=j&#FF`; m=4: `mem[j]^=key[k++ % len]` |

The result position is the `outputadr` at the directive; a poker result inside another poker's range: warning.
NOCODE: no poker recorded, but SUMMEM/XORMEM still emit their placeholder byte(s) (into nowhere).

## 8. Crunched sections and crunched includes

### 8.1 Sections (`__LZ*` l. 18990-19278, `__LZCLOSE` l. 19279, processing `Assemble` l. 25690-25942)

| Directive | `lzversion` | Compressor |
|-----------|-------------|------------|
| `LZ4` | 4 | `LZ4_crunch` (l. 1739, raw LZ4 block) |
| `LZX7` | 7 | `ZX7_compress(zx7_optimize())` (Einar Saukas ZX7) |
| `LZEXO` | 8 | `Exomizer_crunch` (exomizer raw) |
| `LZAPU` | 17 | `APULTRA_crunch` (aPLib via apultra) |
| `LZSA1 [minmatch]` / `LZSA2 [minmatch]` | 18 (version 1/2) | `LZSA_crunch` (raw block, flags `LZSA_FLAG_RAW_BLOCK`); minmatch 2..5 else default 5 (v1) / 2 (v2); output > 64K = error |
| `LZX0` | 70 | **salvador** (`salvador_compress`, flags `FLG_IS_INVERTED` = ZX0 v2 format, max offset 32640), not Einar's zx0; bytes can differ from `zx0` |
| `LZX0B` | 71 | input reversed, salvador with `INVERTED|BACKWARD`, output reversed |
| `LZ48` / `LZ49` | 48 / 49 | Roudoudou's LZ48/LZ49 (`LZ48_encode_legacy`) |

`-quick` sets `MAX_OFFSET_ZX0=2176` but the salvador calls pass the literal 32640, so it only changes the "may take a
while" warning threshold.

Model: `LZxx` opens a section at the current `outputadr` (error if one is already open, or inside a STRUCT; ORG
inside an open section = error). Code inside is assembled normally at its logical addresses. `LZCLOSE` ends it and
opens an "intermediate" zone (lzversion 0). At the end of the pass, for each section in order: its expressions are
popped (`PopAllExpression(ae,i)`), the bytes `[memstart,memend)` are compressed; if the result is not smaller,
warning "LZ section is bigger than original, it wont be crunched!" and the data stays raw. Otherwise the packed
data replaces the section and everything after it in the same ORG zone and in **contiguous** following ORG zones
(same bank, `memstart == previous memend`) is moved down by `lzshift = packed - raw`; the freed tail is zeroed.
Labels after the section in those zones are shifted by `lzshift`, as are later expressions' write pointers
(`wptr`); their `ptr` (the `$` value) shifts only for "in place" zones (`ORG a` without separate output). Later
sections' bounds shift too. Then expressions of intermediate zones and finally all others are popped — so a label
after a crunched block has its **post-crunch** address and code after it is resolved against moved labels.
Referencing, from before a section, a label inside or after a later crunched section: the label is not processed
(`process_label=0`, l. 7189) -> error. EQU aliases defined in crunched zones are re-evaluated before crunching
(`%.8lf`). A section that grows (`lzshift>0`) is a TODO in rasm (no move).

### 8.2 Crunched includes (pre-processing l. 28656-28810, `__HEXBIN` l. 23337)

The pre-processor rewrites `INCxxx "file"` into `HEXBIN #n` (index into `ae->hexbin`); the crunch code:

| Keyword | Code | Compressor |
|---------|------|------------|
| `INCBIN`, `INCWAV` | 10 -> 0 | none (`INCWAV` = INCBIN; sample conversion is selected by the `SMP/SM2/SM4/DMA` option) |
| `INCLZ4` | 4 | LZ4 |
| `INCZX7` | 7 | ZX7 |
| `INCEXO` | 8 | Exomizer |
| `INCAPU` | 17 | apultra |
| `INCLZSA1` | 18 (v1, minmatch 5) | LZSA1 |
| `INCLZSA2` | 19 -> 18 (v2, minmatch 2) | LZSA2 |
| `INCZX0` | 70 | salvador ZX0 v2 |
| `INCZX0B` | 71 | salvador backward |
| `INCL48` / `INCL49` | 48 / 49 | LZ48 / LZ49 |
| `INCLUDE` | 1 | source include (§9.1) |

The file is read when the HEXBIN statement executes, offset/size/options applied, then compressed (l. 23930-24045),
then emitted.

## 9. Files

### 9.1 INCLUDE (pre-processing, l. 28700)

Textual, at pre-processing time, **before** any IF is evaluated: an INCLUDE inside `if 0` is still expanded; a missing
file becomes `ERRRD n` and errors only if that statement is assembled (probe `inc2`: `if 0 : include "missing.asm" :
endif` assembles). Search order: directory of the **including file** (`MergePath(ae->filename[current], name)`), then
each `-I` path in order. Absolute `/x` taken as is; `./x` = relative to the including file. On macOS/Linux the path
is case-resolved (`ResolveCaseInsensitivePath`). Probe: `sub/outer.asm` doing `include "inner.asm"` reads
`sub/inner.asm`, not `./inner.asm`. The filename must be a quote on the same line ("A quoted string must follow any
INCLUDE/INC* directive"). There is no include-once guard; recursion is limited only by memory. `READ` is **not** a
directive (commented out of the table, parses as a label).

### 9.2 INCBIN (`__HEXBIN` l. 23337)

`INCBIN "file"[,offset[,size[,offset_high]]][,options...]` — numbers and keywords may be mixed; the first three bare
numbers are offset, size, then `offset += 65536*n`. All immediate (forward labels error).

| Parameter | Rule |
|-----------|------|
| offset < 0 | `filesize + offset` (`incbin "six.bin",-2` = last 2 bytes) |
| size 0 / omitted | `filesize - offset` |
| size < 0 | `filesize + size` (**not** relative to offset: `incbin f,2,-1` on 6 bytes = error "size+offset is greater than filesize (5+2>6)"; `incbin f,1,-2` = 4 bytes) |
| size > 65536 | error |
| `SKIPHEADER` | if the file has a valid AMSDOS header (checksum of bytes 0..66 at 67..68, user < 16, type <= 2, length fields consistent) then `offset += 128`; otherwise ignored |
| `EXISTS` | missing file is silently skipped |
| `OFF` / `ON` | disable/enable the overwrite check |
| `REVERT` | reverse the bytes |
| `REMAP n`, `VTILES n`, `GTILES w`, `ITILES w`, `REPACK`, `SLICE w,h`, `ISLICE w,h[,MISALIGNED]` | CPC graphics reorderings (mutually exclusive) |
| `SMP`/`SM2`/`SM4`/`DMA` (2nd word) | WAV -> CPC sample/Plus DMA list conversion (`_AudioLoadSample` l. 23048) |

Search: the pre-processor already resolved the path like INCLUDE (including file's directory, then `-I`); at
execution the name with `{tags}` translated is tried as is, then the `-I` paths. Probe `inc2`: `incbin "six.bin",1,2`
= `42 43`, `,-2` = `45 46`, `,1,-2` = `42 43 44 45`, `,0,0` = whole file.

### 9.3 HEXBIN

Internal only (`HEXBIN #n` produced by the pre-processor); writing it by hand indexes the include table.

### 9.4 FILESIZE / FILEBYTE (`__FILESIZE` l. 6146, `__FILEBYTE` l. 5537)

`FILESIZE("f")`, `FILEBYTE("f",offset)`: name with `{tags}` translated; tried as given (CWD), then relative to the
file containing the expression, then `-I` paths. FILEBYTE caches files up to 128K (by name), reads bigger ones per
call; offset beyond the end = error. Both are immediate where the expression is immediate and deferred in data.

## 10. Symbols in and out

### 10.1 Export (`Assemble` l. 27362-27545)

Written only without errors. File `<out>.sym` (`-os file` overrides). `-sm` = one file per bank
(`<out>.sym.bank<N>`). Order = label definition order, then variables (dictionary tree order), then aliases.

| Option | `export_sym` | Label line | Variable line (`-sv`) | Alias line (`-sq`) |
|--------|--------------|------------|------------------------|---------------------|
| `-s` | 1 | `NAME #%X B<bank> L` (bank > 31 printed as 0) | `NAME #%X B0 V` | `NAME #%X B0 A` (struct field aliases: `I`) |
| `-sp` | 2 (pasmo) | `NAME EQU 0%04XH` | same | same |
| `-sw` | 3 (WinApe) | `NAME #%04X` | same | same |
| `-sc "fmt"` | 4 | `sprintf(fmt, name, value)` + `\n`; fmt must hold `%s` then one value conversion | same | same |
| `-sz` | 5 (ZX/zesarux style) | `%d:%04X NAME` (bank number; the default space is **260**) | **`%s %04X` without newline** (lines fuse: rasm bug) | `%04X NAME` |
| `-ss` | 1 + locals | | | |
| `-sl` / `-sv` / `-sq` / `-sa` | modifiers only (locals / variables / aliases / all three); **alone they write nothing** | | | |

Probe `sy` (`org #8000 : glob nop : .prox nop : repeat 1 : @loc nop : rend : v1=5 : al equ #1234 : module mo : ml
nop : endmodule : noexport glob`), `-s -sa`:

```
GLOB.PROX #8001 B0 L
@LOCR0R1 #8002 B0 L
MO_ML #8003 B0 L
V1 #5 B0 V
SYNCHRO #100 B0 A
RASM_VERSION #3 B0 A
AL #1234 B0 A
```

`NOEXPORT glob` removed `GLOB` but not `GLOB.PROX`; local (`@`) labels need `-sl`; the predefined aliases are
exported with `-sq`. Values are the final (post-crunch) addresses. Names are upper case unless `-ec` (case recovered
by searching the raw source text). Other exports: `-rasm` -> `<out>.rasm` (REMU binary chunk), `-eb` -> `<out>.brk`
(`BREAKPOINT` addresses + labels named `BRK...`), `-sb` (breakpoints into the CPC snapshot), `-er` ->
`<out>.rom_labels` (CPC ROM names), `-map` prints a verbose assembly listing to stdout (bytes before fixups); there
is no listing file.

### 10.2 Import `-l file` (l. 28257)

Lines upper-cased, one symbol per line, first match wins: `NAME: EQU 0xxxxH` (sjasm), `NAME EQU 0xxxxH` (pasmo),
`NAME #xxxx` (WinApe/rasm `-s` output, trailing fields ignored). Hex via `strtol(…,16)`. Imported names become
plain labels in the label tree before assembly (a source label with the same name = duplicate error).

### 10.3 Dependencies

`-depend=make` / `-depend=list` imply `-no` (check mode: assemble, write nothing). After a clean pass rasm prints the
output name (only if `-o` was given, i.e. not `rasmoutput`), every included source file except the main one, and every
INC* binary file, space separated (make) or one per line (list). With errors: "Fix errors to see dependencies".

## 11. Errors, warnings, semantic options

- `MakeError` -> message `[file:line] text` (with `inside macro => [file:line]` / repeat context), `MaxError`
  counts; at `-me N` errors (default 20) rasm prints "Too many errors!" and exits with the error count. Assembly
  continues after an error (for later messages) but **no file is written** if `nberr>0` (`if (!ae->nberr &&
  !ae->checkmode)`), exit code 255 (`Rasm` returns -1).
- Warnings print `Warning:` and do not stop output (exit 0). `-twe` / `ae->erronwarn`: each warning also calls
  `MaxError` (counts as an error: no output, exit 255). `-w`: suppress warnings. `-wu`: warn about unused
  labels/variables/aliases at the end. `-wc`: no "cruncher may take a while" messages. `-xr`: also print the source
  line.
- `STOP` / failed `ASSERT` / `FAIL`: assembly stops, no output, exit 1/255.

Main warnings (all suppressible): "truncating value #X to #Y" (V8/V16/V32 range, §3.2), "shift count" (<<,>> > 31),
"index out of array size" (struct arrays), "confinement overflows", "LZ section is bigger than original",
"crunched section is empty", "useless parameters in macro", "SKIPHEADER is useless with negative offset", "INCBIN of an
empty file", "run address redefinition", "using BUILDZX whereas there was a previous ORG or code", "Not a single byte to
output", "poker result is inside another poker calculation", "keystring parameter will be ignored", "UTF8 mapping was
defined but no code were found", "struct field description is an expression [...]", empty quote.

Errors a re-implementer must reproduce as errors (not warnings): J8 range, IV8 range, IM/RST/BIT values, undefined
symbol in an immediate context, single `=` in an expression, string as DEFS count, overlapping ORG / overwrite,
ORG inside an LZ section, nested macro definition, macro argument count, unknown instruction form ("Unknown LD
format", "Use OR with A,B,...").

Semantic CLI options (everything else is file naming):

| Option | Effect |
|--------|--------|
| `-m` | rough 0 -> floor rounding **and** Maxam evaluation (§3.3) |
| `-ass` / `-uz` | `as80` = 1 / 2: AS80 / UZ80 compatibility (32-bit masks, braceless macro params) |
| `-pasmo` | Pasmo compatibility (`param.pasmo`) |
| `-dams` | parsed, no effect in 3.3.3 |
| `-amper` | `&` starts a hex literal (rewritten to `#` in the pre-processor outside quotes) |
| `-fq` / `FREEQUOTES` | allow any byte inside quotes |
| `-utf8` | UTF-8 aware quotes |
| `-void` | macros without params must be called with `(void)` |
| `-mml` | multi-line macro call arguments |
| `-ec` | keep symbol case in exports |
| `-msep c` | module separator (default `_`) |
| `-D NAME=value` | upper-cased, defines a **variable** before assembly; value parsed with `atof` (not an expression: `-DZZ=3+1` gives 3, `-DX=#10` gives 0); IFDEF sees it |
| `-I path` | include search path (a `/` is appended) |
| `-l file` | label import |
| `-quick` | ZX0 offset limit for the warning only (§8.1) |
| `-v2` | CPC snapshot v2 |
| `-no` | check only, no output |
| `-oa` | output name = source name without extension |
| `-o name`, `-ob`, `-os`, `-ok`, `-oi`, `-ot`, `-or`, `-oc`, `-ol` | output base name / binary / symbol / breakpoint / snapshot / tape / rom / cartridge / rom-label file names |

## 12. Built-in autotests (`RasmAutotest` l. 31902, `rasm -autotest`)

Structure: ~320 `#define AUTOTEST_*` snippets (l. 29747-34100) plus inline tables, executed in-process; each check
`exit(-1)`s on the first failure except the keyword table (counts failures). Families by name: EDSK 28, NOCRUNCH 10,
MACRO 10, LATE 10, GETSIZE 8, LZSA1/LZSA2 7 each, ENHANCED 7, every LZ* section 6 each, STRCHAR, SLICE, MODULE,
OPERATORASSIGN 3 each, then ~150 single topics (STRUCT, SWITCH, TICKER, UNDEF, UTF8, SAVEINVALID0-3, REBANK, ...).

How they run — this changes the semantics:

| Call | Count | Mode |
|------|-------|------|
| `RasmAssemble(src,…)` | 289 | `param==NULL`: **floor rounding (rough 0) with rasm precedence**, no CLI defaults (no `-I`, maxerr unset) |
| `RasmAssembleInfo(src,…,&debug)` | 27 | same mode, error list in `debug` |
| `RasmAssembleInfoParam(src,…,&param)` | 71 | `param` mostly `memset(0)` then one flag (erronwarn, freequote, dams, macrovoid, pasmo, noampersand, mml, -ec): rough 0 -> **Maxam mode** (`maxam = !rough`), unless the test sets something else |
| `RasmAssembleInfoIntoRAM(ROM)` | 2 | assembles into a caller buffer (emulator mode) |

Assertion styles:

| Style | Count | Meaning for a port |
|-------|-------|--------------------|
| `if (!ret) {} else fail` | 157 | must assemble without error; often the snippet itself carries `ASSERT`s, so success = all asserts true |
| `if (ret) {} else fail` | 57 | must produce an error |
| `if (!ret && opcodelen==N && opcode[i]==…)` | 92 | exact size and selected bytes |
| `memcmp(opcode, ref, n)` | 21 | exact bytes against a table (e.g. `RealDump` for DEFR/DEFF floats) |
| `debug->symbol` checks | 9 | symbol table contents |
| `autotest_keyword[]` (l. 31035, 759 entries `{"src", expect_error}`) | 1 loop | 0 = must assemble, 1 = must fail (e.g. `{"ldir 5",1}`, `{"ld lx,h",1}`) |
| `AUTOTEST_INSTRMUSTFAILED` | 1 loop | `:`-split list, each must fail |
| `AUTOTEST_OPCODES` | 1 | the whole Z80 set must assemble — **no byte comparison** |
| Z80 emulator (`z80_runTest`, `lzCompare`) | ~43 | crunched sections are decrunched by running the Z80 decruncher on an emulated CPU; exact instruction counts and decrunched bytes compared (e.g. ZX0: 188550 instructions, 29158 bytes) |
| file tests | ~44 | earlier tests write `rasmoutput_*.bin`/`.dsk` into CWD and later snippets INCBIN them (`SKIPHEADER`, `GTILES`, ...) — order-dependent |

Consequences for unreal-asm's rasm engine and the `rasmautotests.py` corpus: snippets replayed through the **CLI
binary** run with rough 0.5 and rasm precedence, not with the in-process mode the C test used; any snippet that
depends on rounding (`.5` values, `/`) or precedence (InfoParam tests in Maxam mode) can legitimately differ between
"rasm CLI output" and "rasm autotest expectation". Generate the reference with the same mode the test used (or pass
`-m` for InfoParam snippets), and run the file-producing snippets in source order in one directory. The integrity
tests at the start (`rasm.c` grep for `activebank=` lines, CRC constants) are not language tests.
