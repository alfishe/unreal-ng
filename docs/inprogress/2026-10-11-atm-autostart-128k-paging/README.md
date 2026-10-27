# ATM: disk autostart boots the 128K way (Ball Quest, ballq_gr.scl)

Status: fixed for `ATM710` and `ATM450` (2026-10-11).

## Report

`testdata/machines/atm/ballq_gr.scl` (Ball Quest, Alone Coder / Shiru 2006-2007), `BQ ATM` on the ATM710 (1024 KB):
garbage instead of the EGA picture. `BQ 16C` on the Pentagon 1024 from the same disk: fine.

## What the picture showed

Mode `ATM16` (EGA), video pages 5 / 1 (or 7 / 3): half of every 8-pixel group black. The pixel planes in RAM 5 / 7
were right, RAM 1 / 3 nearly empty. The game's data was in RAM 25 / 27 / 31 instead: 24 + its `#7FFD` page.

On an ATM710 with 128 KB (the page base is masked off) the game runs and shows correctly: the EGA renderer is right.

## Cause

Port trace from the reset: one `#FFF7` write with PC = `#3CD6`, value `#E7`, after `#7FFD = #17`. That is the DOS
ROM's page select at `#3CC5` (`A & 7 | #10` to `#7FFD`, `~(A & #18)` to `#FFF7`), called with A = `#1F` by the
resident probe at `#3C50`:

```text
0000  di / ld de,#FFFF / jp #3C50
3C50  ld sp,#5BFF / ld a,#1F / call #3CC5     ; RAM page 31 in window 3
      ld hl,#C000 / ld a,(hl) / cp #C3 / jr nz,#3C4B
      ...checksum, #AA / #55 at #FFFE...          ; a resident (snapshot) in pages 27-31?
      ...restore it..., xor a / call #3CC5       ; only this path selects page 0 again
3C4B  ld a,7 / jp #0009                         ; no resident: TR-DOS cold start, page 31 stays
```

The same code is in every ATM2 ROM here: `data/rom/atm2.rom`, BIOS 1.06, 1.06.02, 1.07 (klug-bbs), 1.07.12,
1.07.13, 1.07.13EC. `#E7` lands in window 3 of the `#7FFD.4 = 1` register set: "RAM from `#7FFD`" over base 24. The
game pages with `#7FFD = #1n` (48K ROM, needed for `#3D13`), so its pages are 24-31 while the video reads RAM 5 / 7
and 1 / 3. Its ATM init writes only `#FF77 = #A8` (through `OUT (C),A` at `#2A53`); it never touches `#xFF7`.

The paging model is not the bug. In this mode `#7FFD` gives page bits 0-2 only, the high bits come from the window
register:

- `ATM2PORT.TXT` / `TURBO2.TXT` (klug-bbs ATMSRC): D7 MX_A "enable page_num (0-2) from S128 page port".
- UnrealSpeccy `memory.cpp` (`(p7FFD & 7) | (pFFF7 & 0xF8)`), Xpeccy `atm2.c` (`(page & 0x38) | (p7FFD & 7)`), ZXMAK2
  `MemoryAtm710.cs` (`(w & 0x38) | kpa0`), ZX-Evo `atm_pager.v` (`{pages[7:3], pent1m_page[2:0]}`).

The BIOS menu's "TR-DOS" entry (it reads "TR-DOS 48") goes through the same `#0000` -> `#3C50`, so a real 256 KB+
ATM booted that way should show the same thing (not checked on hardware). Its "SPECTRUM 128" entry lays out both
sets with window 3 = `#7FFD` pages 0-7, and the 128 menu's "TR-DOS" item enters TR-DOS without the probe
(traced: no `#FFF7` write, window 3 = RAM 0). Booted that way the game works. The autostart's `Reset(RM_DOS)` is the
cold start, the "TR-DOS 48" way.

## Fix

`PortDecoder::FinishAutostartColdStart(pc)` (default: nothing). `DiskAutostart::ArmColdStartFinish()` is armed after
every autostart reset; the first arrival of the cold start at the TR-DOS command loop (`$02EF`, the same hook as the
name rewrite) calls it, once. `PortDecoder_ATM710` runs `EnterSpectrum128Paging` there: the "SPECTRUM 128" layout.
Under TTD the hook is one tool edit, device state included.

Only the autostart changes. The BIOS menu path still behaves like the ROM.

Tests: `DiskAutostart_Atm_Test.ThePagesOfThe7FFDAreTheFirst128KAfterTheColdStart` (ATM710, ATM450),
`DiskAutostart_Atm710.BallQuestShowsAllFourEgaPlanes` (the game from `ballq_gr.scl`; fails without the fix:
RAM 25 full, RAM 1 / 3 empty). Checked live: 1024 KB and 512 KB,
title and level 1 (both screen buffers), original SCL through the commander.

## ATM450

`BQ ATM` shows garbage on the `ATM450` too, for another reason: the game does not support that board. It sets the
video mode only with `#FF77`, which the 4.50 does not have (EGA there is an `OUT` to `#xx9E` / `#xx1E`); trace: no
such write, the mode stays ZX. The readme names "ATM Turbo 2(+)" = boards 6.xx / 7.xx (`#xx77` / `#xxF7` ports, our
`ATM710`); boards 4.xx are ATM Turbo 1.

The 4.50's DOS ROM has the same resident probe: its page select (`#3CC5`) writes `A >> 3 & 3` to `#FDFD`, so the
cold start leaves `#FDFD = 3` and `#7FFD` pages at 24-31 on 512 KB. `PortDecoder_ATM450::FinishAutostartColdStart`
runs its `EnterSpectrum128Paging` (`#FDFD = 0`; the rest equals the `RM_DOS` reset). The game still shows garbage
there (no EGA without `#FF77`), 128K programs written for the 4.50 get pages 0-7.
