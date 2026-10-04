# Open Sprinter items - snapshot of 2026-10-03

A full list of what is still open on the Sprinter, taken on 2026-10-03 after the Spectrum-mode timing work
(INT at the PLD edge, border latch, PS/2 keyboard, ZX mode report) landed on master. [TODO.md](TODO.md) stays
the working list; this page is the owner-approved overview to come back to. Owner decision the same day: next
come the ISA slots and the network cards (section 3).

## 1. Demos from the MAME-pack hard disk (`DEMOS/`, 21 items)

- **Pass done 2026-10-03** ([demo-status.md](demo-status.md)): all 76 programs below `DEMOS/` verdicted with the
  fixed demo runner (the earlier "15 of 21 run" came from a runner bug that called every program running). They run,
  except: ~~GAME_00 (3 programs) and LDConf's `START.BAT` need the "Game" PLD configuration (V10, deferred)~~ - **done
  2026-10-03**, they run on the Game module ([game-configuration.md](game-configuration.md));
  BUYAN/20X20 stops with interrupts off (open, compare with MAME); WILDSND needs the ISA Wild Sound card;
  DNTBLINK is investigated separately. FBIRD and NOTHENG run after the CTC fix.
- scroller.trd in P128: 60 s at 3.5 MHz with no CNF / turbo change in the PLD journal; the 21 MHz jump does not
  reproduce (likely cured by the PS/2 overrun fix).

## 2. Spectrum (ZX) mode - doubtful (owner, 2026-10-03)

The owner marked every item of this section as **doubtful**: kept for the record, not scheduled, not to be
deleted. Take one only on the owner's request.

- Border against a Pentagon: 8 ZX pixels less border at each side (blank squares in the launcher's mode
  table? check against MAME) and a border color change 8 lines off in the bottom border.
- Flex Navigator: Enter on a `.trd` ran `C:\ZX\spectrum.exe sp.zx ...` although `C:\FN\FN.EXT` on the
  same disk says `p128.zx`; the same command line typed by hand starts P128 correctly. Find where Flex
  Navigator really takes its associations from.
- Z5: snapshots (SNA / Z80) into the Spectrum mode through the cell table. Known bug: today the loaders write
  the Sprinter's physical pages 0-7 (system pages). Waits for the shared snapshot pipeline (PLAN #84).
- Z6: a `zx run` macro on all five automation surfaces, a recipe, a TTD replay test.
- The launchers parse `int-sc`, not the `/sc-int` that `SC256.ZX` and `SCORPION.ZX` carry, so the Scorpion
  INT is never applied. A bug in the launcher or the mode files: report upstream.
- Owner reports not reproduced: `/ret-fn` into the 128 menu on the second Ctrl+Alt+Del; "Disk Error after
  the catalog" from a RAM-disk TRD (needs the image).
- Against a real board: the border latch (4 T after IORQ by owner decision, the PLD sources give 3 T). The CT
  phase of the original waits is derived from the PLD since 2026-10-03 (INT is a `CT5` rise: 0, 2, 1, 0 T by T1
  from INT, tdd-zx-mode §3.3); a board would only confirm it.
- Keyboard: a TTD replay that hands input back while the host holds other keys than the journal left held
  is not reconciled; the PLD's own ZX matrix decoder (code `#40` from the wire) is still the host's matrix keys.

## 3. Devices, in the owner's order of 2026-10-02

- **Network first:** ISA slots (I1, **done 2026-10-03**, branch `sprinter-isa-network`), then NE2000 Ethernet and
  the gateway (SN0-SN2, **done 2026-10-03**: the RTL8019AS kit runs end to end; open: host-side receive pause, TCP
  zero-window probes; PIO IRQ lines I4 **done 2026-10-03** (branch `sprinter-isa-i4`); the kit runs from a floppy only on BIOS 3.06 Hotfix 2, or from C: copied
  on the host - see §4, BIOS 3.07 BETA 1), SprinterESP (SN3, **done 2026-10-03**, branch `sprinter-esp-sn3`: `[ISA] Slot1=SPRINTERESP`, the ESP kit's `NETUP` / `PING` / `WGET` / `UNETESP.DLL` end to end
  on ESP-AT 2.2.2 and 2.2.1, TTD blobs 46 / 47; open: FTP / NTP / TELNET / TFTP / Gopher not yet run, the IRQ3 line reaches PB0 since ISA I4), modem / SprinterSerial (SN4 **built 2026-10-03**, branch `sprinter-sn4-modem`: the shared Hayes modem peer, the ISA modem card, SprinterSerial; BC-Term 1.11 dials a BBS and talks over the ISA interrupt; open: network Q12 / Q13, BC-Term file transfers), 3C509B (SN5)
  ([ISA](../2026-10-02-sprinter-isa/TODO.md), [network](../2026-10-02-sprinter-network/TODO.md)).
- Then: NeoGS behind the ZX-bus adapter in an ISA slot (S6b, ProPlay MOD playback); the mouse in the GUI
  through the shared MouseManager.
- P2: ATAPI CD on the Sprinter's IDE (media change, eject, ATAPI boot); the CompactFlash identity check;
  LDConf (reloading the PLD configuration at run time).
- Lower: two Sega-style pads, serial mouse variants, tape input (`#FE` bit 6), Centronics printer, SIO B
  as a COM port, the sp2000-light and sp2022d board profiles, the Wild Sound ISA card.

## 4. Small and deferred

- BIOS 3.06 Hotfix 2 does not scroll DSS text at the bottom line (MAME too): a question for the BIOS author.
- BIOS 3.07 BETA 1 (the default) with DSS 1.71.57: programs on a floppy do not start ("Invalid EXE file" /
  "Bad command or file name") and `copy` from the floppy writes 0 bytes. Firmware, not emulation (found 2026-10-03):
  the beta's FDD driver returns with IY changed; MAME agrees; a 3.07 build saving IY works, as does the DSS of the
  3.07 recovery disk. Owner decision: 3.07 BETA 1 stays the default, with a warning in the BIOS report
  (`known_issues`, all surfaces, Qt status bar) and the recipes. Open: send
  [upstream-bios-307-fdd-iy.md](upstream-bios-307-fdd-iy.md) to the BIOS author ([bios-versions.md](bios-versions.md) §5.2).
- Automation audit leftovers G16-G21: per-frame wait totals, a Qt view of the mode map / palettes / video RAM.
- Floppy leftovers: the WD1793 turbo time base on the other turbo machines, the FDC off bit.

- **ISA I4 (2026-10-03), settled from the PLD source:** when the PIO port B (or any on-chip source) and the PLD's
  `/INT` are pending together, the chip answers the acknowledge and the PLD presets its INT flip-flop on the same
  `/M1` + `/IORQ` cycle (`SP2_1K30.TDF:744`, `INT_X = !DFF(GND, INTT & KEYS.int, , (/IO or /M1) & ...)`): the frame /
  keyboard / Covox-Blaster INT pending at that moment ends there. `Z84C15Engine` now passes every acknowledge to the
  board's INT logic (it kept the PLD's INT pending before).
