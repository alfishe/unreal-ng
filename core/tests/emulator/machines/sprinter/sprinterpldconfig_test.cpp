// The PLD configuration loader: the bitstream sink, the end of load, the
// watchdog, the reload code #2E and the fast start (Sprinter test-plan §2.3
// T-CFG; tdd-ports-memory §6).

#include "sprinterfixture.h"

#include <string>

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

// T-CFG-2 (two full 473 720-write loads, ~30 ms): a stream whose head matches MAME's Game constant but whose full hash
// is not the Game bitstream's runs Game by the MAME-compatible head hash; a stream nobody knows runs Standard
TEST_F(SprinterPldConfig_Test, Load_GameHeadStreamRunsGameUnknownRunsStandard)
{
    _decoder->BeginLoading();
    for (uint8_t b : {0xA4, 0xCF, 0x61, 0x38})
        _decoder->OnConfigurationWrite(b);
    Feed(SprinterPldConfig::kPldConfigurationWrites - 4, 0x00);
    EXPECT_EQ(Pld().bitstreamHashHead, 0x3861CFA4u);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(Pld().configModule, SprinterPldConfigurationRegistry::kGameIndex);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    std::string key, why;
    _decoder->ModuleSelection(key, why);
    EXPECT_EQ(key, "head_hash") << why;

    _decoder->BeginLoading();
    Feed(SprinterPldConfig::kPldConfigurationWrites, 0x5A);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(Pld().configModule, SprinterPldConfigurationRegistry::kStandardIndex);
    _decoder->ModuleSelection(key, why);
    EXPECT_EQ(key, "unknown_bitstream") << why;
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
        Step();
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

// The loader runs on the Z84C15's wait generator (research-cpu-z84c15.md section 5, CPU library
// design section 6): it sets WCR = #04 (one memory wait per cycle, M1s included), so one bitstream
// byte - 29 memory cycles, 113 T in MAME, which stores WCR without applying it - takes 142 T, and
// the 59 215-byte stream ends about 1.7 M T later than MAME's last write at 6 691 665 T.
// Slow on purpose (~2.3 M loader instructions at 3.5 MHz): it runs the whole loader
TEST_F(SprinterPldConfig_Test, FullStart_LoaderRunsWithTheChipWaits)
{
    if (Rom304().empty())
        GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

    ASSERT_TRUE(RebuildWithRealRom(false));
    ASSERT_NE(_z80->GetEngine(), nullptr);
    ASSERT_EQ(Pld().configState, SprinterConfigState::Loading);
    _z80->t = 0;

    const uint32_t kBytes = SprinterPldConfig::kPldConfigurationWrites / 8;
    uint32_t tByte1 = 0;
    uint32_t tByte1001 = 0;
    uint32_t tLast = 0;
    uint32_t lastCount = 0;
    for (uint64_t steps = 0; Pld().configState == SprinterConfigState::Loading && steps < 4'000'000; steps++)
    {
        Step();
        const uint32_t count = Pld().bitstreamCount;
        if (count != lastCount)
        {
            if (count == 8)
                tByte1 = _z80->t;
            else if (count == 8 * 1001)
                tByte1001 = _z80->t;
            else if (count == SprinterPldConfig::kPldConfigurationWrites)
                tLast = _z80->t;
            lastCount = count;
        }
        if (Pld().resetPending)
            _decoder->OnMachineStep(_z80->t);
    }
    ASSERT_EQ(Pld().configState, SprinterConfigState::Configured) << "the loader did not finish";
    EXPECT_EQ(tByte1001 - tByte1, 1000u * 142) << "142 T per bitstream byte with WCR = #04";
    EXPECT_EQ(_decoder->GetZ84().system.wcr, 0x04);

    // The last stream write: the 59 214 bytes after the first at 142 T, plus the loader's start.
    // 6 691 671 T without the chip's waits (S1, roadmap section 6.1: MAME 6 691 665 + 6)
    EXPECT_EQ(tLast - tByte1, (kBytes - 1) * 142u);
    std::cout << "[Sprinter loader] last stream write at T " << tLast << " (MAME, no WCR: 6 691 665)" << std::endl;
}
