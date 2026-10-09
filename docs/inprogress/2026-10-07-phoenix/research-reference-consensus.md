# ZXM-Phoenix: what the reference emulators say, and the consensus

**Date:** 2026-10-07 · part of [README.md](README.md)

## 1. Sources read

No hardware document for the board is in the local collection (the collection's `zx-pk.ru` pages only list Phoenix boards in
forum signatures, for example "ZXM Phoenix 1024kB + VGA converter"). All facts below come from emulator source that was read
for this folder.

| ID | Source | Phoenix code read |
|:--|:--|:--|
| U | Unreal Speccy of the zx-evo project ([tslabs/zx-evo](https://github.com/tslabs/zx-evo), `pentevo/unreal/Unreal/`) | [`memory.cpp`](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/memory.cpp) `case MM_PHOENIX` (line 362), [`io.cpp`](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/io.cpp) lines 677, 886, 915, [`config.cpp`](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/config.cpp) lines 903, 1004, `vars.cpp` line 278 (model table: 1024 default, `RAM_1024 \| RAM_2048`) |
| X | Xpeccy ([samstyle/Xpeccy](https://github.com/samstyle/Xpeccy)) | [`src/libxpeccy/hardware/phoenix.c`](https://github.com/samstyle/Xpeccy/blob/master/src/libxpeccy/hardware/phoenix.c) whole file (older, with port `#FF`) |
| P | Xpeccy+ ([dotkoval/xpeccy-plus](https://github.com/dotkoval/xpeccy-plus)) | [`src/libxpeccy/hardware/phoenix.c`](https://github.com/dotkoval/xpeccy-plus/blob/master/src/libxpeccy/hardware/phoenix.c), [`res/machines/phoenix.conf`](https://github.com/dotkoval/xpeccy-plus/blob/master/res/machines/phoenix.conf), [`docs/machines-reference.md`](https://github.com/dotkoval/xpeccy-plus/blob/master/docs/machines-reference.md), [`CHANGELOG.md`](https://github.com/dotkoval/xpeccy-plus/blob/master/CHANGELOG.md) (the "ZXM-Phoenix paged the wrong memory" entry, credited to Volutar), [`config/roms/PROVENANCE.md`](https://github.com/dotkoval/xpeccy-plus/blob/master/config/roms/PROVENANCE.md) |
| B | Black_Cat's port table "zx-ports-full-table" (28.09.2006), the KAY-1024SL column | rows `#1FFD` and `#7FFD`; it has **no** Phoenix column |
| N | the repository's own notes | [bus slots](../2026-10-03-zx-bus-slots/research-machines.md) (NemoBus v1.1m), [mouse](../2026-09-12-kempston-mouse/hardware-reference.md) |

Machines that have **no** Phoenix: UnrealSpeccyP, the 0.39 UnrealSpeccy line, ZXMAK2 (its memory models are Spectrum 48 / 128,
Pentagon 128 / 512 / 1024, Profi, Scorpion, Quorum, ATM, PentEvo and Plus3 only), the Black_Cat table, pico-spec.

## 2. The RAM page number (the one real disagreement)

The page is 16K, mapped at `#C000`. Bits of the 7-bit page number (128 pages = 2 MB):

| Page bit | U (Unreal) | X (Xpeccy, older) | P (Xpeccy+) | Xpeccy comment "real" |
|:--|:--|:--|:--|:--|
| 0, 1, 2 | `#7FFD` bits 0, 1, 2 | same | same | same |
| 3 | **`#7FFD` bit 7** | `#1FFD` bit 4 | `#1FFD` bit 4 | `#1FFD` bit 4 |
| 4 | **`#1FFD` bit 4** | `#7FFD` bit 7 | `#7FFD` bit 7 | `#7FFD` bit 7 |
| 5 | `#1FFD` bit 7 | **`#1FFD` bit 6** | `#1FFD` bit 7 | `#1FFD` bit 7 |
| 6 | `#1FFD` bit 6 | **`#1FFD` bit 7** | `#1FFD` bit 6 | `#1FFD` bit 6 |

- X's bits 5 and 6 were wrong; P fixed them ("bits 4, 6 and 7 of port `#1FFD` were masked and shifted as one", CHANGELOG), and
  P's formula now equals the order of X's own comment, which says it is the real wiring.
- U has bits 5 and 6 as P does, and swaps bits 3 and 4.
- **Consensus: the P order.** Bits 5 and 6 are agreed by U and P (and the "real" comment); bits 3 and 4 are agreed by P, X and
  the comment against U. The only effect of choosing wrong is the page numbering of pages 8-31, so a program that touches all
  pages in order cannot tell. A program that hard-codes a page number (a Phoenix-aware demo or RAM disk) can. Open question
  Q2: confirm on a real board or a schematic.
- 1024K boards: all three emulators mask the page number by the RAM size (U `bank & temp.ram_mask`, X/P `memSetBank` on a 1 MB
  or 2 MB RAM), so on 1024K the top bit (`#1FFD` bit 6) is dropped. Consensus.
- **Fixed windows:** `#4000` = page 5, `#8000` = page 2 (U, X, P).
- **Screen:** `#7FFD` bit 3 selects page 7 or 5 (X, P: `vidPage = (val & 8) ? 7 : 5`; U: `vpage = (val & 8) ? 7 : 5`). The high page
  bits do not move the screen. Consensus.

## 3. Port decode

| Port | U | X | P | B (KAY-1024SL) | Consensus |
|:--|:--|:--|:--|:--|:--|
| `#7FFD` | A15 = 0 and not the `#1FFD` rule, so A14 = 1 in effect; A1 and A0 are **not** decoded | mask `#C007`, match `#4005`: A15 = 0, A14 = 1, A2 = 1, A1 = 0, A0 = 1 | same as X | `01xxxxxxxxxxxx01`: A15 = 0, A14 = 1, A1 = 0, A0 = 1 | A15 = 0, A14 = 1, A1 = 0, A0 = 1 (B, X, P; U ignores A1, A0); X and P also require A2 = 1 |
| `#1FFD` | mask `#C003`: A15 = 0, A14 = 0, A1 = 0, A0 = 1 | mask `#C007`, match `#0005`: also A2 = 1 | same as X | `00xxxxxxxxxxxx01` | the same split |
| `#EFF7` | exact port `#EFF7` (full 16 bits) | mask `#FFFF` | same | not in the KAY column | full decode `#EFF7` (U, X, P agree) |
| AY | (U's shared rule) | `#BFFD` / `#FFFD` with mask `#C007` | same | n/a | as the Pentagon decoder |
| `#FE`, `#1F`, mouse `#FADF` / `#FBDF` / `#FFDF` | shared rules | X: `#FE` mask `#0007`; mouse exact; no `#1F` | P adds `#1F` (Kempston joystick, mask `#00FF`) | n/a | Kempston joystick at `#1F`, mouse at the three exact ports |

The A2 difference: with A2 not decoded, the port `#1FF9`-style addresses would also page. A program that uses `OUT (C),A`
with BC = `#7FFD` / `#1FFD` is unaffected. **Recommendation (Q3):** decode A15, A14, A1, A0 (U and B agree for `#1FFD`, B, X and P for `#7FFD`, and B is the
board family's own table); revisit if a schematic shows A2.

## 4. ROM at `#0000`

ROM pages (the 64K image, four 16K pages): 0 = SYS, 1 = TR-DOS, 2 = 128K editor, 3 = 48 BASIC (U `config.cpp` 903-908, X/P
`phxMapMem`, and the repository's `rom.cpp`, which already maps them this way for `MM_PHOENIX`).

Priority, highest first (the same order in U, X, P):

1. `#1FFD` bit 0 set: RAM page 0 at `#0000` (U, X, P all show it writable RAM).
2. `#1FFD` bit 1 set: ROM page 0 (SYS).
3. `#1FFD` bit 3 set (alternate ROM pair):

   | Case | X, P | U |
   |:--|:--|:--|
   | TR-DOS active | page 3 (48 BASIC) | `#7FFD` bit 4 ? 48 BASIC : 128K editor |
   | TR-DOS not active | `#7FFD` bit 4 ? TR-DOS : SYS | same |

4. Otherwise: TR-DOS active -> page 1 (TR-DOS); else `#7FFD` bit 4 ? page 3 (48 BASIC) : page 2 (128K editor).
   (X, P. U does this through the shared `set_banks` code: under TR-DOS it picks page 1 or page 0 by `#7FFD` bit 4, so with `#7FFD` bit 4 = 0 it maps the SYS page, which the local image has erased.)

**Consensus: the X / P table.** The `#1FFD` bit 3 / TR-DOS row is the only place U and X / P differ; it needs the SYS page
contents to matter (Q1). Nothing in the three sources says what SYS holds on a real board; the local image has it erased.

## 5. Other behavior

| Behavior | U | X / P | Consensus |
|:--|:--|:--|:--|
| `#7FFD` bit 5 (lock) | after a write with bit 5 = 1, further `#7FFD` writes are ignored (the exceptions - Pentagon 1024, Profi, GMX - do not apply) | same: `if (p7FFD & 0x20) return` (the whole write, screen bit included) | lock, whole write ignored; `#1FFD` is **not** locked in any source |
| `#EFF7` | stored; bits 2 (lock mem) and "ROCACHE" re-run `set_banks`, video bits re-run `init_raster` (the Pentagon / ATM3 code, so any effect on Phoenix is likely incidental) | stored; **bit 7 forces the Beta Disk ports on** (`flgBDI = 1`) | store the byte; bit 7 forces the DOS ports (U: `CF_DOSPORTS` when `pEFF7 & 0x80`, X / P: `flgBDI`). Other bits: no effect (Q4) |
| Gluk RTC `#DFF7` / `#BFF7` | enabled for Phoenix when `CMOS=` is set | none | optional, off by default (Q5) |
| `IN #00F7` | n/a | returns 0 ("version") | return 0; origin unknown (Black_Cat lists `#00F7` "Version" for the ZX Multi Card-2) (Q6) |
| Unclaimed ports | n/a | X: `IN #FF` = attribute byte at the beam (border: `#FF`); P: a floating-bus value (`floatbus = attr`) | floating bus, like the Pentagon decoder of unreal-ng |
| Reset | n/a | `#7FFD`, `#1FFD`, `#EFF7` = 0 | all three latches 0 |
| Timing | n/a | P: 3 500 000 Hz, geometry "Pentagon" (224 T x 320 lines = 71 680 T), INT 36 T, no contention, no early timing | Pentagon timing (one source, Q7) |
| Turbo | n/a | P: `cpu.turbo = 1,2` (7 MHz by the emulator's switch); no port | no hardware port known (Q8) |
| Sound | n/a | P: one YM2149 at 1.75 MHz, ABC, Covox; General Sound `gs105b.rom` | as the Pentagon config |
| Storage | n/a | Beta Disk (TR-DOS); no IDE of its own | Beta Disk; IDE through the global scheme (the storage survey says the same) |
| Mouse | n/a | the three Kempston mouse ports decoded in full (`#FADF`, `#FBDF`, `#FFDF`); the mouse stays visible with TR-DOS paged (`dos = 2`, "don't care"; [mouse notes](../2026-09-12-kempston-mouse/hardware-reference.md) section 3) | exact ports `#FADF`, `#FBDF`, `#FFDF`, also in TR-DOS |
| Autostart | n/a | P: 128 menu, 48 TR-DOS BASIC | as the Pentagon |
| TR-DOS entry | `CF_LEAVEDOSRAM` (not the Pentagon's `CF_LEAVEDOSADR`) | by `flgDOS` | open (Q9) |

## 6. Conclusions that the design uses

1. Paging: the P formula, 7-bit page, masked by the RAM size; `#1FFD` bits 0, 1, 3 as in section 4; `#7FFD` bit 5 locks `#7FFD` only.
2. Decode: `#7FFD` and `#1FFD` by A15, A14, A1, A0; `#EFF7` in full.
3. The Pentagon decoder supplies everything else.
4. Every unknown has a switch-free default and an open question; none needs a board option now.
