# ZX-MultiSound card logic: technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | CL-0 and CL-1 built (2026-10-04, branch `multisound`, not committed); CL-2 partly (one real-program trace); see §8 |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §2-5 |
| **Architecture** | [architecture.md](architecture.md) §3-4 |
| **Oracle** | the card's own CPLD source `cpld/rtl/top.v` ([UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound)), run in Verilator (installed on the dev machine) |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Goal

`MultiSoundLogic` reproduces everything the CPLD does with the Spectrum bus, cycle for cycle at the level the
emulator sees (one port access = one bus cycle): port decode, IORQGE, the control byte, the ROM-fetch lock, the GS
mailbox registers and flags, and the four-channel DAC arbitration. It holds no audio; the card turns its decisions
into module calls. The firmware's own Verilog is the oracle, so "exact" is measurable.

## 2. Interface (as built)

`core/src/emulator/slots/cards/multisound/multisoundlogic.{h,cpp}`: self-contained (no slot, emulator or audio
dependency), so it builds on master before the slots framework lands.

```cpp
struct MultiSoundOptions { bool ym = true, saa = true, gs = true, sd = true;
                           MultiSoundGsRam gsRam = MultiSoundGsRam::OneMb;          // OneMb | TwoMb
                           MultiSoundCtrlMask ctrlMask = MultiSoundCtrlMask::Pro; };  // Pro | Classic

struct MultiSoundBusAction { enum class Kind : uint8_t { None, YmAddress, YmData, Control, SaaAddress, SaaData,
                                                         GsData, GsCommand, SoundriveSample } kind;
                             uint8_t chip;    // YM chip 0 (U4) / 1 (U10), or DAC channel
                             uint8_t value; };// the raw bus byte
using MultiSoundBusActions = std::array<MultiSoundBusAction, 2>;

struct MultiSoundReadResult { enum class Source : uint8_t { None, YmStatus, YmRegister, GsOutput, GsStatus } source;
                              uint8_t chip; uint8_t value; bool Drives() const; };

class MultiSoundLogic
{
public:
    void Configure(const MultiSoundOptions& o);     // DIP / firmware options; latches kept (the DIP is read live)
    void Reset();                                   // the CPLD reset branches (L17)

    void OnM1(uint16_t address);                    // every M1 cycle: opcode, prefixes, interrupt acknowledge
    bool Iorqge(uint16_t port) const;               // direction-independent (the RTL decodes the address only)
    MultiSoundBusActions Write(uint16_t port, uint8_t value);   // control byte = Control + YmAddress
    MultiSoundReadResult Read(uint16_t port);       // not const: reading #B3 clears the data flag
    MultiSoundReadResult Peek(uint16_t port) const; // debugger read without side effects

    void GsPortWrite(uint8_t gsPort, uint8_t value);
    uint8_t GsPortRead(uint8_t gsPort);             // returns what the CPLD drives (#FF for undecoded ports)
    void GsMemoryRead(uint16_t address, uint8_t value);         // #6000-#7FFF: DAC sample of channel A9-A8
    uint8_t GsStatus() const;
    MultiSoundGsMapping GsMemoryMap(uint16_t address) const;    // ROM / RAM1-4 + gma (GS bus controller)

    void GsDacSample(int ch, uint8_t sample);
    void GsDacVolume(int ch, uint8_t volume6);
    MultiSoundDacState Dac(int ch) const;           // {sample (converted dac* register), volume}
    static uint8_t ConvertSample(uint8_t);          // L15
    static int SampleLevel(uint8_t converted);      // -127..+127
    static int VolumeGain64(uint8_t volume);        // vol / 64, 63 -> 64 / 64

    const MultiSoundLatches& Latches() const;       // chip select, read mode, FM mute, SAA clock, ROM lock,
                                                    // GS data / command / page / output registers, both flags
};
```

Changes from the draft sketch, each forced by the RTL: `Iorqge` lost its `read` parameter (no direction term in
`zxiorqge_n`); `Read` is not const (`#B3` read clears the data flag) and `Peek` was added for the debugger;
`GsPortRead` returns the driven value; `GsMemoryRead` (address decode) and `GsMemoryMap` (the GS bus controller, the
only place `gsRam` matters) were added.

## 3. Rules (each one a test, each test mirrored in the Verilator oracle run)

| # | Rule | From |
|---|---|---|
| L1 | YM register decode A15-A14 = `11`, A3-A0 = `1101`; data A15-A14 = `10` | `port_fffd`, `port_bffd` |
| L2 | IORQGE for `port_fffd_full` (A13 = 1), `port_bffd`, `#B3`, `#BB` only, never during M1, for reads and writes alike (`IN #BFFD` asserts it without driving) | `zxiorqge_n` |
| L3 | control byte when `d[7:4] = 1111` (`pro`); bits 0-3 as hardware reference §3.3 | `ym_chip_sel` block |
| L4 | `classic` mask: `d[7:3] = 11111` while `saa` is off (issue #11 patch), else as L3 | issue #11 |
| L5 | a control byte is also a YM address write, latched by the chip selected by that byte (the old chip sees a 15.6 ns CS + /WR overlap without a latching edge) | `ym1_cs_n` not gated by data |
| L6 | FM mute: bit 2 = 1 drives both `FM*_ENA` low; 0 floats them | `fm1_ena`, `fm2_ena` |
| L7 | SAA clock: bit 3 = 0 enables; written by any control byte (four-bit mask) while `saa` is on, also with `ym` off | `saa_clk_en`, `port_fffd_saa` |
| L8 | SAA port `#FF` (A8 = SAA A0), ignored while the ROM lock is set | `port_ff`, `rom_m1_access` |
| L9 | SounDrive `A7 = 0, A5 = 0, A3-A0 = F`, channel {A6, A4}, ignored while the ROM lock is set | `port_xf` |
| L10 | ROM lock = the last M1 address had A15-A14 = `00` | `rom_m1_access` |
| L11 | YM read: `#FFFD` family returns the selected chip's status (bit 1 = 0) or register | `ym_a0` |
| L12 | GS mailbox: `#B3` write -> data reg + data flag; `#BB` write -> command reg + command flag; reads `#B3` = output reg (clears data flag), `#BB` = status | GS external registers |
| L13 | GS internal ports 0 (page), 3 (output), 6-9 (volumes); flag rules on **any** access to 2 / 3 / 5 / `#0A` / `#0B`; reads 1 / 2 / 4, others `#FF`; A3-A0 only | GS internal registers, status block, `gd` |
| L14 | DAC: SounDrive write sets volume 63 and the sample; GS volume ports set 6-bit volume; GS memory reads at `#6000-#7FFF` set the sample of channel A9-A8; overlapping strobes: the one whose last active 32 MHz edge is later wins (only on the same last edge: GS sample over SounDrive sample, SounDrive volume over GS volume). The card orders events by strobe end; `MultiSoundLogic` applies them in call order | DAC block |
| L15 | sample conversion: `v >= #80 ? v : {v[7], ~v[6:0]}` | `dac*` assignment |
| L16 | disabled DIP function: its ports are not decoded and not IORQGE | `*_ena` in the decode terms |
| L17 | reset: chip 0, register read mode, FM muted, SAA clock off, GS registers and DACs zero | reset branches |

## 4. Verification against the RTL (as built)

`tools/verification/multisound/` ([README](../../../tools/verification/multisound/README.md)):

1. `fetch-rtl.sh` pins `top.v` at `d7f3ac2` (full hash and SHA-256 checked) into `refs/` (git-ignored) and derives
   two testbench variants: `fm*_ena`'s `1'bz` written as `1'b1` (Verilator has no Z in registers), and the issue #11
   patch for `classic`. `build.sh` verilates four variants (pro / classic x 1 MB / 2 MB) into one binary, `mscosim`.
2. The testbench drives whole Z80 bus cycles (M1, memory read / write, I/O read / write; GS I/O and memory cycles) at
   the half-period of 32 MHz (1/64 µs), host timing per the Z80 datasheet at any host clock (`cpu` directive), and
   records per cycle IORQGE, the latching YM / SAA writes (chip, `aa0`, `ad`), SounDrive channel strobes, the driven
   read value, the GS memory chip select and `gma`, then the latches, `fm*_ena`, `saa_clk` activity, `dac*`, `vol*`,
   and the GS registers and flags. A YM2203 stand-in answers reads with `#C0 | chip << 4 | A0`.
3. Scenario format `.msc` (one bus cycle per line, plus `par` for overlapping host / GS strobes). Parser, record
   format and the `MultiSoundLogic` player are one shared helper (`core/tests/_helpers/multisoundscenario.{h,cpp}`)
   compiled into both `mscosim` and `core-tests`; `mscosim run` reports the first differing cycle and field.
4. Corpus: hand-written scenarios for L1-L17 plus the GS memory map and a 14 MHz host
   (`testdata/sound/multisound/scenarios/*.msc`, RTL records in `*.expected`); the decode sweep (every port x read /
   write x ROM lock off / on x 16 DIP settings x 2 control masks = 8.4 M cycles; write value = low byte ^ `#5A`, so
   control bytes at `#xxAD` exercise L3-L7 inside the sweep); the GS map sweep (128 pages x 10 addresses x 2 RAM
   builds); one real-program trace (§8). Result: **zero differences**.
5. Frozen in `core-tests`: the sweep as one FNV-1a hash and five event counts per mask / DIP plus the event codes of
   the all-enabled sweep at the 720 witness ports with A12-A9 = 0, generated into
   `core/tests/emulator/slots/cards/multisound/multisoundrtltables.h` by `regenerate.sh` (which refuses to write data
   the logic disagrees with).
6. RTL-only measurements: the DAC transfer function (`mscosim dac`, exact against `SampleLevel` x `VolumeGain64`) and
   the GS INT timing (`mscosim gsint`).

Abstraction proven by the sweep: the CPLD samples on 32 MHz edges, `MultiSoundLogic` acts once per bus cycle; the
end-of-cycle outcome is identical at 3.5 and 14 MHz host clocks.

## 5. Tests (core-tests, under 50 ms)

`multisoundlogic_test.cpp` (named after the file under test):

| Test | Checks |
|---|---|
| `MultiSoundLogic_Test.DecodeSweep` | L1, L2, L8, L9, L16 over all ports and DIP combinations against a table generated from the RTL run |
| `MultiSoundLogic_Test.ControlBytePro` / `ControlByteClassic` | L3-L7 |
| `MultiSoundLogic_Test.DffdReachesChipWithoutIorqge` | `#DFFD` write: YM action, no IORQGE |
| `MultiSoundLogic_Test.RomLock` | L10 with fetches from `#0000`, `#3FFF`, `#4000` |
| `MultiSoundLogic_Test.GsMailboxFlags` | L12, L13 |
| `MultiSoundLogic_Test.DacArbitration` | L14, L15 |
| `MultiSoundLogic_Test.ResetState` | L17 |
| `MultiSoundLogic_Test.ScenarioCorpus` | the frozen scenarios from §4 step 4, line for line |
| `MultiSoundLogicSweep_Test.DecodeSweep/0-31` (as built) | the sweep, one instance per control mask x DIP setting (262 148 cycles each) |
| `MultiSoundLogic_Test.DecodeWitnessPorts` (as built) | names the first differing port when a sweep hash fails |
| `MultiSoundLogic_Test.GsMemoryMap` (as built) | the GS bus controller, both RAM builds |
| `MultiSoundLogic_Test.ScenarioParserErrors` (as built) | the `.msc` parser |
| `MultiSoundLogic_Test.DISABLED_CaptureTsfmPlayerTrace` | not a check: regenerates the CL-2 player trace (§8) |

## 6. Phases

| Phase | Content | Size |
|---|---|---|
| CL-0 | Verilator testbench, scenario format, RTL sweep tables | M |
| CL-1 | `MultiSoundLogic` + tests §5 | M |
| CL-2 | Real-program trace corpus through both | S |

## 7. What the RTL showed (vs the hardware reference draft)

All corrected in [hardware-reference.md](hardware-reference.md).

| # | Finding | Effect on the model |
|---|---|---|
| F1 | IORQGE has no direction term: `IN #BFFD` asserts it, but the card does not drive the bus (floating read) | `Iorqge(port)`; `Read(#BFFD)` = not driven |
| F2 | IORQGE also follows the address in memory and refresh cycles (M1 high) | none (machines qualify it with their own I/O decode) |
| F3 | A control byte that switches chips is latched by the **new** chip; the old one sees a 15.6 ns CS + /WR overlap | `YmAddress` carries the new chip; the glitch is not modeled (below the YM2203 write pulse width) |
| F4 | The SAA clock control uses its own decode (`port_fffd_saa`): works with `ym` off, without IORQGE | `Control` action without `YmAddress` |
| F5 | GS flag rules fire on any GS access (read or write) to ports 2 / 3 / 5 / `#0A` / `#0B`, A3-A0 only; `#0B` reads volume 3, shared with SounDrive channel 3 | L13 |
| F6 | GS reads of undecoded ports and the interrupt acknowledge return `#FF` (CPLD drives) | `GsPortRead` |
| F7 | L14 "GS wins" holds only on an identical last clock edge; in general the later-ending strobe wins, and for the volume the SounDrive has priority | L14 reworded; the card orders events by strobe end |
| F8 | GS INT is 12 MHz / **321** (37.383 kHz), low 33 clocks (2.75 µs), not / 320 | for MS-2 (GS profile); `architecture.md` §4.2 corrected |
| F9 | DAC transfer: `0.5 + 0.5 x level/128 x gain/64`, level -127..+127 (two zeros: `#7F`, `#80`), gain = volume except 63 -> 64 | for `MultiSoundDacs`; `architecture.md` §4.4 corrected (it said `/ 63`) |
| F10 | GS map: `#0000-#3FFF` = ROM with `gma = 1`, `#4000-#7FFF` = RAM 1 chip address `#C000-#FFFF`; page bit 6 makes a page non-zero (no ROM) but selects no RAM; 1 MB ignores page bit 5 | `GsMemoryMap`; the ROM's A15 wiring is to be checked in the schematic for MS-2 |
| F11 | The issue #11 patch was written against a revision with an inverted chip select | applied with the current non-inverted select |

## 8. Status and open items

- CL-0, CL-1 done; all scenarios and the full sweep agree with the RTL.
- CL-2 partly: one real-program trace, the TFM Music Compiler 1.12 player from `TSFM-EL.TAP` (100 frames, its
  `#FFFD` / `#BFFD` writes only, captured with the existing TSFM player harness): `tfm-player-trace.msc`. Pending for
  the integration phase (they need the card on the bus or the slots port trace): traces with reads and M1 context
  from TSFM players with status polling, VGMPLAY.WMF, a GS MOD player, WC's MIDI player and Ball Quest.

