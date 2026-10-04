# TODO — Peters Plus Sprinter Sp2000 machine support

**Status marker:** design drafted and **review round 1 done** (2026-09-28). **S0 done**
(2026-10-01, branch `sprinter-s0`; the MAME captures on branch `sprinter-mame`); **S1 done**
(2026-10-01, branch `sprinter-s1`); CPU library (2026-10-01, branch `sprinter-cpu`); **S2 done** (2026-10-01, branch `sprinter-s2`); **S3a done** (2026-10-01, branch `sprinter-s3a`); **S3b done** (2026-10-02, branch `sprinter-s3b`); **S6 done** (2026-10-02, branch `sprinter-s6`, [s6-sound-outcome.md](s6-sound-outcome.md)); S4, S5 and S7-TTD have their own outcome files. PLAN.md row **#59** (T4): the owner started
the program on 2026-10-01 (TSConf exists; the trigger is no longer "after #41"); the shared pieces
this design introduced are PLAN row **#60** (shared infrastructure, done).

## Goal

A creatable `SPRINTER` model that runs the real BIOS and Estex DSS (floppy, HDD image, PC folder),
the Spectrum mode with TR-DOS, and Sprinter-native software, with TTD, debugger and automation
parity. Details: [README.md](README.md), [goals-and-requirements.md](goals-and-requirements.md).

## Progress

- [x] Source survey ([materials.md](materials.md)): MAME, ZXMAK2, SprintEm, the designer's manual,
  BIOS sources (Peters Plus 2.17 and the Tolik-Trek continuation), DSS sources and binaries, the
  PLD (AHDL) design, a published DSS 1.62 boot floppy
- [x] Hardware reference ([hardware-reference.md](hardware-reference.md)) incl. the decoded
  standard port table and the source-disagreement table
- [x] High-level and technical designs ([high-level-design.md](high-level-design.md),
  [technical-design.md](technical-design.md) and five `tdd-*.md` files)
- [x] Mapping onto unreal-ng ([unreal-ng-mapping.md](unreal-ng-mapping.md)), plan
  ([roadmap-and-plan.md](roadmap-and-plan.md)), tests ([test-plan.md](test-plan.md))
- [x] Review round 1 (2026-09-28): decisions D1-D11 (high-level design §7), Q1-Q6 and the shared
  infrastructure decisions ([roadmap-and-plan.md](roadmap-and-plan.md) §5); modular PLD
  configurations (`SprinterPldConfiguration`)
- [x] S0 (2026-10-01, branch `sprinter-s0`; [roadmap-and-plan.md](roadmap-and-plan.md) §1):
  - [x] BIOS 3.04 in `data/rom/sprinter/sp2k-3.04.rom` + `data/rom/README-ROMS.md` + ROM signature
    catalog (`rom.cpp`); 3.06 **not found publicly** (recorded; add from the MAME set later)
  - [x] disassembly of ROM pages 8 and 0, SETUP (unpacked from page 0) and the PLD loader in
    [docs/disasm/rom/sprinter/](../../disasm/rom/sprinter/README.md), names carried from BIOS-TT
    `0271ac3` and BIOS-PP `1273243`; symbol files in `data/symbols/sprinter/` (load with
    `LabelManager`)
  - [x] port-table decoder `tools/machines/sprinter/dcp-table/dcp-table.py`; the 3.04 table checked statically
    ([hardware-reference.md](hardware-reference.md) §4.4: three differences to the BIOS-TT table)
  - [x] Q4: 473 720 writes, statically ([tdd-ports-memory.md](tdd-ports-memory.md) §6)
  - [x] Q3: the PLD has the INT-suspend; default on (owner decision 2026-10-01)
    ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §1.3)
  - [x] DSS 1.62 boot floppy and DSS 1.60R files in `testdata/machines/sprinter/` +
    `testdata/NOTICE.md`; `LoaderRawPcFloppyDss_Test` reads the real floppy through the WD1793 at
    500 kbit/s
  - [x] Sprinter sources added to the local corpus ([materials.md](materials.md))
  - [x] MAME reference captures (2026-10-01, branch `sprinter-mame`; MAME 0.289 subset build
    `zxsp` with the `sprinter` driver, scripts in `tools/machines/sprinter/mame-capture/`) in
    [testdata/machines/sprinter/reference/](../../../testdata/machines/sprinter/reference/README.md): page `#40` after POST (equals the static table,
    CRC `b7f09600`), the logo frame (frame 60, 1.229 s) and the boot screen (frame 507, 10.383 s),
    INT positions for the FN_SYNC modes (Scorpion = 3.04 default, Pentagon +16 lines, Spectrum +8
    more), the first 10 000 port accesses with codes, the loader write count at run time (473 720,
    confirms Q4)

- [x] S1 (2026-10-01, branch `sprinter-s1`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §6): creatable `SPRINTER`; `PortDecoder_Sprinter`,
  `SprinterMemory`, the configuration modules (Standard + a test stub), the bitstream sink and
  fast start, `SprinterVideoRam` + `SprinterIntSource`, the Z84C15 package, the CMOS, the turbo
  waits, the TR-DOS M1 signal; BIOS 3.04 reaches its boot prompt with the fast and the full start
  (ACC-1a); page `#40` at the prompt equals the static 3.04 table
- [x] S1 checked against the MAME references (2026-10-01, roadmap §6.1): the first 10 000 port
  accesses identical (order, values, PCs, codes) and timed to the T-state at 3.5 MHz; the turbo
  port wait fixed (2 clocks early; 21-MHz drift over the trace 1 245 µs → 5.7 µs); logo palette,
  INT positions per FN_SYNC mode, INT acknowledge, loader count equal; `SprinterReference_Test`
- [x] S2 (2026-10-01, branch `sprinter-s2`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §7): `ScreenSprinter` (every mode of the mode table,
  palettes, border, flash, HOLD, RGMOD page, 320 / 312 lines), `M_SPRINTER` / `R_736_288`,
  `SprinterVideoRenderer` + configuration-module hook 3, `SprinterVideoMapper`, screenshots and
  recordings; palette order settled (R, G, B in video RAM); ACC-1 (the logo frame equals MAME's
  `logo.png` exactly when drawn with the frame-end state, within one fade step as the beam drew it),
  ACC-2 adapted (SETUP 1.58 has no date page: "Memory Test" saved to the CMOS file)

- [x] The Sprinter runs on its own CPU library (2026-10-01, branch `sprinter-cpu`; owner decision,
  [research-cpu-z84c15.md](research-cpu-z84c15.md) §8.0): `core/src/3rdparty/z84c15/` - the CMOS
  Z84C00 core forked from unreal-z80 0.5.0 with the Z84C15's wait generator, chip selects,
  watchdog, CTC / SIO / PIO and daisy chain; the engine seam in `Z80` keeps every other machine on
  the native interpreter. Timing changes: the loader 142 T per bitstream byte (MAME 113), the BIOS
  start 22 T before `InitCpuPorts` clears WCR
  ([2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/README.md))

- [x] S3a (2026-10-01, branch `sprinter-s3a`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §8): the `#BD` latch on the WD1793 `Latched` policy, the FDC
  off bit, code `#15` with the Kempston bits, the `#1F` operand rewrite, the WD1793 time base at 21 MHz;
  shared WD1793 fixes: drive select (bits 1-0 of `#FF` were ignored) and a separator-rate change during an
  ID search; **ACC-3**: DSS 1.62.92 boots from the HD floppy in drive B to `B:\>` (the BIOS probe flips to
  1.44 MB); **ACC-6**: Spectrum mode through DSS `SPECTRUM.EXE` (BIOS 3.04 has no Spectrum ROMs), TR-DOS
  7.01 lists a TRD in drive A and `LOAD ... CODE` is byte-exact; MAME 0.289 cannot read the HD floppy
  (its PLL is set at the command start), its probe timing equals ours

- [x] S3b (2026-10-02, branch `sprinter-s3b`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §9, as built in [tdd-storage.md](tdd-storage.md) §3.4): the Sprinter
  decode in the shared `IdeAdapter` (codes `#20-#2B`, one A8-selected data latch, channel select), two
  `AtaChannel`s in `IdeController`, slots `ide0.*` / `ide1.*`, `[HDD] Scheme=SPRINTER` + `CHS2/3`, `CD2/3`,
  `Image2/3`; the `AtaChannel` TTD blob carries the second channel; `state ide` on every surface; **ACC-4**: DSS
  1.62.92 boots from a built FAT16 image (BIOS 3.04, `C:\>` at frame 495, golden screen, MKDIR in the image);
  both channels through BIOS 3.06; the owner's real disks: DSS 1.71.57 (MAME pack) on BIOS 3.06, DSS 1.62.93
  (ZXMAK2 VHD) on BIOS 3.04; MAME HDD captures; the GUI lets PC-keyboard machines have bare F1-F12 (F4 at the
  IDE wait); a Sprinter teardown use-after-free fixed

- [x] S6 (2026-10-02, branch `sprinter-s6`; outcome [s6-sound-outcome.md](s6-sound-outcome.md), as built in
  [tdd-accel-sound-input.md](tdd-accel-sound-input.md) §2.1): `CovoxBlaster` (Covox, ring, 16 rates, mono / stereo,
  8 / 16 bit, half-ring INT through the PLD INT source, `#FE` bits 7 / 5, the accelerator's page-`#FD` path) in the
  COVOX mixer slot; one AY (`TurboSound=Single`, 1.75 MHz); TTD id 32 and the Sprinter fixture re-recorded;
  `state/sprinter` `sound` and the shared Covox report; `AccelIntSuspend` default 0 (owner decision 2026-10-02, reversing 2026-10-01: WAVPLAY refills the ring
  from its INT handler and goes silent with the block on; the literal PLD `ACC_BLK` reading and MAME agree); PT3PLAY and WAVPLAY against MAME (pitch, tempo, rates equal; MAME swaps 16-bit stereo); recipe
  `.recipe/machines/sprinter-sound.md`

- [x] Default BIOS 3.07 BETA 1 (owner decision 2026-10-02, "switch the default straight to 3.07"; replaces Q1's 3.04;
  branch `sprinter-default-bios-306`; [bios-versions.md](bios-versions.md) §6.1): `[ROM] SPRINTER=` in the shipped
  config, catalog labels, automation texts, recipes; 3.04-pinned tests select 3.04 explicitly
  (`SprinterFixture::SelectBios`); the TTD corpus fixture `boot.ttd` re-recorded on 3.07 BETA 1
- [x] Default BIOS back to 3.06 Hotfix 2 (owner decision 2026-10-03, branch `sprinter-bios-head`;
  [bios-versions.md](bios-versions.md) §6.1): the upstream head is the kept 3.07 BETA 1 and still has the floppy
  IY bug; 3.07 BETA 1 stays selectable with its `known_issues` warning. `[ROM] SPRINTER=`, catalog labels,
  automation texts, recipes, AGENTS.md; `boot.ttd` re-recorded on 3.06 Hotfix 2
- [x] Debugger crash at address 0 during a Sprinter reset (2026-10-02, branch `sprinter-debugger-null-bank`;
  [crash-debugger-null-window.md](crash-debugger-null-window.md)): `Memory::Reset` put the null 48K ROM role into
  window 0 until the decoder's reset mapped the Sprinter layout, and the debugger read it from the UI thread. A
  window is never null now, and tool reads (`DirectReadFromZ80Memory`) follow the Sprinter's read redirect
  (graphics pages, ISA `#FF`, loader fast RAM) like the CPU does
- [x] Keyboard overrun, stuck keys, F12 (2026-10-03, branch `sprinter-ps2-overrun`;
  [s4-input-outcome.md](s4-input-outcome.md) last section): the board never holds the keyboard off (PLD KBD_CX =
  KBD_DX = GND), so unread bytes are lost in the SIO as on the board; the Z84C15 SIO now overruns as the data sheets
  and MAME do (newest entry overwritten, RR1 bit 5 when it reaches the top, latched until Error Reset); F12 and
  Ctrl+Alt+Del come from the PLD's keyboard block decoding the wire (typematic F12 repeats toggle again; an SIO
  overrun cannot switch the turbo), TTD blob 31 v3, `boot.ttd` re-recorded; focus out releases the ZX matrix keys
  as well as the PS/2 keys

- [x] Spectrum mode: attributes lagged behind the pixels in Pentagon multicolor demos (owner report 2026-10-03,
  `scroller.trd`, `atarin.trd` in P128; branch `sprinter-zx-shadow-squares`; [research-zx-mode.md](research-zx-mode.md)
  §7.1): the frame INT sat at MAME's place, 10 T after the PLD's edge (`CT5` rising 2 T into the first square
  after the INT run), so the Sprinter read each cell 17 980 T after the INT where a Pentagon reads it after
  17 988; now 17 990. With it: a video RAM byte lands 1 T before the write cycle's end, a `#7FFD` write catches
  the beam up (bit 3 picks the screen), and a text / Spectrum square keeps the font byte latched at its start
  (the attribute is read every half T). The shadow copy was right: the same conditions and address as MAME, the
  address as the PLD's `VXA` (`VIDEO2.TDF`), and both demos' pixels equal the PENTAGON model's frame for frame. Open: mode bytes written inside a square are not latched (no known program needs it)

## Remaining

Overview of everything open, owner-approved 2026-10-03: [open-items-2026-10-03.md](open-items-2026-10-03.md).

- **Next (owner order, 2026-10-02):**
  1. ~~Automation audit P1 + P2~~ **done** (2026-10-02, branch `sprinter-automation`; status per gap in
     [automation-audit-2026-10-02.md](automation-audit-2026-10-02.md) §4, outcome in
     [automation-outcome.md](automation-outcome.md) "Audit round"). Follow-ups from it:
     - G16 / G17 remainder (P3): per-frame wait totals by kind (counters on the wait path: needs an A/B), the
       Z84C15 power-on M1 counter and after-ED flag (a library accessor in `core/src/3rdparty/z84c15/`, carried
       into unreal-z80);
     - the video change log counts TS-Conf CRAM writes from the CPU's FM window only, not the DMA's (DMA time
       base differs); a per-T table-write history (not only first / last) if a tool needs it;
     - a Qt view of the mode map / palettes / video RAM (the debugger-model work; the data is all in the reports);
     - G18-G21 (P3) unchanged.
     - ~~the GUI status bar said "text 40 (mixed)" in the Spectrum mode~~ **fixed** (2026-10-02, branch
       `sprinter-statusbar-zx`): the classifier reads all three mode bytes, ZX-40 squares are `spectrum`
       ([tdd-video.md](tdd-video.md) §7 "Spectrum screen squares").
  2. Demos from the MAME-pack HDD (`DEMOS/`, 21 items) one by one against MAME on the same image: hangs, no
     picture, no sound - find and fix each cause with MAME's code as the reference. **Pass done 2026-10-03:
     [demo-status.md](demo-status.md)** (open: BUYAN/20X20). ~~The Game PLD configuration for GAME_00 / LDConf
     START.BAT~~ **done 2026-10-03** (branch `sprinter-pld-game`, [game-configuration.md](game-configuration.md)): the
     Game module (selected by the bitstream's full hash `#C0FA3055`, cell `#EE` = `#41`, the per-square grid-offset
     picture), all five automation surfaces + the status bar, recipe, MAME captures, env-gated HDD tests.
     Known facts per demo (from the authors, via the owner, 2026-10-02): deMarche "dontBlink" does not use the
     GS - it plays through the Covox-Blaster with the data streamed from disk in the interrupt handler (standard
     Sprinter hardware only), so no sound there points at CBL / IDE-in-INT timing, not at the missing ISA.
     dontBlink's picture freeze at ~5:10 (2026-10-03): a race in the demo's SP-repair log (an INT between
     `#0D1E LD SP,#3F74` and `#0D25 LD HL,(#031D)` overwrites its return address). Timing luck at clock level, MAME
     freezes the same way in 4 of 6 runs; analysis [tdd-accel-sound-input.md](tdd-accel-sound-input.md) §2.2. It hits with BIOS 3.06 Hotfix 2
     (the default again since 2026-10-03) and 3.07 BETA 1 alike. The owner checks it on a real board.
  3. Mouse in the GUI through the shared MouseManager (branch `sprinter-mouse` on `mouse-manager`), then S6b
     (ISA / ZX-bus / NeoGS: PROPLAY MOD playback), the S7 remainder (Qt docks, CD).
  4. Designs in progress (2026-10-02): ISA slots ([2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/tdd.md), owner
     decisions Q1-Q3 recorded), network adapters ([2026-10-02-sprinter-network](../2026-10-02-sprinter-network/tdd.md): NE2000 ISA Ethernet
     confirmed; maximum reuse of the shared network stack), ZX mode (`tdd-zx-mode.md`), the peripherals survey (`peripherals-survey.md`).
  5. **Owner decision 2026-10-02** (the developer-interest ranking, [peripherals-survey.md](peripherals-survey.md)
     §10, accepted; "the network definitely first"). After the demo pass:
     - ISA I1 and network SN1-SN3 **before** the NeoGS (S6b). The network kits had about 340 commits in 2026
       and are the only new programs that need a card. **ISA I1 done 2026-10-03** (branch `sprinter-isa-network`:
       `SprinterIsaBus`, window-3 routing, the `#9FBD` latch, `[ISA]` slots, TTD blob 33, `state/isa` /
       `control/isa` on every surface, recipe `.recipe/machines/sprinter-isa.md`; ISA tdd §14). **Network SN0-SN2 done
       2026-10-03** (same branch): NE2000 RTL8019AS in ISA slot 2 by default, the Ethernet gateway, the RTL8019AS kit
       end to end, TTD blob 45; recipe `.recipe/machines/sprinter-network.md`, network tdd §18.
     - The ATAPI CD (with media change, eject, ATAPI boot) and the CF identity check, raised to P2. They are
       the BIOS / DSS developer's main work since 2024-10.
     - The Centronics printer drops to P4.
- **ZX mode, phase S8 (design 2026-10-02: [research-zx-mode.md](research-zx-mode.md),
  [tdd-zx-mode.md](tdd-zx-mode.md); roadmap §1 row S8).** The real machine loads TRD / SCL into a BIOS RAM disk
  that only the Sprinter TR-DOS 7.0x reads (no PLD trap, unlike ZX-Evo vdos); TAP has no software, only the
  tape input; snapshots exist only as an emulator convenience. Checked on MAME (BIOS 3.06, MAME-pack disk): TRD,
  SCL, the reset back to DSS and a snapshot in ZX mode work; MAME's tape input never toggles (`kbd_fe_r`).
  - **Owner, 2026-10-03: the open items of this ZX-mode section are doubtful** - kept for the record, not
    scheduled, not to be deleted; take one only on the owner's request ([open-items-2026-10-03.md](open-items-2026-10-03.md) §2)
  - [x] Z1 (S) faithful path on unreal-ng against MAME (2026-10-02, branch `sprinter-zx-timing`, tdd-zx-mode §4.1, §11):
    every launcher mode (SP, P128, P512, SC256, ORIGIN) runs the zxtime program; frame, clock, INT position (identical
    mode tables), INT count / repeat, 21 MHz loop counts and the picture equal MAME's; launcher + TRD / SCL RAM disk,
    Flex Navigator Enter, `/ret-fn` three times. Still open: the Peters Plus launcher with a TRD on the floppy is ACC-6
    (unchanged)
  - [x] Z2 (S-M) tape (2026-10-02): `Tape::ClockCount` / `SetBaseClockTimeBase` (Sprinter only, Q2), a TAP through 48
    BASIC's `LOAD ""` loads at 3.5 MHz and fails at 21 MHz (T-ZX-7, T-ZX-8)
  - [x] Z3 (M) "original waits" (2026-10-02): `SprinterOrigWaits`, the PLD's 4-T CT5 period (not 5.33 T), windows 1 and
    3 with `#7FFD` bit 2; A/B in tdd-zx-mode §11.1
  - [x] Z3 follow-up (Q1): the CT phase - **derived from the PLD** (2026-10-03, branch `sprinter-origwait-demos`): INT is
    a `CT5` rise, so the waits are 0, 2, 1, 0 T by T1 from INT mod 4 (tdd-zx-mode §3.3, §10 Q1; T-ZX-9
    `OrigWaits_ExactPatternFromInt`). Open, low: a board report (zxtime's average, an INT-relative probe) would confirm it
  - [ ] BIOS 3.06 Hotfix 2: DSS text does not scroll at the bottom line (MAME too; BIOS 3.06 of 2025 scrolls): find
    out whether HF2 needs a newer PLD bitstream or has a bug; ask the BIOS author
  - [ ] Owner's report (a), `/ret-fn` into the 128 menu on the second Ctrl+Alt+Del: not reproduced (tdd-zx-mode §11
    finding 3); the turbo-after-reset fix may be it. Ask for the exact steps (BIOS, mode, what ran, which keys; a held
    SPACE / ESC right after the reset swaps `/ret-fn` and `/ret-zx` by design)
  - [x] Owner's report (c), 2026-10-02: Flex Navigator's video mode not back after the ZX mode and a reset - done
    2026-10-02 (branch `sprinter-zx-reset-video`, tdd-zx-mode §11 finding 5): `/RESET` presets ALL_MODE `#FF`,
    clears RGMOD / PORT_Y (PLD), BIOS 3.07 BETA 1 reads ALL_MODE back; MAME gap B8
  - [ ] Owner's report (b), "Disk Error after the catalog" from a RAM-disk TRD: not reproduced on 11 images; ask for
    the image. The "comdos" catalog was TWIX's disk (finding 4)
  - [x] Temporal effects in the Spectrum mode (2026-10-03, branch `sprinter-temporal-effects`; owner report "ZX DLSS
    does not work on the Sprinter"): the ZX DLSS input came only from the ZX per-T renderer's plane B; now the
    Sprinter renderer writes plane B too (`DrawSpanPlaneB`), `ScreenSprinter::TemporalInput` hands over the 352 x 288
    ZX frame of the Spectrum squares and the output goes back two pixels wide; native modes report "not applicable".
    Across the Edge: paper plane B identical to a Pentagon on the same frames
    ([temporal-effects-manager.md](../2026-09-27-zxdlss-gigascreen/temporal-effects-manager.md) §7 "Machine support")
  - [x] Spectrum mode: the border one character (8 ZX pixels) ahead of the paper (owner report 2026-10-03, Across
    the Edge in P128; branch `sprinter-zx-border-phase`): the border color was drawn from the port callback (IORQ);
    the PLD latches it on `/IOWR` rising, at the I/O cycle's end (`SP2_ACEX.TDF:310-315`), and samples it with the
    attribute every half T. Now drawn from IORQ + 4 T (`ScreenSprinter::CatchUpToBorderLatch`): Across's split-screen
    border edge 168 -> 176 against the paper's 176, as the PENTAGON model ([research-zx-mode.md](research-zx-mode.md) §7.1).
    The PLD sources give IORQ + 3 T (edge 174, still visibly 2 ZX pixels ahead); the owner chose the PENTAGON picture
  - [ ] Check the border latch against a board (a photo / capture of Across the Edge's split screen on a Sprinter):
    `kBorderLatchAfterIorqT` is 4 T by owner decision, 1 T beyond the PLD's latch; one constant if a board says 3
  - [ ] Border in the Spectrum mode against a Pentagon (seen with Across the Edge, 2026-10-03): 8 ZX pixels less
    border at each side (blank squares in the launcher's table - check against MAME / a board) and a border color
    change 8 lines off in the bottom border
  - [x] Z4 (2026-10-03, branch `sprinter-zx-mode-report`; [tdd-zx-mode.md](tdd-zx-mode.md) §12): the ZX mode report
    (which mode file - SP.ZX / P128.ZX / ORIGIN.ZX ... - from the hardware and from the launcher's own text and option
    table in RAM, each option with its evidence, clock request / F12 / MHz, INT, ROMs by CRC, the decode of `#01FD` and
    the other ZX ports), the PLD journal (who changed CNF, turbo, `#1FFD`, `#7FFD`, ALL_MODE, the port table ... with
    frame, T, PC; also from a TTD recording), the TTD port journals on the Sprinter (no NeoGS without a ZX-bus), the
    Qt status line "ZX: Sprinter ZX (turbo req, 21 MHz, /1FFD)"; all five surfaces
  - [ ] The launchers parse `int-sc`, not the `/sc-int` that SC256.ZX and SCORPION.ZX carry: the Scorpion INT is never
    applied (both launchers' option tables; research §4 corrected). Report upstream (the `.ZX` files or the parser)
  - [ ] Z5 (M) snapshots into the ZX mode through the cell table; **bug found**: today the SNA / Z80 loaders
    write physical pages 0-7 on the Sprinter (system pages) and nothing refuses (goals FR-51). Q4 decided
    2026-10-02 (owner: yes, via the shared pipeline, lower priority): built on the shared snapshot pipeline
    ([proposal](../2026-10-02-snapshot-pipeline/proposal.md), PLAN #84, T3) - its P0-P3 first, the Sprinter commit policy is its P4
  - [ ] Z6 (M) `zx run` macro on all surfaces, recipe `.recipe/machines/sprinter-zx-mode.md`, TTD replay test
  - Open questions Q1-Q7 for the owner: [tdd-zx-mode.md](tdd-zx-mode.md) §10
- **Input and device extras (from [mame-gap-analysis.md](mame-gap-analysis.md), owner 2026-10-02: functional items
  only):**
  - Two extended Sega-style pads (8 directions, A/B/C/X/Y/Z, Start, Select): pad 1 selected by SIO B DTR toggles,
    pad 2 read on PIO A with PIO B bit 7, the select counters reset at each frame INT (gap I10, C13) - S-M; host
    gamepads through the shared input path, journaled for TTD, all five automation surfaces.
  - Serial mouse variants (Logitech 3-button, wheel, Mouse Systems) (gap I7) - S.
  - [x] CTC counter mode, TRG inputs and ZC/TO outputs (gap C11), the mouse baud from CTC ZC0 (gap I7) - done
    2026-10-02 (branch `sprinter-ctc-trg`): TRG0-2 = 875 kHz in real time, ZC/TO2 -> TRG3, ZC/TO0 -> SIO B;
    Bad Apple and dontBlink (both wait for the 48.83 Hz CTC 3 tick, vector #06) play with sound
    (`SprinterCtcDemo_Test`, env `UNREAL_SPRINTER_HDD`); TTD blob 29 v2; `state sprinter` z84c15.ctc shows the
    inputs, live counts and ZC/TO rates.
  - ATAPI CD on the Sprinter's IDE (S7 remainder: wire the shared ATAPI CD-ROM into `IDE_SPRINTER`, `ide0.slave`
    as in MAME); CD audio comes from the shared CDDA work, PLAN #83. **Recommended P2** (survey §10): include
    media change, eject and ATAPI boot (BIOS 3.06+), and test the DSS CD file system (`beta_cdfs`) and CDX 2025.
    **CD audio status (PLAN #83, 2026-10-02, branch `cdda`):** done in the shared drive; any Sprinter IDE unit is
    a CD drive with `CDn=1` or `device=cdrom` and plays audio on its own mixer row (`IdeControllerCd_Test`, the
    secondary slave). Left for S7: whether the shipped config puts the CD on `ide0.slave` (the BIOS detection
    screens change), and the real-software check with `CD_PLAY.TRD` (Peters Plus 2001, TR-DOS in Spectrum mode)
    and `CDPLAYER.FLX` ([2026-10-02-cd-audio](../2026-10-02-cd-audio/TODO.md)). Since branch `cd-folder-audio`
    (2026-10-02) the test disc `testdata/machines/sprinter/cd/music.cue` (untracked) is an Enhanced CD: audio
    tracks 1-3 first, the data track 4 in session 2 (what `CD_PLAY.TRD` should list as tracks 1-3); a folder of
    MP3 / FLAC / WAV files in the CD slot is an audio CD too. `CD_PLAY.TRD` plays (owner, live, BIOS 3.06 + DSS
    1.71); the Flex Navigator plugin `C:\FN\FLX\cdplayer.flx` did not - its PLAY MSF 00:02:00 - 80:00:74 was refused
    for the end past the lead-out, fixed per MMC-3 (only the start is checked); the plugin plays from track 1
    only and has no track skip ([disassembly](../../disasm/software/sprinter/cdplayer-flx/README.md)). The
    owner's "no INT after Play, FN stuck" (2026-10-02): INTs and FN keep working (PC #A441 is FN's idle HALT); the
    plugin's Stop / Pause / skip buttons are unimplemented, and its Eject was ignored by the drive - fixed on branch
    `cd-plugin-int` (START STOP UNIT stops the play, the tray opens).
  - Tape input `#FE` bit 6 on the Sprinter: a test through the shared tape path (gap I5) - S.
  - Not planned: commands to the keyboard (LEDs, reset, typematic rate; gap I2) - owner: not needed. (The board
    cannot send them either: the PLD drives KBD_CX = KBD_DX = GND, hardware-reference §13.)
  - Keyboard, open (2026-10-03): the owner's live GUI check of the overrun / focus fixes; a TTD replay that hands
    input back while the host holds other keys than the journal left held is not reconciled (the keyboard keeps
    the recorded keys down until the host presses and releases them); the PLD's own ZX matrix decoder (code `#40`
    from the wire, disabled while ALL_MODE bit 0 is set) is still the host's matrix keys - S.
- **Peripherals not yet planned (from [peripherals-survey.md](peripherals-survey.md) §8, 2026-10-02, re-ranked by
  developer interest in §10; priority order, functional items only):**
  - P2: CompactFlash identity check: DSS 1.71 boots from a disk that reports itself as a CF card (BIOS-TT
    `AUTOIDE`) - S. Raised from P3 (§10: CF fixes in BIOS-TT 2024-12 / 2025-05; CF is the usual disk).
  - P2 research, changed focus (§10): first a runtime configuration reload, that is LDConf with the MAME-pack
    `DEMOS/LDCONF` streams `STREAM.300-305` and back to Standard, plus the `ALL MODE` port restore (LDConf
    2026-09-27). Then the community logic firmware of 2026 (`k30.acx` / `k50.acx` of 2026-09-24): accelerator
    control codes `#80` / `#81`, 1 KB buffer, rectangle mode, X / Y clipping ("tmkonf", written by Andrei Holub in
    MAME, carried by Tolik-Trek's fork), the `ACEX.SCALE` port (removed from the BIOS start-up 2026-08-26). Build
    tmkonf only when a released bitstream and a program use it - S research, M build (survey Q2).
  - P2: correct the ISA research §7.1 row "Wild Sound XM player": the card is the ISA Wild Sound (Robus, STM32F405,
    AYX-32 compatible) - S, with the next ISA design edit.
  - P3 research, P4 build: ISA Wild Sound card (protocol from its author first; one known program, `prosiak.exe`) - L
    (survey Q3).
  - P4 (lowered from P3 by survey §10: no developer activity): Centronics printer port with a print-to-file
    printer: PIO A data and RDY strobe, PIO B bits 6 / 7, the SIO A / B status lines (BUSY, ACK, SELECT, PAPER END); one connector slot shared with the LPT Sega pad (PLAN #82); DSS
    `#5F PRINT` end to end, journaled for TTD, all five automation surfaces - S-M (survey Q1).
  - P4 on demand: SIO B as a COM port (the `MOUSE` connector holds the mouse or a `ComPort` peer) - S (survey Q4).
  - P4 on demand: sp2000-light board profile (no ISA slots, one IDE channel) - S (survey Q6).
  - P4 on evidence: 512 KB video RAM / 512 KB ROM of the sp2022d board - S-M (survey Q5).
- Queued after S6b I1: **S6c network cards** ([2026-10-02-sprinter-network](../2026-10-02-sprinter-network/TODO.md),
  roadmap row S6c): SN0 fixtures (S), SN1 NE2000 chip + slots (M), SN2 Ethernet gateway + RTL kit end to end (M-L),
  SN3 SprinterESP with the Sprinter ESP Network Kit ([sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi),
  `UNETESP.DLL`, owner: must be supported) (M; **built 2026-10-03**, see the network TODO), SN4 modem / SprinterSerial (S-M; ISA I4 PIO IRQ lines **built 2026-10-03**, branch `sprinter-isa-i4`, ISA tdd §14), SN5 3C509B (M), SN6
  host-LAN bridge (M, optional).
- Phases S0-S7 ([roadmap-and-plan.md](roadmap-and-plan.md) §1), PLAN row #59.
- Prerequisites (all before #59): shared infrastructure PLAN #60 (clock ratio, CMOS core and
  migrations, wait-state hook, per-model `Screen`, raw PC floppy loader, port-trace internal
  codes), TTD v2 (PLAN #40), video mappers (PLAN #42), media manager (PLAN #58),
  IDE core (PLAN #13a), ZX-Evo E2b keyboard event (PLAN #55), TSConf (PLAN #41).
- Floppy follow-ups after S3a ([roadmap-and-plan.md](roadmap-and-plan.md) §8):
  - optional: DD-mode turbo VG ending at the PLD read/write strobe (`TURBING`, `SP2_MAX.TDF:272-306`);
    not modeled, only seek time differs. Settle first which pins `WSTB`/`RSTB` are (research open
    question 2);
  - a Type II command (READ / WRITE SECTOR) does not re-run its ID search when the latch changes the rate
    mid-command (READ ADDRESS and Type I verify do); nothing seen needs it;
  - the WD1793 rate-retry state is not in the TTD blob (S7 decides with the `SprinterPld` blob);
  - the WD1793 time base in 3.5 MHz T-states under a hardware turbo is opt-in (Sprinter only): the other
    turbo machines (ATM3 / ZX-Evo, Scorpion, ATM710 turbo) still run the FDC N times fast; switching them
    moves the ATM3 CI-gate figures (device blobs) and needs a re-recording - a separate shared change;
  - FDC off bit (density write data bit 1, MAME) unverified in the PLD;
  - a MAME reference for the floppy boot time needs a MAME whose WD1793 PLL follows `set_clock_scale`
    during a command (0.289 does not).
  - [x] 3.07 BETA 1 "Invalid EXE file" for programs on a floppy (2026-10-03): firmware, not emulation - the
    beta's FDD driver returns with IY changed and DSS 1.71.57 relies on it; MAME shows the same; the DSS of
    the beta's recovery disk works ([bios-versions.md](bios-versions.md) §5.2). Tests:
    `Fdc_Bios307SectorReadLoop_HdSide1`, `SprinterFloppyExe_Test` (env-gated). Owner decision 2026-10-03: 3.07 BETA 1
    stays the default, unchanged; the warning is the BIOS report's `known_issues` (all surfaces, Qt status bar) and
    the recipes. Upstream: [upstream-bios-307-fdd-iy.md](upstream-bios-307-fdd-iy.md) (to send to the BIOS author).
  - [x] Upstream head check (2026-10-03): the public `beta` head `f546c4e` is the kept `sp2k-3.07-beta1.rom`,
    byte for byte (every beta commit since 2026-01-19 calls itself "3.07 BETA 1"); its FDD driver still
    changes IY. `make-bios.py` now fixes the default CMOS date (`--cmos-date`), which used to come from the
    host clock ([bios-versions.md](bios-versions.md) §3.2, §5.3).
  - [ ] When the BIOS author pushes the build with the newer fixes: build it, run `SprinterFloppyExe_Test` and
    dontBlink to the end logo, then add it or replace the default (owner decides).
- Flex Navigator (ACC-8, S4) stops after its splash: the BIOS `RESETD` RESTORE from track 71 (213 ms)
  outlasts the BIOS `WREST` wait (65 536 polls, ~184 ms here), the BIOS zeroes the track register and
  the RESTORE ends at track 9 ([roadmap-and-plan.md](roadmap-and-plan.md) §8). The wait needs at least
  68 T per poll at 21 MHz (ours ~59 T): settle with the origin of the wait rule (below).
- Configuration end, not visible in MAME (no PLD model): when CONF_DONE rises, the extra clocks
  before the PLD starts and the CPU reset (tdd-ports-memory §6). Needs the PLD sources or real
  hardware.
- BIOS 3.06 image (CRC `187f4382`, MAME's): copies exist in the owner's MAME pack (`roms/sprinter.zip`,
  used for the S3b MAME captures) and inside the official updater `UP306.EXE` on the DSS 1.71 floppy; not
  kept in `data/rom/` (the kept 3.06 is Hotfix 2, [bios-versions.md](bios-versions.md) §3.3).
- **Deferred (owner, 2026-10-02: secondary feature, plan only): BIOS flash emulation.** The 256 KB ROM
  is a flash chip (write enable = ROM page port `#5C` bit 4). Model it with the JEDEC flash device that
  already exists for NeoGS (`core/src/emulator/io/flash/flash29f040b.*`, a 256 KB 29F020-class variant
  with the IDs the BIOS updater checks), so `UP306.EXE` (DSS 1.71 floppy) can update the BIOS: the
  instance's ROM starts from the selected image (`[ROM] SPRINTER=`) and keeps every write until the
  instance is destroyed (resets keep it); a "save ROM to file" dump on every automation surface and in
  the GUI, an optional persistent flash file like `CmosFile=`; flash contents in the TTD state. End-to-end
  test: boot 3.04, run `UP306.EXE`, reboot, ROM CRC = `187f4382`. First step when it is picked up:
  identify the chip and IDs from the BIOS flash routines and `UP306.EXE`.
- Sound follow-ups after S6 ([s6-sound-outcome.md](s6-sound-outcome.md) §7): the stereo order on a real board
  (MAME swaps 16-bit stereo), the PLD's AY + CBL mix levels in one DAC word, MOD playback through the General
  Sound on the ISA ZX-bus adapter (S6b), a real-board check of the accelerator INT suspend (default now off).
- IDE follow-ups after S3b ([roadmap-and-plan.md](roadmap-and-plan.md) §9):
  - DSS 1.71 needs a BIOS newer than 3.04 (bios-versions.md §5.1): which BIOS function, and whether 3.05 does;
  - code `#29` (drive address) reads `#FF` (the shared core has no drive-address register);
  - the device register reads back with bits 7 / 5 set (MAME clears them); harmless for BIOS and DSS;
  - CD boot (BIOS-TT, not 3.04) and CD audio (MAME routes it from the primary slave): S7;
  - the GUI F-key rule has no automated test (the Qt widgets have none);
  - teardown use-after-free of the same class in `~PortDecoder_ATM710` (keyboard) and `~PortDecoder_TSConf`
    (memory), found with ASan in S3b, not fixed there (shared code, other machines).
- ACC-6 as written ("ESC at the boot menu → Spectrum mode") does not hold for BIOS 3.04: the Spectrum
  ROMs come from DSS `ZX\SPECTRUM.EXE`. BIOS 3.06 and later carry them (ESC works; research-zx-mode §5.1).
- Settled in S2: palette byte order R, G, B in video RAM; 640 graphics high nibble first; blank
  square = pen `#400`; HOLD power-on `#77` (hardware-reference §4.5, §6.3). Settled in S1: BIOS 3.04 never programs the Z84C15
  watchdog and sends no keyboard commands (SETUP `KeyboardInit` only sets SIO A, WR1 = 0: no
  Z84C15 interrupts, the keys are polled in the frame INT). Settled against MAME and the board files
  (2026-10-01, [roadmap-and-plan.md](roadmap-and-plan.md) §6.1): IDE with no drive reads `#FF`
  (no DD7 pull-down, LS245 inputs float high), the PLD ends the INT at the acknowledge. Still open:
  the runtime CONF_DONE timing (S1's full start resets after write 473 720 and boots, consistent with
  the static count); the CPU emulation approach, the origin of the wait rule and the PLD wait on
  Z84C15 port writes (pending `research-cpu-z84c15.md`); the unacknowledged INT length (PLD 32-64 T,
  MAME 32 T).
- Renderer speed (naive v1): `BM_SprinterRender_Logo` 512 µs per frame against 46 µs for the TS-Conf
  setup screen. Idea for the backlog: cache decoded squares (MAME's tilemap: the mode bytes and the
  source address once per square, invalidated by video RAM writes into the mode table or the
  square's source) and measure with the same benchmark.
- Hook 3, second half: a configuration module's own INT source (with the first module that needs
  it; Game does not: its INT is Standard's, the program waits with EI / HALT on the mode table's INT).
- S7: the `SprinterPld` TTD serializer (id 25, declared in S1 so TTD refuses to record until
  then), fast RAM in TTD (cache pages are not journaled), the video RAM region.
- After v1: DooM and Video PLD configuration modules, after analyzing their bitstreams
  against MAME (Game: done 2026-10-03, [game-configuration.md](game-configuration.md); its open points there §7:
  Mode0 bits 5-4, whether `/RESET` clears the grid offset).
- LDConf's `STREAM.300` / `.303` / `.305` (other Standard core builds?) run as Standard with "unknown bitstream";
  name them once their source is known.
