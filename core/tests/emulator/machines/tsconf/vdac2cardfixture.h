#pragma once

// The TS-Conf VDAC2 test stand shared by the card tests (vdac2card_test.cpp) and
// the control tests (vdac2control_test.cpp): a TS-Conf machine in the VDAC2 build,
// the FT812 driven through #77 / #57 as the TS-Labs SDK drives it, raster time
// moved on by hand.

#ifdef ENABLE_VDAC2

#include "tsconffixture.h"

#include <vector>

#include "emulator/platforms/tsconf/vdac2card.h"

namespace Vdac2Test
{

// Ports and #77 values: D1 = SD /CS (kept high: SD deselected), D2 = FT812 CS
inline constexpr uint16_t kPortConfig = 0x77;
inline constexpr uint16_t kPortData = 0x57;
inline constexpr uint8_t kDeselectAll = 0x02;
inline constexpr uint8_t kSelectFt812 = 0x06;

// FT812 host commands and registers (behavior spec §2.2, §3)
inline constexpr uint8_t kHostActive = 0x00;
inline constexpr uint8_t kHostSleep = 0x42;
inline constexpr uint8_t kHostPowerDown = 0x43;
inline constexpr uint8_t kHostClkExt = 0x44;
inline constexpr uint8_t kHostClkSel = 0x61;
inline constexpr uint8_t kHostRstPulse = 0x68;
inline constexpr uint8_t kClkSelRangeBits = 0x40;
inline constexpr uint32_t kRegId = 0x302000;
inline constexpr uint32_t kRegClock = 0x302008;
inline constexpr uint32_t kRegCpuReset = 0x302020;
inline constexpr uint8_t kRegIdValue = 0x7C;
inline constexpr uint32_t kRamDl = 0x300000;
inline constexpr uint32_t kRegHcycle = 0x30202C;
inline constexpr uint32_t kRegHoffset = 0x302030;
inline constexpr uint32_t kRegHsize = 0x302034;
inline constexpr uint32_t kRegVcycle = 0x302040;
inline constexpr uint32_t kRegVoffset = 0x302044;
inline constexpr uint32_t kRegVsize = 0x302048;
inline constexpr uint32_t kRegDlswap = 0x302054;
inline constexpr uint32_t kRegPclk = 0x302070;
inline constexpr uint32_t kRegIntFlags = 0x3020A8;
inline constexpr uint32_t kRegIntEn = 0x3020AC;
inline constexpr uint32_t kRegIntMask = 0x3020B0;
inline constexpr uint32_t kIntSwap = 0x01;
inline constexpr uint32_t kDlswapFrame = 2;
inline constexpr uint32_t kDlClear = 0x26000007;    // CLEAR(1, 1, 1)
inline constexpr uint32_t kDlDisplay = 0x00000000;  // DISPLAY
inline constexpr uint32_t kDlClearColorRed = 0x02FF0000;  // CLEAR_COLOR_RGB(255, 0, 0)
inline constexpr uint8_t kMsel = 0x04;              // V_CONFIG bit 2: the monitor shows the FT812
// One FT812 frame of the small scan in raster tacts, rounded up
inline constexpr uint32_t kSmallFrameTacts = static_cast<uint32_t>((100ull * 50 * 3500000 + 48000000 - 1) / 48000000);
inline constexpr uint32_t kRamG = 0x000000;

inline constexpr uint8_t kMul48MHz = 6;  // 8 MHz crystal x 6 (TS-Labs modes 0, 5, ...)
inline constexpr uint64_t kHz48MHz = 48'000'000;
inline constexpr uint32_t kPollTacts = 1000;
inline constexpr int kMaxPolls = 1000;

inline constexpr uint8_t kStatusRegister = 0x00;  // TS register #00 read: STATUS
inline constexpr uint8_t kVdacVersionMask = 0x07;


class Vdac2CardFixture : public TsConfFixture
{
protected:
    void SetUp() override
    {
        TsConfFixture::SetUp();
        _context->config.ts_vdac = 7;
        _context->config.vdac2_rom_path[0] = '\0';  // the ROM image is the user's; not needed here
        _decoder->reset();
        _position = 0;
        _z80->t = 0;
    }

    Vdac2Card* Card() { return _decoder->GetVdac2Card(); }

    /// region <Raster time>

    /// Move the CPU on by `tacts` raster tacts (crossing frames as Core does:
    /// Z80::t rebased, the engine rolled over)
    void Tick(uint64_t tacts)
    {
        const uint32_t multiplier = Multiplier();
        while (tacts > 0)
        {
            const uint64_t room = TsConfEngine::kFrameTacts - _position;
            if (tacts < room)
            {
                _position += static_cast<uint32_t>(tacts);
                break;
            }
            tacts -= room;
            _z80->t = TsConfEngine::kFrameTacts * multiplier;
            _decoder->CatchUpEngine();
            _z80->t = 0;  // Core rebases Z80::t first (AdjustFrameCounters), then calls the hook: nothing is left over
            _decoder->GetEngine().OnMachineFrameRollover(TsConfEngine::kFrameTacts * multiplier);
            _position = 0;
        }
        _z80->t = _position * multiplier;
    }

    uint32_t Multiplier() const
    {
        const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier;
        return multiplier ? multiplier : 1;
    }

    /// endregion

    /// region <The FT812 through #77 / #57, as the TS-Labs SDK drives it>

    void HostCommand(uint8_t command, uint8_t parameter = 0)
    {
        Out(kPortConfig, kSelectFt812);
        Out(kPortData, command);
        Out(kPortData, parameter);
        Out(kPortData, 0x00);
        Out(kPortConfig, kDeselectAll);
    }

    void Write(uint32_t address, const std::vector<uint8_t>& bytes)
    {
        Out(kPortConfig, kSelectFt812);
        Out(kPortData, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        Out(kPortData, static_cast<uint8_t>(address >> 8));
        Out(kPortData, static_cast<uint8_t>(address));
        for (uint8_t value : bytes)
            Out(kPortData, value);
        Out(kPortConfig, kDeselectAll);
    }

    /// Memory read: address, one dummy byte, then the data. A read of #57
    /// returns the byte of the PREVIOUS exchange, so the first IN only clocks
    std::vector<uint8_t> Read(uint32_t address, size_t count)
    {
        Out(kPortConfig, kSelectFt812);
        Out(kPortData, static_cast<uint8_t>((address >> 16) & 0x3F));
        Out(kPortData, static_cast<uint8_t>(address >> 8));
        Out(kPortData, static_cast<uint8_t>(address));
        Out(kPortData, 0x00);  // dummy
        In(kPortData);
        std::vector<uint8_t> bytes(count);
        for (uint8_t& value : bytes)
            value = In(kPortData);
        Out(kPortConfig, kDeselectAll);
        return bytes;
    }

    uint32_t Read32(uint32_t address)
    {
        const std::vector<uint8_t> b = Read(address, 4);
        return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24));
    }

    void Write32(uint32_t address, uint32_t value)
    {
        Write(address, {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
                        static_cast<uint8_t>(value >> 24)});
    }

    /// A tiny FT812 scan (100 x 50 clocks, PCLK = system clock: one frame =
    /// 5000 clocks, 364.6 raster tacts at 48 MHz), a cleared display list,
    /// a frame swap requested and INT_N enabled for SWAP only
    void StartSmallScanWithSwapInterrupt()
    {
        Write32(kRegHcycle, 100);
        Write32(kRegHoffset, 20);
        Write32(kRegHsize, 40);
        Write32(kRegVcycle, 50);
        Write32(kRegVoffset, 10);
        Write32(kRegVsize, 20);
        Write32(kRamDl + 0, kDlClear);
        Write32(kRamDl + 4, kDlDisplay);
        Write32(kRegPclk, 1);
        Write32(kRegIntMask, kIntSwap);
        Write32(kRegIntEn, 1);
        Write32(kRegDlswap, kDlswapFrame);
    }

    /// One CPU step's worth of interrupt and engine work at the current tact
    /// (TsConfEngine::OnMachineStep, as Z80 calls it after an instruction)
    void Step() { _decoder->GetEngine().OnMachineStep(_z80->t); }
    bool LineIntPending() { return (_decoder->GetState().intPending & TsConfInt::Line) != 0; }

    /// Tick in steps of `stride` tacts until the line INT latches (max `limit` tacts)
    /// @return tacts it took, or UINT32_MAX
    uint32_t TactsUntilLineInt(uint32_t limit, uint32_t stride = 1)
    {
        for (uint32_t elapsed = 0; elapsed <= limit; elapsed += stride)
        {
            Step();
            if (LineIntPending())
                return elapsed;
            Tick(stride);
        }
        return UINT32_MAX;
    }

    /// ft_init's power-up: external clock x `mul`, reset pulse, wait for
    /// REG_ID = 0x7C and REG_CPURESET = 0
    void Boot(uint8_t mul = kMul48MHz)
    {
        HostCommand(kHostPowerDown);
        HostCommand(kHostActive);
        HostCommand(kHostSleep);
        HostCommand(kHostClkExt);
        HostCommand(kHostClkSel, static_cast<uint8_t>(mul | kClkSelRangeBits));
        HostCommand(kHostActive);
        HostCommand(kHostRstPulse);
        int polls = 0;
        while (Read(kRegId, 1)[0] != kRegIdValue && polls++ < kMaxPolls)
            Tick(kPollTacts);
        ASSERT_EQ(Read(kRegId, 1)[0], kRegIdValue);
        polls = 0;
        while (Read32(kRegCpuReset) != 0 && polls++ < kMaxPolls)
            Tick(kPollTacts);
        ASSERT_EQ(Read32(kRegCpuReset), 0u);
    }

    /// endregion

    uint32_t _position = 0;  // raster tact inside the frame
};

} // namespace Vdac2Test

#endif // ENABLE_VDAC2
