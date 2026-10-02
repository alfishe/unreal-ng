// The PLD configuration loader: the bitstream sink, the end of load, the
// watchdog, the reload code #2E and the fast start (Sprinter test-plan §2.3
// T-CFG; tdd-ports-memory §6).

#include "sprinterfixture.h"

#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterpldstandard.h"

class SprinterPldConfig_Test : public SprinterFixture
{
protected:
    void Feed(uint32_t writes, uint8_t value = 0xFF)
    {
        for (uint32_t i = 0; i < writes; i++)
            _decoder->OnConfigurationWrite(value);
    }
};

// Worked example of the header: the sink sees the rotated byte 8 times
TEST_F(SprinterPldConfig_Test, Sink_HashesAndCount)
{
    SprinterPldState pld{};
    SprinterPldConfig::Begin(pld);
    EXPECT_EQ(pld.configState, SprinterConfigState::Loading);
    EXPECT_FALSE(SprinterPldConfig::OnWrite(pld, 0xA4));
    EXPECT_FALSE(SprinterPldConfig::OnWrite(pld, 0xCF));
    EXPECT_FALSE(SprinterPldConfig::OnWrite(pld, 0x61));
    EXPECT_FALSE(SprinterPldConfig::OnWrite(pld, 0x38));
    EXPECT_EQ(pld.bitstreamHashHead, 0x3861CFA4u) << "MAME: data << 8 x (count mod 4)";
    EXPECT_EQ(pld.bitstreamCount, 4u);
    for (uint32_t i = 4; i < SprinterPldConfig::kHeadWrites; i++)
        SprinterPldConfig::OnWrite(pld, 0);
    SprinterPldConfig::OnWrite(pld, 0x55);
    EXPECT_EQ(pld.bitstreamHashHead, 0x3861CFA4u) << "only the first 4 096 writes";
}

// T-CFG-1: the load ends at write 473 720, not at MAME's 4 096; then the CPU
// starts the BIOS with window 3 = page #40 and the decoder closed
TEST_F(SprinterPldConfig_Test, Load_EndsAtTheFullBitstreamCount)
{
    _decoder->BeginLoading();
    ASSERT_EQ(Pld().configState, SprinterConfigState::Loading);
    _z80->pc = 0x0088;

    Feed(SprinterPldConfig::kHeadWrites);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Loading) << "not configured after 4 096 writes";
    EXPECT_EQ(Pld().resetPending, 0);

    Feed(SprinterPldConfig::kPldConfigurationWrites - SprinterPldConfig::kHeadWrites - 1);
    EXPECT_EQ(Pld().resetPending, 0);
    Feed(1);
    EXPECT_NE(Pld().resetPending, 0) << "the last write ends the load";
    EXPECT_EQ(Pld().configState, SprinterConfigState::Loading) << "the reset comes at the instruction boundary";

    Feed(100);  // the loop keeps writing: ignored
    EXPECT_EQ(Pld().bitstreamCount, SprinterPldConfig::kPldConfigurationWrites);

    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(_z80->pc, 0x0000);
    EXPECT_EQ(Pld().starting, 1);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 8) << "BIOS page 8";
    EXPECT_EQ(_memory->GetRAMPageForBank3(), SprinterMemory::kPortTablePage);
    EXPECT_EQ(_z80->GetMachineStepHook(), nullptr) << "the step hook goes when nothing waits";
}

// T-CFG-2 (one full 473 720-write load, ~15 ms): a stream whose head matches MAME's Game constant, no Game module: Standard
TEST_F(SprinterPldConfig_Test, Load_UnknownGameStreamRunsStandard)
{
    _decoder->BeginLoading();
    for (uint8_t b : {0xA4, 0xCF, 0x61, 0x38})
        _decoder->OnConfigurationWrite(b);
    Feed(SprinterPldConfig::kPldConfigurationWrites - 4, 0x00);
    EXPECT_EQ(Pld().bitstreamHashHead, 0x3861CFA4u);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(Pld().configModule, SprinterPldConfigurationRegistry::kStandardIndex);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
}

// T-CFG-4: code #2E reloads: back to loading, fast RAM and RAM kept, the module chosen again
TEST_F(SprinterPldConfig_Test, Reload_Code2E)
{
    OpenDcp();
    SetCode(0x40BC, false, 0x2E);
    _sprinterMemory->FastRam()[0x1234] = 0x6B;
    Ram(0x20, 0x10) = 0x7C;

    Out(0x40BC, 0x00);
    ASSERT_NE(Pld().resetPending, 0);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Loading);
    EXPECT_EQ(Pld().bitstreamCount, 0u);
    EXPECT_EQ(_z80->pc, 0x0000);
    EXPECT_EQ(Tag(0x0000), kRomTagBase + 0x0C) << "the loader runs again";
    EXPECT_EQ(_sprinterMemory->FastRam()[0x1234], 0x6B);
    EXPECT_EQ(Ram(0x20, 0x10), 0x7C);
    EXPECT_EQ(_z80->GetMachineStepHook(), _decoder) << "the watchdog runs while loading";
}

// T-CFG-5: a load that stops before the count: the watchdog takes Standard and resets the CPU
TEST_F(SprinterPldConfig_Test, Watchdog_EndsAStalledLoad)
{
    _decoder->BeginLoading();
    Feed(1000);
    for (uint32_t frame = 0; frame + 1 < SprinterPldConfig::kWatchdogFrames; frame++)
        _decoder->OnMachineFrameRollover(71680);
    EXPECT_EQ(Pld().resetPending, 0);
    _decoder->OnMachineFrameRollover(71680);
    ASSERT_NE(Pld().resetPending, 0);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(Pld().configModule, SprinterPldConfigurationRegistry::kStandardIndex);
}

// T-CFG-6: both hashes over the BIOS 3.04 bitstream equal the Standard module's constants
TEST_F(SprinterPldConfig_Test, Hashes_Bios304Bitstream)
{
    const std::vector<uint8_t> rom = Rom304();
    if (rom.empty())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    SprinterPldState pld{};
    SprinterPldConfig::Begin(pld);
    const uint8_t* stream = rom.data() + 0x0C * PAGE_SIZE + 0x0100;
    bool ended = false;
    for (uint32_t i = 0; i < SprinterPldConfig::kPldConfigurationWrites / 8; i++)
    {
        uint8_t value = stream[i];
        for (int bit = 0; bit < 8; bit++)
        {
            ended = SprinterPldConfig::OnWrite(pld, value);
            value = static_cast<uint8_t>((value >> 1) | (value << 7));
        }
    }
    EXPECT_TRUE(ended);
    EXPECT_EQ(pld.bitstreamHashFull, SprinterPldStandard::kFullHash304);
    EXPECT_EQ(pld.bitstreamHashHead, SprinterPldStandard::kHeadHash304);
    EXPECT_EQ(stream[0x0E84E - 0x0100 + 1], 0xFF) << "the #FF filler follows the 59 215 bytes";
}

// Fast start = what the loader leaves: IX / IY, the Z84C15 chip selects, the fast RAM #FExx
TEST_F(SprinterPldConfig_Test, FastStart_LeavesTheLoaderHandOver)
{
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(_z80->iy, 0x0107);
    EXPECT_EQ(_z80->ix, 0xFFFD);
    EXPECT_EQ(_decoder->GetZ84().system.csbr, 0xFE);
    EXPECT_EQ(_decoder->GetZ84().system.mcr, 0x03);
    EXPECT_EQ(Pld().bitstreamCount, SprinterPldConfig::kPldConfigurationWrites);
    // The stream's last 256 bytes lie in the synthetic ROM page #F (all #EF): the last write
    // of each byte is RRCA^7(#EF) = #DF
    EXPECT_EQ(_sprinterMemory->FastRam()[0xFE00], 0xDF);
}

// T-CFG-3 (ROM): the full start through the real loader and the fast start leave the same machine.
// Slow on purpose (~2.3 M loader instructions at 3.5 MHz): the only test that runs the loader
TEST_F(SprinterPldConfig_Test, FastStartEqualsFullStart_Bios304)
{
    if (Rom304().empty())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    ASSERT_TRUE(RebuildWithRealRom(false));
    ASSERT_EQ(Pld().configState, SprinterConfigState::Loading);
    uint64_t steps = 0;
    while (Pld().configState != SprinterConfigState::Configured && steps < 4'000'000)
    {
        _z80->Z80Step();
        if (Pld().resetPending)
            _decoder->OnMachineStep(_z80->t);
        steps++;
    }
    ASSERT_EQ(Pld().configState, SprinterConfigState::Configured) << "the loader did not finish";
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
    EXPECT_EQ(Pld().bitstreamHashFull, SprinterPldStandard::kFullHash304);

    const SprinterPldState full = Pld();
    std::vector<uint8_t> fullFast(_sprinterMemory->FastRam(), _sprinterMemory->FastRam() + 0x10000);
    const uint16_t fullIx = _z80->ix;
    const uint16_t fullIy = _z80->iy;
    const uint8_t fullCsbr = _decoder->GetZ84().system.csbr;
    const uint16_t fullPc = _z80->pc;
    const uint64_t fullRam = [&] {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < 256u * PAGE_SIZE; i++)
            h = (h ^ _memory->RAMBase()[i]) * 0x100000001b3ULL;
        return h;
    }();

    ASSERT_TRUE(RebuildWithRealRom(true));
    EXPECT_EQ(std::memcmp(&full, &Pld(), sizeof(SprinterPldState)), 0) << "PLD state";
    EXPECT_EQ(std::memcmp(fullFast.data(), _sprinterMemory->FastRam(), fullFast.size()), 0) << "fast RAM";
    EXPECT_EQ(_z80->ix, fullIx);
    EXPECT_EQ(_z80->iy, fullIy);
    EXPECT_EQ(_z80->pc, fullPc);
    EXPECT_EQ(_decoder->GetZ84().system.csbr, fullCsbr);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < 256u * PAGE_SIZE; i++)
        h = (h ^ _memory->RAMBase()[i]) * 0x100000001b3ULL;
    EXPECT_EQ(h, fullRam) << "main RAM";
}
