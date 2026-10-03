// TS-Conf VDAC2 card: the FT812 on the Z-Controller SPI bus, its time base
// (vdac2-integration-design.md §4-§5, phase I1; tests listed in §12).
//
// The chip is the eve-emu library; these tests drive it the way VDAC2
// software does, through #77 (chip select) and #57 (data), and check what the
// integration owns: the build gate, the bus, and raster time -> FT812 clocks.

#ifdef ENABLE_VDAC2

#include "vdac2cardfixture.h"

#include <vector>

#include "emulator/platforms/tsconf/vdac2card.h"
#include "emulator/video/screen.h"

#include <eve/eve.h>

#include <cstring>
#include <filesystem>

using namespace Vdac2Test;

class Vdac2Card_Test : public Vdac2Test::Vdac2CardFixture
{
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

/// The TTD memory blob drops zero runs of 64+ bytes (vdac2-integration-design.md §9.1):
/// every byte of every region comes back exactly, wherever the zero runs are -
/// at a region's start or end, across a region boundary, one byte short of the
/// threshold, a single non-zero byte at the very end - and an empty chip costs
/// a few tokens. The blob never outgrows the worst case it declares
TEST_F(Vdac2Card_Test, TtdMemoryBlobRestoresEveryByteWhereverTheZerosAre)
{
    Vdac2Card* card = Card();
    ASSERT_NE(card, nullptr);
    EveChip* chip = card->Chip();
    ASSERT_NE(chip, nullptr);
    const size_t regions = EveRegionCount(chip);
    ASSERT_GE(regions, 2u);

    auto region = [&](size_t i) {
        EveRegion r{};
        EveGetRegion(chip, i, &r);
        return r;
    };
    auto fill = [&](uint8_t value) {
        for (size_t i = 0; i < regions; i++)
            std::memset(region(i).base, value, region(i).size);
    };
    auto snapshot = [&]() {
        std::vector<std::vector<uint8_t>> all;
        for (size_t i = 0; i < regions; i++)
            all.emplace_back(region(i).base, region(i).base + region(i).size);
        return all;
    };
    uint32_t encodedBytes = 0;
    auto roundTrip = [&](const char* name) {
        SCOPED_TRACE(name);
        const auto before = snapshot();
        std::vector<uint8_t> blob(card->TtdMemorySize(), 0xEE);
        const size_t written = card->TtdSaveMemory(blob.data());
        std::memcpy(&encodedBytes, blob.data() + 4, 4);
        EXPECT_EQ(written, encodedBytes + 8u) << "header + tokens";
        EXPECT_LE(written, blob.size()) << "never more than the worst case";
        blob.resize(written);  // what a checkpoint stores
        fill(0xA5);  // whatever the live chip holds before the restore
        ASSERT_TRUE(card->TtdLoadMemory(blob.data()));
        const auto after = snapshot();
        for (size_t i = 0; i < regions; i++)
            ASSERT_TRUE(after[i] == before[i]) << "region " << region(i).name;
    };

    fill(0x00);
    roundTrip("all zero");
    EXPECT_LE(encodedBytes, 4u * regions) << "an empty chip: one zero-run token per region";

    fill(0xFF);
    roundTrip("no zero at all (worst case)");

    // Zero runs everywhere that matters
    fill(0x5A);
    const EveRegion first = region(0);
    std::memset(first.base, 0, 1000);                            // at the start
    std::memset(first.base + 5000, 0, 63);                       // one short of the threshold: stays data
    std::memset(first.base + 6000, 0, 64);                       // exactly the threshold
    std::memset(first.base + 7000, 0, 65);                       // one over
    std::memset(first.base + first.size - 4096, 0, 4096);        // at the end ...
    std::memset(region(1).base, 0, 2048);                        // ... continuing into the next region
    region(1).base[region(1).size - 1] = 0x01;                   // a single non-zero byte at a region's very end
    std::memset(region(1).base, 0, region(1).size - 1);
    for (size_t i = 0; i < first.size; i += 4093)                // isolated non-zero bytes inside long zero stretches
        first.base[i] = static_cast<uint8_t>(i | 1);
    roundTrip("mixed runs and boundaries");

    // Pseudo-random data with zero stretches of random lengths
    uint32_t seed = 0x1234567u;
    auto next = [&]() { return seed = seed * 1664525u + 1013904223u; };
    for (size_t i = 0; i < regions; i++)
    {
        EveRegion r = region(i);
        size_t pos = 0;
        while (pos < r.size)
        {
            const size_t length = std::min<size_t>(r.size - pos, 1 + next() % 300);
            const bool zero = (next() & 3) == 0;
            for (size_t k = 0; k < length; k++)
                r.base[pos + k] = zero ? 0 : static_cast<uint8_t>(next() >> 24);
            pos += length;
        }
    }
    roundTrip("random data and random zero stretches");

    // A damaged blob is refused, not half applied silently
    std::vector<uint8_t> blob(card->TtdMemorySize());
    card->TtdSaveMemory(blob.data());
    blob[0] ^= 0xFF;
    EXPECT_FALSE(card->TtdLoadMemory(blob.data())) << "wrong magic";
}

#endif // ENABLE_VDAC2
