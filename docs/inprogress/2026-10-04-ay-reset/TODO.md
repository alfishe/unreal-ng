# AY-3-8910 / YM2149 reset state

Status: the register file fix is done in the working tree of branch `ay-reset` (not committed). Three open owner
decisions about the generator start state remain (see the end of this note).

## The defect

`SoundChip_AY8910::reset()` (`core/src/emulator/sound/chips/soundchip_ay8910.cpp`) cleared the register file and then
set R7 = `#FF`. `#FF` makes both I/O ports outputs with latch 0, so a program reading R14 / R15 after a reset saw 0.
On the chip, R7 = 0 after a reset: both ports are inputs and their pins read `#FF` through the on-chip pull-ups.

A second defect was on the read path. `IN #FFFD` returned the R14 / R15 latch even when the port was an input. The
datasheet says an input port reads its pins.

Found on branch `multisound`: `docs/inprogress/2026-10-03-zx-multisound/tdd-midi-line.md` §2.0 and §5 (not on
master).

The reset funnel is `SoundChip_AY8910::reset()` for every user, so one change covers all of them:

| Path | How it reaches the AY reset |
|---|---|
| Power-on, model switch | the chip constructors call `reset()` (`SoundChip_TurboSound` allocates both chips, `TsfmChip` holds one SSG) |
| Machine reset | `Core::Reset()` -> `SoundManager::reset()` -> `SoundChip_TurboSound::reset()` (both chips) or `SoundChip_TurboSoundFM::reset()` -> `TsfmChip::resetChip()` -> `ssg.reset()` |
| YM2203 /IC (TSFM) | the same `ssg.reset()`; ymfm's `ssg_reset()` override is a no-op on purpose, so the SSG is reset once |
| `.sna` | `core.Reset()` only (no AY state in the format): the reset state stays |
| `.z80`, `.szx` (and RZX, which starts from them) | `core.Reset()`, then all 16 registers are written from the file: the loaded state wins |
| TTD restore | `TTDLoadState` overwrites the registers and the generator state from the blob: no reset state involved |

## Sources

Every row below was checked: each URL returned HTTP 200 on 2026-10-04.

| Chip / source | Kind | R0-R13 | R7 (port direction) | R14 / R15 | Input port read | Generators after reset |
|---|---|---|---|---|---|---|
| GI AY-3-8910/8912 data manual ([f.rdw.se](https://f.rdw.se/AY-3-8910-datasheet.pdf)), p. 11, 12, 26 | datasheet | 0 ("will reset all registers to 0") | 0 = both inputs | 0 ("the data will remain on the I/O port(s) until changed ... by applying a reset") | pins, "all pins will read normally high" (p. 12); "registers R16 and/or R17 will follow the signals applied to the I/O port(s)" (p. 26) | not stated |
| GI AY-3-8910/8912/8913 datasheet ([grauw](http://map.grauw.nl/resources/sound/generalinstrument_ay-3-8910.pdf), scanned), p. 2 | datasheet | 0 | 0 | 0 | pins high | not stated |
| GI 1980 Data Catalog ([deramp](https://deramp.com/downloads/mfe_archive/050-Component%20Specifications/GI/1980_GI_Microelectronics_Data_Catalog.pdf)), printed p. 5-24 | datasheet | 0 (same wording) | 0 | 0 | pins high | not stated |
| Yamaha YM2149 ([grauw](http://map.grauw.nl/resources/sound/yamaha_ym2149.pdf), [bitsavers](http://www.bitsavers.org/components/yamaha/YM2149_199209.pdf)), p. 3, 6, 9 | datasheet | 0 ("the contents of all registers in the array are reset to 0") | 0 ("Input is selected when 0 is written", p. 6) | 0 | pull-ups 60-600 kOhm (p. 9) | not stated; SEL (p. 3) has no reset effect |
| Yamaha YM2203 /IC ([bitsavers](http://bitsavers.informatik.uni-stuttgart.de/components/yamaha/YM2203_198911.pdf)), p. 3 | datasheet | 0 ("All the content of register array become 0") | 0 | 0 | pull-ups | not stated |
| Microchip AY8930 ([datasheet4u](https://datasheet4u.com/pdf/542002/AY8930.pdf)), p. 2 (DS500100-2) | datasheet | 0 (table) | 0 | 0 | pins | "counter work registers ... zeros", noise LFSR "initialized to ones" |
| GI AY-3-8910 decap model, deathsoft + lvd ([lvd2/ay-3-8910_reverse_engineered](https://github.com/lvd2/ay-3-8910_reverse_engineered), copy in [jt49 doc/ay_model.v](https://github.com/jotego/jt49/blob/master/doc/ay_model.v)) | decap | 0 (reset = a forced write of 0 to every register) | 0 | 0 | - | tone counters 0, noise LFSR 0 (zero-detect feeds a 1), envelope restarted as after a write of R13 = 0 |
| MAME [`ay8910.cpp`](https://github.com/mamedev/mame/blob/master/src/devices/sound/ay8910.cpp) `ay8910_reset_ym` | emulator | 0 (written) | 0 | not written (left as they were) | the latch unless a read callback is wired | LFSR 1 |
| jt49 [`jt49.v`](https://github.com/jotego/jt49/blob/master/hdl/jt49.v) | FPGA | 0 | 0 | 0 | port input | LFSR 0 |
| MikeJ YM2149 VHDL (copy in [ZX-Uno](https://github.com/zxdos/zxuno/blob/master/cores/Oric/source/YM2149_linmix.vhd)) | FPGA | 0 | 0 | 0 | `#00` (input path commented out) | - |
| ZX Spectrum Next YM2149 (MikeJ derived, copy in [kliveide](https://github.com/Dotneteer/kliveide/blob/master/_input/next-fpga/src/audio/ym2149.vhd)) | FPGA | 0 | **`#FF`** (no reason given) | 0 | port input | - |
| MiSTer ZX Spectrum [`ym2149.sv`](https://github.com/MiSTer-devel/ZX-Spectrum_MISTer/blob/master/rtl/ym2149.sv) | FPGA | 0, **R13 = `#08`** ([commit 1c80342d48](https://github.com/MiSTer-devel/ZX-Spectrum_MISTer/commit/1c80342d48), 2026-07-24, "expect a reasonable default rather than the hardware's undefined state") | **`#FF`** (since the 2020 import; its own port comment says "set all Registers to '0'") | 0 | port input (AND latch when output) | - |
| Fuse [`peripherals/ay.c`](https://github.com/speccytools/fuse/blob/master/peripherals/ay.c) | emulator | 0 | 0 | 0 | `#FF` (R15); R14 `#BF` on the 128K (board wiring) | - |
| Xpeccy [`ay-3-8910.c`](https://github.com/samstyle/Xpeccy/blob/master/src/libxpeccy/sound/ay-3-8910.c) | emulator | 0 | 0 | 0 | `#FF` | - |
| ZXMAK2 [`PsgChip.cs`](https://github.com/zxmak/ZXMAK2/blob/master/src/ZXMAK2.Hardware.Circuits/Sound/PsgChip.cs) | emulator | 0 | 0 | 0 | `#00` without a handler | - |
| floooh [`ay38910.h`](https://github.com/floooh/chips/blob/master/chips/ay38910.h) | emulator | 0 | 0 | 0 | - | LFSR 1 |
| Unreal Speccy, ZX-Evo branch ([`sndchip.cpp`](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/sndchip.cpp)) | emulator | 0 | 0 | not written | the latch | - |

Not found: a Microchip AY-3-8910A datasheet in a readable form, KC89C72 or WF19054 datasheets (sources only call them
pin and software compatible), any measurement of a real chip's state after /RESET.

**Consensus.** Every datasheet, the decap and most emulators agree: every register is 0, including R14 / R15. The
only exceptions are R7 = `#FF` in two MikeJ-derived FPGA cores (ZX Next, MiSTer) and R13 = `#08` in MiSTer. All three
are author choices with no hardware source given. MAME and Unreal leave R14 / R15 unwritten. An input port reads its
pins, which are `#FF` with nothing attached (GI p. 12, YM2149 p. 9). Fuse, Xpeccy and the datasheets agree on that;
ZXMAK2 and MikeJ return 0, MAME returns the latch.

No variant differs, so there is no per-variant reset state. The emulator's `AYChipModel` (AY8910 / YM2149) selects
only the DAC table.

The owner remembered that "not all registers are zero after reset". No primary source supports that for the register
file. It does hold for this emulator's generators: they keep a start state that is not derived from the registers
(see the open items).

## What changed

- `SoundChip_AY8910::reset()`: every register is 0. The `_registers[AY_MIXER_CONTROL] = 0xFF` line is gone; the
  generator-side register view (`_appliedRegisters`) is 0 as well. The generator resets themselves are unchanged.
- `SoundChip_AY8910::readRegisterOnBus(reg)` (new): the value a bus read returns. R14 / R15 return
  `IO_PORT_INPUT_PINS` (`#FF`) while R7 bit 6 / 7 makes the port an input; every other case returns the register as
  before. `portDeviceInMethod` (IN `#FFFD`), `readCurrentRegister` (the TSFM device's IN) and the YM2203 adapter's
  `ssg_read` (ymfm's data read) use it. `readRegister` / `getRegisters` stay the register file (latch) for snapshot
  savers, TTD and debuggers.
- `dumpAY8910MixerState`: port B direction now reads R7 bit 7 (it read bit 6).

Behavior kept, as the owner requires (pinned by `SoundChip_AY8910_Test.GeneratorStartAfterResetIsUnchanged`, which
passes with both the old R7 = `#FF` reset and the new one):

- Tone generators: period 1, counter 0, output low, tone / noise gates off (e4c3bbbaf, b852df9f3).
- Noise: period 1, counter 0, output low, LFSR seed 1. The first 256 ticks are pinned.
- Envelope: shape 8 (continuous sawtooth, 279eb3d4e), period 1, counter 0, segment 0, output 31. R13 still reads 0,
  as it did before. The first 256 ticks are pinned.
- Audio: identical. All amplitudes are 0 after a reset and DAC entries 0 and 1 are both 0.0 in both tables.

## Tests and gate data

New:

| Test | Checks |
|---|---|
| `SoundChip_AY8910_Test.PowerOnStateIsTheDatasheetResetState`, `ResetClearsAllRegisters` | all 16 registers 0, selection 0, IN `#FFFD` of R14 / R15 = `#FF` |
| `SoundChip_AY8910_Test.ResetStateIsSilent` | no output after reset |
| `SoundChip_AY8910_Test.OutputPortReadsItsLatchInputPortReadsPins` | each R7 direction bit exposes or hides its latch; other registers read back unchanged |
| `SoundChip_AY8910_Test.GeneratorStartAfterResetIsUnchanged` | tone / noise / envelope start state and the first 256 ticks |
| `SoundChipTurboSound_Test.PowerOnAndMachineResetGiveTheDatasheetState` | both TurboSound chips, power-on and `Core::Reset()` |
| `TsfmPort_Test.ResetStateSsgIsTheDatasheetState` | both YM2203 SSG halves, through the device and through ymfm's data read |
| `TTD_AY_Serializer_Test.RestoreKeepsSavedPortStateNotTheResetState` | a restore keeps R7 / R14 / R15 |
| `LoaderZ80_Test.loadKeepsAYPortDirectionsAndLatches` | a `.z80` load keeps R7 / R14 / R15 |

Changed. Each of these tests had encoded the old reset value:

- `TsfmPort_Test.ReadWithFmAddress` read back R14 = `#20` with R7 = `#FF` from the old reset. It now checks that R14
  reads `#FF` as an input and `#20` once R7 makes port A an output.
- `TsfmPlayerHarness_Test.InitWritesChipResetThenParksInWaitStatus` relied on R7 = `#FF` parking the TFM player at its
  first R7 status poll. Now the player finishes the reset sequence on both chips and parks in frame 3 in the channel
  writer's poll (`0x62DF..0x62F2`). Per-frame traffic is pinned at 674, 2, 2, 9, 0, 0, 0, 0.
  [player-entry-points.md](../2026-09-10-turbosound-fm/verification/player-entry-points.md) and the TSFM
  implementation plan row were updated to match.

Unchanged and passing:

- `CoreGolden.EveryModelRunsExactlyAsRecordedInFastAndDebugMode`: no golden run reads R14 / R15 in input mode.
- `TTD_Corpus_Test`: fixtures restore from their blobs.
- `TsfmPlayerHarness_Test.TrafficIsDeterministicAcrossInstances`.

CI gate `testdata/ttd/bench/v1-ci-gate.txt` (0 % tolerance), regenerated with the documented procedure
([tools/verification/ttd-bench](../../../tools/verification/ttd-bench/README.md), `export-gate`). The AY blob holds
R7 in both register views, so device blob sizes moved on every case:

| Case | device blobs (bm3, B/frame) | file (bm7_file_bytes) | other exact rows |
|---|---|---|---|
| 48K/idle | 462.47 -> 459.37 (-3.10) | 71937 -> 71751 (-186) | bm2_work_device_blobs 455.03 -> 451.97 |
| ATM3/idle | 2340.97 -> 2327.63 (-13.33) | 513471 -> 512687 (-784) | bm2_work_device_blobs 2303.93 -> 2290.67; bm3_ram_payload 898.83 -> 899.07 (+0.23) |
| PENTAGON/game | 897.18 -> 894.30 (-2.88) | 603230 -> 603057 (-173) | bm2_work_device_blobs 884.02 -> 881.13 |
| PENTAGON/idle | 888.82 -> 892.73 (+3.92) | 84067 -> 84302 (+235) | bm2_work_device_blobs 875.17 -> 879.07 |

`bm7_file_bpf` follows the file size. The heap totals (`bm3_total_bpf`, `bm4_resident_*`, 25 % tolerance) moved by
the same few bytes and were re-exported with the rest. No row was added or removed, and every `bm2_work_*` capture-cost
row other than the device blobs is unchanged. The reference runs (`v1-ci.json` and the others) were not refreshed;
earlier device-state changes did not refresh them either.

ATM3/idle also changed RAM content (`bm3_ram_payload_bpf`). This is a real dependency. The firmware runs the 128K
ROM-style routine at `#387F` (`OUT #FFFD,7` / `IN H,(C)` / `OUT #FFFD,14` / `IN A,(C)` / `OR #F0`), which reads R14
while port A is still an input. It now gets `#FF` from the pins instead of 0 and stores a different value. The
PENTAGON cases run the same routine after their ROM has written R7 = `#FF`, so their RAM did not move.

## Open items (owner decisions; nothing applied)

1. **Generator gates vs R7 = 0.** On the chip, R7 = 0 after reset mixes tone and noise into all three channels. The
   emulator's generators keep both gates off until a program writes R7 (e4c3bbbaf). The register file and the
   generators therefore disagree until the first R7 write. It is inaudible while the amplitudes are 0. A program that
   sets only R8 after reset would hear a DC level here, but ultrasonic tone and noise on the chip.
2. **Noise LFSR seed.** The emulator uses 1, as MAME and floooh do. The sources disagree: the AY8930 datasheet says
   all ones; the GI decap and jt49 say all zeros with a zero-detect.
3. **Envelope after reset.** The emulator runs shape 8 (continuous sawtooth), kept deliberately for software that
   never writes R13 (279eb3d4e). R13 reads 0, and the decap restarts the envelope as shape 0 (decay, then hold at 0).
4. **Board wiring of the I/O pins.** Nothing drives the pins in the emulator, so an input port reads `#FF`. On the 128K
   / +2 / +3, port A carries the keypad and RS-232 lines; Fuse returns `#BF` for R14. That is a board model, not a
   chip model.
5. **Unused register bits on readback.** The AY-3-8910 reads unused bits as 0, the YM2149 returns them (MAME, Next
   core). The emulator returns them on both. This is unchanged and out of scope.
