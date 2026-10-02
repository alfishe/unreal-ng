# Profi v3 / v5: technical design

**Date:** 2026-10-01 · part of [README.md](README.md) · requirements in [requirements.md](requirements.md)

## 1. Today

One model, `MM_PROFI`. It behaves like a v5 superset: palette, extended port map, RTC, Profi IDE, 1024K, and a
frame of 69888 T with INT 12580 T before the paper. Its parts are:

| Part | Where |
|:--|:--|
| Port decoder (if-chain, no mask table) | `core/src/emulator/ports/models/portdecoder_profi.{h,cpp}` |
| Hi-res renderer and its video mapper | `core/src/emulator/video/profi/screenprofi.*`, `profivideomapper.*` |
| Paging TTD state | `core/src/debugger/ttd/profi/ttdprofipaging.*` |
| Frame and INT | `core/src/emulator/config.cpp`, `MM_PROFI` cases (INI-driven and canonical geometry) |
| ROM roles | `core/src/emulator/memory/rom.cpp` (`PROFI_ROLES`, `profi_rom_path`) |
| Other `== MM_PROFI` checks | `memory.cpp`, `screen.cpp`, `emulator.cpp` (NMI to DOS), `devicestate.cpp`, `diskfastload.cpp`, `idecontroller.cpp`, CLI `cli-processor-state.cpp`, WebAPI `state_memory_api.cpp`, Qt `menumanager.cpp`, 15 test files |

## 2. Two models, one decoder, a board profile

**Decision:** v3 becomes a new model, `MM_PROFI3`, appended to the `MEM_MODEL` enum so no model is renumbered.
`MM_PROFI` stays and is the v5 board.

| Option | For | Against |
|:--|:--|:--|
| **New model `PROFI3`** (chosen) | The frame, the device set and the default ROM all differ, and the model is what the frame, the ROM roles and the automation model list key on. Every other emulator that has both (ZXMAK2, xpeccy-plus) made two machines. Same pattern as `SCORP` / `PROFSCORP` and `PLUS2` / `128K` | about 30 call sites learn the new id |
| `[PROFI] Board=3\|5` key | no new id | the frame geometry switch, `list_models`, snapshots and TTD headers would all need a second key next to the model; a WebAPI `start` by model name could not ask for v3 |

The two boards share almost everything, so they share one decoder class. Their differences are one value,
fixed when the model is created:

```cpp
// core/src/emulator/ports/models/profiboard.h
struct ProfiBoard
{
    bool palette;          // #xx7E palette and colour hi-res (v5); false: monochrome hi-res (v3)
    bool extendedPorts;    // the CP/M + ROM14 port map, RTC and IDE (controller v4.0 and up)
    bool floatingBus;      // v3.2: the screen byte reads back during the visible frame
    bool fePaletteBit7;    // #FE bit 7 from the palette (v5) or 1 (v3)
    static ProfiBoard For(MEM_MODEL model);
};
```

- `PortDecoder_Profi` reads `_board` instead of assuming v5. Each requirement R12-R21 is one `if` on one field,
  in the arm that already handles that port. Nothing moves to a shared path (G4).
- `bool IsProfiModel(MEM_MODEL)` in `platform.h` replaces every `== MM_PROFI` that is about "a Profi" (NMI to DOS,
  fast disk load, DOS-latch reporting, the video mapper). The checks that are about a board (IDE fitment in
  `idecontroller.cpp`, the RTC binding) ask `ProfiBoard`.
- `ProfiMonochrome` stays as an INI key, now meaning "a v5 board without its palette chips" (MAN v5.0: the
  "PROFI+ V4.02" colours). On v3, `palette == false` makes hi-res monochrome whatever the key says.

## 3. Firmware and configuration

| Item | v3 | v5 |
|:--|:--|:--|
| Config folder | `data/configs/profi3/unreal.ini` (new, from `profi/`) | `data/configs/profi/unreal.ini` (unchanged) |
| `[ROM]` key | `PROFI3=rom\profi\kramis-v02.rom` (`config.profi3_rom_path`) | `PROFI=rom\profi.rom` |
| `RAMSize` | 512 (1024 allowed) | 1024 (512 allowed) |
| `[HDD] Scheme` | `NONE` (`PROFI` is refused with a log line: the board has no IDE) | `PROFI` |
| ROM roles (`rom.cpp`) | SYS/Menu (Kramis), TR-DOS, 128K Editor, 48K BASIC | today's |
| Model name / short name | "Profi v3" / `PROFI3` | "Profi v5" / `PROFI` (alias `PROFI5`) |

`rom.cpp`'s `PROFI_ROLES` third entry reads "128K Editor + STS Monitor". That describes `profi.rom` only; the
factory images have the Pentagon 128 editor there. The role becomes "128K ROM", and the debugger's ROM
identification names the image, not the role.

## 4. Ports

### 4.1 What v3 drops

With `extendedPorts == false` the decoder never enters the CP/M + ROM14 branch. The RTC, IDE, the extended FDC
addresses and the Covox aliases `#C7`/`#A7` are then not decoded, and those ports read as an undecoded port (4.4).
`DecodeFDCPort` on v3 becomes: DOS latch only gives `#1F..#7F` and `#FF`; CP/M (any ROM14) gives `#1F..#7F` and `#BF`.
`IdeGate().profiExt` is false on v3, so the IDE adapter stays off even if an INI asks for it.

### 4.2 Shared fixes found on the way

These are not v3/v5 differences, but the cross-check confirmed them for both boards:

| Fix | Requirement | Evidence |
|:--|:--|:--|
| AY decode adds A13 (`(port & 0xE002)`), so `IN #DFFD` no longer reads the AY register | R11 | P3 |
| `IN #1F` outside DOS and CP/M = Kempston joystick (8255 port A); the working tree already has this, uncommitted | R19 | P12 |

### 4.3 The decoder PROM as the reference

Both boards' port decoder PROMs (K556RT4, 256 x 4) are now available, and their wiring is read; the result is [decoder-prom.md](decoder-prom.md):

| Board | Source | Kind |
|:--|:--|:--|
| v4.0 / 4.01 / 5.0 | printed in the v4.01 manual (book p.25) and the v5.0 album (p.8) | transcription; the two books are typeset differently and print the same bytes |
| v3.2 (U5) | `Profi_RT4_line.BIN` in the MDESK Profi 3.2 project ([alemorf/retro_computers](https://github.com/alemorf/retro_computers/tree/master/Profi_3_2), `doc/Profi3.zip`, read off a board in 2009) | dump. That project names it a character generator, but its content is the decoder: the same rows as v4/v5 plus row `D0`, the ADR15 input |

The v5 inputs are LADR5, LADR6, CP/M, ROM14, ADR7, ADR1, ADR0 and IORQ, and DD11 (ИД4) splits ADR2-ADR4 into P0-P7.
v3.2 has ADR15 where v5 has ROM14. Which input drives which PROM address bit is read off the schematics: the
v5.0 album sheet 1п, and the MDESK P-CAD netlist for v3.2.

1. Put both tables into `testdata/machines/profi/decoder/` (`556rt4-v3.2.bin`, `556rt4-v4-v5.bin`), with a README
   that gives the source, the page and the CRC32.
2. A test enumerates the 256 low bytes x CP/M x ROM14 x DOS latch, and asks both the PROM table and
   `PortDecoder_Profi` which device answers. The two must agree, which settles P8 / Q7 (the SYS-ROM extended map)
   for v5.
3. The same test on v3 uses the v3.2 table. A15 replaces ROM14 there, so the v3 port set (4.1) is checked against
   the board's own PROM, not just against the manuals.
4. The floppy data-separator PROMs ("ФАПЧ") of both boards are in the same collection; the emulator does not need
   them (the WD1793 model takes whole bytes).

### 4.4 Floating bus on v3

The v3.2 manual says the board reads back the screen pixel during the visible frame (P16). E3 traced it on the v3.2
schematic ([research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md) part B), and
`PortDecoder_Profi::FloatingBusV3` implements it (R21):

- Only the **pixel** byte: the pixel latch U9 drives the data bus through U57 while the paper fetch signal FLD1 is
  high. The attribute latch never reaches the bus.
- Only an `IN` with **A0 = 1** that no device answers; DOS and CP/M change nothing beyond which ports answer.
- **Timing:** the latch leads the displayed byte by one 4-T tick. With `d` = the T of the Z80's T3 minus the first
  displayed pixel of the line: `d` = -4 gives byte 31 of the line before (line 191 for line 0), -3..-1 byte 0,
  0..123 byte `d/4` at the tick's first T and `d/4 + 1` after it, anything else `#FF` (border, blanking, the last
  tick). The screen page is 5 or 7 per `#7FFD` bit 3.
- Hi-res (DS80) reads `#FF` (its fetch is not modeled). The v5 board has no such path: it reads `#FF`.

### 4.5 The #DFFD decode and the CP/M switch (v5, phase 7)

From the 5.06 netlist and the 5.0 album ([research-profi-v5-open-items.md](research-profi-v5-open-items.md)):

| Item | Board | Emulator |
|:--|:--|:--|
| Palette write | any `OUT` with A7=0, A0=0 while DS80 = 1; neither CP/M nor BLOCK gates it (the manual's sentence contradicts its own schematic and the BIOS) | unchanged |
| #DFFD decode | 5.0x: A13=0, A1=0 (A15 not decoded, so a 128K `OUT` to `#1FFD` writes #DFFD too); 5.06: high byte #DF, A1=0, and never from `OUT (n),A` (DD75 `/BLOCK`) | `[PROFI] DffdDecode=emulators` (A15=1, A13=0, A1=0, the default), `v50`, `v506` |
| CP/M switch | holds #DFFD (and the 5.06 `/BLOCK` flip-flop) cleared through the latches' clear input while pressed; #7FFD and the ROM lines untouched; the front-panel RST clears only #7FFD | `FrontPanelSwitch::Cpm` on v5 (`[PROFI] CpmSwitch`), on every surface next to TURBO; TTD records it, ProfiPaging byte 33 bit 1 |

The switch needs no link to the ROM pages. No other emulator or RTL models it (ZXMAK2, Xpeccy, xpeccy-plus,
UnrealSpeccy, pico-spec, Karabas-Pro: all reset into the SYS ROM with #DFFD cleared), and the emulated BIOS shows
where the manual's behavior comes from: with #DFFD held at #00 it cannot raise its hi-res menu and starts Spectrum
128 instead ("pressed = Spectrum 128"; `ProfiBoot_Test.CpmSwitchAtPowerOnStartsSpectrum128`).

## 5. Video timing

### 5.1 The sync PROM is a board option

Profi boards shipped with different sync PROMs, and they give different frames ([cross-check.md](cross-check.md)
section 4). The emulator consensus and xpeccy-plus's measured 71680 T frame are both right: they describe
different v3 PROMs. So the frame is chosen by a `[PROFI] SyncProm=` key, read by both models. Each value is one row
of a table in `config.cpp`:

| `SyncProm=` | PROM | `frame` | `t_line` | INT -> first paper | INT length | Default of |
|:--|:--|:--|:--|:--|:--|:--|
| `0a1d` | `0A1DFAFD` (v3/4; the original PROM of a 3.2 board) | 69888 | 224 | 12580 (today's value; the decode says 12584, within one tick) | 28 | **v3** |
| `samx6` | `15E9B638` (v3/4, Kondor) | 69888 | 224 | 12592 | 32 | |
| `fb0579b6` | `FB0579B6` (v3/4) | 71680 | 224 | 48 | 32 | |
| `v503` | `D2D4A7C8` (v5 boards, DD53 D1 grounded) | 69888 | 224 | 14368 | 28 | **v5** |

The defaults:

- **v3:** `0a1d`. `Profi_RF2.BIN`, the original sync PROM that the MDESK project read off a Profi 3.2 board
  together with its Kramis V0.2 system ROM and both 556RT4 PROMs, is byte-identical to `0A1DFAFD`. It also gives
  UnrealSpeccy's "thanks to DDp" preset to within one tick. `fb0579b6` stays selectable for boards like the one
  xpeccy-plus measured (photographs, timing test and Tact Meter on a v3.2 Kramis): that board ran a different
  PROM.
- **v5:** `v503`. Three things agree on it:
  - It is the PROM read off a Kondor 5.04 board, and the 5.03 replica uses it too.
  - With the board's DD53 wiring, which is in the maker's own fix list and in the 5.06 netlist, it gives a PAL
    64 us line and the 69888 T frame that TEST 4.30 measured on a 5.06.
  - That frame is today's, so only the INT position moves: from 12580 T to 14368 T.
  
  The v5 TTD fixtures and the timing tests that pin 12580 are re-recorded. `0a1d` keeps the old value available.

The INT length is 28 T on v5, from Gromov's 8-8.6 us. It is 32 T elsewhere until the INT circuit is traced (T5).
Each row's comment in the code names its PROM and the decode output line.

The table holds only checked rows:

- SAMX12 (`57D728AD`) is left out. Its frame is the same as `v503`, but it decodes to a 4 T INT, which no board
  can have; a trace of the INT circuit would settle it.
- The Pentagon-fix PROMs (`02BB2120`, `BCD770D5`) and the album's printed PROM (`B88AF9D1`, which does not run on
  a board with the fix) are left out too.

A PROM never runs as a live state machine: the raster stays the model's, and the PROM only chooses the numbers.

### 5.2 The raster

`profigeometry.h` derives the paper position from `t_line` and the frame. A 320-line frame needs no
new code path, only the numbers. `t_line` already drives the renderer, the INT counter and the TTD frame index for
the 228 T 128K. The border sizes come from the same decode output (the vertical runs "P192 b16 B32 V16 B47 b17"
and so on), so the visible border matches the PROM.

### 5.3 Hi-res (DS80)

In DS80 the generator runs from the 12 MHz crystal and the upper half of the PROM. The decode gives 48 ticks per
line (64 us) on the v3 PROMs and 46 ticks on the v5 ones. What the CPU clock is in that mode, and so how many T
make a frame, is not settled (the decode assumed 3 MHz; the open item from the 2026-09-21 design section 12 Q3).
Hi-res timing stays as it is today until that is settled; this design changes only the standard mode.

## 6. Turbo and wait states

### 6.1 The switch

Both boards switch turbo with a front-panel switch, not a port (R32). The emulator models it as a machine input,
`FrontPanelSwitch::Turbo`:

| Surface | Form |
|:--|:--|
| Core | `Emulator::SetFrontPanelSwitch` / `GetFrontPanelSwitch` (-1 on a machine without it); `PortDecoder_Profi::SetFrontPanelSwitch` sets `EmulatorState::profi_turbo_switch`, `hw_turbo_ratio` (the ATM / Scorpion mechanism, applied mid-frame by `Z80::ApplyHardwareTurboNow`) and installs or removes the wait overlay. `[PROFI] Turbo=1` sets it at power-on; a reset leaves it |
| CLI | `switch` (list), `switch turbo [on\|off]` |
| WebAPI / MCP | `GET /api/v1/emulator/{id}/switches`, `POST .../switches` `{"name":"turbo","on":true}` (OpenAPI; MCP through `invoke_api`, described in `unreal://machine/profi`) |
| Lua / Python | `emu:get_switch("turbo")` / `emu.get_switch("turbo")` (nil / None without the switch), `set_switch("turbo", true)` |
| Qt | Machine > TURBO Switch, checkable, enabled only on a Profi; the status line shows the clock |
| TTD | an outside input: `TTDInputKind::FrontPanelSwitch`, recorded at its T-state through `SubmitLiveInput` and replayed from the track (sealed replay); the switch position also travels in the `ProfiPaging` blob (byte 33, the former padding) |

On v3 the VG93's HLD pin is the board's `/TURBO` (E3, A3): a loaded floppy head holds 3.5 MHz while the switch is
on. `PortDecoder_Profi` follows HLD with a machine step hook, armed only while the switch is on, so a v3 at 3.5 MHz
pays nothing. The v5 drawings have no such link.

### 6.2 Wait states: one overlay, both boards

`ProfiWaitOverlay : HostBusOverlay` (`core/src/emulator/memory/profi/`), the `ScorpionTurboOverlay` pattern. It is
installed only while a rule can apply (v5 at 3.5 MHz with SB8 in its PROFI3+ position, or turbo on either board) and
adds waits only with the `contention` feature on. Other machines never see it (G4).

| Board, clock | Who waits | Extra CPU clocks per access | Source |
|:--|:--|:--|:--|
| v5, 3.5 MHz | RAM opcode fetch / read / write in the paper fetch window (192 lines x 128 T) | 1 on every other T; the parity is the power-on phase (`[PROFI] WaitPhase=0..3`, 1 = never) | E4, 6.4 |
| v5, 3.5 MHz | ROM reads | 0, or 1 with `[PROFI] RomWait=1` | E4 |
| v5, 7 MHz | RAM / ROM | approximation: RAM 1 in the border, 2 in the paper; ROM reads 1 | E4 section 3 |
| v3, 3.5 MHz | nobody | 0 | E3 |
| v3, 7 MHz | RAM opcode fetch / read / write, paper and border alike | 2 when T1 starts on an even 7 MHz clock, 3 on an odd one | E3, A4 |

ROM, I/O, interrupt acknowledge and refresh never wait on v3. "RAM" is the RAM select, so RAM paged at `#0000`
waits too. Hi-res (DS80) has no waits at 3.5 MHz, and none on v5 in turbo (its third crystal is not modeled).

The v3 rule is **not** taken from xpeccy-plus: E3 read it off the v3.2 schematic
([research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md) part A; the slot logic U27/U28 and
READYT) and then checked it against every figure available
([tools/machines/profi/turbomodel/](../../../tools/machines/profi/turbomodel/README.md)):

| Figure | Source | The rule gives |
|:--|:--|:--|
| Tact Meter 1.0: 88208 T per frame (code in RAM) | XP+, one v3.2 board | 88222 (`INC DE : JP`, 1.2308x); the other rules tried give 81920-104262 |
| Tact Meter 1.0: 143206 T (code in ROM) | XP+ | 139776 (ROM never waits: 2x) |
| "в 1.7 раза" (overall speed-up) | MAN v3.2 p2 | between 1.33x (RAM) and 2x (ROM), so a mix |
| 116920 T per frame | Unreal_NS `PRESET.PROFI_TURBO` | not reproduced; an unnamed meter, left as is |

XP+'s own model ("2 waits on a slot edge, 3 between, VG93 HLD drops turbo") agrees with the schematic.

### 6.3 v5 turbo

The v5 turbo arbitration depends on the history of the slot ring (E4 section 3), so the overlay uses the
approximation in the table above. TEST 4.30 on a real 5.06 shows 1.68x in turbo, the emulator 1.64x
([test-programs.md](test-programs.md)).

The third crystal ZQ3 (16-24 MHz) is settled (phase 7): DS80 selects it, no jumper or switch, and the CPU runs at
ZQ3 / 4, or ZQ3 / 2 in turbo, only in hi-res (4-6 MHz, 8-12 MHz); the 12 MHz crystal is the hi-res pixel clock
only. It is not modeled: the hi-res frame timing is open (5.3).

### 6.4 v5 video WAIT at 3.5 MHz

The v5 board holds a CPU RAM access until the CPU's DRAM slot comes round (/REDYT), in Spectrum mode as well
([cross-check.md](cross-check.md) 4.4). E4 modeled the 5.06 netlist gate by gate
([research-profi-v5-wait.md](research-profi-v5-wait.md),
[tools/machines/profi/waitmodel/](../../../tools/machines/profi/waitmodel/README.md)):

- **When:** v5, standard mode (DS80 = 0), `[PROFI] WaitConfig=profi` (the default; SB8 in its PROFI3+ position),
  the `contention` feature on. `WaitConfig=pentagon` turns it off at 3.5 MHz, like SB8's other position. Never on v3.
- **The rule:** with `d` = the access's T1 minus the first paper fetch (frame T 16152), `L = floor((d + 1) / 224)`
  (0..191) and `q = d - 224 L`: phase 0 waits 1 T when `q` is even in 0..126; phases 2 and 3 when `q` is odd in
  -1..125; phase 1 never. One wait at most, then the CPU is in step: a NOP stream in the paper waits once and runs
  at 4 T from then on; `LD A,(HL)` takes 8 T in the paper and 7 in the border.
- **Board variation:** the rule is for the 5.06 board, with the diode VD22 on /REDYT. Without VD22 the model waits
  more (`examples-novd22-output.txt`); the factory 5.03 "diode mod" may be that variant. It becomes a later
  `WaitConfig=` value if a measurement ever separates the two.
- **Still to check:** floatspy, the TEST 4.30 timing pages and the demos Gromov names (QARX, ACADEMY, SHOCK
  MEGADEMO) as emulated test programs.

### 6.5 Cost (A/B)

`BM_HostFrame_*` (`core/benchmarks/emulator/memory/hostbusoverlay_benchmark.cpp`), A = master ff8c5d3ec, B = the
branch at e861cd694 (the waits, the floating bus, the switch), Release builds, 2026-10-02, load 9-13, rounds A B A
B A B B A B A; CPU time per host frame, paired differences B / A:

| Benchmark | Pairs | Mean |
|:--|:--|:--|
| 48K fast | -0.7 -0.1 +0.2 +0.0 -0.5 | -0.2 % |
| Pentagon fast | -1.4 -0.1 -0.2 +2.6 -0.8 | +0.0 % |
| Scorpion fast | -0.4 -0.2 -0.9 +1.0 +0.1 | -0.1 % |
| Profi v5 fast | +2.3 +2.8 +3.3 +2.2 +3.1 | +2.7 % |
| Profi v5 debug | +4.0 +1.8 +1.5 +4.4 +3.6 | +3.1 % |

Other machines pay nothing. The v5 pays about 3 % for its video WAIT, the overlay it now runs at 3.5 MHz. The v3
(no overlay at 3.5 MHz) runs its BIOS menu at 1303 us per host frame (B only; A has no v3).

## 7. TTD, snapshots, automation, UI

| Area | Change |
|:--|:--|
| TTD | `GetTTDModelStateIds()` on v3: `ProfiPaging` only (no `Ds12887`, no `AtaChannel`). The turbo switch is a recorded input (6.1). v5 fixtures stay as they are; v3 gets its own fixture (`testdata/ttd/`, re-recorded with the rest after any format change) |
| Snapshots | `.sna` / `.z80` / `.szx` have no Profi machine id; loading one into either Profi keeps today's behavior. A TTD checkpoint carries the model, so a v3 checkpoint refuses to load into v5 |
| Port map / port trace | `portdecoder.cpp` `GetPortMapInfo`: the v3 rows without the extended map, RTC and IDE; `modelName` "Profi v3" / "Profi v5" |
| Automation | `IsModelSupported`, model tables in the CLI (`cli-processor-state.cpp`), WebAPI (`state_memory_api.cpp`, OpenAPI enum), Lua, Python; MCP `unreal://machine/profi` describes both boards and their differences |
| Qt | `menumanager.cpp` `supportedModels`: "Profi v3 (512K)" next to "Profi v5 (1024K)" |
| Docs | `.recipe/machines/profi.md`, `AGENTS.md` models list, permanent machine docs, `data/rom/README-ROMS.md` (done) |

## 8. Plan

| Phase | Work | Needs | Settles |
|:--|:--|:--|:--|
| E1 | ~~Sync-PROM decode~~ done ([tools/machines/profi/syncprom/profisync.py](../../../tools/machines/profi/syncprom/profisync.py), cross-check 4); remaining: trace the INT flip-flop (INT length) and the DS80 CPU clock | - | T5, R31 |
| E2 | ~~Decoder PROMs: tables, wiring, port map, check against unreal-ng~~ done ([decoder-prom.md](decoder-prom.md), tables in `testdata/machines/profi/decoder/`) | - | Q7, R18 |
| E3 | ~~v3.2 schematic study: turbo and the floating bus~~ done ([research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md)) | - | Q3, Q4 |
| E4 | ~~v5.06 netlist study: the /REDYT slot pattern per T, the ROM one-shot, the SB8 positions~~ done ([research-profi-v5-wait.md](research-profi-v5-wait.md)) | - | Q9 |
| 1 | `MM_PROFI3`, `ProfiBoard`, `IsProfiModel`, ROM and config plumbing; `PROFI3` creatable; v3 boots Kramis V0.2 / V0.3 | - | R1-R4, R6 |
| 2 | v3 port set (4.1) and the shared fixes (4.2); PROM-table tests for both boards | - | R10-R20 |
| 3 | `SyncProm=` table and per-board defaults (5.1); v5 TTD fixtures re-recorded for the new INT position | - | R30 |
| 3b | v5 video WAIT (`ProfiWaitOverlay`, 6.2, 6.4) | E4 | R36 |
| 4 | v3 floating bus | E3 | R21 |
| 5 | Turbo switch for both boards; the turbo waits in `ProfiWaitOverlay`; HLD holds 3.5 MHz on v3 | E3 | R32, R33 |
| 6 | Automation, Qt, docs, TTD fixture | 1-5 | R40-R43 |
| 7 | v5 open items: palette gate, 15 MHz clock, the CP/M boot switch | sources | Q5, Q6, R34 |

Phases 1, 2 and 3 have no open questions and can start now; 3b waits for E4. Each phase ends with the full build with zero warnings
and `core-tests` green (AGENTS.md); the A/B benchmark is in section 6.5
(`docs/guidelines/performance-guidelines.md`).
