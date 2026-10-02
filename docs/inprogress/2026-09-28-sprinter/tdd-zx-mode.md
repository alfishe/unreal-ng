# Sprinter Sp2000 — technical design: the ZX (Spectrum-compatible) mode

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Design, not built. Open questions in §10 wait for the owner |
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
| Snapshot parse/apply split | changed (shared, small) | `core/src/loaders/snapshot/loader_sna.*`, `loader_z80.*` (SZX later) | shared | the parsers already hold `_memoryPages[]` + registers; expose them as a `ParsedSnapshot` so a machine with its own paging can apply them; other machines keep the current apply. See Q4 |
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

Per the PLD (research §7.3): while ALL_MODE bit 2 = 0 and the CPU is not in turbo, a memory read or write
to `#4000-#7FFF`, or to window 3 when its cell holds a Spectrum screen page, is held while `CT5` = 0. `CT`
is the 42 MHz counter, period 64 clocks = 5.33 T, phase locked to the line (42 periods per 224-T line).

```text
phase   = (frameT * 12) mod 64             // 42 MHz clocks since the period start; frameT in 3.5 MHz T
wait    = (phase < 32) ? ceil((32 - phase) / 12) : 0   // whole T until the T2 sample sees CT5 = 1
```

Where the CT phase sits relative to the frame start must be measured once (Q1); until then the model
takes phase 0 at the frame start, in one constant. Gate: one bool `_origWaits` updated on ALL_MODE and
turbo writes; the access path tests it only on the already-taken "not fast RAM" branch, so the default
(off) costs one predictable branch. A/B benchmark per performance-guidelines (`BM_Sprinter*` frame loop).

### 3.4 Tape time base

`Tape::handlePortIn` uses `t_states + cpu.t` (CPU clocks). On a machine with a hardware clock ratio, the
opt-in `SetBaseClockTimeBase(true)` divides by `hw_turbo_ratio` the way the WD1793 does it
(`portdecoder_sprinter.cpp` S3a). Worked example: a 2 168-T pilot pulse lasts 2 168 base T = 13 008 CPU
clocks at 21 MHz; the ROM's edge loop at 21 MHz times it as ~6 times too long and rejects it — as on the
board. The fast-load trap is unaffected (it does not time pulses) and stays the convenience it is on
every machine; its on/off switch is the existing one.

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
| INT | Pentagon / Scorpion / Spectrum position by `FN_SYNC` | none (S1) |
| Contention | none | — |
| "Original waits" | §3.3 | new, off by default (follows ALL_MODE) |
| 21 MHz | memory and port waits as built | none |
| Tape | base-clock time base (§3.4) | new, opt-in |
| WD1793 | base-clock time base (S3a) | none |

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

## 9. Phased plan

| Phase | Content | Size | Depends on |
|---|---|---|---|
| **Z1** | Faithful path verification: T-ZX-2..6 on unreal-ng against the MAME captures; fix what fails (expected: nothing new in the emulator; possibly DSS 1.71 / launcher behavior) | S | S3b, S4 (done) |
| **Z2** | Tape: I5 test, the base-clock tape time base (Sprinter opt-in), T-ZX-7, T-ZX-8; one-line note in the shared tape docs | S-M | none; Q2 for other machines |
| **Z3** | "Original waits": model, gate, T-ZX-9, A/B T-ZX-10; Q1 for the CT phase | M | none (real-board measurement for Q1 is a follow-up) |
| **Z4** | `SprinterZxMode` state + automation of the `zx` block on all five surfaces, OpenAPI, MCP resource text | S-M | the automation audit P1 branch (`sprinter-automation`) merged first, to extend its `state/sprinter` instead of forking it |
| **Z5** | Snapshots: parse/apply split (SNA, Z80; SZX after), `SprinterZxSnapshot`, `SnapshotLauncher` routing and refusal, T-ZX-11, T-ZX-12, all surfaces | M | Z4; Q4 |
| **Z6** | `zx run` macro on all surfaces, recipe `.recipe/machines/sprinter-zx-mode.md`, T-ZX-13, T-ZX-14 (TTD) | M | Z1, Z4; S7-TTD (done) |

Total about M-L (5-7 weeks of focused work at the repo's scale, Z1 first). **No dependency on S6b
(ISA)**: General Sound in Spectrum programs is S6b's matter (research §7.4), not a ZX-mode feature. Z2 and
Z3 can run in parallel with Z1.

## 10. Open questions (each with a recommendation)

1. **Q1 — the `CT` phase of the "original waits".** Where the 5.33-T wait window sits relative to INT is
   not in any document; MAME has no model. *Recommendation:* ship Z3 with phase 0 at the frame start in one
   constant, and ask the Sprinter community (Telegram `zx_sprinter`) for a measurement with a small test
   program (the emulated test-program pattern: a timing loop printing T counts); adjust the constant.
2. **Q2 — base-clock tape time base for every turbo machine?** On ATM3 / ZX-Evo, Scorpion and ATM710 turbo
   the tape also speeds up with the CPU today. *Recommendation:* Sprinter-only opt-in now (as the WD1793
   was); a separate shared change for the others, because it moves their TTD fixtures.
3. **Q3 — show the BIOS TR-DOS drive table in the state?** It is BIOS-internal RAM. *Recommendation:* yes,
   read-only, only for the BIOS builds whose export file gives the address (3.06 HF2, 3.07 β), absent
   otherwise; never written by the emulator.
4. **Q4 — the snapshot parse/apply split in shared code.** Needed so the Sprinter can apply parsed pages
   its own way. *Recommendation:* do it (a `ParsedSnapshot` struct + the existing apply as the default);
   it is small, keeps the parsers single, and other machines with non-identity paging (ATM, TS-Conf) can
   use it later.
5. **Q5 — may `zx run` copy the file into an attached hard-disk image?** *Recommendation:* no: only into
   an attached host folder, or use a file already on a volume; refuse otherwise with the reason. Images
   are the user's data.
6. **Q6 — default mode for `zx run`.** *Recommendation:* the disk's own `SPECTRUM.CFG` (what a user gets
   typing `spectrum game.trd`); `mode` overrides; tape files default to `P128.ZX` because the default mode
   has `/turbo`.
7. **Q7 — report MAME's tape bug upstream?** *Recommendation:* yes, a one-line MAME issue/PR (`kbd_fe_r`:
   drop the `^ 0x40` or set bit 6 from the cassette like `spectrum.cpp`); our T-ZX-7 does not depend on it.
