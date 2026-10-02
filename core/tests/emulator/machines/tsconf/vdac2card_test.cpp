// TS-Conf VDAC2 card: the FT812 on the Z-Controller SPI bus, its time base
// (vdac2-integration-design.md §4-§5, phase I1; tests listed in §12).
//
// The chip is the eve-emu library; these tests drive it the way VDAC2
// software does, through #77 (chip select) and #57 (data), and check what the
// integration owns: the build gate, the bus, and raster time -> FT812 clocks.

#ifdef ENABLE_VDAC2

#include "tsconffixture.h"

#include <vector>

#include "emulator/platforms/tsconf/vdac2card.h"
#include "emulator/platforms/tsconf/vdac2control.h"
#include "emulator/video/screen.h"

#include <eve/eve.h>

#include <cstring>
#include <filesystem>

namespace
{

// Ports and #77 values: D1 = SD /CS (kept high: SD deselected), D2 = FT812 CS
constexpr uint16_t kPortConfig = 0x77;
constexpr uint16_t kPortData = 0x57;
constexpr uint8_t kDeselectAll = 0x02;
constexpr uint8_t kSelectFt812 = 0x06;

// FT812 host commands and registers (behavior spec §2.2, §3)
constexpr uint8_t kHostActive = 0x00;
constexpr uint8_t kHostSleep = 0x42;
constexpr uint8_t kHostPowerDown = 0x43;
constexpr uint8_t kHostClkExt = 0x44;
constexpr uint8_t kHostClkSel = 0x61;
constexpr uint8_t kHostRstPulse = 0x68;
constexpr uint8_t kClkSelRangeBits = 0x40;
constexpr uint32_t kRegId = 0x302000;
constexpr uint32_t kRegClock = 0x302008;
constexpr uint32_t kRegCpuReset = 0x302020;
constexpr uint8_t kRegIdValue = 0x7C;
constexpr uint32_t kRamDl = 0x300000;
constexpr uint32_t kRegHcycle = 0x30202C;
constexpr uint32_t kRegHoffset = 0x302030;
constexpr uint32_t kRegHsize = 0x302034;
constexpr uint32_t kRegVcycle = 0x302040;
constexpr uint32_t kRegVoffset = 0x302044;
constexpr uint32_t kRegVsize = 0x302048;
constexpr uint32_t kRegDlswap = 0x302054;
constexpr uint32_t kRegPclk = 0x302070;
constexpr uint32_t kRegIntFlags = 0x3020A8;
constexpr uint32_t kRegIntEn = 0x3020AC;
constexpr uint32_t kRegIntMask = 0x3020B0;
constexpr uint32_t kIntSwap = 0x01;
constexpr uint32_t kDlswapFrame = 2;
constexpr uint32_t kDlClear = 0x26000007;    // CLEAR(1, 1, 1)
constexpr uint32_t kDlDisplay = 0x00000000;  // DISPLAY
constexpr uint32_t kDlClearColorRed = 0x02FF0000;  // CLEAR_COLOR_RGB(255, 0, 0)
constexpr uint8_t kMsel = 0x04;              // V_CONFIG bit 2: the monitor shows the FT812
// One FT812 frame of the small scan in raster tacts, rounded up
constexpr uint32_t kSmallFrameTacts = static_cast<uint32_t>((100ull * 50 * 3500000 + 48000000 - 1) / 48000000);
constexpr uint32_t kRamG = 0x000000;

constexpr uint8_t kMul48MHz = 6;  // 8 MHz crystal x 6 (TS-Labs modes 0, 5, ...)
constexpr uint64_t kHz48MHz = 48'000'000;
constexpr uint32_t kPollTacts = 1000;
constexpr int kMaxPolls = 1000;

constexpr uint8_t kStatusRegister = 0x00;  // TS register #00 read: STATUS
constexpr uint8_t kVdacVersionMask = 0x07;

} // namespace

class Vdac2Card_Test : public TsConfFixture
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

/// The card exists exactly in the VDAC2 firmware build; STATUS[2:0] says 7
TEST_F(Vdac2Card_Test, FittedOnlyInTheVdac2Build)
{
    ASSERT_NE(Card(), nullptr);
    EXPECT_TRUE(Card()->IsReady());
    EXPECT_EQ(_decoder->ReadRegister(kStatusRegister) & kVdacVersionMask, 7);

    _context->config.ts_vdac = 0;
    _decoder->reset();
    EXPECT_EQ(Card(), nullptr);
    EXPECT_EQ(_decoder->ReadRegister(kStatusRegister) & kVdacVersionMask, 0);
    EXPECT_FALSE(_decoder->GetZController().IsSlotSelected(Vdac2Card::kSpiSlot));
}

/// [VDAC2] RomImage is loaded at the card's creation and appears at the top
/// of the chip's ROM space (a full image covers 0x1E0000..0x2FFFFF)
TEST_F(Vdac2Card_Test, RomImageAppearsInTheChipRomSpace)
{
    const std::string romPath = TestPathHelper::GetUniqueTestScratchPath("vdac2card_test_ft81x.rom");
    std::vector<uint8_t> image(Vdac2Card::kRomImageSize);
    for (size_t i = 0; i < image.size(); i++)
        image[i] = static_cast<uint8_t>(i * 13 + (i >> 16));
    ASSERT_TRUE(FileHelper::SaveBufferToFile(romPath, image.data(), image.size()));

    _context->config.ts_vdac = 0;
    _decoder->reset();  // remove the card, so the next reset creates it with the ROM
    std::strncpy(_context->config.vdac2_rom_path, romPath.c_str(), sizeof(_context->config.vdac2_rom_path) - 1);
    _context->config.ts_vdac = 7;
    _decoder->reset();
    ASSERT_NE(Card(), nullptr);
    EXPECT_TRUE(Card()->HasRom());

    Boot();
    constexpr uint32_t kRomFontRoot = 0x2FFFFC;  // the last word of the ROM
    const std::vector<uint8_t> expected(image.end() - 4, image.end());
    EXPECT_EQ(Read(kRomFontRoot, 4), expected);
    constexpr uint32_t kRomStart = 0x1E0000;
    EXPECT_EQ(Read(kRomStart, 4), std::vector<uint8_t>(image.begin(), image.begin() + 4));

    std::error_code ec;
    std::filesystem::remove(romPath, ec);
}

/// The SDK's boot handshake through #77 / #57 reaches the chip, and RAM_G
/// written by the Z80 reads back
TEST_F(Vdac2Card_Test, BootAndMemoryThroughTheZControllerPorts)
{
    Boot();
    const std::vector<uint8_t> data = {0x11, 0x22, 0x33, 0x44, 0x55, 0xAA, 0x00, 0xFF};
    Write(kRamG + 0x1234, data);
    EXPECT_EQ(Read(kRamG + 0x1234, data.size()), data);
}

/// D2 selects the FT812 only: with #77 = #02 the chip sees nothing and MISO
/// is the SD line (no card inserted: #FF)
TEST_F(Vdac2Card_Test, DeselectedChipIgnoresTheBus)
{
    Boot();
    Write(kRamG, {0x5A});
    Out(kPortConfig, kDeselectAll);
    Out(kPortData, 0x80);  // a write header the chip must not see
    Out(kPortData, 0x00);
    Out(kPortData, 0x00);
    Out(kPortData, 0xA5);
    In(kPortData);
    EXPECT_EQ(In(kPortData), 0xFF);
    EXPECT_EQ(Read(kRamG, 1)[0], 0x5A);
}

/// Raster tacts become FT812 system clocks exactly: whole frames give an
/// exact count (71680 tacts x 48 MHz / 3.5 MHz = 983040 clocks), and between
/// any two reads the count is the floor or the ceiling of the exact share
TEST_F(Vdac2Card_Test, ClockFollowsRasterTimeWithoutDrift)
{
    Boot();
    const uint32_t start = Read32(kRegClock);
    constexpr uint32_t kFrames = 50;
    constexpr uint64_t kClocksPerFrame = TsConfEngine::kFrameTacts * kHz48MHz / Vdac2Card::kRasterHz;
    static_assert(TsConfEngine::kFrameTacts * kHz48MHz % Vdac2Card::kRasterHz == 0);

    // Uneven access points: 7, 14, 21 ... tacts apart, then the rest of the frames
    uint64_t elapsed = 0;
    uint32_t previous = start;
    for (uint32_t step = 7; elapsed + step < kFrames * TsConfEngine::kFrameTacts; step += 7)
    {
        Tick(step);
        elapsed += step;
        const uint32_t now = Read32(kRegClock);
        const uint64_t exactFloor = step * kHz48MHz / Vdac2Card::kRasterHz;
        const uint32_t delta = now - previous;
        EXPECT_GE(delta, exactFloor) << "after " << elapsed << " tacts";
        EXPECT_LE(delta, exactFloor + 1) << "after " << elapsed << " tacts";
        previous = now;
        if (HasFailure())
            break;
    }
    Tick(kFrames * TsConfEngine::kFrameTacts - elapsed);
    EXPECT_EQ(Read32(kRegClock) - start, static_cast<uint32_t>(kFrames * kClocksPerFrame));
}

/// The CPU turbo does not change raster time: at 14 MHz (4 CPU clocks per
/// tact) a frame is still 983040 FT812 clocks
TEST_F(Vdac2Card_Test, ClockIndependentOfCpuTurbo)
{
    Boot();
    _decoder->WriteRegister(TsConfReg::SysConfig, 0x02);  // 14 MHz
    ASSERT_EQ(Multiplier(), 4u);
    _z80->t = _position * Multiplier();
    const uint32_t start = Read32(kRegClock);
    Tick(TsConfEngine::kFrameTacts);
    EXPECT_EQ(Read32(kRegClock) - start, TsConfEngine::kFrameTacts * kHz48MHz / Vdac2Card::kRasterHz);
}

/// msel = 0: the line INT keeps the line starts (tact 224 n - 1) with the
/// card fitted, and FT812 edges do not reach it
TEST_F(Vdac2Card_Test, LineInterruptFromLineStartsWithoutMsel)
{
    Boot();
    StartSmallScanWithSwapInterrupt();
    Tick(TsConfEngine::kFrameTacts - _position);  // a fresh frame
    _decoder->WriteRegister(TsConfReg::IntMask, TsConfInt::Line);
    EXPECT_EQ(TactsUntilLineInt(TsConfEngine::kLineTacts), TsConfEngine::kLineTacts - 1);
}

/// msel = 1: no line starts; the FT812's INT_SWAP edge starts the line INT,
/// within one FT812 frame of the swap request, at the tact the chip lowers
/// INT_N (stepped between bus accesses, not at one)
TEST_F(Vdac2Card_Test, Ft812SwapInterruptIsTheLineInterruptWithMsel)
{
    Boot();
    Tick(TsConfEngine::kFrameTacts - _position);
    _decoder->WriteRegister(TsConfReg::VConfig, kMsel);  // from line 1: line 0 has latched it already
    Tick(TsConfEngine::kLineTacts);
    Step();
    _decoder->WriteRegister(TsConfReg::IntMask, TsConfInt::Line);
    Read32(kRegIntFlags);  // nothing pending

    // No FT812 interrupt: no line INT for a whole frame
    EXPECT_EQ(TactsUntilLineInt(TsConfEngine::kFrameTacts - 2 * TsConfEngine::kLineTacts, 7), UINT32_MAX);

    StartSmallScanWithSwapInterrupt();
    const uint32_t tacts = TactsUntilLineInt(2 * kSmallFrameTacts);
    ASSERT_NE(tacts, UINT32_MAX) << "INT_SWAP never reached the line INT";
    EXPECT_LE(tacts, kSmallFrameTacts + 1);
    EXPECT_GT(tacts, 0u) << "the edge comes from the scan, not from the bus write";

    // Acknowledged on both sides: the next swap interrupts again
    EXPECT_EQ(_decoder->GetInterrupts().AcknowledgeInterrupt(_z80->t), 0xFD);
    EXPECT_EQ(Read32(kRegIntFlags) & kIntSwap, kIntSwap);
    Write32(kRegDlswap, kDlswapFrame);
    EXPECT_NE(TactsUntilLineInt(2 * kSmallFrameTacts), UINT32_MAX);
}

/// The edge counts only on a line latched with msel: msel written mid-line
/// takes effect from the next line start
TEST_F(Vdac2Card_Test, MselIsLatchedAtTheLineStart)
{
    Boot();
    Tick(TsConfEngine::kFrameTacts - _position + 10);  // tact 10 of line 0
    _decoder->WriteRegister(TsConfReg::IntMask, TsConfInt::Line);
    _decoder->WriteRegister(TsConfReg::VConfig, kMsel);  // line 0 already latched msel = 0
    EXPECT_EQ(TactsUntilLineInt(TsConfEngine::kLineTacts), TsConfEngine::kLineTacts - 1 - 10)
        << "line 0 still ends with its line-start INT";
    EXPECT_EQ(_decoder->GetInterrupts().AcknowledgeInterrupt(_z80->t), 0xFD);
    Tick(1);
    EXPECT_EQ(TactsUntilLineInt(3 * TsConfEngine::kLineTacts, 3), UINT32_MAX) << "lines 1.. drive from the FT812";
}

/// msel = 1 at a machine frame end: the Screen shows the FT812 picture
/// (HSIZE x VSIZE), latched at the FT812's frame end with its pixels in the
/// framebuffer's RGBA order; msel = 0 brings the Evo's raster back
TEST_F(Vdac2Card_Test, MonitorShowsTheFt812PictureWhileMselIsSet)
{
    Screen* screen = _context->pScreen;
    ASSERT_NE(screen, nullptr);

    Boot();
    StartSmallScanWithSwapInterrupt();
    Write32(kRamDl + 0, kDlClearColorRed);
    Write32(kRamDl + 4, kDlClear);
    Write32(kRamDl + 8, kDlDisplay);
    Write32(kRegDlswap, kDlswapFrame);

    _decoder->WriteRegister(TsConfReg::VConfig, kMsel);
    Tick(TsConfEngine::kFrameTacts - _position);  // the frame end sees msel latched
    EXPECT_TRUE(Card()->IsShowing());
    ASSERT_TRUE(screen->IsExternalPictureActive());
    const FramebufferDescriptor& shown = screen->GetFramebufferDescriptor();
    EXPECT_EQ(shown.width, 40);
    EXPECT_EQ(shown.height, 20);

    const uint64_t before = Card()->LatchedFrames();
    Tick(4 * kSmallFrameTacts);
    Read32(kRegId);  // any access brings the chip to now
    EXPECT_GE(Card()->LatchedFrames(), before + 3) << "one latch per FT812 frame";

    std::vector<uint8_t> presented(shown.memoryBufferSize);
    screen->SetPresentDelayFrames(0);
    ASSERT_TRUE(screen->CopyPresentedFramebuffer(presented.data(), presented.size()));
    const size_t center = (static_cast<size_t>(10) * 40 + 20) * 4;
    EXPECT_EQ(presented[center + 0], 0xFF) << "R";
    EXPECT_EQ(presented[center + 1], 0x00) << "G";
    EXPECT_EQ(presented[center + 2], 0x00) << "B";
    EXPECT_EQ(presented[center + 3], 0xFF) << "A";

    _decoder->WriteRegister(TsConfReg::VConfig, 0x00);
    Tick(TsConfEngine::kFrameTacts - _position);
    EXPECT_FALSE(Card()->IsShowing());
    EXPECT_FALSE(screen->IsExternalPictureActive());
    EXPECT_EQ(&screen->GetFramebufferDescriptor(), &screen->GetNativeFramebufferDescriptor());
}

/// A capture started on a running chip replays into a fresh chip: the state
/// record restores it, then every byte's answer and every frame count match
/// (vdac2-test-corpus.md §4)
TEST_F(Vdac2Card_Test, CaptureOnARunningChipReplaysExactly)
{
    Boot();
    StartSmallScanWithSwapInterrupt();
    Tick(3 * kSmallFrameTacts);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2card_test_capture.evr");
    std::string error;
    ASSERT_TRUE(Card()->StartCapture(path, &error)) << error;
    EXPECT_TRUE(Card()->IsCapturing());

    // Traffic: RAM_G writes and reads, a swap, scan frames
    Write(kRamG + 0x100, {1, 2, 3, 4, 5, 6, 7, 8});
    Read(kRamG + 0x100, 8);
    Write32(kRamDl + 0, kDlClearColorRed);
    Write32(kRamDl + 4, kDlClear);
    Write32(kRamDl + 8, kDlDisplay);
    Write32(kRegDlswap, kDlswapFrame);
    Tick(5 * kSmallFrameTacts);
    Read32(kRegClock);
    ASSERT_TRUE(Card()->StopCapture());
    EXPECT_FALSE(Card()->IsCapturing());
    const Vdac2Capture::Stats stats = Card()->GetCaptureStats();
    EXPECT_GT(stats.exchanges, 30u);
    EXPECT_GE(stats.frames, 4u);

    std::vector<uint8_t> file(static_cast<size_t>(std::filesystem::file_size(path)));
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(std::fread(file.data(), 1, file.size(), f), file.size());
        std::fclose(f);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_GE(file.size(), Vdac2Capture::kHeaderSize);
    ASSERT_EQ(std::memcmp(file.data(), "EVR1", 4), 0);

    size_t pos = 0;
    auto u8 = [&]() { return file[pos++]; };
    auto le = [&](int bytes) {
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++)
            v |= static_cast<uint64_t>(file[pos++]) << (8 * i);
        return v;
    };
    pos = 40;
    EXPECT_EQ(le(4) & 1u, 0u) << "not from power-on";
    pos = Vdac2Capture::kHeaderSize;

    // Replay into a fresh chip
    EveConfig config{};
    config.structSize = sizeof(EveConfig);
    config.model = EVE_MODEL_FT812;
    config.externalClockHz = Vdac2Card::kCrystalHz;
    EveChip* chip = EveCreate(&config);
    ASSERT_NE(chip, nullptr);
    uint64_t clock = 0;
    bool started = false;
    size_t bytes = 0;
    size_t frames = 0;
    bool ended = false;
    while (pos < file.size() && !ended && !HasFailure())
    {
        const uint8_t kind = u8();
        uint64_t delta = 0;
        for (int shift = 0;; shift += 7)
        {
            const uint8_t b = u8();
            delta |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80))
                break;
        }
        clock += delta;
        if (started && clock > EveTotalClocks(chip))
            EveAdvance(chip, clock - EveTotalClocks(chip));
        switch (kind)
        {
        case Vdac2Capture::kState:
        {
            const size_t stateSize = static_cast<size_t>(le(4));
            ASSERT_EQ(EveLoadState(chip, file.data() + pos, stateSize), 0) << EveLastError();
            pos += stateSize;
            const size_t regions = static_cast<size_t>(le(4));
            for (size_t r = 0; r < regions; r++)
            {
                const size_t nameLength = u8();
                const std::string name(reinterpret_cast<const char*>(file.data() + pos), nameLength);
                pos += nameLength;
                const size_t size = static_cast<size_t>(le(4));
                EveRegion region{};
                EveGetRegion(chip, r, &region);
                ASSERT_EQ(name, region.name);
                ASSERT_EQ(size, region.size);
                std::memcpy(region.base, file.data() + pos, size);
                pos += size;
            }
            EveMemoryRestored(chip);
            ASSERT_EQ(EveTotalClocks(chip), clock) << "the state carries the chip's clock";
            started = true;
            break;
        }
        case Vdac2Capture::kSelect:
            EveSelect(chip, u8());
            break;
        case Vdac2Capture::kByte:
        {
            const uint8_t mosi = u8();
            const uint8_t miso = u8();
            ASSERT_EQ(EveExchange(chip, mosi), miso) << "byte " << bytes << " at clock " << clock;
            bytes++;
            break;
        }
        case Vdac2Capture::kFrame:
        {
            const uint64_t count = le(8);
            pos += 1 + 2 + 2 + 8;
            EXPECT_EQ(EveCompletedFrames(chip), count) << "at clock " << clock;
            frames++;
            break;
        }
        case Vdac2Capture::kEnd:
            ended = true;
            break;
        default:
            FAIL() << "unknown record " << int(kind);
        }
    }
    EveDestroy(chip);
    EXPECT_TRUE(ended);
    EXPECT_EQ(bytes, stats.exchanges);
    EXPECT_EQ(frames, stats.frames);
}

/// Vdac2Control, what every automation surface calls: the capture through
/// it, and the reasons it gives without a card
TEST_F(Vdac2Card_Test, ControlStartsStopsAndExplainsRefusals)
{
    Boot();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2card_test_control.evr");
    std::string error;
    Vdac2Control::CaptureStatus status;
    EXPECT_TRUE(Vdac2Control::HasCard(_context, &error)) << error;
    EXPECT_FALSE(Vdac2Control::StopCapture(_context, &error)) << "nothing runs yet";
    EXPECT_FALSE(Vdac2Control::StartCapture(_context, "", &error)) << "no path";

    ASSERT_TRUE(Vdac2Control::StartCapture(_context, path, &error)) << error;
    Write(kRamG, {1, 2, 3, 4});
    ASSERT_TRUE(Vdac2Control::GetCaptureStatus(_context, status, &error)) << error;
    EXPECT_TRUE(status.capturing);
    EXPECT_EQ(status.path, path);
    EXPECT_GT(status.exchanges, 0u);
    EXPECT_GT(status.bytesWritten, Vdac2Capture::kHeaderSize) << "counts what is still buffered";
    ASSERT_TRUE(Vdac2Control::StopCapture(_context, &error)) << error;
    ASSERT_TRUE(Vdac2Control::GetCaptureStatus(_context, status, &error));
    EXPECT_FALSE(status.capturing);
    EXPECT_EQ(std::filesystem::file_size(path), status.bytesWritten);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Without the card: the reason names the configuration
    _context->config.ts_vdac = 0;
    _decoder->reset();
    EXPECT_FALSE(Vdac2Control::StartCapture(_context, path, &error));
    EXPECT_NE(error.find("TS_VDAC2"), std::string::npos) << error;
    EXPECT_FALSE(Vdac2Control::GetCaptureStatus(_context, status, &error));
}

/// DMA RAM -> SPI (the SDK's ft_load_cfifo_dma path) delivers the bytes to
/// the selected FT812
TEST_F(Vdac2Card_Test, DmaRamToSpiReachesTheChip)
{
    Boot();
    constexpr uint32_t kSource = 0x40000;  // DRAM byte address
    constexpr uint32_t kWords = 8;
    std::vector<uint8_t> expected;
    for (uint32_t i = 0; i < kWords * 2; i++)
    {
        const uint8_t value = static_cast<uint8_t>(0x30 + i * 7);
        Ram(static_cast<uint16_t>(kSource / PAGE_SIZE), static_cast<uint16_t>(kSource % PAGE_SIZE + i)) = value;
        expected.push_back(value);
    }

    // Header by the CPU, payload by the DMA, CS held throughout
    Out(kPortConfig, kSelectFt812);
    Out(kPortData, 0x80 | 0x00);
    Out(kPortData, 0x20);
    Out(kPortData, 0x00);  // RAM_G 0x002000
    _decoder->WriteRegister(TsConfReg::DmaSAl, static_cast<uint8_t>(kSource));
    _decoder->WriteRegister(TsConfReg::DmaSAh, static_cast<uint8_t>((kSource >> 8) & 0x3F));
    _decoder->WriteRegister(TsConfReg::DmaSAx, static_cast<uint8_t>(kSource >> 14));
    _decoder->WriteRegister(TsConfReg::DmaLen, kWords - 1);
    _decoder->WriteRegister(TsConfReg::DmaNum, 0);
    _decoder->WriteRegister(TsConfReg::DmaCtrl, 0x8A);  // RAM -> SPI
    for (int lines = 0; _decoder->GetDma().Busy() && lines < 3 * 320; lines++)
    {
        Tick(TsConfEngine::kLineTacts);
        _decoder->CatchUpEngine();
    }
    ASSERT_FALSE(_decoder->GetDma().Busy());
    Out(kPortConfig, kDeselectAll);

    EXPECT_EQ(Read(0x002000, expected.size()), expected);
}

#endif // ENABLE_VDAC2
