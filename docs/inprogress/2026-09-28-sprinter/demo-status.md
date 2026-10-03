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
| BUYAN/20X20 `test20x20.exe` | **open** | draws the left 384 px of a picture, then loops on a 4-entry queue at `#03D0` with interrupts off (`DI`, IM 1); no key changes anything. Not compared with MAME yet (the scripted MAME session did not get the typed command line into Flex Navigator). TTD: `keys/demos-buyan-20x20-test20-1-exe.ttd` |
| DNTBLINK | runs (BIOS 3.06 Hotfix 2, the default) | plays to the end logo; on BIOS 3.07 BETA 1 the picture freezes at about 5:10 from a race in the demo ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §2.2) |
| DOOM2 | runs | title + menu |
| EXAMPLES (13: 256COLOR, 3D_STARS, FAN256, FISH, FLAMES, FLAMES1, PLASMA, PLASMA1, PSY, REAL_3D, SD, splines, STARWAY) | run | PSY: animated logo, Esc exits |
| FBIRD: `FBIRD.EXE`, `FBIRD_.EXE`, `_FBIRD.EXE` | run | re-checked after the CTC fix: the bird and pipes move (IM 2) |
| FRACTALS | runs | |
| GAME_00: `GAME_00.EXE`, `TEST_005.EXE`, `TEST_010.EXE` | **needs the "Game" PLD configuration** | each loads its own bitstream `GAME_00.ACX` (head hash `#3861CFA4`) and reloads the PLD (code `#2E`, through a patched port-table entry). The machine restarts with the new logic, which owns the reset memory map and starts the game. unreal-ng has no module for that configuration ([mame-gap-analysis.md](mame-gap-analysis.md) V10, deferred after v1), falls back to Standard, and the BIOS cold-boots to Flex Navigator. MAME recognizes the hash and has `screen_update_game` |
| kosarew (18: ANIME, ANIME2, ARCANO, ARCANOI, ARCANOID, Dep, DIAMONDS/DEMO, Full, LISA, LOST/DEMO, LOST/DEMO2, Matrix, MEMTEST, MEMTESTO, MK3, NEWYEAR, Parks, Term) | run | DIAMONDS: a title screen that waits for a key (reacts to Space / Enter) |
| kosarew/RCACHE | runs (tool) | prints its report on the DSS screen and returns on a key |
| LDCONF/`LDCONF.EXE` | as designed | without arguments it prints its help and exits |
| LDCONF/`300.BAT` ... `305.BAT` (`ldconf a c stream.30x`) | as designed | the stream loads through `#2E`; the logic runs as Standard (the streams' hashes are not the shipped Standard one) and the BIOS boots back to DSS, as LDConf's help says for a plain configuration file |
| LDCONF/`START.BAT` (`ldconf c gc.bin e scroll.exe`), `SCROLL.EXE` | **needs the "Game" PLD configuration** | `GC.BIN` has the Game head hash `#3861CFA4` (same as GAME_00.ACX); started alone, `SCROLL.EXE` draws garbage text on Standard logic |
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

1. BUYAN/20X20: compare with MAME (or real hardware), then decide whether the `DI` queue loop is our bug.
2. The "Game" PLD configuration (V10): GAME_00 (3 programs) and LDConf's `START.BAT` need it. Needs an analysis of
   `GAME_00.ACX` / `GC.BIN` (the mode-byte-3 per-square scroll renderer, the reset memory map). Deferred after v1 by
   the owner; these four are the known programs that would use it.
3. Side observation: the Covox-Blaster `int_requests` counter in `/state/sprinter` survives a machine reset (it kept
   counting across programs in the runner). Harmless for the picture; check whether the report should reset it.
