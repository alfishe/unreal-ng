#pragma once

// ZX-MultiSound (UzixLS) card logic: the CPLD's decisions on the Spectrum bus and the General Sound bus, without
// any audio. The card executes the returned actions on its sound modules.
//
// Oracle: the card's CPLD source cpld/rtl/top.v (github.com/UzixLS/zx-multisound, commit d7f3ac2), run in Verilator
// by tools/verification/multisound/. Design: docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md (rules L1-L17).
//
// Granularity: one call = one bus cycle. The CPLD samples on 32 MHz edges; the RTL testbench proves that the
// per-cycle outcome below equals what the latches hold when the cycle ends.

#include <array>
#include <cstddef>
#include <cstdint>

enum class MultiSoundGsRam : uint8_t
{
    OneMb,      // official firmware rev_A1.pof: two 512 KB chips, page bits 0-4
    TwoMb       // official firmware rev_A1_2mb.pof (GS_RAM_2MB): four chips, page bits 0-5
};

enum class MultiSoundCtrlMask : uint8_t
{
    Pro,        // current firmware: a control byte is d[7:4] = 1111
    Classic     // unofficial issue #11 patch: d[7:3] = 11111 for the YM latches while the SAA DIP is off
};

struct MultiSoundOptions
{
    bool ym = true;     // DIP SW1.1 (cfg[0]): TurboSound FM + MIDI ports
    bool saa = true;    // DIP SW1.2 (cfg[1]): SAA1099 port and clock control
    bool gs = true;     // DIP SW1.3 (cfg[2]): General Sound host ports
    bool sd = true;     // DIP SW1.4 (cfg[3]): SounDrive ports
    MultiSoundGsRam gsRam = MultiSoundGsRam::OneMb;
    MultiSoundCtrlMask ctrlMask = MultiSoundCtrlMask::Pro;
};

/// What one host bus cycle causes on the card's chips.
struct MultiSoundBusAction
{
    enum class Kind : uint8_t
    {
        None,
        YmAddress,          // YM2203 'chip' address write (A0 = 0)
        YmData,             // YM2203 'chip' data write (A0 = 1)
        Control,            // control byte latched (see Latches()); value = the byte
        SaaAddress,         // SAA1099 address / control register write (port #1FF family, A8 = 1)
        SaaData,            // SAA1099 data write (port #FF family, A8 = 0)
        GsData,             // GS mailbox data register (#B3) written, data flag set
        GsCommand,          // GS mailbox command register (#BB) written, command flag set
        SoundriveSample     // SounDrive channel 'chip' written: sample + volume 63 (Dac() holds the result)
    };

    Kind kind = Kind::None;
    uint8_t chip = 0;       // YM chip 0 (U4, ym1) / 1 (U10, ym2), or DAC channel 0-3
    uint8_t value = 0;      // the byte written (raw, as on the data bus)
};

using MultiSoundBusActions = std::array<MultiSoundBusAction, 2>;

/// What the card drives on the data bus for a host read.
struct MultiSoundReadResult
{
    enum class Source : uint8_t
    {
        None,           // the card does not drive (it may still assert IORQGE: #BFFD family)
        YmStatus,       // the selected YM2203's status (A0 = 0)
        YmRegister,     // the selected YM2203's register read (A0 = 1)
        GsOutput,       // GS output register: value
        GsStatus        // GS status: value = {data flag, 111111, command flag}
    };

    Source source = Source::None;
    uint8_t chip = 0;       // YM chip for the Ym* sources
    uint8_t value = 0xFF;   // the byte for the Gs* sources

    bool Drives() const { return source != Source::None; }
};

/// The CPLD's latches.
struct MultiSoundLatches
{
    uint8_t ymChip = 0;         // ym_chip_sel: 0 = U4 (ym1), 1 = U10 (ym2)
    bool ymReadStatus = false;  // ym_get_stat: IN #FFFD returns the status (true) or the selected register (false)
    bool fmMuted = true;        // fm*_ena driven 0 (true) or floated (false)
    bool saaClock = false;      // saa_clk_en
    bool romLock = false;       // rom_m1_access: the last M1 fetch was from #0000-#3FFF

    uint8_t gsData = 0;         // gs_regdata: host -> GS data (#B3 write, GS port 2 read)
    uint8_t gsCommand = 0;      // gs_regcmd: host -> GS command (#BB write, GS port 1 read)
    uint8_t gsPage = 0;         // gs_reg00: GS port 0 (bits 0-6 = page)
    uint8_t gsOutput = 0;       // gs_reg_out: GS -> host (GS port 3 write, host #B3 read)
    bool dataFlag = false;      // gs_flag_data (status bit 7)
    bool commandFlag = false;   // gs_flag_cmd (status bit 0)
};

/// One DAC channel as the CPLD holds it.
struct MultiSoundDacState
{
    uint8_t sample = 0;     // the converted register dac*: bit 7 = sign (1 = positive), bits 0-6 = magnitude
    uint8_t volume = 0;     // vol*: 6 bits
};

/// Where a GS memory access goes (the CPLD's GS bus controller).
struct MultiSoundGsMapping
{
    enum class Chip : uint8_t { Rom, Ram1, Ram2, Ram3, Ram4 };

    Chip chip = Chip::Rom;
    uint8_t gma = 0;            // gma[18:15], the chip's address bits 15-18 (chip address = gma << 15 | A14-A0)

    uint32_t ChipOffset(uint16_t address) const { return (static_cast<uint32_t>(gma) << 15) | (address & 0x7FFFu); }
};

class MultiSoundLogic
{
public:
    MultiSoundLogic() = default;
    explicit MultiSoundLogic(const MultiSoundOptions& options) { Configure(options); }

    /// Sets the DIP functions and firmware options. Latches are kept (the DIP is read live by the CPLD).
    void Configure(const MultiSoundOptions& options) { _options = options; }
    const MultiSoundOptions& Options() const { return _options; }

    /// The CPLD reset branch (bus /RESET).
    void Reset();

    // Host (Spectrum) bus

    /// An M1 cycle (opcode fetch, every prefix byte, and interrupt acknowledge) at this address.
    void OnM1(uint16_t address) { _latches.romLock = (address & 0xC000u) == 0; }

    /// The card's IORQGE during an I/O cycle at this port. Independent of the direction (the RTL decodes the address
    /// only) and never asserted during M1.
    bool Iorqge(uint16_t port) const;

    /// An I/O write cycle. A control byte gives Control followed by YmAddress (the chip select is not gated by data).
    MultiSoundBusActions Write(uint16_t port, uint8_t value);

    /// An I/O read cycle. Not const: reading #B3 clears the data flag.
    MultiSoundReadResult Read(uint16_t port);

    /// Read without side effects (debugger).
    MultiSoundReadResult Peek(uint16_t port) const;

    // General Sound bus (called by the GS card's port and memory hooks)

    /// A GS Z80 OUT. Only A3-A0 are decoded.
    void GsPortWrite(uint8_t gsPort, uint8_t value);

    /// A GS Z80 IN: the value the CPLD drives (ports 1, 2, 4; #FF for the others).
    uint8_t GsPortRead(uint8_t gsPort);

    /// A GS memory read (opcode fetches included). #6000-#7FFF also sets the sample of DAC channel A9-A8.
    void GsMemoryRead(uint16_t address, uint8_t value);

    /// The GS status byte {data flag, 111111, command flag}.
    uint8_t GsStatus() const;

    /// The GS memory map for this address under the current page register.
    MultiSoundGsMapping GsMemoryMap(uint16_t address) const;

    // DAC arbitration (four channels shared by the GS and the SounDrive; the last strobe to end wins)

    void GsDacSample(int channel, uint8_t sample);
    void GsDacVolume(int channel, uint8_t volume);
    MultiSoundDacState Dac(int channel) const { return _dac[static_cast<size_t>(channel & 3)]; }

    /// dac* register from a bus byte: offset binary in, sign-magnitude out (v >= #80 ? v : {v[7], ~v[6:0]}).
    static uint8_t ConvertSample(uint8_t value) { return (value & 0x80u) ? value : static_cast<uint8_t>(value ^ 0x7Fu); }

    /// Signed level of a dac* register, -127..+127 (sign-magnitude: #7F and #80 on the bus are both 0).
    static int SampleLevel(uint8_t converted)
    {
        const int magnitude = converted & 0x7F;
        return (converted & 0x80u) ? magnitude : -magnitude;
    }

    /// Volume gate duty in 1/64: vol / 64, except 63 = 64 / 64 (the RTL's '|| (&vol)').
    static int VolumeGain64(uint8_t volume) { return (volume & 0x3F) == 0x3F ? 64 : (volume & 0x3F); }

    const MultiSoundLatches& Latches() const { return _latches; }

private:
    bool IsYmRegisterPort(uint16_t port) const { return _options.ym && (port & 0xC00Fu) == 0xC00Du; }
    bool IsYmDataPort(uint16_t port) const { return _options.ym && (port & 0xC00Fu) == 0x800Du; }
    bool IsSaaPort(uint16_t port) const { return _options.saa && !_latches.romLock && (port & 0x00FFu) == 0x00FFu; }
    bool IsGsDataPort(uint16_t port) const { return _options.gs && (port & 0x00FFu) == 0x00B3u; }
    bool IsGsCommandPort(uint16_t port) const { return _options.gs && (port & 0x00FFu) == 0x00BBu; }
    bool IsSoundrivePort(uint16_t port) const { return _options.sd && !_latches.romLock && (port & 0x00AFu) == 0x000Fu; }
    static uint8_t SoundriveChannel(uint16_t port) { return static_cast<uint8_t>(((port >> 5) & 0x02u) | ((port >> 4) & 0x01u)); }

    MultiSoundOptions _options;
    MultiSoundLatches _latches;
    std::array<MultiSoundDacState, 4> _dac{};
};
