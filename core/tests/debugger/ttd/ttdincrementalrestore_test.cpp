// Incremental checkpoint restores (docs/inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md, B1): inside a
// KnownMemoryScope a restore decodes only the pieces written since the previous restore and those whose version
// differs, and leaves memory exactly as a full restore does. Outside the scope every restore starts from nothing.
//
// A second case runs a Pentagon with its sound cards: their memory (NeoGS RAM, MoonSound wave memory) is engine
// regions, written by the cards' own engines during the replay, and must come back the same way.
//
// The program sweeps `inc (hl)` over page 0 (bank 3), about 1,400 bytes a frame, so neighbouring checkpoints differ
// in a few pieces and far ones in many.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr unsigned kFrames = 30;

class TTDIncrementalRestore_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);

        const std::vector<uint8_t> sweep = {
            0xF3,              // 8000 di
            0x21, 0x00, 0xC0,  // 8001 ld hl,#C000
            0x34,              // 8004 loop: inc (hl)
            0x23,              // 8005 inc hl
            0x7C,              // 8006 ld a,h
            0xF6, 0xC0,        // 8007 or #C0          stays in bank 3
            0x67,              // 8009 ld h,a
            0x22, 0x00, 0x81,  // 800A ld (#8100),hl
            0x18, 0xF5,        // 800D jr loop
        };
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < sweep.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), sweep[i]);
        _context->pCore->GetZ80()->pc = 0x8000;
        _context->pCore->GetZ80()->sp = 0xBF00;

        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_GE(_ttd->GetCheckpointCount(), size_t{kFrames});
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::vector<uint8_t> Ram() const
    {
        const uint8_t* ram = _context->pMemory->RAMPageAddress(0);
        return std::vector<uint8_t>(ram, ram + MAX_RAM_SIZE);
    }

    // Writes the CPU could have done between two restores of a verb: the dirty tracker marks them
    void Scribble(uint8_t value) const
    {
        _context->pMemory->DirectWriteToZ80Memory(0x9000, value);   // page 2
        _context->pMemory->DirectWriteToZ80Memory(0xC123, value);   // page 0, the swept one
    }
};

TEST_F(TTDIncrementalRestore_Test, RestoresInsideTheScopeEqualFullRestores)
{
    const size_t last = _ttd->GetCheckpointCount() - 1;
    const std::vector<size_t> order = {3, 4, 4, 20, 7, 0, last, 10, 11};

    std::vector<std::vector<uint8_t>> expected;
    uint64_t fullPieces = 0;
    for (size_t idx : order)
    {
        ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(idx));
        EXPECT_EQ(_ttd->LastMemoryRestoreStats().piecesSkipped, 0u) << "outside the scope, checkpoint " << idx;
        fullPieces = _ttd->LastMemoryRestoreStats().piecesDecoded;
        expected.push_back(Ram());
    }

    {
        const ttd::TimeTravelController::KnownMemoryScope known(*_ttd);
        for (size_t k = 0; k < order.size(); k++)
        {
            Scribble(static_cast<uint8_t>(0xA0 + k));
            ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(order[k]));
            const ttd::TTDRestoreStats& stats = _ttd->LastMemoryRestoreStats();
            if (k > 0)
            {
                EXPECT_GT(stats.piecesSkipped, 0u) << "checkpoint " << order[k];
                EXPECT_LT(stats.piecesDecoded, fullPieces) << "checkpoint " << order[k];
            }
            ASSERT_TRUE(Ram() == expected[k]) << "memory after the incremental restore of checkpoint " << order[k];
        }
    }

    // The scope is over: nothing is known about memory any more
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(order[0]));
    EXPECT_EQ(_ttd->LastMemoryRestoreStats().piecesSkipped, 0u);
    EXPECT_TRUE(Ram() == expected[0]);
}

TEST_F(TTDIncrementalRestore_Test, LiveStatePutBackForgetsTheKnownMemory)
{
    const ttd::TimeTravelController::KnownMemoryScope known(*_ttd);
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(5));
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(6));
    EXPECT_GT(_ttd->LastMemoryRestoreStats().piecesSkipped, 0u);

    // A seek composes the picture and puts the live state back: memory is no longer what a restore left
    ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{8, 0}));
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(6));
    EXPECT_EQ(_ttd->LastMemoryRestoreStats().piecesSkipped, 0u);
}

// Every engine region (machine RAM and the cards' memory) after an incremental restore equals a full restore's,
// while the cards run between the restores as in a verb's replay
TEST(TTDIncrementalRestoreCards_Test, CardMemoryEqualsFullRestores)
{
    SoundCardScope sound(TestSound::GeneralSound | TestSound::MoonSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    emulator->EnableTurboMode();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
    ttd->StopRecording();

    // Regions restored through their device (no live memory pointer) are left out
    std::vector<ttd::TTDRegionDesc> regions;
    size_t cardRegions = 0;
    for (const ttd::TTDRegionDesc& r : ttd->GetEngine().Regions())
    {
        if (!r.memory || r.bytes == 0)
            continue;
        regions.push_back(r);
        if (r.ownerType != ttd::TTDRegionDesc::kNoOwner)
            cardRegions++;
    }
    ASSERT_GE(cardRegions, 2u) << "the cards' memory is no engine region";
    auto memory = [&regions]() {
        std::vector<std::vector<uint8_t>> all;
        for (const ttd::TTDRegionDesc& r : regions)
            all.emplace_back(r.memory, r.memory + r.bytes);
        return all;
    };

    const std::vector<size_t> order = {2, 3, 15, 4, 25, 25, 1};
    std::vector<std::vector<std::vector<uint8_t>>> expected;
    for (size_t idx : order)
    {
        ASSERT_TRUE(ttd->RestoreCheckpointForTesting(idx));
        expected.push_back(memory());
    }
    {
        const ttd::TimeTravelController::KnownMemoryScope known(*ttd);
        for (size_t k = 0; k < order.size(); k++)
        {
            ASSERT_TRUE(ttd->RestoreCheckpointForTesting(order[k]));
            const std::vector<std::vector<uint8_t>> now = memory();
            for (size_t r = 0; r < regions.size(); r++)
                ASSERT_TRUE(now[r] == expected[k][r]) << "region " << r << " after checkpoint " << order[k];
            // The machine and its cards run on, writing their memory, as a verb's replay of the frame does
            ttd::TimeTravelController::ReplayModeScope replay(*ttd);
            emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
}  // namespace
