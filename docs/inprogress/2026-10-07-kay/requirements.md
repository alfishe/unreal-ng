# Kay-1024: requirements

**Date:** 2026-10-07 · part of [README.md](README.md) · design in [design.md](design.md)

Confidence: **H** = vendor passport, schematic and one more source agree; **M** = one primary source; **O** = open
(a question in [TODO.md](TODO.md)).

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `KAY` is a creatable machine that behaves like the Kay-1024/3SL/TURBO and boots the factory firmware (Kramis V0.3, V0.2) |
| G2 | Every behavior rests on a primary source; where Unreal Speccy differs from the passport and the schematic, the primary sources win and the difference is written down |
| G3 | No cost for other machines: Kay code lives in its decoder, its TTD blob and its capture policy |
| G4 | Automation parity in the phase that adds a feature: CLI, WebAPI + OpenAPI, MCP, Lua, Python, Qt |
| G5 | Real hardware variants (Kay-256, 2006 NB, 2010 SL4) are separate, later, faithful models, not flags on this one |

Not goals now: the NemoBus slot cards as cards (BETA-TURBO, NemoIDE, XT keyboard, modem), the CPLD boards, a printer.

## 2. Requirements

| ID | Requirement | Conf. | Source |
|:--|:--|:--|:--|
| R1 | Model `KAY` (`MM_KAY`) creatable; default RAM 1024 KB, 256 KB allowed; aliases `KAY1024`, `KAY256` | M | S1, S5 |
| R2 | RAM page at #C000 from #7FFD D0-D2 plus #1FFD D4, #1FFD D7, #7FFD D7, masked by RAM size; #4000 page 5, #8000 page 2 | H (bits), O (order, gating) | S1, S2, S5 |
| R3 | #1FFD D0 = 1 puts RAM page 0 at #0000 | H | S1, S2, S5 |
| R4 | ROM role = `(ROMS xor TR-DOS) * 2 + #7FFD.D4`; image `kay1024.rom` (V0.3), `kay1024b.rom` through a layout key | M, O (page table) | S1, S2, S5 |
| R5 | #7FFD decodes A15 = 0, A14 = 1, A1 = 0, A0 = 1; #1FFD A15 = A14 = 0, A1 = 0, A0 = 1; #7FFD D5 locks #7FFD only | H | S1, S2, S4 |
| R6 | AY at #BFFD / #FFFD with A1 = 0, A0 = 1 | H | S1, S2, S4 |
| R7 | Kempston joystick answers every odd port (A0 = 1) outside the DOS ports and #FFFD; D5-D7 = 0 | H | S1, S4 |
| R8 | #FE read: D5 = 0, D7 = BUSY (1 with no printer); write D5-D7 = 0 | M | S1, S2 |
| R9 | Built-in Beta 128 behavior through the DOS ports; the TR-DOS trap does not start from RAM | H | S4, S5 |
| R10 | NemoIDE through `[HDD] Scheme=NEMO` | H | exists |
| R11 | Frame 69888 T, 224 T line, INT 32 T, 4T border, no contention at 3.5 MHz | M, O (numbers) | S3, S6 |
| R12 | Turbo from the switch and #1FFD D2 = 0; 7 MHz; ratio ROM 2.0 / RAM 1.75 to 1.9 measured, not assumed | M | S1, S3 |
| R13 | TTD: `KayPaging` blob; the turbo switch is a recorded input; replay equal | - | design 7 |
| R14 | Snapshot view for Kay (`KayCapture`); `PagingFamily::Kay` for state transfer | - | design 7 |
| R15 | `.recipe/machines/kay.md`, verified | - | AGENTS.md |

## 3. Acceptance

- Phase 1: `KAY` creatable on every surface; each of the four ROM roles reaches its screen (128 menu, 48 `(C) 1982`,
  TR-DOS banner, Kramis menu); `core-tests` green, zero warnings (also gcc: `docker/linux/build.sh --test`).
- Each later phase: its tests in [tdd-plan.md](tdd-plan.md), the parity list of [design.md](design.md) section 8 done and the
  recipe re-run.
