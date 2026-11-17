#pragma once

// Hardware source: the Pentagon 2.666 LE FPGA (LE/PAE_FPGA/PaE.vhd, common/zx_main.vhd) of
// https://github.com/koe1234/pentagon_2_666_le at e808402ff193491b94cfd3733f3e1f045c2aca6c. Every rule below cites
// docs/inprogress/2026-11-11-pentagon-2666-le/hardware-reference.md (HR) and the VHDL line it was read from; the CPU
// clock and INT timing were run in the board's own VHDL (tools/poc/051-p2666-hdl-timing-oracle).
#include "stdafx.h"

#include <array>
#include <memory>

#include "emulator/cpu/z80.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/zcontrollerspi.h"
#include "emulator/media/mediaslot.h"
#include "emulator/platforms/p2666/p2666betadisk.h"
#include "emulator/platforms/p2666/p2666uart.h"

namespace unrealdisk { class DiskImage; }
using unrealdisk::DiskImage;
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/p2666audio.h"

/// Pentagon 2.666 LE (koe, 2025): a Pentagon-1024 with the ATM Turbo 2 shadow layer (`xx77`, two dispatcher maps,
/// the palette), the Evo BaseConf extras (`x7F7`, `xxBE`, `xxBF`), 2 MB, 3.5 / 14 / 28 MHz.
///
/// The model is the board as built (owner decision 2026-11-11): where the VHDL looks like a bug the decoder copies it
/// and carries the fix as a commented-out block tagged `P2666-FIX D-n`; nothing enables those blocks.
///
/// Timing (HR §1, POC 051): no memory wait states at any speed (the SRAM is time-sliced with the video); every I/O
/// machine cycle runs on the 3.5 MHz clock grid (`cpu_speed = not IOcycle & ...`), so at 14 / 28 MHz its length
/// depends on the phase of the slow clock; a clock switch takes effect at the end of the I/O cycle that wrote it;
/// the INT pulse lasts 32 CPU clock rising edges at whatever speed (also slow I/O edges). The decoder computes these
/// on the board's master clock (`mainclk`, 114 MHz, 2293760 edges per frame) and sets the CPU's clock and INT
/// window from them.
class PortDecoderP2666 : public PortDecoder, public IMachineM1Hook
{
public:
    /// region <Constants>
    static constexpr uint32_t kFrameEdges = 2293760;       ///< mainclk edges per frame (896 x 640 x 4)
    /// The INT strobe's first edge (VGA line 607, pixel int_delay + 1 = 705: zx_main.vhd:462-468) in the board's
    /// frame numbering, as the oracle counts it
    static constexpr uint32_t kIntStrobeBoardEdge = 2178313;
    /// The emulator's frame starts this many edges (a multiple of 32: the clock phases stay) before the board's
    /// raster origin: the ZX paper's cell fetches land where the renderer latches the cells, line 80 + Y at
    /// T 30 + 4 c (P2666Geometry: the board reads them 1472 + 128 c edges into the VGA line pair 2 (Y + 64)), and the
    /// INT strobe on base T 71640
    static constexpr uint32_t kFrameOriginShift = 114176;
    static constexpr uint32_t kIntStrobeEdge = kIntStrobeBoardEdge + kFrameOriginShift;  ///< in the emulator's frame
    static constexpr uint8_t kRomFirstPage = 0x48;          ///< RAM page of image page 0 (Gluk); #48..#4C protected
    static constexpr uint8_t kRomLastPage = 0x4C;
    static constexpr uint32_t kAyClockHz = 1838710;  ///< 114 MHz / 62 (HR §5.1)

    static constexpr uint8_t kEff7P16 = 0x01;      ///< Pentagon 16 colour
    static constexpr uint8_t kEff7Lock128 = 0x04;  ///< 128 mode: #7FFD lock, no palette, no INT flag
    static constexpr uint8_t kEff7Ram0 = 0x08;     ///< RAM page 0 at #0000
    static constexpr uint8_t kEff7Slow = 0x10;     ///< turbo0: 3.5 MHz while xx77 bit 3 = 0
    static constexpr uint8_t kEff7Cmos = 0x80;     ///< #DFF7 / #BFF7 on
    static constexpr uint8_t kX77Turbo = 0x08;     ///< turbo1: 28 MHz
    /// endregion </Constants>

    /// region <Constructors / Destructors>
public:
    PortDecoderP2666() = delete;
    explicit PortDecoderP2666(EmulatorContext* context);
    ~PortDecoderP2666() override;
    /// endregion </Constructors / Destructors>

    /// region <PortDecoder>
public:
    void reset() override;
    /// The MCU's upload (HR §2.1, §7): the boot logo into RAM page 5, the default palette, the CMOS cells
    void PowerCycle() override;
    /// The board always starts in Gluk: DOS on, #7FFD = 0 (HR §7); the RESET= modes are not a board feature
    ROMModeEnum ResolveResetROMMode(ROMModeEnum mode) override
    {
        (void)mode;
        return RM_SYS;
    }
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;
    /// The four windows from the active map (HR §2.2)
    void UpdateModelMemoryBanks() override;
    bool BoardHasKempstonJoystick() const override { return true; }
    /// Nemo IDE: outside the FPGA, on `A2 = A1 = 0` in any mode (the TR-DOS ports never have `A2 = A1 = 0`)
    IdeAdapter::Gate IdeGate() override { return {}; }
    RtcBinding GetRtcBinding() override;
    bool IsPagingLocked() const override;
    uint8_t TtdClockUnits() const override { return 8; }
    /// The SD card's protocol state (EvoSdCard); the rest of the board is P5
    std::vector<ttd::TTDDeviceType> GetTTDModelStateTypes() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// xx77 bit 3 in the low byte, #EFF7 bit 4 in the high byte
    uint16_t GetClockSelect() const override;
    void RestoreClockSelect(uint16_t select) override;
    /// A 48K / 128K snapshot runs on a plain Pentagon 128: every window in Pentagon mode, DOS off, the ZX picture; the
    /// clock select stays
    void EnterSpectrum128Paging(uint16_t pc) override;
    /// #7FFD bit 5 is always a page bit of window 3 (HR §2.5, D-3): a locked state's bank goes into page 32 + bank
    void FinishSpectrum128State() override;
    /// The INT window in CPU clocks at the clock now in use (HR §1.2): the strobe's edge, 32 CPU rising edges long
    bool FrameIntWindow(uint32_t& start, uint32_t& end) override;
    /// endregion </PortDecoder>

    /// region <IMachineM1Hook: the DOS signal (HR §3, PaE.vhd:1207-1226)>
public:
    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override { (void)address; }
    bool RepeatM1IsInert(uint16_t address) const override { return !DosChangeAt(address); }
    /// endregion </IMachineM1Hook>

    /// region <Board model (public for the tests)>
public:
    /// CPU clock ratio the latches select: 8 (28 MHz), 4 (14 MHz) or 1 (3.5 MHz) (HR §1.1)
    uint8_t SelectedRatio() const;
    /// Physical 16 KB page of a window (0..3) now, 0..127 (HR §2.2)
    uint8_t WindowPage(uint8_t window) const;
    /// The palette word as a host colour (ABGR, HR §6.2)
    static uint32_t PaletteWordToAbgr(uint16_t word);
    /// The board's default palette (pollitra.hex)
    static const std::array<uint16_t, 16>& DefaultPalette();
    /// FE bit 5: an INT strobe since the last OUT (#FE), #20
    bool IntFlag() const;
    /// The CMOS cell the read mux answers for `address` (PaE.vhd:1497-1515)
    uint8_t CmosRead(uint8_t address);
    void CmosWrite(uint8_t address, uint8_t value);
    Ds12887& GetRtc() { return _rtc; }
    const std::array<uint8_t, 64>& McuCmos() const { return _mcuCmos; }
    /// The Z-Controller SD (HR §5.4, PaE.vhd:972-1029, 1286-1296): the media manager's slot "sd.zc". A CPU reset
    /// keeps the card, its session writes and the config (sd_config has no reset term); power-on config 0 = selected
    SdCardSpi& GetSdCard() { return _sdCard; }
    ZControllerSpi& GetZController() { return _zc; }
    /// The board's sound (HR §5.1-5.3): two YM2149, Covox #FB and the beeper in one 8-bit DAC word per side
    P2666Audio& GetAudio() { return *_audio; }
    /// The Beta Disk emulator on the RAM disk (HR §5.6, P4 default); null with [P2666] BetaDisk=wd1793 (path A)
    P2666BetaDisk* GetRamDisk() { return _ramDisk ? &_beta : nullptr; }
    /// The FPGA's UART on #F8EF..#FFEF (HR §5.7): the machine's serial port, its INT ANDed into the frame INT
    P2666Uart& GetUart() { return _uart; }
    const P2666Uart& GetUart() const { return _uart; }
    NetworkCapabilities DescribeNetwork() override;
    void OnFrameEnd() override;
    /// endregion </Board model>

    /// region <Fields>
protected:
    friend struct P2666Codec;  // the TTD blobs (debugger/ttd/p2666/ttdp2666.cpp)
    Ds12887 _rtc;                       ///< the MCU's clock behind CMOS cells 0..9
    std::array<uint8_t, 256> _cmos{};   ///< the FPGA's CMOS RAM (cmos_data_out); 64 cells persisted by the MCU
    uint8_t _cmosAddress = 0;           ///< #DFF7
    /// The MCU's copy of cells #00..#3F: uploaded into the FPGA at power-on, taken back after every #BFF7 write
    /// (cmos_mcu_update, PaE.vhd:871-910, 943); [P2666] CmosFile across sessions
    std::array<uint8_t, 64> _mcuCmos{};
    bool _mcuCmosLoaded = false;
    std::unique_ptr<P2666Audio> _audio;
    /// Absolute base T of the CPU now (the sound's time source)
    double Now() const;
    /// mainclk edges since the session start (32 per base T): the Beta Disk emulator's clock
    uint64_t Edge() const { return static_cast<uint64_t>(Now() * 32.0); }
    P2666BetaDisk _beta;
    P2666Uart _uart;
    bool _ramDisk = true;
    DiskImage* _ramDiskImage = nullptr;  ///< the drive A image the RAM disk holds
    /// The MCU loads the TRD of drive A into the RAM disk at power-on (firmware/test.trd on the board)
    void LoadRamDisk();

    class SdSlot : public IMediaSlot
    {
    public:
        explicit SdSlot(PortDecoderP2666& owner);
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override;
        void Detach() override;
        bool IsBusy() const override;

    private:
        PortDecoderP2666& _owner;
        SlotDescriptor _descriptor;
    };

    // The bus analyzer's view of the Z-Controller bus: declared before the card and the controller, which point at it
    BusTap _spiTap{"zc.spi", BusProtocol::Spi, "Z-Controller SPI (#57 data, #77 / #xx57 config): SD card"};
    SdCardSpi _sdCard;
    ZControllerSpi _zc;
    SdSlot _sdSlot{*this};
    /// endregion </Fields>

    /// region <Helpers>
protected:
    bool DosOn() const { return (_state->flags & CF_TRDOS) != 0; }
    bool DosChangeAt(uint16_t address) const;
    /// The writes of one OUT, in the board's parallel decode (HR §4.1)
    void WritePorts(uint16_t port, uint8_t value, uint16_t pc);
    uint8_t ReadPort(uint16_t port, uint16_t pc);
    void WriteFE(uint16_t port, uint8_t value, uint16_t pc);
    void Write7FFD(uint8_t value);
    void Write77Atm(uint16_t port, uint8_t value);
    void WriteDispatcher(uint16_t port, uint8_t value);
    void WritePalette(uint16_t port, uint8_t value);
    /// @param live a CPU write: the picture up to it keeps the old colours (not at power-on, nothing to flush yet)
    void SetPaletteEntry(uint8_t index, uint16_t word, bool live = true);
    /// End of an I/O cycle (PaE.vhd:1220): dos_1 := cpm AND NOT BF.0
    void LatchDos1();

    /// region <Clock>
    /// Frame-relative mainclk edge of CPU clock `t` at the composed multiplier `u` (1 / 4 / 8)
    static uint32_t EdgeOfClock(uint32_t t, uint8_t u) { return t * (32u / u) + (u == 8 ? 2u : 0u); }
    /// The I/O machine cycle the CPU is in (the decoder runs 1 T into it): stretch it to the 3.5 MHz grid, switch the
    /// clock to `newRatio` at its end and count its clock edges into the INT pulse
    void IoCycleTiming(uint8_t newRatio);
    /// The INT pulse of this frame: (re)start it on a new frame
    void SyncIntPulse();
    /// The I/O cycle [m, e] (T1 rising .. the next T1 rising) at multiplier u, then u2: its rising edges in the pulse
    void NoteIoForIntPulse(uint32_t m, uint32_t f, uint32_t e, uint8_t u, uint8_t u2);
    /// The edge INT goes high, counting from the anchor on the grid of multiplier u
    uint32_t IntRiseEdge(uint8_t u) const;
    uint32_t IntFallEdge(uint8_t u) const;
    void RecomputeIntWindow();
    /// endregion </Clock>
    /// endregion </Helpers>
};
