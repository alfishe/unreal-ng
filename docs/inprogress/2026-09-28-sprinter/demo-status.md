# Sprinter demo status - the MAME-pack hard disk (`DEMOS/`)

Pass of 2026-10-03 (branch `sprinter-origwait-demos`, BIOS 3.07 BETA 1, the MAME-pack `sp_hdd_sys.chd`), every
`.exe` below `DEMOS/` (76 programs in 21 folders) run with the demo runner
([tools/machines/sprinter/demo-runner](../../../tools/machines/sprinter/demo-runner/README.md)): boot to Flex
Navigator, walk to the file, Enter, 8 emulated seconds, a screenshot every 2 s, TTD recording from the Enter on.
The BIOS 3.07 BETA 1 floppy issue ([open-items-2026-10-03.md](open-items-2026-10-03.md) §4) does not touch
programs on the hard disk.

**Earlier verdicts were not reliable.** The runner compared the whole `/state/screen/digest` response, which
carries the frame number, so every program was "running" (the "15 of 21 run" of the first pass included). Fixed:
it compares `combined` (video memory + border). Every verdict below was also checked on the screenshots (the
contact sheet). Screenshots, `results.json`, the contact sheet and the kept `.ttd` sessions: `scratch/demos-2026-10-03/`
of the main checkout (`final/`, `keys/`, `ldconf/`, `scroller/`; not committed).

## Results

| Folder / program | Verdict | Notes |
|:--|:--|:--|
| BADAPPLE | runs | picture + Covox-Blaster sound (CTC 3 tick) |
| BALLS (6 builds) | runs | |
| BUYAN: PLASMA2, PLASMA3, SCROLL, SCROLL2, SPIRO, XOR_B1, XOR_B2, sprite95, wave6fb, wave6fb2 | run | |
| BUYAN/20X20 `test20x20.exe` | **runs, with a race in the demo** | the hang (left 384 px, then a ROM loop at `#0505-#052A` with `DI`) is the demo's own race, not an emulation fault (§ BUYAN/20X20 below). Started at another moment it runs: the picture, then the 11 x 11 tile grid animated from its IM 2 handler |
| DNTBLINK | freezes (both BIOSes) | the picture freezes at about 5:10 (the "flowers" part) on BIOS 3.06 Hotfix 2 and 3.07 BETA 1 alike, from a race in the demo; MAME 0.289 freezes the same way in 4 of 6 runs ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §2.2) |
| DOOM2 | runs | title + menu |
| EXAMPLES (13: 256COLOR, 3D_STARS, FAN256, FISH, FLAMES, FLAMES1, PLASMA, PLASMA1, PSY, REAL_3D, SD, splines, STARWAY) | run | PSY: animated logo, Esc exits |
| FBIRD: `FBIRD.EXE`, `FBIRD_.EXE`, `_FBIRD.EXE` | run | re-checked after the CTC fix: the bird and pipes move (IM 2) |
| FRACTALS | runs | |
| GAME_00: `GAME_00.EXE`, `TEST_005.EXE`, `TEST_010.EXE` | **run** (2026-10-03, the Game module) | each loads its own copy of the "Game" bitstream (`GAME_00.ACX`, full hash `#C0FA3055`) and reloads the PLD (code `#2E`); the **Game** configuration module runs ([game-configuration.md](game-configuration.md)), its cell `#EE` = `#41` makes the BIOS return into the program: GAME_00 scrolls its color grid (like MAME's frame, 0.8 % of the pixels differ), TEST_005 and TEST_010 scroll a landscape over pixel-offset squares. Runner: verdict `pld-reload`, `pld_module` Game by `full_hash`, `after_reload` running; on BIOS 3.07 BETA 1 and 3.06 |
| kosarew (18: ANIME, ANIME2, ARCANO, ARCANOI, ARCANOID, Dep, DIAMONDS/DEMO, Full, LISA, LOST/DEMO, LOST/DEMO2, Matrix, MEMTEST, MEMTESTO, MK3, NEWYEAR, Parks, Term) | run | DIAMONDS: a title screen that waits for a key (reacts to Space / Enter) |
| kosarew/RCACHE | runs (tool) | prints its report on the DSS screen and returns on a key |
| LDCONF/`LDCONF.EXE` | as designed | without arguments it prints its help and exits |
| LDCONF/`300.BAT` ... `305.BAT` (`ldconf a c stream.30x`) | as designed | the stream loads through `#2E`; the logic runs as Standard (the streams' hashes are not the shipped Standard one) and the BIOS boots back to DSS, as LDConf's help says for a plain configuration file |
| LDCONF/`START.BAT` (`ldconf c gc.bin e scroll.exe`), `SCROLL.EXE` | **runs** (2026-10-03, the Game module) | `GC.BIN` is the same Game bitstream: LDConf reloads the PLD, the Game module runs and `SCROLL.EXE` draws on it (see the recipe for what it shows); started alone on Standard logic `SCROLL.EXE` still draws garbage, as it should. MAME restarts the BIOS over and over here (its reload shortcut), so it is no reference for this one |
| MK_DEMO: MK_DEMO, MK_OUTI | run | Covox-Blaster |
| notheng `NOTHENG.EXE` | runs | re-checked after the CTC fix |
| NU `NUPOGODI.EXE` | runs | |
| PLASMA2, ROTOZOOM (2), SCROLL | run | |
| SDK: SPACESHI, UWOL | run | UWOL: title screen, reacts to keys |
| SDK `START.EXE` | runs | a static picture in a `HALT` loop (IM 2, every vector `#FDFD`) until a key; Space leaves to Flex Navigator |
| WILDSND `prosiak.exe` | expected | exits at once: it needs the ISA Wild Sound card, which unreal-ng does not have (peripherals survey, P3/P4) |
| xenon2: X2_BAD, X2_OK, x2_old | run | Covox-Blaster |

**Spectrum mode, `scroller.trd` in P128** (`\zx\spectrum.exe \zx\p128.zx \trd\scroller.trd`, TR-DOS `R` Enter): 60 s
at 3.5 MHz, the picture changes every sample, and the PLD journal shows no CNF / ALL_MODE / turbo / reload event.
The jump to 21 MHz from the owner window (CNF `#07` + an SIO A overrun) does not reproduce; the PS/2 overrun fix is
the likely cure. TTD: `scroller/scroller-run.ttd`. Note for scripted runs: TR-DOS needs a key held for about
6 frames; a 3-frame tap is lost.

## Open

1. ~~BUYAN/20X20: compare with MAME (or real hardware), then decide whether the `DI` queue loop is our bug.~~
   **Closed 2026-10-03: a race in the demo** (below).
2. ~~The "Game" PLD configuration (V10)~~ **done 2026-10-03** (branch `sprinter-pld-game`,
   [game-configuration.md](game-configuration.md)): GAME_00 (3 programs) and LDConf's `START.BAT` run on the Game module.
3. ~~Side observation: the Covox-Blaster `int_requests` counter in `/state/sprinter` survives a machine reset (it kept
   counting across programs in the runner). Harmless for the picture; check whether the report should reset it.~~
   **Fixed 2026-10-03** (branch `sprinter-small-fixes`): the statistics (`int_requests`, `ticks`, `ring_writes`,
   `covox_writes`) are the emulator's, not the PLD's, and now restart at every reset (the PLD's `/RESET` path,
   `CovoxBlaster::Reset`); the ring and the tick divider keep their values, as the PLD has no reset term for them.

## BUYAN/20X20: a race in the demo (2026-10-03)

**What the program does.** `test20x20.exe` draws a 640 x 256 picture with the block accelerator, in ten calls of a
routine at `#84D6` (5 from `#844E`, 5 from `#8492`); each call copies 64 columns with the accelerator (`LD L,L`
copy line, `LD A,(DE)`, `LD A,A` vertical copy, `LD (HL),A`, `LD B,B` off). The callers run with `DI`, but the
routine ends with `EI : RET` (`#84ED`), so from the second call on the frame INT (IM 1, BIOS at `#0038`) is open
in the middle of the copying. Then it copies a block to `#0000` and runs its own IM 2 handler (the tile grid).

**What goes wrong.** When the INT is accepted inside an accelerator operation pair (mode still on, after
`LD (HL),A` or `LD A,(DE)`), the CPU's own stack writes of the acknowledge and every memory access of the BIOS
handler go through the accelerator as 256-byte block operations: the return address is lost, and the handler runs
on garbage until it ends in the BIOS loop at `#0505-#052A` with interrupts off. The PLD does not stop this: its
`ACC_BLK` suspend is held "enabled" ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §1.3), and MAME has none.

**Evidence.**

- unreal-ng TTD (the runner's recording, `scratch/` of the worktree, not committed): checkpoint frame 754 at
  `#8442` (in the program), frame 755 in the ROM loop. Stepping frame 754: the INT is accepted at `#84E8` (after
  `LD (HL),A` in vertical-copy mode, IFF1 = 1 from the previous call's `EI`): the pulse at base T 64 470 + 32 T
  covers the end of that 788-T instruction. Every handler instruction then takes about 1 560 T (block operations).
- MAME (`sprinter`, BIOS 3.06, the same disk with `SYSTEM.BAT` starting the program; a debugger breakpoint at
  `#0038` printing the return address): one INT lands inside the same loop too (line 289, the word at SP reads
  `#A6A6`: the stacked return address was overwritten the same way). MAME happens to survive it and shows the
  picture; the grid does not appear in its 60 s.
- unreal-ng started at other moments (the program run from `SYSTEM.BAT`, with 0-3 extra `VER` commands before it to
  move it against the frame INT; TTD on every run): BIOS 3.06 runs; BIOS 3.07 BETA 1 runs with 1 and 3 extra
  commands, hangs with 0 and crashes elsewhere (PC `#FAFA`) with 2. Same program, same emulator: only the phase of
  the INT against the copy loop decides.

**Verdict.** Not an emulation fault; the demo enables interrupts inside its accelerator routine. A board shows
the same lottery (the outcome depends on the load time from the disk). Nothing to change in the emulator.
