// The `cpu` selector on the debugger reads that every surface shares (debugger model TODO, Phase 3 stage 3): the CPU
// list, the snapshot, page reads, the stack, the byte search, and the refusals of what only the main CPU has, with the
// error codes of debugerror.h.
//
// A Pentagon with the classic General Sound (gs105a): ROM at #0000, RAM at #4000 (cpuview_test.cpp).

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "debugger/debugerror.h"
#include "debugger/debugmanager.h"
#include "debugger/search/memorysearch.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "debugger/targets/cpuview.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace
{
class CpuParam_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _sound = std::make_unique<SoundCardScope>(TestSound::GeneralSound);
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::Z80));
        _emulator->RunNFrames(2);
        _card = _context->pSoundManager->getGeneralSound();
        ASSERT_NE(_card, nullptr);
        ASSERT_TRUE(_card->cardRegisters(_regs));
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::vector<uint8_t> CardBytes(uint16_t address, uint32_t length)
    {
        std::vector<uint8_t> bytes;
        EXPECT_TRUE(CpuView::Read(_context, DebugCpuId::GS, address, length, bytes));
        return bytes;
    }

    std::unique_ptr<SoundCardScope> _sound;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    GeneralSoundCard* _card = nullptr;
    Z80CpuRegisters _regs{};
};
}  // namespace

TEST_F(CpuParam_Test, TheCpuListNamesWhatIsThere)
{
    StateNode list = CpuView::Targets(_context);
    EXPECT_EQ(list["selected"].s, "main");
    ASSERT_EQ(list["targets"].size(), 3u);
    const struct
    {
        const char* cpu;
        bool available;
        const char* card;
    } expected[] = {{"main", true, nullptr}, {"gs", true, "gs"}, {"ms", false, nullptr}};
    for (size_t i = 0; i < std::size(expected); i++)
    {
        StateNode& target = list["targets"].items[i];
        EXPECT_EQ(target["cpu"].s, expected[i].cpu);
        EXPECT_EQ(target["available"].b, expected[i].available) << expected[i].cpu;
        if (expected[i].card)
            EXPECT_EQ(target["card"].s, expected[i].card);
        else
            EXPECT_EQ(target.find("card"), nullptr) << expected[i].cpu;
    }

    // The selector's codes: a name that is no CPU, a card CPU that is not there
    EXPECT_STREQ(CpuView::SelectCode("z81"), DebugError::kInvalidArgument);
    EXPECT_STREQ(CpuView::SelectCode("ms"), DebugError::kCpuUnavailable);
}

TEST_F(CpuParam_Test, TheSnapshotIsTheCardCpus)
{
    ASSERT_EQ(CpuView::Write(_emulator, DebugCpuId::GS, "test", 0x4000, {0xC3, 0x5A}), "");
    DebugSnapshot::Options options;
    options.cpu = DebugCpuId::GS;
    options.disasm = 2;
    options.stack = 2;
    options.memory = {"cpu:0x4000:2"};
    options.rawBytes = true;
    DebugSnapshot::Result result = DebugSnapshot::Build(_emulator, options);
    ASSERT_EQ(result.error, "");
    StateNode& s = result.snapshot;
    EXPECT_EQ(s["cpu"].s, "z80") << "kept";
    EXPECT_EQ(s["debug_cpu"].s, "gs");
    EXPECT_EQ(s["regs"]["special"]["pc"].i, _regs.pc);
    EXPECT_EQ(s["regs"]["cpu"].s, "gs");
    EXPECT_EQ(s["pages"].size(), 4u);
    EXPECT_EQ(s["pages"].items[0]["kind"].s, "rom");
    EXPECT_EQ(s["pages"].items[1]["kind"].s, "ram");
    EXPECT_EQ(s["stack"]["sp"].i, _regs.sp);
    const std::vector<uint8_t> top = CardBytes(_regs.sp, 2);
    EXPECT_EQ(s["stack"]["words"].items[0].i, top[0] | (top[1] << 8));
    EXPECT_EQ(s["disasm"].items[0]["address"].i, _regs.pc);
    EXPECT_EQ(s["time"]["card_time"].i, _card->cardTime());
    EXPECT_EQ(s["memory"].items[0]["space"].s, "gs");
    EXPECT_EQ(s["memory"].items[0]["bytes"].s, std::string("\xC3\x5A"));

    DebugSnapshot::Options main;
    DebugSnapshot::Result mainResult = DebugSnapshot::Build(_emulator, main);
    ASSERT_EQ(mainResult.error, "");
    EXPECT_EQ(mainResult.snapshot["debug_cpu"].s, "main");
    EXPECT_EQ(mainResult.snapshot["cpu"].s, "z80");
}

TEST_F(CpuParam_Test, PagesStackAndSearchReadTheCardCpu)
{
    ASSERT_EQ(CpuView::Write(_emulator, DebugCpuId::GS, "test", 0x4000, {0xDE, 0xAD, 0xBE, 0xEF}), "");

    // The page behind #4000, read by its own number: the same bytes
    StateNode windows = CpuView::Windows(_context, DebugCpuId::GS);
    StateNode& at4000 = windows["windows"].items[1];
    std::vector<uint8_t> page;
    EXPECT_EQ(CpuView::PageRead(_context, DebugCpuId::GS, at4000["kind"].s, static_cast<uint32_t>(at4000["page"].i), 0,
                                4, page),
              "");
    EXPECT_EQ(page, CardBytes(0x4000, 4));
    std::vector<uint8_t> rom;
    EXPECT_EQ(CpuView::PageRead(_context, DebugCpuId::GS, "rom", 0, 0, 3, rom), "");
    EXPECT_EQ(rom, CardBytes(0x0000, 3));

    StateNode stack = CpuView::Stack(_context, DebugCpuId::GS, 1);
    EXPECT_EQ(stack["cpu"].s, "gs");
    EXPECT_EQ(stack["sp"].i, _regs.sp);
    const std::vector<uint8_t> top = CardBytes(_regs.sp, 2);
    EXPECT_EQ(stack["words"].items[0].i, top[0] | (top[1] << 8));

    MemorySearchRequest request;
    std::string error;
    ASSERT_TRUE(MemorySearch::BuildRequest("DE AD BE EF", nullptr, "", "", 0, 0xFFFF, 4, 1, request, error)) << error;
    request.cpu = DebugCpuId::GS;
    const MemorySearchResult found = MemorySearch::Search(_context, request);
    ASSERT_EQ(found.error, "");
    ASSERT_FALSE(found.matches.empty());
    EXPECT_EQ(found.matches[0].address, 0x4000u);
    EXPECT_EQ(found.matches[0].contextStart, 0x3FFCu);
    EXPECT_EQ(found.matches[0].bytes, CardBytes(0x3FFC, 12)) << "4 before, the match, 4 after: the card's bytes";
}

// What a card CPU cannot do is refused with the reason and a code, never done on the main CPU
TEST_F(CpuParam_Test, MainOnlyCapabilitiesRefuseACardCpu)
{
    EXPECT_EQ(CpuView::MainOnly(DebugCpuId::Main, "the PC history"), "");
    EXPECT_EQ(CpuView::MainOnly(DebugCpuId::GS, "the PC history"), "cpu gs: the PC history is the main CPU's only");

    DebugSnapshot::Options pchist;
    pchist.cpu = DebugCpuId::GS;
    pchist.pchist = 4;
    const DebugSnapshot::Result refused = DebugSnapshot::Build(_emulator, pchist);
    EXPECT_NE(refused.error, "");
    EXPECT_STREQ(refused.code, DebugError::kNotSupported);

    DebugSnapshot::Options absent;
    absent.cpu = DebugCpuId::MultiSoundGS;
    const DebugSnapshot::Result missing = DebugSnapshot::Build(_emulator, absent);
    EXPECT_NE(missing.error, "");
    EXPECT_STREQ(missing.code, DebugError::kCpuUnavailable);

    MemorySearchRequest request;
    std::string error;
    ASSERT_TRUE(MemorySearch::BuildRequest("00", nullptr, "", "ram", 0, 0xFFFFFFFFu, 1, 1, request, error));
    request.cpu = DebugCpuId::GS;
    EXPECT_NE(MemorySearch::Search(_context, request).error, "") << "a card CPU searches its own view only";

    std::vector<uint8_t> out;
    EXPECT_NE(CpuView::PageRead(_context, DebugCpuId::GS, "cache", 0, 0, 1, out), "");
    EXPECT_NE(CpuView::PageRead(_context, DebugCpuId::GS, "ram", 100000, 0, 1, out), "");
    EXPECT_NE(CpuView::PageRead(_context, DebugCpuId::MultiSoundGS, "ram", 0, 0, 1, out), "");
}
