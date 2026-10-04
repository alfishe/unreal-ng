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
  ~~BUYAN/20X20 stops with interrupts off~~ - a race in the demo (its accelerator routine ends with `EI`; MAME and
  other start moments confirm it, [demo-status.md](demo-status.md)), not our fault.
  WILDSND needs the ISA Wild Sound card. The dontBlink final-version crash seen 2026-10-03 was triggered from the
  host: the macOS Command key reached the machine as PS/2 Left Ctrl (`14 F0 14`), and a stray press during loading
  latched the PLD keyboard INT. Fixed in unreal-qt (branch `qt-mac-cmd-keymap`): Command is a host key, the
  Control key is Ctrl ([keyboard.md](../../features/keyboard.md#host-keys-on-macos)); re-check the demo without
  touching Command. FBIRD and NOTHENG run after the CTC fix.
- dontBlink (MAME-pack version) freezes in the "flowers" part (~305 s): a race in the demo, not an emulation fault
  ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §2.2). An interrupt between `LD SP,#3F74` and
  `LD HL,(#031D)` makes its SP-repair loop overwrite its own return address. It hits with BIOS 3.07 and 3.06
  Hotfix 2 alike, and MAME 0.289 freezes the same way in 4 of 6 runs. The final version also dies when Ctrl is
  pressed during loading (its init enables interrupts in IM 1 before its own handler is in place) - also the
  demo's; untouched, it plays.
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

- **Network: done 2026-10-03 / 04**, all on master ([ISA](../2026-10-02-sprinter-isa/TODO.md),
  [network](../2026-10-02-sprinter-network/TODO.md)): ISA I1 (slots) and I4 (IRQ lines to the PIO); NE2000 + the
  Ethernet gateway (SN0-SN2, the RTL8019AS kit); SprinterESP (SN3, the ESP kit, ESP-AT 2.2.1 / 2.2.2); the Hayes modem,
  the ISA modem card and SprinterSerial (SN4, BC-Term over the interrupt; Q12 = inactive inputs + the `PLUG` test plug,
  Q13 = high wins + a warning); 3Com 3C509B (SN5, the 3C509B kit). Each kit runs end to end with a TTD replay without
  the host. The kits run from a floppy only on BIOS 3.06 Hotfix 2 (the default), see §4.
  The **bridge to the host LAN is built** (2026-10-04, wired and Wi-Fi: the RTL kit gets a lease from the real router). Open: the kits' other programs (FTP, NTP, TELNET, TFTP, WTERM,
  Gopher, `UNET509B.DLL`), BC-Term's file transfers over the modem; the gateway's host-side receive pause and TCP
  zero-window probes; ISA I3 (RAM card), `DACK`, the 16550 character timeout.
  ISA Plug and Play (I9, owner 2026-10-04: design and build after SN6; no PnP model on the bus today).
- ~~Then: NeoGS behind the ZX-bus adapter in an ISA slot (S6b, ProPlay MOD playback)~~ **done 2026-10-04** (ISA I2,
  branch `sprinter-isa-i2-neogs`, [i2-outcome.md](../2026-10-02-sprinter-isa/i2-outcome.md)): ProPlay plays a MOD at
  MAME's pitch and timing; open: the MAME ISA I/O tap; the NeoGS RAM joins TTD with TTD v2 (owner, 2026-10-04).
  ~~The mouse in the GUI through the shared MouseManager~~ done (capture only while polled, automation on the board
  mouse: [2026-10-03-mouse-api-routing](../2026-10-03-mouse-api-routing/design.md)).
- P2: ~~ATAPI CD on the Sprinter's IDE (media change, eject, ATAPI boot)~~ done 2026-10-04 ([atapi-cd-boot.md](atapi-cd-boot.md));
  ~~the CompactFlash identity check~~ done 2026-10-04 (an IDE unit as a CF card, DSS 1.71 boots from it);
  LDConf (reloading the PLD configuration at run time).
- Lower: two Sega-style pads, serial mouse variants, tape input (`#FE` bit 6), Centronics printer, SIO B
  as a COM port, the sp2000-light and sp2022d board profiles, the Wild Sound ISA card.

## 4. Small and deferred

- BIOS 3.06 Hotfix 2 does not scroll DSS text at the bottom line (MAME too): a question for the BIOS author.
- BIOS 3.07 BETA 1 (the default until 2026-10-03; now 3.06 Hotfix 2) with DSS 1.71.57: programs on a floppy do not start ("Invalid EXE file" /
  "Bad command or file name") and `copy` from the floppy writes 0 bytes. Firmware, not emulation (found 2026-10-03):
  the beta's FDD driver returns with IY changed; MAME agrees; a 3.07 build saving IY works, as does the DSS of the
  3.07 recovery disk. Owner decision 2026-10-03: the default goes back to 3.06 Hotfix 2 until the author publishes
  his fixed build; 3.07 BETA 1 stays selectable with a warning in the BIOS report (`known_issues`, all surfaces,
  Qt status bar) and the recipes. Open: send
  [upstream-bios-307-fdd-iy.md](upstream-bios-307-fdd-iy.md) to the BIOS author ([bios-versions.md](bios-versions.md) §5.2).
  Checked 2026-10-03: the public upstream head (`beta` `f546c4e`) **is** the kept 3.07 BETA 1, byte for byte,
  and still changes IY; the author's newer fixes are not pushed. Waiting for the author to push the build
  ([bios-versions.md](bios-versions.md) §5.3).
- Automation audit leftovers G16-G21: per-frame wait totals, a Qt view of the mode map / palettes / video RAM.
- Floppy leftovers: the WD1793 turbo time base on the other turbo machines, the FDC off bit.
- ~~DooM and Video PLD configurations (gap V12)~~ - **closed 2026-10-03, not planned**: they exist only as
  Sprinter 97 (FLEX EPF10K10) bitstreams, no Sp2000 build exists, and the Sp2000 merged their functions into
  Standard (DooM's line stretching = the accelerator's `#C7` scale register; Video = `HDD_FLIP` / `HDDR`); the
  Sp2000 DOOM demo and the 2026 video player run on Standard ([pld-configurations.md](pld-configurations.md) §6).

- **ISA I4 (2026-10-03), settled from the PLD source:** when the PIO port B (or any on-chip source) and the PLD's
  `/INT` are pending together, the chip answers the acknowledge and the PLD presets its INT flip-flop on the same
  `/M1` + `/IORQ` cycle (`SP2_1K30.TDF:744`, `INT_X = !DFF(GND, INTT & KEYS.int, , (/IO or /M1) & ...)`): the frame /
  keyboard / Covox-Blaster INT pending at that moment ends there. `Z84C15Engine` now passes every acknowledge to the
  board's INT logic (it kept the PLD's INT pending before).
