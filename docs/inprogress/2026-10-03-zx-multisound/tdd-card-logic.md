# ZX-MultiSound card logic: technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §2-5 |
| **Architecture** | [architecture.md](architecture.md) §3-4 |
| **Oracle** | the card's own CPLD source `cpld/rtl/top.v` ([UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound)), run in Verilator (installed on the dev machine) |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Goal

`MultiSoundLogic` reproduces everything the CPLD does with the Spectrum bus, cycle for cycle at the level the
emulator sees (one port access = one bus cycle): port decode, IORQGE, the control byte, the ROM-fetch lock, the GS
mailbox registers and flags, and the four-channel DAC arbitration. It holds no audio; the card turns its decisions
into module calls. The firmware's own Verilog is the oracle, so "exact" is measurable.

## 2. Interface

```cpp
// core/src/emulator/slots/cards/multisound/multisoundlogic.h (sketch)
struct MultiSoundOptions { bool ym = true, saa = true, gs = true, sd = true; GsRam gsRam = GsRam::OneMb;
                           CtrlMask ctrlMask = CtrlMask::Pro; };

struct BusAction                                   // what one bus cycle causes
{
    enum class Kind : uint8_t { None, YmAddress, YmData, Control, SaaAddress, SaaData, GsData, GsCommand,
                                SoundriveSample } kind;
    uint8_t chip;                                   // YM chip / DAC channel
    uint8_t value;
};

class MultiSoundLogic
{
public:
    void Configure(const MultiSoundOptions& o);
    void Reset();                                   // the CPLD reset branch

    void OnM1(uint16_t pc);                         // ROM-fetch lock (called only when the card has locked claims)
    bool Iorqge(uint16_t port, bool read) const;    // the card's IORQGE for this cycle
    std::array<BusAction, 2> Write(uint16_t port, uint8_t value);   // a control byte is Control + YmAddress
    ReadResult Read(uint16_t port) const;           // drives?, value source (YM chip / GS out / GS status)

    // GS side (called by the GS card's port hooks)
    void GsPortWrite(uint8_t gsPort, uint8_t value);
    void GsPortRead(uint8_t gsPort);
    uint8_t GsStatus() const;                       // {data flag, 111111, command flag}

    // DAC arbitration
    void GsDacSample(int ch, uint8_t sample);       // GS memory read at #6000-#7FFF
    void GsDacVolume(int ch, uint8_t volume6);      // GS ports 6-9
    DacState Dac(int ch) const;                     // {sample (converted), volume}

    const MultiSoundLatches& Latches() const;       // chip select, read mode, FM mute, SAA clock, ROM lock
};
```

## 3. Rules (each one a test, each test mirrored in the Verilator oracle run)

| # | Rule | From |
|---|---|---|
| L1 | YM register decode A15-A14 = `11`, A3-A0 = `1101`; data A15-A14 = `10` | `port_fffd`, `port_bffd` |
| L2 | IORQGE for `port_fffd_full` (A13 = 1), `port_bffd`, `#B3`, `#BB` only, and never during M1 | `zxiorqge_n` |
| L3 | control byte when `d[7:4] = 1111` (`pro`); bits 0-3 as hardware reference §3.3 | `ym_chip_sel` block |
| L4 | `classic` mask: `d[7:3] = 11111` while `saa` is off (issue #11 patch), else as L3 | issue #11 |
| L5 | a control byte is also a YM address write to the selected chip | `ym1_cs_n` not gated by data |
| L6 | FM mute: bit 2 = 1 drives both `FM*_ENA` low; 0 floats them | `fm1_ena`, `fm2_ena` |
| L7 | SAA clock: bit 3 = 0 enables; written by any control byte while `saa` is on | `saa_clk_en` |
| L8 | SAA port `#FF` (A8 = SAA A0), ignored while the ROM lock is set | `port_ff`, `rom_m1_access` |
| L9 | SounDrive `A7 = 0, A5 = 0, A3-A0 = F`, channel {A6, A4}, ignored while the ROM lock is set | `port_xf` |
| L10 | ROM lock = the last M1 address had A15-A14 = `00` | `rom_m1_access` |
| L11 | YM read: `#FFFD` family returns the selected chip's status (bit 1 = 0) or register | `ym_a0` |
| L12 | GS mailbox: `#B3` write -> data reg + data flag; `#BB` write -> command reg + command flag; reads `#B3` = output reg (clears data flag), `#BB` = status | GS external registers |
| L13 | GS internal ports 0 (page), 2 / 3 / 4 / 5 / `#0A` / `#0B` flag rules | GS internal registers, status block |
| L14 | DAC: SounDrive write sets volume 63 and the sample; GS volume ports set 6-bit volume; GS memory reads at `#6000-#7FFF` set the sample of channel A9-A8; same-cycle SounDrive + GS sample: GS wins (the RTL's `else if`) | DAC block |
| L15 | sample conversion: `v >= #80 ? v : {v[7], ~v[6:0]}` | `dac*` assignment |
| L16 | disabled DIP function: its ports are not decoded and not IORQGE | `*_ena` in the decode terms |
| L17 | reset: chip 0, register read mode, FM muted, SAA clock off, GS registers and DACs zero | reset branches |

## 4. Verification against the RTL

`tools/verification/multisound/`:

1. `fetch-rtl.sh` pins `top.v` at a commit (`d7f3ac2`) into `refs/` (not committed).
2. A Verilator testbench (`tb_top.cpp`) wraps `zx_multisound`, drives the Z80 bus signals for whole cycles (IN, OUT,
   M1 fetch, memory read) at 32 MHz resolution and records, per cycle: `zxiorqge_n`, the chip selects, `aa0`, `ad`,
   `zxd` when driven, `fm*_ena`, `saa_clk` activity, `dac*` registers, `vol*`, GS status.
3. A scenario file format (`.msc`: one bus cycle per line) is played into both the testbench and `MultiSoundLogic`;
   a diff reports the first differing cycle and signal.
4. Corpus: hand-written cases for L1-L17, a generated sweep over all 65 536 ports × read / write × ROM lock on / off ×
   DIP combinations, and bus traces captured from real programs with our port trace (TSFM players, VGMPLAY.WMF, a GS
   MOD player, WC's MIDI player, Ball Quest).
5. The agreed behavior is frozen in `core-tests` as table-driven tests; Verilator is not a build dependency.

The testbench also documents the one place where the emulator abstracts: the CPLD samples on 32 MHz edges, the
emulator acts per bus cycle; the sweep proves the per-cycle outcome is the same.

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
| `MultiSoundLogic_Test.ScenarioCorpus` | the frozen scenarios from §4 step 4 |

## 6. Phases

| Phase | Content | Size |
|---|---|---|
| CL-0 | Verilator testbench, scenario format, RTL sweep tables | M |
| CL-1 | `MultiSoundLogic` + tests §5 | M |
| CL-2 | Real-program trace corpus through both | S |
