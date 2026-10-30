// memory-at space cpu at a frame start (docs/inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md, B4):
// the view comes from the store through the windows the capture mapped, without a replay, and equals the machine
// after a seek there. Every model boots its ROM while recording, so the windows move as the ROM banks; the Next's
// 8K slots and the Sprinter's redirected windows answer through the replay and must be equal as well.

#include <gtest/gtest.h>

#include <cctype>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr unsigned kFrames = 8;

class TTDCpuViewStore_Test : public ::testing::TestWithParam<const char*>
{
protected:
    Emulator* _emulator = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_P(TTDCpuViewStore_Test, FrameStartViewEqualsTheMachineAfterASeek)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    EmulatorContext* context = _emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    _emulator->EnableTurboMode();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
    ttd->StopRecording();

    Memory* memory = context->pMemory;
    const uint64_t first = ttd->GetSessionInfo().sessionStartFrame;
    const uint64_t last = ttd->SessionEndPosition().frame;
    unsigned fromStore = 0;
    for (uint64_t f = first; f <= last; ++f)
    {
        SCOPED_TRACE(f);
        const uint64_t restoreNs = ttd->GetPerfCounters().lastRestoreTotalNs();
        const ttd::TTDCpuView view = ttd->ReadCpuView({f, 0}, 0, 0x10000);
        ASSERT_TRUE(view.ok) << view.error;
        fromStore += ttd->GetPerfCounters().lastRestoreTotalNs() == restoreNs ? 1 : 0;

        ASSERT_TRUE(ttd->SeekTo({f, 0}));
        size_t differ = 0;
        for (size_t i = 0; i < view.bytes.size(); ++i)
            differ += view.bytes[i] != memory->DirectReadFromZ80Memory(static_cast<uint16_t>(i));
        ASSERT_EQ(differ, 0u);
        for (uint8_t w = 0; w < 4; ++w)
        {
            const MemoryPageDescriptor page = memory->MapZ80AddressToPhysicalPage(static_cast<uint16_t>(w * 0x4000));
            EXPECT_EQ(view.windows[w].kind, static_cast<uint8_t>(page.mode)) << int(w);
            EXPECT_EQ(view.windows[w].page, page.page) << int(w);
            EXPECT_EQ(view.windows[w].name, memory->GetCurrentBankName(w)) << int(w);
        }
    }
    // The models whose windows are what the CPU reads answer every frame start from the store
    const std::string model = GetParam();
    if (model != "NEXT" && model != "SPRINTER")
        EXPECT_EQ(fromStore, unsigned(last - first + 1));
    RecordProperty("fromStore", static_cast<int>(fromStore));
}

INSTANTIATE_TEST_SUITE_P(AcrossModels, TTDCpuViewStore_Test,
                         ::testing::Values("48K", "PENTAGON", "SCORPION", "PROFI", "ATM3", "TSL", "PLUS3", "SPRINTER",
                                           "NEXT"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
}  // namespace
