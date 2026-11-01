# Native recipe, end to end: Skool Daze (hold-out)

The first run of [the native recipe](../../../../.recipe/analysis/reverse-engineering.md) on a game no detector was
tuned on: Skool Daze (48K, 1984, tape from ZXDB), scored against SkoolKid's disassembly (`sd.skool`, 916 entries,
231 code). Run on 2026-10-29, work folder `scratch/re-sd/run4`. Reviewers were blind to the reference; the judge of
the name pairs read it after the last naming resolve.

## Steps

| Step | What happened |
| --- | --- |
| capture | tape loaded in 12200 frames; the explorer got past the menu and the names prompt (`LONGWAIT ENTER N ENTER`), found O/P (Kempston L/R) as the controls; 15000 frames recorded |
| cover | 149 one-way branches, 20 flipped, 179 new instructions, 1.8 s |
| review | 451 items, 448 resolved by 7 Sonnet reviewers in parallel (groups below); the last 3 naming batches skipped (see Names) |
| export, check | `rebuild-plain` ok (42240 of 42240 bytes), `assemble` ok |

Two capture fixes came out of the first three attempts (`corpus.py`, general, not per game): the control test now
needs a key shot close to the idle shot (attract-mode animation phases looked like control), and a tap that changes
nothing is retried as a 30-frame hold (the game polls keys seldom). The retry lasts a LONGWAIT in all, and a retried
key that leaves the LONGWAIT screen is no candidate: otherwise its "only time passed" screen (a blink phase) hid a
real key from another state (Protector lost control that way). Over the 60 corpus games, the explorer reaches
control in 46 (43 before), none lost; on Skool Daze the path is now `LONGWAIT ENTER N LONGWAIT`.

## Score

| | unreviewed | reviewed |
| --- | ---: | ---: |
| bytes by entry | 84.54 % | 89.03 % |
| bytes by line | 88.67 % | 89.53 % |
| code / non-code | 97.21 % | 96.82 % |
| code entry starts | 180 / 231 (draft 361) | 187 / 231 (draft 381) |
| graphics regions | 5 / 5 covered | 4 / 5 covered, 1 partial |
| graphics geometry | 2 match, 3 mismatch | 2 match, 3 mismatch |
| text bytes found | 4 of 5027 | 1810 |
| names the same (with the judge) | 0 / 989 | 19 / 989 (1.92 %) |
| `skool2asm_warnings` | 198 | 415 |

Byte confusion of the reviewed draft (draft row, reference column): code as code 9151, reference code still data
368 (unreviewed 1177), reference text as code 755, reference data as text 582. The unreviewed draft had no false
code at all; the review traded 809 bytes of found code for 755 bytes of text a reviewer confirmed as code.

Names: 542 of the 989 reference names sit where the draft has no entry or label at all, 290 are still auto labels;
of the 157 named pairs, 3 match by the rules and 16 more by the judge (`same`), 28 are `close`.

## Review groups and tokens

| Group | Items | Tokens | Tool calls | Wall s |
| --- | --- | ---: | ---: | ---: |
| A1 bugs + conflicts | 45 bugs (all wrong), 40 conflicts | 74111 | 17 | 273 |
| A2 conflicts | 91 (38 ok, 53 both hold) | 50856 | 13 | 207 |
| B1 low confidence | 126 (116 ok, 10 wrong) | 63290 | 14 | 244 |
| B2 unknown areas | 71 with kinds | 53612 | 14 | 311 |
| C hot routines + names | 30 routines, N1-N12 | 81679 | 21 | 275 |
| D names | 178 names | 49920 | 10 | 97 |
| E names | 171 names | 54662 | 13 | 142 |
| all | 448 | 428130 | 102 | 311 (in parallel) |

## Limits met

- **Text through a proportional font.** The game's messages are plain zero-terminated ASCII, mostly one per 256-byte
  page, printed glyph by glyph into a buffer: the text detector sees no text, because value flow follows values and
  here the byte only selects a glyph (an index). Built afterwards, see below; this game is no hold-out for it.
- **Every bug rule fired falsely here** (45 of 45): deliberate stack resets, command streams read through the stack,
  a key table longer than the rule assumed.
- **Page-strided records.** Character records are 256 bytes apart (one field per page); the small record claims
  inside them conflict with the table, and the sheets do not show the stride.
- **Review sheets lack caller roles and field meaning**, which is where most names went generic
  (`CharacterTimer...`); the gallery draws only claimed regions, so unclaimed graphics were judged from bytes.
- **A `#` in reviewer text stopped skool2asm** (`unknown macro: #FE`): fixed in the export (`#CHR(35)#HTML()`,
  test `AnalysisL2_Test.ReviewTextHashIsNoMacro`).

## After: text through its glyphs' index (2026-10-30)

The mechanism the first limit asked for, built after the run (so Skool Daze is its tuning game, not a hold-out):

- **L1 `indexes`.** The pointer provenance of the registers now also keeps the memory byte a register's value came
  from, through AND / SUB / a shift / `ADD HL,rr`; a read through a pair holding one such byte (in one half or both)
  is an index use (selector byte, the access, the addresses read). Two bytes in the two halves are a pointer word.
- **Text rule.** An instruction that read through indexes taken from bytes no code wrote is a font reader when those
  bytes are words: at least 8 distinct codes, 95 % printable (bit 7 aside), 75 % letters or spaces. Its selector
  bytes are text with no print path needed (evidence `glyph`), when the run has a terminator: zero, bit 7, a length
  byte, or (new, for any text) a control code the same routine read next. On this game the readers of the skool map
  (tile codes over the whole byte range: 18-24 % letters) and of the tunes (note bytes indexing pitches: 30 %)
  qualified on printable codes alone and gave 144 false bytes; the letter share removes them.
- **A walk over memory reads no glyphs** (any text, not only the new rule): the game's random number generator reads
  the whole memory with one `XOR (HL)`, the ROM character set included, which made it a "font reader" whose every
  read byte counted as pixels ("MR WITHIT" lost "MR W"). A routine that read more than 16 K in all is neither a font
  reader nor a printer now.

A fresh unreviewed capture (`LONGWAIT ENTER N LONGWAIT`), scored the same way:

| | before (run4, unreviewed) | after |
| --- | ---: | ---: |
| text bytes found (of 5027) | 4 | 414 |
| draft text that is not reference text | 0 | 0 |
| rebuild-plain / assemble | ok / ok | ok / ok |

The recording printed 438 of the reference's text bytes (the bytes used as a glyph index); 414 of them are found, 24
missed. The other 4589 were never printed in the 15000 frames (lesson and question texts, names): only a longer or a
scripted capture would show them.

Over the 60 dev corpus games (`scratch/corpus9`, compared with `corpus8` byte by byte), runs found through the index
alone added text in 18 games, of three kinds: the messages of games with their own print routines (Tetris 2's menu,
Juggernaut's, "DAMAGE CONTROL REPORT" in Star Trek), the ROM's keyword table (16 games, the BASIC listing reads it),
and the lines of BASIC programs (string literals and expressions alike, read by the ROM's interpreter and LIST).
Blank fillers (Camelot: 84 runs of spaces), digits (`250`, `000`) and BASIC expressions (`n)=`, `f(i)`, `D-";F`) are
glyph indices too but no words: an index-only run now needs letters for at least half of its non-space characters
(135 runs, about 880 bytes, dropped over the corpus; none on Skool Daze; lost: " CRL  1985"). Still claimed: the
word-like pieces of BASIC lines (`gametur`, `s(ay,ax)`); a BASIC program detector (line headers, tokens, the 5-byte
numbers) would place them, none exists yet. The other differences between the corpus runs come from the capture
(a game going further in 15000 frames, e.g. 32121 block copies against 2825), not from this rule.
