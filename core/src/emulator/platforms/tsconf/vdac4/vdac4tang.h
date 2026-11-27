#pragma once

/// @file vdac4tang.h
/// @brief The VDAC4 board's Tang Nano 20K FPGA as the Evo and the ESP see it.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §5; facts:
/// research-tang-video-ft-core.md (R3) in the same folder.
///
/// The FPGA is multiboot. Slot 0 is the TS-Conf HDMI core (no FT812 there:
/// MISO reads 0), slot 1 an FT812 emulation (with the BL616 coprocessor).
/// From the Evo the board sits where the VDAC2 card's FT812 sits: slot 1 of
/// the Z-Controller SPI hub, #77 D2 active high, INT_N on the line interrupt
/// while M_SEL = 1. So the Tang is an EveHostCard with a personality:
///
///   - TS (slot 0): the Evo's FT812 traffic is watched, not answered. An
///     ACTIVE host command (24 zero bits) or a memory access of 32+ bits
///     while armed starts the slot-1 load (RTL ts_fb_core.sv:495-554: go =
///     24 bits all zero || (>= 32 bits && fs_sr[30] == 0)).
///   - Loading: LoadDelayMs (174 ms); MISO still 0, writes lost.
///   - FT (slot 1): the chip runs, it came up ACTIVE at 64 MHz (8 MHz x 8,
///     ft_core.sv:33-37). It falls back to TS after M_SEL = 0 and 500 ms
///     without Evo FT traffic (SWITCH_IDLE_MS, ts_fb_core.sv:561-590), unless
///     the ESP is the host; RAM_G, display lists and registers are gone then.
///
/// Arming: slot 0 resets disarmed (ts_fb_core.sv:568-580); the ESP writes
/// "ARM1" to LINK BOOT after its boot. Until the ESP model drives the LINK the
/// Tang arms itself at EspBootMs after power-on (design §5.3).
///
/// ESP host mode (FT_CFG bit 2): the Evo's SPI is cut off (reads 0, writes
/// dropped), INT_N inactive, the idle timer frozen (R3 §3.5).
///
/// The ESP reaches the board through the LINK register file (§5.4):
/// LinkRead / LinkWrite, 32-bit words at byte addresses.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "emulator/platforms/tsconf/evehostcard.h"

class EmulatorContext;
class Vdac4BomzhCore;

class Vdac4Tang : public EveHostCard
{
public:
    /// Where the board's FT812 core sits on the Z-Controller hub (as VDAC2)
    static constexpr uint8_t kSpiSlot = 1;
    static constexpr uint8_t kSpiSelectMask = 0x04;  // #77 D2
    static constexpr bool kSpiSelectActiveHigh = true;

    /// The Tang's FT812 core runs from 8 MHz x 8 (ft_core.sv:33-37)
    static constexpr uint32_t kCrystalHz = 8'000'000;
    /// Raster tacts per millisecond (3.5 MHz)
    static constexpr uint64_t kTactsPerMs = kRasterHz / 1000;
    /// FT -> TS after this long without Evo FT traffic while M_SEL = 0 (RTL SWITCH_IDLE_MS)
    static constexpr uint32_t kSwitchIdleMs = 500;
    /// The Quake II core's HDMI frame (60 Hz)
    static constexpr uint32_t kQ2FramePeriodUs = 16'667;

    enum class Personality : uint8_t
    {
        Ts = 1,       ///< slot 0, TS-Conf HDMI core
        Loading = 0,  ///< slot 1 bitstream loading
        Ft = 2,       ///< slot 1, FT812 core
        Q2 = 3,       ///< a core from the Evo card (core_load "Q2"): the Quake II GPU, its own HDMI picture
    };

    /// LINK register addresses (R3 §2)
    enum LinkRegister : uint32_t
    {
        LinkId = 0x00,
        LinkVersion = 0x04,
        LinkStatus = 0x08,
        LinkFtRstat = 0x28,
        LinkFtRtime = 0x2C,
        LinkCpInfo = 0x30,
        LinkFtFree = 0x38,
        LinkScratch = 0x3C,
        LinkFtSeen = 0x50,
        LinkFtAddr = 0x54,
        LinkFtData = 0x58,
        LinkFtStat = 0x5C,
        LinkFtHost = 0xE8,
        LinkBoot = 0xF0,
        LinkFtBurst = 0x40000,
        LinkFtBurstEnd = 0x80000,
    };

    Vdac4Tang(EmulatorContext* context, std::function<uint32_t()> rasterInFrame);
    ~Vdac4Tang() override;

    /// region <EveHostCard>
    void PowerOn() override;
    /// The Evo's reset: TS-Conf clears M_SEL; the FT core falls back by the idle rule (R3 §3.4)
    void Reset() override;
    void OnFrameEnd() override;
    void SetShowing(bool showing) override;
    /// The ESP job that hosts the FT812 is done with the chip
    void WaitChipIdle() override { WaitEsp(); }
    size_t TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max) override;
    void select(bool selected) override;
    uint8_t exchange(uint8_t mosi) override;
    /// endregion

    /// region <LINK (the ESP's side)>
    uint32_t LinkRead(uint32_t address);
    void LinkWrite(uint32_t address, uint32_t value);
    /// LinkWrite(FT_ADDR, ftAddress), then LinkWrite(FT_DATA, word) for each of `count` words (little-endian bytes)
    void LinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count);
    /// core_load of a core the BL616 holds (its VDAC4 mark `usercode`): the Tang runs it and answers its LINK
    /// VERSION. kZmOk (0), or -1 when no emulated core has that mark. Only the Quake II core (0x564434'Q') is
    /// emulated
    int LoadCore(uint32_t usercode, uint32_t& version);
    /// tang::backToSlot0: BOOT, then the TS-Conf core runs. True when it does
    bool CoreBack();
    /// The Quake II core while it runs (Personality::Q2), else nullptr
    const Vdac4BomzhCore* Q2Core() const { return _q2.get(); }
    /// Quake II pictures latched to the Screen since the core was loaded
    uint64_t Q2LatchedFrames() const { return _q2Latched; }

    /// region <The ESP's jobs (vdac4board.h)>
    /// The owner's position now, absolute raster tacts (the stamp of an ESP job; a job running meanwhile may have
    /// pinned "now" to its own stamp)
    uint64_t CurrentTact() const { return OwnerNow(); }
    /// A job of the ESP may be running on another thread with this Tang (its FT812 included): the owner's
    /// entry points (SPI select / exchange, frame end, showing, power, reset) first call `waitEsp`, and INT_N
    /// edges are not looked at (the ESP is the FT host then: INT_N is held inactive)
    void SetEspJobOutstanding(bool outstanding) { _espJob = outstanding; }
    void SetEspWait(std::function<void()> waitEsp) { _waitEsp = std::move(waitEsp); }
    /// endregion
    /// endregion

    Personality GetPersonality() const { return _state.personality; }
    /// Armed by "ARM1" or by the ESP's boot time having passed
    bool IsArmed() const { return _state.armed != 0 || Now() >= _state.armAt; }
    bool IsEspHost() const { return (_state.ftCfg & kFtCfgEspHost) != 0; }
    bool IsRamG3M() const { return (_state.ftCfg & kFtCfgRamG3M) != 0; }

    /// The Tang's own state, plain bytes (TTD)
    struct State
    {
        Personality personality = Personality::Ts;
        uint8_t armed = 0;
        uint8_t msel = 0;
        uint8_t selected = 0;
        uint32_t ftCfg = 0;
        uint64_t armAt = 0;          ///< absolute raster tact the ESP arms the Tang (UINT64_MAX: never by itself)
        uint64_t loadDoneAt = 0;     ///< Loading ends here
        uint64_t lastEvoTraffic = 0; ///< the last Evo FT transfer (idle rule)
        uint32_t windowBytes = 0;    ///< bytes of the Evo transfer in flight (TS personality decoder)
        uint8_t windowFirst = 0;
        uint8_t windowAllZero = 1;
        uint8_t ftSeen = 0;          ///< Evo FT traffic since the ESP last read FT_SEEN
        uint8_t reserved = 0;
        uint32_t ftAddr = 0;         ///< FT window address (LINK FT_ADDR)
        uint32_t scratch = 0;
    };
    const State& GetState() const { return _state; }
    /// The Quake II core's drawing state is not part of it: TTD is unavailable while it runs (Vdac4Board); a
    /// state with personality Q2 starts the core afresh
    void SetState(const State& state);

    static constexpr uint32_t kFtCfgRamG3M = 0x01;
    static constexpr uint32_t kFtCfgNoDither = 0x02;
    static constexpr uint32_t kFtCfgEspHost = 0x04;
    static constexpr uint32_t kFtCfgRomWrite = 0x08;

private:
    /// Personality changes due by `now` (absolute raster tacts)
    void Update(uint64_t now);
    void StartLoading(uint64_t now);
    void EnterFt();
    void EnterTs();
    void EnterQ2();
    /// The Quake II core goes (BOOT, power): the Screen's picture back to what the TS / FT personality shows
    void LeaveQ2();
    /// A new picture of the Quake II core: latched to the Screen (its own picture while the core runs)
    void PresentQ2();
    uint64_t NowMs() const { return Now() / kTactsPerMs; }
    /// RAM_G size from FT_CFG bit 0 (2 MB + ROM fonts, or 3 MB)
    void ApplyRamMode();
    /// An Evo transfer ended in TS personality: does it start the FT core?
    void DecodeEvoWindow(uint64_t now);
    /// Chip bus access from the ESP side (FT window, host commands)
    void ChipWrite32(uint32_t address, uint32_t value);
    uint32_t ChipRead32(uint32_t address);
    void ChipHostCommand(uint8_t command, uint8_t parameter);

    void WaitEsp()
    {
        if (_espJob && _waitEsp)
            _waitEsp();
    }

    State _state;
    std::unique_ptr<Vdac4BomzhCore> _q2;
    std::vector<uint8_t> _q2Picture;
    uint16_t _q2Width = 0;
    uint16_t _q2Height = 0;
    uint32_t _q2Frames = 0;     ///< the core's FRAMES as of the last latch
    uint64_t _q2Latched = 0;
    bool _espJob = false;
    std::function<void()> _waitEsp;
};
