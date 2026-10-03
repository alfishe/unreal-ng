# Sprinter Sp2000 — technical design: the ZX (Spectrum-compatible) mode

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Z1-Z3 built on branch `sprinter-zx-timing` (2026-10-02, as built: §11); Z4 built 2026-10-03 on `sprinter-zx-mode-report` with the PLD journal and the Sprinter TTD port journals (§12); Z5-Z6 open. Owner decisions in §10 |
| **Research** | [research-zx-mode.md](research-zx-mode.md) (how the real machine does it, MAME runs) |
| **Plan** | phase **S8** (Z1-Z6) in [roadmap-and-plan.md](roadmap-and-plan.md) §1, [TODO.md](TODO.md) |
| **Parallel work** | `sprinter-automation` (state/automation, audit P1/P2), `sprinter-mouse`, the ISA design [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/tdd.md) (S6b). This design does not repeat them; it names the points where it plugs into them |

## 1. Goal and principle

A user of the emulated Sprinter runs Spectrum software **the way a Sprinter owner does** (DSS launcher,
ESC at the BIOS prompt, TR-DOS commands, a real floppy, the tape input), and gets the same result as the
real board and MAME. On top of that, automation gets **convenience paths** that save typing but are
clearly marked as not happening on real hardware.

The faithful path needs almost no new emulation: the ZX mode is the Standard PLD configuration with other
register values, and the RAM disk is software (research §5.4). The work is mostly verification, three
hardware details that are missing or wrong (tape time base under turbo, the "original waits", snapshot
writes going to the wrong pages), a ZX-mode state view, and the convenience paths.

Worked example of the split:

- **Faithful:** `type_input` "spectrum atarin.trd" + ENTER at the DSS prompt, ENTER at the menu, R ENTER
  in TR-DOS. The file comes from the emulated hard disk through DSS, the launcher and TR-DOS 7.03.
- **Convenience:** `POST /api/v1/emulator/{id}/sprinter/zx/run {"file": "atarin.trd"}`: the emulator puts
  the file where DSS can see it and types the same command for the user (§5.3). Nothing is poked into BIOS
  tables.
- **Convenience that is not on real hardware:** loading `action.sna` into the running ZX mode (§5.4). The
  reply says so (`"hardware_path": false`).

## 2. Components and ownership

| Component | New / changed | Where | Owner | Why there |
|---|---|---|---|---|
| `SprinterZxMode` (detector + state view) | new | `core/src/emulator/ports/models/sprinter/sprinterzxmode.{h,cpp}` | Sprinter only | reads PLD cells and the CNF / ALL_MODE bits; no other machine has vROM |
| `SprinterZxSnapshot` (apply a parsed snapshot to Spectrum pages) | new | `core/src/emulator/ports/models/sprinter/sprinterzxsnapshot.{h,cpp}` | Sprinter only | the page resolution is the Sprinter cell table |
| Snapshot parse/apply split | changed (shared, small) | `core/src/loaders/snapshot/loader_sna.*`, `loader_z80.*` (SZX later) | shared | the parsers already hold `_memoryPages[]` + registers; expose them as a `ParsedSnapshot` so a machine with its own paging can apply them; other machines keep the current apply. See Q4: decided 2026-10-02, done as the shared [snapshot pipeline](../2026-10-02-snapshot-pipeline/proposal.md) (`SnapshotImage`, PLAN #84) |
| `SnapshotLauncher` routing | changed (shared, one branch) | `core/src/loaders/snapshot/snapshotlauncher.cpp` | shared | Sprinter: in ZX mode → `SprinterZxSnapshot`; otherwise refuse with a reason (goals FR-51) |
| "Original waits" | new | `core/src/emulator/ports/models/sprinter/sprinterwaits.{h,cpp}` (+ the memory access path that already charges the 21 MHz waits) | Sprinter only | PLD `WAIT_ORIG` (research §7.3); zero cost when ALL_MODE bit 2 = 1 or turbo (the default) |
| Tape time base on clock-ratio machines | changed (shared, opt-in) | `core/src/emulator/io/tape/tape.cpp` | shared, opt-in per model (as the WD1793's `SetBaseClockTimeBase`) | real tapes play in real time; the Sprinter turns it on; other turbo machines are a separate decision (Q2) |
| ZX-run macro | new | `core/src/emulator/ports/models/sprinter/sprinterzxrun.{h,cpp}` (logic) + the automation surfaces | Sprinter only | composes existing media and keyboard automation |
| Automation (state, run, snapshot) | changed | WebAPI, OpenAPI, MCP, CLI, Lua, Python | per surface | §6 |
| Recipe | new | `.recipe/machines/sprinter-zx-mode.md` | docs | §6.6 |
| Tests | new | `core/tests/emulator/machines/sprinter/sprinterzxmode_test.cpp` and friends | tests | §8 |

Nothing in the WD1793, Beta-128, TR-DOS selection rule, video, interrupt or sound code changes: ZX mode
uses them as they are (research §10).

## 3. Memory, ports, PLD

### 3.1 No new PLD configuration module

The ZX mode runs on the Standard configuration module. The launcher and `GOTO_SPECTRUM` only change the
port table (one `DCP_CONFIG` record for the AY write port), the cells (`#E0-#EF` vROM, `#F0-#FF` Spectrum
pages, `#C0`/`#C1` `#1FFD`/`#7FFD`), the CNF/SYS byte and ALL_MODE. All of these are modeled (S1) and
match MAME's `update_memory`. TR-DOS 7.0x's `/Sprinter 1`, `/Sprinter 2`, `/AY` commands reload one of the
other PLD configurations in ROM: those run the Standard module with the "unknown bitstream" warning until
their modules exist (TODO "After v1"); not needed for ZX software.

### 3.2 `SprinterZxMode`: when is the machine "in ZX mode"?

There is no ZX-mode bit in the hardware. The detector reads the PLD:

```text
zxActive = ramSys == 1                // window 0 shows RAM (vROM), not the system ROM
        && (allMode & 0x01) == 0       // ZX keyboard + Spectrum screen shadow on, accelerator off
        && vromMapped                  // window 0's page is one of cells #E0-#EF
```

Worked example (MAME run §9.2 of the research): CNF `#07`, ALL_MODE `#FE`, `ram_sys` 1, window 0 = the BASIC
128 vROM → active. At the DSS prompt: ALL_MODE `#FF` → not active.

The state view (`SprinterZxModeState`):

| Field | From | Example |
|---|---|---|
| `active` | the rule above | `true` |
| `rom` | which vROM cell is in window 0: `basic128`, `basic48`, `trdos`, `expansion` | `trdos` |
| `turbo` | CNF/SYS bit 0 | `true` |
| `paging` | `#7FFD` / `#1FFD` enabled (CNF clean bits), `mem512`, `locked48` (PN5) | `{"7ffd": true, "1ffd": true, "mem512": false}` |
| `frame_lines`, `int_mode` | the INT source | `320`, `pentagon` |
| `original_waits` | ALL_MODE bit 2 = 0 | `false` |
| `spectrum_pages` | cells `#F0-#FF` → physical page | `[0x9F, 0x9E, ...]` |
| `vrom_pages` | cells `#E0-#EF` | `{"basic128": 0x8A, ...}` |
| `tr_dos_drives` | the BIOS drive table, **only** when the running BIOS is a known build (3.06 HF2, 3.07 β: address from the build's export file) | `["ramdisk E", "fdd B", ...]` or absent |

The drive table lives in BIOS RAM (system page), so it is read-only information, reported only for known
builds, never written (Q3).

### 3.3 "Original waits"

Per the PLD (research §7.3, corrected 2026-10-02): while ALL_MODE bit 2 = 0 and the CPU is not in turbo, a memory
read or write to `#4000-#7FFF`, or to window 3 while `#7FFD` bit 2 is set (`V_RAM = PN2`, `DCP.TDF:577`: Spectrum
pages 4-7), is held while `CT5` = 0. `CT[5..0]` is the video counter's low part: `CT[2..0]` counts 0, 1, 2, 4, 5, 6
(six 42 MHz clocks, `VIDEO2.TDF:285-293`) and `CT[5..3]` steps once per six clocks, so `CT5` is low for 24 clocks
and high for 24: a **48-clock = 4-T period**, 56 periods per 224-T line, one per 16-pixel square (the first reading,
64 clocks = 5.33 T, missed the mod-6 counter).

```text
fromInt = (accessStartT - kCt5RiseT) mod 4       // T1 of the access, counted from the INT edge (= a CT5 rise)
wait    = {0, 2, 1, 0}[fromInt]                  // T2 on the first / second CT5-low T, or on the high half
```

**The phase follows from the PLD (Q1 closed 2026-10-03, "derived from the PLD").** INT and the waits come from the
same counter, so the phase needs no frame-relative measurement:

| Step | PLD source | Consequence |
|---|---|---|
| `CT[2..0]` counts 0, 1, 2, 4, 5, 6; `CT[5..3]` steps on 6 -> 0 (`CT[5..3].ena = DFF(CT0 & CT2)`) | `VIDEO2.TDF:284-298` | `CT5` edges fall on the 6 -> 0 step, every 24 clocks; `CT5` low 24, high 24 |
| `CLK_Z80 = DFF(TFF(!CT2 & CT1) ...)`, clocked by `!CLK42` | `DCP.TDF:275` | at 3.5 MHz the toggle flips on `CT` = 2 (one per 6 clocks: a 12-clock T); no reset on the toggle or on `CT[2..0]` (both power up at 0), so every T starts 3.5 clocks after a 6 -> 0 step: a `CT5` edge is always 3.5 clocks (83 ns) before a T boundary |
| `INTT = DFF(..., CT5)`; `INT_X` set by the rising edge of `INTT` | `VIDEO2.TDF:394`, `SP2_ACEX.TDF:744` | the INT edge is a `CT5` rise: the first two T after it are `CT5` high, the next two low |
| `WAIT_ORIG = /MR or CT5 or ...`; the CPU samples /WAIT at the falling clock edge of T2 and of each wait state | `SP2_ACEX.TDF:558-559` | T1 at INT + 1 puts T2 on the first low T: 2 T; INT + 2: 1 T; INT + 0, + 3: none |

The sync-copy logic (`COPY_SINC_H`, `SP2_ACEX.TDF:814`) can clear `CT5` asynchronously; it never moves a `CT5` rise
off the 24-clock grid of `CT[4..3]`, so INT stays on a rise and the table holds. It only acts when the external
`SINC_1` input (`XA2` with `SXA` = 1) pulses.

A Verilator run of these lines transcribed to Verilog (the Sprinter verification package of 2026-10-03,
`orig_phase.csv`: every T of a 69 888-T frame as T1, read / M1 / window-3 cycles) gives the same 0, 2, 1, 0 over the
whole frame, the border included. In unreal-ng every INT the mode table can produce sits at frame T 14 + 4a of its
line (`SprinterIntSource`: square a read at T 12 + 4a, `CT5` 2 T later), i.e. frame T mod 4 = `kCt5RiseT` = 2;
`SprinterOrigWaits` counts from that one constant (by frame T1 mod 4: 1, 0, 0, 2 - the table the `kPhase` = 0
placeholder already had, so no program changes behavior). Left for a board: whether the released bitstream has
`WAIT_ORIG` at all (zxtime's average: 1.000 T per `LD A,(nn)` every 13 T if it does) and the one-T question of
where the Z84C15 samples INT against the 83 ns edge - an INT-relative probe would confirm the whole table.

Gate: the rule is a `MemoryWaitOverlay` (`SprinterOrigWaits`, `sprinterwaits.h`) that the decoder installs only
while it applies (`PortDecoder_Sprinter::ApplyOrigWaits`, called on ALL_MODE writes, turbo changes, resets and
TTD loads; window 3 follows `#7FFD` in `OnBanksChanged`). Every other mode and machine pays nothing: the overlay
is not in the bus chain. A/B: `BM_SprinterFrame_ScreenReads/0,1` and the existing `BM_Sprinter*` frame loops (§11).

### 3.4 Tape time base

`Tape::handlePortIn` used `t_states + cpu.t`: base T-states for the frames before, CPU clocks inside the current
frame. Under a hardware clock ratio the in-frame part ran six times faster and the sum stepped back at every
frame boundary. As built: `Tape::ClockCount()` is the one clock of the tape (port reads, the frame start, the
per-step EAR edge), and the opt-in `Tape::SetBaseClockTimeBase(true)` divides the in-frame part by
`hw_turbo_ratio_applied`, as the WD1793 does. The Sprinter's decoder turns it on in its constructor and off in its
destructor (the next model decides). Worked example: a 2 168-T pilot pulse lasts 2 168 base T = 13 008 CPU clocks
at 21 MHz; the ROM's edge loop at 21 MHz times it as ~6 times too long and rejects it - as on the board. The
fast-load trap is unaffected (it does not time pulses) and stays the convenience it is on every machine; its
on/off switch is the existing `fasttape` feature. Other turbo machines keep the CPU-clock time base (Q2).

### 3.5 Snapshots into the ZX mode

`SprinterZxSnapshot::Apply(const ParsedSnapshot&)`:

1. Refuse unless `SprinterZxMode::active`; refuse a 128K snapshot unless `/7FFD` paging is enabled; refuse
   pages beyond 7 unless `mem512` or `#1FFD` paging covers them. Each refusal names the reason and the
   `.ZX` mode that would work ("needs a 128K mode: start `spectrum p128.zx`").
2. For each Spectrum page n in the file: physical page = cell `#F0 + n` (`#F8+` for 8-15 per the CNF
   rules); copy 16 KB with `Memory::RAMPageAddress(physical)`.
3. `OUT (#7FFD)` through `PortDecoder_Sprinter::DecodePortOut` (so the PLD clean rules apply), border via
   `#FE`, then the registers, IM, IFF, and for SNA 48K the `RETN` stack pop, as the shared applier does.
4. TR-DOS paging flag (SNA 128 byte): through the TR-DOS signal rule, not by writing the latch.

Worked example: P128 mode on a 4 MB board, cells `#F0-#F7` = `#9F, #9E, ..., #98` (BIOS-allocated).
`action.sna` holds page 3 in its extra-page list → written to physical `#9C`; `#7FFD` = `#14` from the
header → window 3 = cell `#F4` = `#9B`; PC = `#D055`. The screen equals the Pentagon run of the same file
at the same frame count after the load (the frame is 71 680 T in both).

## 4. Timing

| Item | Value | Changes |
|---|---|---|
| Frame | 71 680 T (320 lines) or 69 888 T (312) | none (S1) |
| INT | the mode table's blank + INT squares (`FN_SYNC`): line 287 T 192 (320 lines), line 295 T 192 (`/origin /lines312`) in MAME; the PLD's edge, which unreal-ng uses since 2026-10-03, is 10 T earlier (T 182; research-zx-mode §7.1) | none (S1) |
| Contention | none | — |
| "Original waits" | §3.3: 4-T CT5 period, waits 0, 2, 1, 0 by T1 from INT (phase derived from the PLD) | new: on with ALL_MODE bit 2 = 0 at 3.5 MHz |
| 21 MHz | memory and port waits as built | none |
| Turbo after a CPU reset | the PLD presets its turbo bit (`DCP.TDF:663`, `TB_SW.prn = /RESET`): Ctrl+Alt+Del or a page-`#A0` write brings the CPU back at 21 MHz (front-panel switch permitting) | new (§11) |
| Tape | base-clock time base (§3.4) | new, Sprinter only |
| WD1793 | base-clock time base (S3a) | none |

### 4.1 Measured: unreal-ng against MAME (zxtime, 2026-10-02)

Each launcher mode of the owner's MAME-pack disk (`C:\ZX`, BIOS 3.06) runs `zxtime` (testdata/machines/sprinter/
zx-timing) as a user does: `spectrum <mode>.zx zxtime.trd` (SP / P128 / P512: the RAM disk; SC256 / ORIGIN: the
floppy, their TR-DOS reads only that), TR-DOS, `RUN`. MAME 0.289 `zxsp` with `-bios v3.06`
(`tools/machines/sprinter/mame-capture/mame-zxsteps.sh`); unreal-ng: `SprinterZxTimeModes_Test.LauncherModes`.
The INT position comes from the mode table: both emulators' video RAM mode tables were dumped at the menu and are
byte-identical in every mode; the positions follow from the same rule (MAME `update_int`, ours `SprinterIntSource`).

| Mode (options) | CPU | Frame (T) / rate | INT: line, T | INTs in 50 frames / repeats | 21 MHz loop passes per frame | Screen reads, extra T x 1000 (`#4000` / `#C000` p5 / p1) |
|---|---|---|---|---|---|---|
| SP.ZX (`/sprinter /turbo /7FFD /1FFD`) | 21 MHz both | 320 lines (PLD), not timed in turbo | 287, 192 both | 50 / 0 both | 7 164 both | not timed (turbo) |
| P128.ZX (`/7FFD`) | 3.5 both | 71 680, 48.83/s both | 287, 192 both | 50 / 0 both | — | 0 / 0 / 0 both |
| P512.ZX (`/turbo /7FFD /mem512`) | 21 both | 320 lines, not timed | 287, 192 both | 50 / 0 both | 7 164 both | not timed |
| SC256.ZX (`/turbo /7FFD /1FFD /sc-int /lines312`) | 21 both | 312 lines, not timed | **287, 192 both** (not 271: §11) | 50 / 0 both | 6 984 both | not timed |
| ORIGIN.ZX (`/7FFD /origin /lines312`) | 3.5 both | 69 888, 50.08/s both | 295, 192 both | 50 / 0 both | — | **ours 996 / 996 / 0, MAME 0 / 0 / 0** (MAME has no original waits; PLD: §3.3) |
| BIOS 3.06 ESC (own Spectrum mode) | 21 (ours) | — | 287, 192 | 50 / 0 | 7 164 | not timed |

**Re-run 2026-10-03** (unreal-ng only, branch `sprinter-origwait-demos`: INT at the PLD's CT5 edge since `a23aa1748`,
the original waits' phase derived from the PLD, §3.3; `SprinterZxTimeModes_Test.LauncherModes` with
`UNREAL_SPRINTER_HDD`): the INT column now reads **T 182** for unreal-ng in every mode (line 287; ORIGIN.ZX line
295), 10 T before MAME's 192; zxtime's handler first fetch at T 206-207 at 3.5 MHz, T 188 at 21 MHz. Every other
figure is unchanged: ORIGIN.ZX 69 888 T, 50 INTs / 0 repeats, **996 / 996 / 0** extra T x 1000 (`#4000` / `#C000`
p5 / p1; counts 14 592 / 13 928 / 13 928 / 14 592); P128.ZX 71 680 T, 0 / 0 / 0. That is expected: the derived
phase gives the same per-T table as the old placeholder, and `LD A,(nn)` every 13 T falls into alternating 2-T / 0-T waits (1 T per read) from any starting phase, so zxtime cannot see the phase.

- The picture: the Spectrum screen of the 128 menu is pixel-identical in both (736 x 288; the same raster origin
  and border).
- The INT pulse: 32 base T in both, ended by the acknowledge in both (MAME `irqack_cb` clears the line; zxtime's
  "REPEAT 0" shows it from the program's side, at 3.5 and at 21 MHz).
- The handler's first fetch lands at line 287 T 210-215 in MAME's debugger (`beamx` / 4; MAME's beam position
  inside a time slice is approximate) and at T 217 in ours (the fetch callback runs 3 T into the cycle).
- What the table does not show: the tape (MAME's cassette never reaches `#FE` bit 6, mame-gap I5; ours: §3.4,
  `SprinterZxTimeModes_Test.Tape_LoadsAt35MhzNotInTurbo`).

## 5. How images get in

### 5.1 Faithful paths (nothing new to build)

| Medium | How | Prerequisite in unreal-ng |
|---|---|---|
| TRD / SCL on the hard disk | `spectrum <file>` at the DSS prompt (`type_input`), or TR-DOS `/HDD`, `/LOAD E file` | the disk in `ide0.master` (CHD, VHD or raw), BIOS 3.06+ for launcher v2.03 / SCL |
| TRD on the floppy's FAT | `a:\zx\spectrum.exe a:\zx\pent128.zx a:\game.trd` | the DSS floppy in B or A |
| TRD as a floppy | TR-DOS on drive A (`fdd.a`) | done (ACC-6) |
| TAP / TZX / WAV | the tape slot, played into `#FE` bit 6; a non-turbo `.ZX` mode | §3.4; the I5 test |
| Host folder | the media manager's host-folder FAT volume (PLAN #58, the DSS folder boot) holding the TRD | S4 folder volume |

### 5.2 What is deliberately not offered

- **Writing a TRD straight into a BIOS RAM disk** (patching `RAM_TABLE`, `RAMD_KEYS`, `DISK_TYPE` in the
  system page). It depends on BIOS-internal layouts that change between builds (3.04 PP, 3.06, 3.07 β)
  and would leave the BIOS memory accounting inconsistent. The macro of §5.3 gets the same result through
  the software.
- **A WD1793 trap that serves a TRD from RAM** (ZX-Evo vdos style). The Sprinter has no such hardware.

### 5.3 Convenience: `zx run`

`SprinterZxRun::Run({file, mode, autostart})`, a scripted user:

1. Precondition: DSS at its prompt (`SprinterText` shows `X:\...>`), else refuse with the reason.
2. Make the file visible to DSS, preferring what does not modify the user's media:
   a. the file already on an attached volume (path inside the image or host folder) → use it;
   b. else the host-folder volume if one is attached → copy the file there;
   c. else refuse ("put the file on the disk or attach a host folder"). No silent writes into images (Q5).
3. Type `spectrum [<mode>.zx] <file>` + ENTER through the keyboard automation; wait for the Spectrum menu
   (screen OCR of the Spectrum screen, as `SpectrumScreenHas` in the tests).
4. `autostart`: ENTER on the menu's TR-DOS entry, then `RUN` ENTER (TRD/SCL), or "Tape Loader" + tape
   play (TAP/TZX in a non-turbo mode).

The reply lists every step taken and the state of `SprinterZxMode` at the end.

### 5.4 Convenience: snapshots

The generic snapshot endpoints (`load_software` MCP, `POST /api/v1/emulator/{id}/snapshot/load`, CLI `snapshot load`, Lua
`snapshot_load`, Python `snapshot_load`) reach `SnapshotLauncher`, which routes a Sprinter to `SprinterZxSnapshot` (§3.5). Outside ZX mode
the reply is a refusal with `"needs": "zx_mode"`; inside, `"hardware_path": false` marks it as an emulator
convenience. Saving a snapshot of the ZX mode in SNA/Z80 is out of scope (the Sprinter state needs TTD).

## 6. Automation (all five surfaces + docs)

| Surface | State | Run | Snapshot |
|---|---|---|---|
| WebAPI + OpenAPI | `GET /api/v1/emulator/{id}/state/sprinter` gains `zx` (§3.2); also `GET .../state/sprinter/zx` | `POST /api/v1/emulator/{id}/sprinter/zx/run` `{file, mode?, autostart?}` | `POST .../snapshot/load` (exists); Sprinter routing |
| MCP | `inspect_state` aspect `sprinter` (the `zx` part), `screen` reports "Spectrum screen" when active | `load_software` action `sprinter_zx_run` | `load_software` with a `.sna/.z80` |
| CLI | `sprinter zx` | `sprinter zx run <file> [mode] [--autostart]` | `snapshot load` |
| Lua | `emu:sprinter().zx` | `emu:sprinter_zx_run(file, mode, autostart)` | `emu:snapshot_load(path)` |
| Python | `emulator.sprinter()["zx"]` | `emulator.sprinter_zx_run(file, mode=None, autostart=False)` | `emulator.snapshot_load(path)` |

The automation audit (P1/P2, worked on in parallel by `sprinter-automation`) already adds a "the picture is
a Spectrum screen" flag (audit row 14); the `zx` block extends the same `state/sprinter` object and must be
merged with that branch, not built twice.

### 6.6 Recipe

`.recipe/machines/sprinter-zx-mode.md`: (1) faithful: boot the MAME-pack disk, `cd \trd`, `spectrum
atarin.trd`, menu, `R`; (2) ESC at the BIOS 3.06 prompt; (3) tape in `P128.ZX`; (4) `zx run`; (5) snapshot
in ZX mode; (6) back to DSS with Ctrl+Alt+Del; each verified on a build before it lands.

## 7. TTD

No new TTD state: the ZX mode lives in RAM, the PLD cells, the CNF / ALL_MODE bytes and the INT source,
all already in the Sprinter blobs (s7-ttd-outcome.md). The "original waits" derive from the frame position
and ALL_MODE (no state). The tape time-base flag is configuration. `zx run` and snapshot applies are inputs
outside the machine: a snapshot apply ends the recorded track the way a snapshot load does on other
machines (TTD sealed replay rule); typed keys are recorded as keys.

## 8. Tests

| Id | Test | Level | Data | Compared with |
|---|---|---|---|---|
| T-ZX-1 | `SprinterZxMode` detector truth table over the cells (DSS, BIOS, BASIC 128, TR-DOS, 48 locked) | unit | none | the rule |
| T-ZX-2 | ESC at the 3.06 HF2 prompt → menu "Sprinter", detector active, `rom = basic128` | L3 | kept ROM | MAME `mame-menu-sprinter.png` (text) |
| T-ZX-3 | launcher v2.03 + `atarin.trd` from the MAME-pack disk → TR-DOS 7.03 banner, `RUN` → the demo; RAM disk, no WD1793 command issued | L3 (`UNREAL_SPRINTER_HDD`, skip if absent) | MAME-pack image | MAME `mame-trdos-703.png`, `mame-atarin-ramdisk.png` (picture by Spectrum-screen digest once stable) |
| T-ZX-4 | launcher v2.03 + `bcity.scl` → the boot menu text "B.CITY-3" | L3 | same | MAME `mame-bcity-scl.png` |
| T-ZX-5 | `/ret-fn`: Ctrl+Alt+Del in the Spectrum → DSS screen with "EXIT from Spectrum mode" | L3 | same | MAME `mame-reset-back-to-dss.png` (MAME used RESET) |
| T-ZX-6 | Peters Plus launcher with a TRD on the DSS 1.62 floppy (`zx-format8.trd` written into the FAT image by the test) → RAM disk drive A, `LIST` | L3, CI | `dss_1_62_92.img` | ACC-6 catalog text |
| T-ZX-7 | tape: `ORIGIN.ZX`, Tape Loader, `greenberet.tap` in real time (fast load off) → the game's title screen; the same with `/turbo` does not load | L3 | MAME-pack image, `testdata/loaders/tap/greenberet.tap` | the 128K machine's title screen; MAME cannot (bug) |
| T-ZX-8 | tape I5 unit: EAR level → code `#40` bit 6, and the base-clock time base at ratio 6 | unit | synthetic pulses | the formula |
| T-ZX-9 | original waits: phase table, wait per access in `#4000-#7FFF` and window 3 screen pages, none in turbo / bit 2 = 1 / other addresses | unit | none | §3.3 |
| T-ZX-10 | original waits A/B: `BM_Sprinter` frame loop with the bit off vs on and against master | benchmark | none | performance guidelines |
| T-ZX-11 | snapshot in ZX mode: `action.sna` in `P128.ZX` → screen digest at frame N equals the Pentagon 128 run | L3 | `testdata/loaders/sna/action.sna` | Pentagon 128 |
| T-ZX-12 | snapshot refused at the DSS prompt and in a 48K-locked mode (reason text) | unit / L3 | same | — |
| T-ZX-13 | `zx run` on each surface (WebAPI, CLI, Lua, Python, MCP smoke) | integration | same as T-ZX-3 | the faithful run's end state |
| T-ZX-14 | TTD: record the T-ZX-3 session from the menu, replay bit-exact | L3 | same | TTD replay check |

Boot-bound tests use the fast start and `EnableTurboMode()` except where the pixels are asserted, and
justify their length in a comment (tests README).

As built (Z1-Z3), the tests behind the ids (`core/tests/emulator/machines/sprinter/`; the `UNREAL_SPRINTER_HDD` ones
skip without the owner's disk):

| Id | Test |
|---|---|
| T-ZX-2 (timing part) | `SprinterZxTimeEsc_Test.Pentagon_FrameIntAndNoWaits`: ESC at BIOS 3.06's prompt, zxtime from a TR-DOS floppy (no hard disk) |
| T-ZX-3 | `SprinterZxMode_Test.RamDisk_TrdAndSclBootToTheirPrograms` (VIBRATE!.SCL, KOL0BOK2.TRD: the "boot" in memory is the image's) |
| T-ZX-5 | `SprinterZxMode_Test.RetFn_CtrlAltDelReturnsToDssEveryTime` (SP, P128, SP; keys held 20 frames) |
| owner's CD_PLAY report | `SprinterZxModeFn_Test.FlexNavigator_EnterOnCdPlayTrd_ShowsItsCatalog` |
| Z1 table | `SprinterZxTimeModes_Test.LauncherModes` (§4.1) |
| T-ZX-7 | `SprinterZxTimeModes_Test.Tape_LoadsAt35MhzNotInTurbo` (zxtime.tap through 48 BASIC's `LOAD ""`, fast load off) |
| T-ZX-8 | `Tape_Test.BaseClockTimeBase_ScalesTheInFrameClock`, `SprinterZxTimeEsc_Test.Tape_BaseClockTimeBaseOnTheSprinter` |
| T-ZX-9 | `SprinterWaits_Test.OrigWaits_*` (phase table from INT, every mode-table INT on the CT5 rise, windows, the gate, LD A,(nn) timing, and `OrigWaits_ExactPatternFromInt`: per T1 = INT + 0..3 at the INT, in the top border, the paper and the bottom border, the M1 fetch, a read and a write of `#4000`, window 3 with and without `#7FFD` bit 2, window 2) |
| T-ZX-10 | `BM_SprinterFrame_ScreenReads/0,1` and `BM_Sprinter*` before / after (§11) |
| state | `SprinterDeviceState_Test.OriginalWaitsAndTapeAreReported` |
| program files | `SprinterZxTimeFiles_Test.CommittedFilesMatchTheSource` |

## 9. Phased plan

| Phase | Content | Size | Depends on |
|---|---|---|---|
| **Z1** (done 2026-10-02, §11) | Faithful path verification: T-ZX-2..6 on unreal-ng against the MAME captures; fix what fails (expected: nothing new in the emulator; possibly DSS 1.71 / launcher behavior) | S | S3b, S4 (done) |
| **Z2** (done 2026-10-02) | Tape: I5 test, the base-clock tape time base (Sprinter opt-in), T-ZX-7, T-ZX-8; one-line note in the shared tape docs | S-M | none; Q2 for other machines |
| **Z3** (done 2026-10-02; Q1 closed 2026-10-03) | "Original waits": model, gate, T-ZX-9, A/B T-ZX-10; Q1 the CT phase: derived from the PLD | M | none (a board measurement would confirm it) |
| **Z4** | `SprinterZxMode` state + automation of the `zx` block on all five surfaces, OpenAPI, MCP resource text | S-M | the automation audit P1 branch (`sprinter-automation`) merged first, to extend its `state/sprinter` instead of forking it |
| **Z5** | Snapshots: parse/apply split (SNA, Z80; SZX after), `SprinterZxSnapshot`, `SnapshotLauncher` routing and refusal, T-ZX-11, T-ZX-12, all surfaces. Done through the shared snapshot pipeline ([proposal](../2026-10-02-snapshot-pipeline/proposal.md), PLAN #84): its steps P0-P3 first, then `SprinterZxSnapshot` is the Sprinter's commit policy (pipeline step P4) | M (+ P0-P3 of the pipeline, about M) | Z4; Q4 (decided 2026-10-02); PLAN #84 P0-P3 |
| **Z6** | `zx run` macro on all surfaces, recipe `.recipe/machines/sprinter-zx-mode.md`, T-ZX-13, T-ZX-14 (TTD) | M | Z1, Z4; S7-TTD (done) |

Total about M-L (5-7 weeks of focused work at the repo's scale, Z1 first). **No dependency on S6b
(ISA)**: General Sound in Spectrum programs is S6b's matter (research §7.4), not a ZX-mode feature. Z2 and
Z3 can run in parallel with Z1.

## 10. Open questions (each with a recommendation)

1. **Q1 — the `CT` phase of the "original waits".** Where the 4-T wait window (5.33 T in the first reading, §3.3) sits relative to INT is
   not in any document; MAME has no model. *Recommendation:* ship Z3 with phase 0 at the frame start in one
   constant, and ask the Sprinter community (Telegram `zx_sprinter`) for a measurement with a small test
   program (the emulated test-program pattern: a timing loop printing T counts); adjust the constant.
   **Owner decision (2026-10-02): as recommended** - a placeholder constant plus a hardware measurement.
   As built: `SprinterOrigWaits::kPhase` = 0; the program is `testdata/machines/sprinter/zx-timing/` (zxtime, with a
   README for people: what to run, what each line means, what to report). Its average-cost lines confirm or refute
   the 4-T model and the waits' presence; the phase itself needs a finer, INT-relative probe on the board (zxtime
   cannot see a 1-T shift: the Sprinter has no floating bus) - a follow-up once a board report confirms the waits.
   **Closed 2026-10-03: derived from the PLD** (§3.3). INT is clocked by the same `CT5` the waits follow, and the
   CPU clock's divider and `CT` both start at 0, so the waits are 0, 2, 1, 0 T by an access's T1 from INT (mod 4),
   in the border too; a Verilator run of the transcribed PLD lines agrees over a whole frame.
   `SprinterOrigWaits::kCt5RiseT` (= `SprinterIntSource::kCt5RiseT`, 2) replaced `kPhase`; T-ZX-9
   (`SprinterWaits_Test.OrigWaits_ExactPatternFromInt`) checks the per-T table from a live INT. A board measurement
   (an INT-relative probe) would still confirm it.
2. **Q2 — base-clock tape time base for every turbo machine?** On ATM3 / ZX-Evo, Scorpion and ATM710 turbo
   the tape also speeds up with the CPU today. *Recommendation:* Sprinter-only opt-in now (as the WD1793
   was); a separate shared change for the others, because it moves their TTD fixtures.
   **Owner decision (2026-10-02): Sprinter only now.**
3. **Q3 — show the BIOS TR-DOS drive table in the state?** It is BIOS-internal RAM. *Recommendation:* yes,
   read-only, only for the BIOS builds whose export file gives the address (3.06 HF2, 3.07 β), absent
   otherwise; never written by the emulator.
4. **Q4 — the snapshot parse/apply split in shared code.** Needed so the Sprinter can apply parsed pages
   its own way. *Recommendation:* do it (a `ParsedSnapshot` struct + the existing apply as the default);
   it is small, keeps the parsers single, and other machines with non-identity paging (ATM, TS-Conf) can
   use it later.
   **Owner decision 2026-10-02: yes, via the shared pipeline proposal, lower priority**
   ([2026-10-02-snapshot-pipeline/proposal.md](../2026-10-02-snapshot-pipeline/proposal.md), PLAN #84, T3). The `ParsedSnapshot` of this design
   becomes the shared `SnapshotImage`, and `SprinterZxSnapshot` becomes the Sprinter's commit policy.
5. **Q5 — may `zx run` copy the file into an attached hard-disk image?** *Recommendation:* no: only into
   an attached host folder, or use a file already on a volume; refuse otherwise with the reason. Images
   are the user's data.
   **Owner decision (2026-10-02): yes, through the change layer** - `zx run` may copy the file into an attached
   hard-disk image, but only into its change layer (the shared CHD-style layer; the base image is never written),
   and the layer can be discarded. A host folder or a file already on a volume needs no copy.
6. **Q6 — default mode for `zx run`.** *Recommendation:* the disk's own `SPECTRUM.CFG` (what a user gets
   typing `spectrum game.trd`); `mode` overrides; tape files default to `P128.ZX` because the default mode
   has `/turbo`.
7. **Q7 — report MAME's tape bug upstream?** *Recommendation:* yes, a one-line MAME issue/PR (`kbd_fe_r`:
   drop the `^ 0x40` or set bit 6 from the cassette like `spectrum.cpp`); our T-ZX-7 does not depend on it.

## 11. As built: Z1-Z3 (2026-10-02, branch `sprinter-zx-timing`)

**Code.**

| Change | Where |
|---|---|
| `SprinterOrigWaits` (the PLD's WAIT_ORIG: 4-T CT5 period, `kPhase` placeholder (replaced 2026-10-03 by the derived `kCt5RiseT`, §3.3), windows 1 and 3-with-`#7FFD`-bit-2) and the decoder's `ApplyOrigWaits` gate | `core/src/emulator/memory/sprinter/sprinterwaits.h`, `portdecoder_sprinter.{h,cpp}` |
| `Tape::ClockCount`, `SetBaseClockTimeBase` (the tape's one clock; base T under a hardware ratio); the Sprinter turns it on | `core/src/emulator/io/tape/tape.{h,cpp}`, the Sprinter decoder's constructor / destructor |
| The turbo bit preset by a CPU reset of the running configuration (`TB_SW.prn = /RESET`): DSS comes back at 21 MHz after Ctrl+Alt+Del from a 3.5 MHz mode (it came back at 3.5 MHz and lost the first key typed at the DSS prompt). MAME keeps its `m_turbo` across a reset (a MAME gap now) | `PortDecoder_Sprinter::ResetPld` |
| `clock.original_waits` and `tape` in the Sprinter state (one builder: WebAPI `/state/sprinter`, MCP `inspect_state sprinter`, CLI `state sprinter`, Lua `emu:sprinter()`, Python `emulator.sprinter()`); OpenAPI text, MCP summary line | `sprinterdevicestate.cpp`, `openapi_state.inc`, `mcp-tools.cpp` |
| zxtime, the ZX timing program (asm, .trd, .tap, .sym, README) | `testdata/machines/sprinter/zx-timing/` |
| MAME session tool: `dbg` (headless debugger, `SPC_DEBUG=1`), `vram`, `fields`, `hardreset`, `SPC_FLOP1`, `ZXK_SEP` | `tools/machines/sprinter/mame-capture/mame-zxsteps.{sh,lua}` |

No TTD format change: the original waits derive from ALL_MODE, the clock and `#7FFD` (all in the PLD blob) and are
re-derived on load (`OnTtdStateLoaded` -> `ApplyTurbo` -> `ApplyOrigWaits`); the tape's time base is configuration.

**Findings while verifying.**

1. **Scorpion INT (`/sc-int`)**: with launcher v2.03 on BIOS 3.06 the INT stays at line 287 T 192 - in MAME too, with
   byte-identical mode tables. The 271 of research §7.2 came from `FN_SYNC` captures of another path; the table there
   is corrected.
2. **BIOS 3.06 Hotfix 2 does not scroll the DSS text** at the bottom line, in MAME as well (MAME with HF2 in its
   v3.06 slot, `ver` x 7: the last lines overwrite each other); BIOS 3.06 of 2025 (MAME's) scrolls in both. After
   Ctrl+Alt+Del the launcher's "EXIT from Spectrum mode" therefore lands on the bottom line and the prompt
   overwrites it. Not an emulation difference; reported as an open point (TODO).
3. **Owner's report (a)**, `/ret-fn` going back into the 128 menu on the second Ctrl+Alt+Del: not reproduced - three
   rounds (SP, P128, SP), keys held 3-50 frames, from the DSS prompt and from Flex Navigator (command line and
   Enter on a TRD), fast and full start, BIOS 3.06 HF2 / 3.06 / 3.07 beta: DSS every time. The launcher swaps
   `/ret-fn` and `/ret-zx` when it sees SPACE (`#7FFE` bit 0) right after the reset (`FIRST_PREPARE`); a held SPACE
   (or ESC, which the GUI maps to CAPS + SPACE) at that moment gives exactly the reported behavior. Fixed on the way:
   the 3.5 MHz return (above). Regression test: T-ZX-5.
4. **Owner's report (b)**, Enter on `CD_PLAY.TRD` showing a "comdos" catalog: our RAM disk shows CD_PLAY's own
   catalog (Title pp, SYSTEM / boot / CD_PLAY) through Flex Navigator's Enter, the DSS command line and TR-DOS; ten
   more images (TRD of 6 KB to 640 KB, odd sizes, SCL) catalog and boot correctly. `comdos.trd` is the TWIX kit's
   Commander DOS (`C:\UTILS\COMDOS`, `C:\UTILS\TWIX`): TWIX creates its own RAM disks (its menu item 1) and its
   disk was what drive A held. The launcher's TRD traces of BIOS / DSS calls are identical to MAME's (only HDD
   geometry words differ). "Disk Error after the catalog" did not occur from the RAM disk here; a likely source:
   ORIGIN.ZX and SC256.ZX run TR-DOS 5.04 builds that read only the real floppy - the launcher's RAM disk is
   invisible to them, so they catalog whatever disk is in `fdd.a` (or say "Disc Error" with none), and a catalog of
   one disk followed by loading another fails. Needs the owner's exact mode and image.
5. **BIOS 3.07 beta 1** returns to DSS with ALL_MODE `#FE` kept (the ZX keyboard and Spectrum screen shadow stay on
   in DSS); 3.06 sets `#FF`. **Our bug, fixed 2026-10-02** (branch `sprinter-zx-reset-video`; the owner's report
   "after the ZX mode and a reset Flex Navigator's accelerated video mode does not always come back"): the board's
   `/RESET` presets ALL_MODE to `#FF` and clears RGMOD and PORT_Y (PLD `SP2_ACEX.TDF:1041`, `:958`,
   `ACCELER.TDF:204`); `ResetPld` kept them, as MAME's `machine_reset` does. 3.07 BETA 1's reset intercept reads
   ALL_MODE back and writes the value it read (3.06 writes `#FF`), so the ZX mode's `#FE` survived Ctrl+Alt+Del and the
   RESET button: Flex Navigator drew with the accelerator off and the Spectrum screen addressing on (a black, broken
   picture). Deterministic per path, not timing: 3.07 BETA 1 failed after Ctrl+Alt+Del and after the RESET button
   every time, 3.06 HF2 never, a power cycle never - the "not always" is the BIOS and the way back. The full
   comparison with the cold start (PLD registers, accelerator, INT source, the Z84C15's system registers and wait
   generator, frame length, HOLD) differed only in ALL_MODE (and the accelerator it gates) and in the last CNF write
   (`#07` instead of `#04`: the same map and turbo, after 3.06 HF2 as well). HOLD now also returns
   to `#77` with every new configuration (its `/RES`). Tests: `PortDecoderSprinter_Test.CpuReset_PresetsAllModeClearsRgModAndPortY`
   (no disk), `SprinterZxResetFn_Test` / `SprinterZxResetFn307_Test` (env `UNREAL_SPRINTER_HDD`: Flex Navigator ->
   Enter on a TRD -> Ctrl+Alt+Del / reset / Ctrl+Alt+Del / reset / power cycle, each compared with the cold start:
   the mode registers, the accelerator and the picture). BIOS 3.04 cannot boot DSS 1.71 (bios-versions §5.1); the
   unit test covers it, the reset path is the PLD's, not the BIOS's.

**Live check** (2026-10-02, the GUI build on spare WebAPI / CLI / MCP ports, BIOS 3.06 HF2, the MAME-pack CHD with
ZXTIME.TRD): ORIGIN.ZX + zxtime printed the table's numbers; `clock.original_waits` (active, windows 1) and `tape`
(`base_clock`) read the same on WebAPI, MCP (`inspect_state` summary lines), CLI (`state sprinter`) and Lua
(`sprinter_state()`). Python reads the same builder but this build has `ENABLE_PYTHON_AUTOMATION=OFF` (the default),
so it was not run live. Recipe: `.recipe/machines/sprinter.md` "ZX-mode timing".

### 11.1 A/B (T-ZX-10)

`core-benchmarks` built from master `28c74d831` (before) and from this branch (after), run interleaved twice
(`--benchmark_repetitions=3`, medians of CPU time). The machine was shared: load average 18-31 (the
performance guidelines ask for below 12), so differences under ~5 % are noise.

| Benchmark | Before (round 1 / 2) | After (round 1 / 2) | Reading |
|---|---|---|---|
| `BM_HostFrame_Pentagon_Fast` (another machine: the tape clock is the only shared change) | 1 582 / 1 666 us | 1 661 / 1 638 us | equal within noise |
| `BM_HostFrame_Sprinter_Fast` | 3 174 / 3 234 us | 3 250 / 3 176 us | equal within noise |
| `BM_SprinterFrame_Logo` (BIOS 3.04, ALL_MODE bit 2 set: no overlay) | 3 994 / 4 152 us | 4 218 / 4 059 us | equal within noise |
| `BM_SprinterFrame_ScreenReads/0` (a frame of `LD A,(#4000)` at 3.5 MHz, waits off) | — | 2 612 us | the mode every program but ORIGIN.ZX runs in |
| `BM_SprinterFrame_ScreenReads/1` (the same with the original waits on) | — | 2 768 us | +6 %, paid only in ORIGIN.ZX |

## 12. As built: Z4, the ZX mode report and the PLD journal (2026-10-03, branch `sprinter-zx-mode-report`)

Why (owner, 2026-10-03): telling whether a session ran as "Sprinter ZX" (`SP.ZX`) or "Pentagon 128" (`P128.ZX`) took
long investigations - Across the Edge hangs only in the first, because `/1FFD` lets its `OUT (#01FD)` reach the
Scorpion latch. The answer must be one call on every automation surface, on a live machine and on a TTD recording.

### 12.1 The ZX mode report (`DeviceState::SprinterZxMode`)

One builder in `sprinterdevicestate.cpp`; `/state/sprinter/zx-mode` (and the `zx_mode` section of `/state/sprinter`),
CLI `state sprinter zx`, MCP `inspect_state` aspect `sprinter_zx_mode`, Lua `sprinter_zx_mode()`, Python
`emu.sprinter_zx_mode()`; the GUI status bar line and its tooltip (`SprinterZxModeBrief`).

| Part | Source | Example (SP.ZX) |
|---|---|---|
| `active` | window 0 shows a vROM page (`romOff`, not fast RAM) and ALL_MODE bit 0 = 0 (§3.2, without the `ramSys` term: the launcher's own vROM cells cover it) | `true` |
| `config.options[]` | the launcher's CNF byte E (`spectrum.asm` PARAMS add-up: turbo `#02/#03`, `/sprinter` map 0 `#04` else map 1 `#0C`, `/7FFD` `#00` else `#30`, `/1FFD` `#00` else `#40`, `/mem512` `#80`); `/lines312` = the frame latch; `/origin` = ALL_MODE bit 2 = 0; `/int-sc` = the INT line | `/sprinter /turbo /7FFD /1FFD`, CNF `#07` |
| `config.best_match` | the known mode files (community v2.03: SP, P128, P512, SC256, ORIGIN; Peters Plus: SPRINTER, PENT128, PENT512, SCORPION, ORIGINAL) scored by option, CNF and ROM-set differences; the launcher's text breaks ties | `SP.ZX`, `certain` |
| `launcher` | the `.ZX` text the launcher read (community: SHARED_PAGE `#FF` from `#0000`, NUL-terminated lines; Peters Plus: page `#41`), its option table (found by the names block `"turbo",#FF,0,"lines312",...` in the launcher's pages `#41:#FFF0-#FFF3`, else all RAM; an option is set when its two bytes are equal), the reset intercept (cell `#EE` = `#41`, `#41:#FFF0-#FFF6`), the BIOS system page's copy of CNF (`#FE:#013A`) | `"Sprinter ZX"`, `ret-fn` set |
| `clock` | `pld.turbo` (CNF request), `turboHard` (F12), `hw_turbo_ratio`, and why | `21 MHz: the CNF turbo request is on and ... F12 allows it` |
| `frame` | the frame latch, the INT positions from the mode table (`SprinterIntSource::ComputePositions`): line 287 Pentagon, 295 original, 271 Scorpion | 320 lines, INT pentagon, line 287 |
| `rom` | the CRC-32 of the pages in cells `#E0-#E3` against the launchers' ROM files and the BIOS flash copies | `sprinter-community` |
| `ports` | the live table (`SprinterPortTable::Index`, one formula with the decoder) for `#7FFD`, `#1FFD`, `#01FD`, `#3FFD`, `#5FFD`, `#9FFD`, `#BFFD`, `#DFFD`, `#FFFD`, `#FE`, `#1F` (`sprinterzxports.h`), TR-DOS off / on, OUT and IN: code, name, what the write does now (the CNF clean rules applied); `ttd_query` port / mask `#E0E7` for `/ttd/port-events` | `#01FD` OUT -> `#C0`: "the #1FFD latch: Scorpion paging" (P128: "stores cell #C0 only: CNF bit 6 'SC clean'") |

Findings: both launchers' tables know the word `int-sc`; SC256.ZX / SCORPION.ZX say `/sc-int`, which matches nothing -
the Scorpion INT is never requested (research §4 corrected, §7.2's line 287 explained). `/1FFD` is a CNF clean bit
only: `#01FD` and `#1FFD` are one table index (A12-A8 are not decoded) and decode to `#C0` in maps 0, 1 and 3.

### 12.2 The PLD journal (`MachineEventJournal`, `DeviceState::SprinterJournal`)

`core/src/emulator/machineeventjournal.h`: a generic, thread-safe ring (8 192 events) of a machine's configuration
events with seq, epoch (machine resets), frame, base T and PC; `PortDecoder::GetMachineEventJournal()` exposes it and
the video change log (`/video/changes`) lists the events of its frames (`machine_events`). The Sprinter decoder fills
it from the port handlers' own cases (cold code), on a change only: `cnf` (turbo request or CNF byte; the vROM-set
switch the ZX BIOS calls flip at `#3FD3` is left out), `clock`, `port_7ffd` / `port_1ffd` (new value or latch change,
with the port used), `all_mode`, `rgmod`, `hold`, `frame_lines`, `pld_load`, `pld_configured` (module, hashes),
`f12`, `ctrl_alt_del`, `reset` (power on, RESET, the page `#A0` soft restart), and `port_table`: page `#40` writes are
counted through a `SprinterMemory::BankAction::PortTable` set only while the journal is on, and the frame end emits
one event with the key ZX port decodes (all maps, both DOS states, both directions) that changed.

On by default; `POST /sprinter/pld-journal {"enabled": false}` removes the table watch and every event build (zero
cost). TTD: nothing is appended while a replay re-executes history (`ttdReplayActive`); an event earlier than the newest
one of its epoch means a seek followed by live running, and the later events are dropped (`rewound`).

From a recording: `?source=ttd` scans the TTD OUT journal for the ports that reach the tracked codes (the cubes of the
current table, map, DOS and PN5) and keeps the writes that change something. Every reply lists `ttd_queries` (port /
mask per kind) for `/ttd/port-events`. Worked example (verified live, Across the Edge in SP.ZX, recorded):
`frame 1873, T 65528, PC #88F1: #1FFD <- #17 via port #01FD` in the live journal, the same write at T 65527 from the
TTD journal (taken at the start of the I/O cycle) - Scorpion paging with RAM at `#0000`.

### 12.3 TTD port journals on the Sprinter

`/ttd/port-events` answered 409 on the Sprinter. Two reasons, both removed:

1. **NeoGS**: the shipped config fits a NeoGS the Sprinter's software cannot reach (it sits behind the not yet emulated
   ISA ZX-bus adapter). `PortDecoder::ZxBusPresent()` (the ISA design's seam, §2 there) is `false` on the Sprinter and
   `SoundManager::attachToPorts` removes the card `[SOUND] GSType` built. Phase I2 fits it again through the adapter.
2. **The engine guards** ("the interrupt source supplies the IM2 vector", "a machine engine stepped with the CPU"):
   `PortDecoder::TtdEnginesSealed()` = true on the Sprinter - its vector (Z84C15 daisy chain, the PLD's `#FF`) and its
   stepped engines (PLD resets, loader watchdog, CTC) follow the checkpointed state and the input journal only
   (s7-ttd-outcome.md). Every Sprinter replay test now runs with the journals feeding the recorded IN results.

The corpus fixture `testdata/machines/sprinter/ttd/boot.ttd` was re-recorded (no GS card, port journals present).

### 12.4 Tests

| Test | What |
|---|---|
| `MachineEventJournal_Test.*` | order, filters, the TTD rewind rule, epochs, the ring, off |
| `SprinterZxReport_Test.*` (`sprinterdevicestate_test.cpp`) | SP.ZX / P128.ZX / ORIGIN.ZX from the hardware state, the `#01FD` effect per CNF, the launcher text as authority, the brief line, the journal (CNF, `#1FFD` by port, change only, ALL_MODE, a port table write with the decode it changed, off = no watch) |
| `SprinterZxMode_Test.LauncherModes_ReportAndJournal` (`UNREAL_SPRINTER_HDD`) | the real launcher: SP, P128, ORIGIN as a user starts them; best match `certain`, CNF `#07` / `#4E` / `#4E`, options, ROM set, `/ret-fn`; the launcher's CNF write in the live journal and the same write (PC, value) from the TTD recording of the SP launch; Ctrl+Alt+Del journaled |
| `TTDSprinterMachine_Test.PortJournal_RecordsAndAnswersPortEvents` | no GS card, the journals record, `port-events` answers, `source=ttd` works, replay with 0 divergences / mismatches |
| `CliSprinterMachine_Test.ZxModeAndJournalRenderAsText` | `state sprinter zx`, `journal`, on / off / clear, errors |
