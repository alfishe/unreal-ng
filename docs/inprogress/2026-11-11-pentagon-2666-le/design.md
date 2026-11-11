# Pentagon 2.666 LE: design for unreal-ng

Part of [README.md](README.md). The hardware facts with source lines are in
[hardware-reference.md](hardware-reference.md) (cited as HR §n); the tests are in [test-plan.md](test-plan.md).

## 1. What the machine is, in our terms

A Pentagon-1024 (71680-T frame, no contention, `7FFD` / `EFF7`, Pentagon 16 colour) with:

- the ATM Turbo 2 shadow layer: `xx77` (video mode, turbo, CP/M, palette enable), the `xFF7` dispatcher with two
  maps chosen by `7FFD.4`, ATM 320 x 200 / 640 x 200 / 80 x 25, the ATM palette `xxFF`;
- the Evo BaseConf additions: `x7F7` 128-page dispatcher, `00BE..07BE` readback, `xxBF` bit 0 shadow lock;
- 2 MB, 3.5 / 14 / 28 MHz with I/O cycles at 3.5 MHz, Gluk CMOS, Z-Controller SD, Nemo IDE, Kempston mouse and
  joystick, TurboSound, Covox, a 16550 UART;
- a Beta Disk emulator over a RAM disk in the upper megabyte (HR §5.6), and ROMs that live in write-protected RAM
  pages `0x48..0x4C` (HR §2.1).

Almost every piece already exists in unreal-ng, split between the Pentagon-1024 and the ATM710 / ATM3 (ZX-Evo
BaseConf) decoders. What does not exist anywhere: a 28 MHz step on an ATM-family decoder, a per-I/O-cycle slowdown
to the base clock, two dispatcher maps selected by `7FFD.4`, the P2.666 palette bit layout with `A8..A15`, ATM video
modes inside a 71680-T Pentagon raster, and the RAM-disk Beta Disk.

## 2. Existing code to reuse

| Need | Existing code | Fit |
|---|---|---|
| Model registration | `MEM_MODEL` (`core/src/emulator/platform.h:337`), `Config::mem_model` (`core/src/emulator/config.h:53`), `model_aliases` (`config.h:86`) | new row |
| Decoder factory | `PortDecoder::IsModelSupported` / `GetPortDecoderForModel` (`core/src/emulator/ports/portdecoder.cpp:130`, `:160`) | new case |
| Pentagon-1024 `7FFD` / `EFF7` | `ports/models/portdecoder_pentagon1024.{h,cpp}` (`Port_EFF7_Out` :128, `switchRAMPage` :217) | logic reference; the lock rules differ (HR §2.5) |
| ATM shadow layer | `ports/models/portdecoder_atm710.{h,cpp}` (`Port_FF77_Out` :838, `Port_FFF7_Out` :898, `Port_ATM_Palette_Out` :944, `updateMemoryBanks` :1001) | base class candidate |
| BaseConf extras | `ports/models/portdecoder_atm3.{h,cpp}` (`ClassifyPort` :171, `IsPort_BE` :632, `Port_37F7_Out` :849, `updateTurboMode` :699) | copy the port classification pattern, not the Evo AVR / flash parts |
| Video modes | `Screen::DetectModePentagon` / `DetectModeATM2` / `DetectModeATM3` (`video/screen.cpp:328-460`), `video/atm/screenatm.*`, `atmgeometry.h`, `atmfont.h`, `video/alco/screenalco.*` | renderers as they are; new geometry |
| Z-Controller SD | `io/spi/zcontrollerspi.*`, `io/sdcard/sdcardspi.*`, slot pattern `PortDecoder_ATM3::EvoSdSlot` (`portdecoder_atm3.h:173`) | as is |
| Nemo IDE | `io/ide/ideadapter.*` scheme `IDE_NEMO` | as is |
| Kempston mouse / joystick | `io/mouse/*`, `Standard_IsPort_KempstonMouse` (`portdecoder.h:719`) | as is |
| CMOS / RTC | `io/rtc/ds12887.*`, `io/rtc/rtcaccess.*` | wrap: the P2.666 constant cells and ADC cells (HR §5.4) |
| TurboSound, Covox | `sound/chips/soundchip_turbosound.*`, `sound/covox.*` | as is, AY clock 1.8387 MHz |
| UART | `io/serial/uart16550.*`, claim `kEvoComClaims` (`slots/refdata/machines.cpp:513`) | as is |
| WD1793 | `io/fdc/wd1793.*`, `fdc.*` | P1 path (§5.1) |
| Turbo | `EmulatorState::hw_turbo_ratio` (`platform.h:1248`), `Z80::ApplyQueuedFrequencyMultiplier` (`cpu/z80.cpp:958`) | ratio 8 exists (Next) |
| Board refdata | `slots/refdata/machines.cpp:677` (`kMachines`), `kAtm3BuiltIns` (:524) | new entry |
| Worked example of a new model | GMX phase 1, `git show --stat 175a568f0` (34 files) | the checklist in §8 follows it |

## 3. Model

| Item | Value |
|---|---|
| Enum | `MM_P2666`, appended before `N_MM_MODELS` (existing values keep their numbers) |
| Full name | `Pentagon 2.666 LE` |
| Short name | `P2666`; aliases `PENTAGON2666`, `P2666LE` |
| RAM | 2048 only (`RAM_2048`) |
| Config folder | `data/configs/p2666/unreal.ini` |
| ROMs | `data/rom/p2666/{gluk,trdos,basic128,basic48,fatall,logo}.rom` from upstream `LE/PAE_MCU/rom/` (`basic128` = `data/rom/128_low.rom`, `basic48` = `data/rom/48for128.rom` byte for byte; Gluk, TR-DOS 6.11E, FATALL 0.25 and the logo are new), plus a `README` line in `data/rom/README-ROMS.md` with the upstream URL and revision |
| `.ini` | `[P2666] Turbo=` (power-on speed is 28 MHz by hardware; no key needed), `[P2666] BetaDisk=ramdisk\|wd1793` (§5.1), `[ZC]`, `[HDD] Scheme=NEMO`, `[SLOTS] ay-socket=ts`, `builtin.covox=on` |

### 3.1 Power-on: the MCU's job

On the board the MCU uploads ROMs into RAM before it releases the CPU. Our model does the same in its power-on path
(not in CPU reset): copy the six images into pages `0x48..0x4C` and page 5, set the write-protect flags on
`0x48..0x4C`, initialise the palette from the `pollitra.hex` values (HR §6.2), restore 64 CMOS cells from the
emulator's CMOS store. A `firmware/` folder on the inserted SD image is **not** read by the emulator (devices
never open files; the user picks ROMs through the config). The `test.trd` upload is replaced by inserting an image
into drive A (§5.1).

CPU reset (F12 / reset verb) resets only the registers of HR §7; RAM, ROM pages, palette and CMOS stay.

## 4. Port decoder `PortDecoderP2666`

Files `core/src/emulator/ports/models/portdecoderp2666.{h,cpp}` (no underscore, AGENTS naming).

**Base class.** Derive from `PortDecoder_ATM710` if its ATM layer (FF77 bit constants, `CPM`, `PEN2`, palette
hook, video-mode hook) can take the P2.666 differences as overrides: two maps, 10-bit window registers, the ROM bit
swap, the Pentagon-1024 `7FFD` formula and lock rules, `x7F7`, `xxBE`, `xxBF`. If more than `updateMemoryBanks`,
`ClassifyPort` and the palette writer need overriding, derive from `PortDecoder` and copy the few ATM helpers. Decide
in a half-day spike at the start of P1 by writing the dispatcher unit tests (test-plan T-M) against both.

**Decode structure.** One `ClassifyPort(port, isWrite, dosOn)` in the ATM3 style returning an enum, built from the
tables HR §4.1 and §4.2 row by row, with the read priority order kept exactly. Writes with `A2 = A1 = 0` go to the
IDE adapter; reads go to the IDE adapter only in the last row of the read mux (HR §4.2 row 13), so even IDE
registers read the `FE` value exactly as on the board (D-5, owner decision: copy, do not fix; §4.1). Overlaps (HR §4.1 list) are explicit: a DOS `xx77` write
calls both `Port_77_Atm` and the WD data write; an `A7 = 1, A1..0 = 11` DOS write also writes the Beta Disk system
register; `BFF7` in DOS with `EFF7.7` hits both CMOS and window 2.

### 4.1 Known board bugs: copied, fix recipes commented out

The owner's rule (2026-11-11): the model is the board as built, nothing improved. Where the VHDL looks like a bug,
the decoder copies it and carries the fix as a commented-out block with a `P2666-FIX` tag, a pointer to the HR
row and a test name that would flip. The block is never compiled in by default, no `.ini` key enables it.

```cpp
// P2666-FIX D-5 (disabled, see hardware-reference.md §11): the board's read mux answers every even port with the
// FE value, so the even Nemo IDE registers (0x10, 0x30..0xF0) never reach the IDE. To make IDE reads work, return
// the IDE class before the FE row; T-I2 then expects sector data instead of FE.
// if (!isWrite && (port & 0x0006) == 0)
//     return PortClass::NemoIde;
```

The same pattern holds the saturating sound mix (D-11, §5.2) and any further bug found later.

**State** (`P2666State`, in `platforms/p2666/p2666state.h`):

| Field | Bits | Reset |
|---|---|---|
| `p7FFD`, `pEFF7`, `pDFF7` | 8 each | 0 |
| `pxx77` | 8 | `0xAB` |
| `cpm`, `pen2`, `bf0` | 1 each | 0, 1, 0 |
| `dosOn`, `dos1` | 1 each | on, (computed) |
| `window[2][4]` | 10 each | `0x3FF`, window 0 `0x3BF` |
| `palette[16]` | 16 each | power-on only |
| `border3` | 1 | 0 |
| `sdConfig`, `covox`, `aySelect`, `intFlag` | | 0 |

**DOS state machine** in the M1 hook exactly as HR §3: enter on M1 at `3D00..3DFF` with `7FFD.4 = 1`; at M1
`>= 0x4000` take `dos1`; `dos1 = cpm AND NOT bf0` latched at the end of every I/O cycle. Reuse the existing Beta 128
M1 trap only if it can be told "no ROM check" (the hardware checks only the address).

**Memory banks** in `UpdateModelMemoryBanks()` with the formula table of HR §2.2, and `MM_P2666` added to
`decoderOwnsMemoryManager` (`memory/memory.cpp:1104`). Write protection of pages `0x48..0x4C` as a per-page
read-only flag in the memory manager (the same mechanism that makes a ROM bank read-only), not as a check in the
decoder.

**Quirks** (HR §11): D-1, D-2, D-3 are copied as the VHDL does them; each has its own named test so a later change
of mind is one test edit.

## 5. Devices

### 5.1 Beta Disk: two paths

| Path | What it is | Pros | Cons |
|---|---|---|---|
| A: `wd1793` | the existing WD1793 with drives A..D and real images, Beta 128 ports gated by the P2.666 `dosOn` | works now, every image format, write-back through the media manager, TR-DOS `FORMAT` works | not what the board does; the RAM disk (TR-DOS drive D in Gluk, pages `0x4E..0x76`) and drive A are different storage |
| B: `ramdisk` | new `P2666RamDiskBdi` device modelled on `betadisk.vhd`: drive A only, image bytes in RAM pages `0x76` downwards, the command subset, status bits and timing of HR §5.6, CPU stall per byte | bit-exact; software that writes the RAM disk directly and then reads it through the BDI (Gluk's drive D, FATALL copying a TRD into the RAM disk) behaves as on the board | new device; inserting an image = copy into RAM (through `MediaManager::Insert` into the `fdd.a` slot), eject = copy back; images larger than 41 pages are refused |

Decided (owner, 2026-11-11): P1 ships path A so TR-DOS boots early; P4 adds path B and makes it the default for this model; path A stays as `[P2666] BetaDisk=wd1793`.

### 5.2 Other devices

| Device | Slot / claim | Notes |
|---|---|---|
| SD | `sd.zc` slot, ZC SPI | port routing by `dosOn` (HR §5.2); config read returns the open bus `0xFF` (D-4, decided) |
| IDE | `ide0.master` / `ide0.slave`, `IDE_NEMO` | writes `A2 = A1 = 0`; reads only where the read mux falls through (odd `0x11` out of DOS): even registers read `FE` (D-5, §4.1) |
| CMOS / RTC | built-in RTC, claims `DFF7` / `BFF7` / `EFF7` | `P2666Cmos` wraps the DS12887 model: constant cells `0A..0D`, `11`, ADC cells `31..34` return 0, 256 bytes, 64 persisted, time from the host clock |
| Mouse | built-in Kempston mouse `FADF` / `FBDF` / `FFDF`, wheel in `FADF` bits 7..4 | `0xFF` for all three when no mouse is attached |
| Joystick | built-in Kempston joystick `xx1F`, DOS off only | bits as the MCU sends its pins (D-7); the direction per bit from the schematic |
| Sound | `ay-socket=ts`, chip select `FF` / `FE`, AY clock 1,838,710 Hz, Covox `FB` | stereo as the VHDL: right = A + B, left = B + C (D-6); 8-bit wrapping sum with beeper +50 / +100 (D-11); the saturating mix is a commented-out `P2666-FIX D-11` block |
| UART | built-in COM, `kEvoComClaims` | INT AND-ed into the frame INT through the interrupt source API |

All of them go through their managers (media slots, slot claims, interrupt source, wait API) as
`docs/guidelines/new-platform-guidelines.md` requires; one `DeviceState` report `p2666` exposes the decoder state
(maps, windows, `dosOn`, `cpm`, `bf0`, `pen2`, video mode, turbo, palette, CMOS address).

## 6. CPU clock and timing

| Item | Design |
|---|---|
| Base clock | 3,562,500 Hz (`mainclk / 32`; 12 MHz MCU quartz -> 72 MHz -> PWM 18 MHz -> FPGA PLL x19/3 = 114 MHz), frame 71680 T = 49.70 Hz; turbo 14.25 / 28.5 MHz (decided: as the HDL) |
| Turbo | `hw_turbo_ratio` 1 / 4 / 8 from `xx77.3` and `EFF7.4` (HR §1.1); applied at the next M1, as ATM3 does |
| Power-on speed | ratio 8 (`xx77 = 0xAB`) |
| I/O cycles | an I/O machine cycle is 4 base-clock T-states at any speed: at ratio r it costs `4r` turbo T-states instead of 4, i.e. `4(r - 1)` extra, added through the wait API (a new `P2666IoSlowdownOverlay`, like `memory/atm/evoturbooverlay.*`) |
| INT | 32 CPU clocks long at the current clock (`intlen` follows the ratio); position from the calibration test (T-V3), first guess `intstart` = Pentagon's 71635 |
| BDI stall | path B only: the CPU waits while the emulated BDI moves a byte |
| No contention | as Pentagon |

## 7. Video

| Item | Design |
|---|---|
| Mode detection | `Screen::DetectModeP2666`: `pxx77[2:0]` and `EFF7.0` -> `M_ZX48` / `M_P16` / `M_ATM16` / `M_ATMHR` / `M_ATMTX`; other combinations keep the last colour of the colour register, as the VHDL (D-8) |
| Renderers | existing ZX, `screenalco` (Pentagon 16c), `screenatm` (three ATM modes) |
| Geometry | new raster descriptor next to `screen.h:613-616`: Pentagon 71680 frame, ZX paper at the Pentagon position, ATM paper 320 x 200 starting 32 ZX pixels left of and at the same line as the ZX paper (HR §1.3: VGA 128 / 128) |
| Screen page | `7FFD.3` in every mode (pages 5 / 7, ATM 1+5 / 3+7) |
| Text font | `otmfnt.hex` (2 KB, 8 x 8); compare with `atmfont.h`, reuse when equal, else a P2.666 font table |
| Palette | 16 x 16-bit entries, the bit map of HR §6.2 to 4-bit RGB, conversion to the host colour space through the existing palette path; writes only under the HR §4.1 conditions; kept across reset |
| Border | 16 colours (`border3 = NOT A3`) in all modes |
| Visible area | the board's: 387 x 295.5 ZX pixels (VGA 774 x 591, HR §1.3), border left 59, right 72, top 55.5, bottom 48 around the ZX paper; the half line at the top is the second VGA line of ZX line 8 (exact with a line-doubled framebuffer, else that ZX line is shown whole: P2 picks the presenter path). Decided: as the hardware, no wider border |

## 8. Fitting checklist (from the GMX phase-1 commit)

| Area | Change |
|---|---|
| Enum, table, aliases | `platform.h`, `config.h`, `config.cpp` (`GetConfigFolderForModel`, `ApplyModelTimingDefaults`, `IsModelCreatable`) |
| Factory | `portdecoder.cpp` cases + `GetPortMapInfo` rows (`modelName` "Pentagon 2.666 LE") |
| ROM | `memory/rom.cpp` path case; the power-on upload of §3.1 |
| Memory | `memory/memory.cpp` `decoderOwnsMemoryManager`, read-only pages |
| Video | `screen.cpp` detect, geometry, palette |
| Slots | `slots/refdata/machines.cpp` `kMachines` entry + `kP2666BuiltIns`; `testdata/slots/fitted-devices.txt` |
| Snapshots | `PagingFamily::P2666` in `MachineStateTransfer`; capture policy: `.sna` / `.z80` only for the plain Pentagon-128 subset (DOS off, all windows in Pentagon mode, `7FFD` page < 8, ATM video off); `.szx` none; TTD checkpoint carries everything; loading a `.sna` / `.z80` sets Pentagon mode |
| TTD | `PeripheralId::P2666` (next free id >= 66), serializer for `P2666State`, `GetTTDModelStateIds`, `CreateTTDSerializers`; blob with explicit filler and reserved bytes |
| Automation planes | model lists in CLI, Python, Lua, MCP, WebAPI, WebSocket; the `p2666` `DeviceState` report; no private endpoints |
| Qt | `unreal-qt/src/menumanager.cpp` `supportedModels`: "Pentagon 2.666 LE (2048K)" |
| Docs | `.recipe/machines/pentagon-2666.md`, `.recipe/_common/machines.md` row, `docs/hardware/zx-ports-full-table.md` column, `docs/features/media.md` / `slots.md` rows, `docs/emulator/environment-variables.md` if a variable is added |
| Tests | `emulatormanager_test.cpp`, `slotmanager_test.cpp`, `modelsregression_test`, the new suites of [test-plan.md](test-plan.md) |

## 9. Phases

Each phase: tests first, then code; target build + filtered tests per sub-task, one full `tools/build/test.sh` before
landing; zero warnings; `docker/linux/build.sh --test` after C++ changes.

| Phase | Content | Done when |
|---|---|---|
| P0 | ROM files, `data/configs/p2666/unreal.ini`, model row, factory case with a decoder that is a Pentagon-1024 + 2 MB + Gluk ROM mapping (HR §2.2 Pentagon-mode rows only), power-on upload, write-protected ROM pages | T-B1: power-on shows the Gluk Reset Service menu; BASIC 128 / 48 / TR-DOS start from it |
| P1 | Full decoder: DOS machine, `xx77`, both maps, `xFF7`, `x7F7`, `xxBE`, `xxBF`, `7FFD` / `EFF7` rules and quirks, FE flag, turbo 1 / 4 / 8 with the I/O slowdown, INT; Beta Disk path A | T-P*, T-M*, T-D*, T-C*; TR-DOS boots a TRD in drive A |
| P2 | Video: detection, geometry, Pentagon 16c, three ATM modes, palette, border, font | T-V*; calibrated INT position |
| P3 | CMOS / RTC, SD, IDE, mouse, joystick, TurboSound clock and stereo, Covox | T-S*, T-I*; FATALL lists a FAT32 SD and a FAT32 IDE image; NedoOS boots from SD |
| P4 | Beta Disk path B (RAM disk), UART | T-B* for the RAM disk; Gluk drive D and drive A see the same data |
| P5 | TTD, snapshots, automation planes, Qt, recipes and docs, Linux / Windows checks | TTD round trip, MCP live block, recipe walked through |

## 10. Oracle

The VHDL is the specification and it runs: `ghdl` is installed (`/opt/homebrew/bin/ghdl`). A co-simulation harness
(`tools/poc/NNN-p2666-vhdl-oracle/`, a POC with its own charter) builds `PaE.vhd` + `zx_main.vhd` + `betadisk.vhd`
+ T80 with stubs for the Altera primitives (`altpll` -> a clock generator, `altsyncram` -> a behavioural RAM with the
`.hex` init) and a behavioural SRAM, loads a test program and the ROM pages, and dumps per frame: port writes, the
physical address of each memory access, the INT edges and the 15-bit video stream. Our emulator dumps the same
trace; a script diffs them. This settles the timing questions (INT position, border / multicolor offsets, turbo
I/O cost, BDI timing) without hardware. Not needed for P0 / P1 logic, which the unit tests cover from the tables.
