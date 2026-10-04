/// @file timetravelmanager_turboclock_test.cpp
/// @brief B4: TTD time under a hardware turbo.
///
/// z80.t counts T-states at the clock running now and is rescaled when a
/// hardware turbo switches mid-frame, so after a switch down it repeats values
/// the same frame already used. TTD positions count T-states at the model's top
/// clock instead: every instant has one value and time only grows. On a model
/// without a hardware turbo the two are the same number.

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdwritejournal.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

TEST(TimeTravelManager_TurboClock_Test, UnitsFollowTheModelsTopClock)
{
    EmulatorState state{};
    state.ttd_clock_units = 2;  // 3.5 / 7 MHz model

    state.hw_turbo_ratio_applied = 1;
    EXPECT_EQ(state.TtdTInFrame(1000), 2000u);  // 1000 T at 3.5 MHz = 2000 T at 7 MHz
    state.hw_turbo_ratio_applied = 2;
    EXPECT_EQ(state.TtdTInFrame(2000), 2000u);  // the same instant after the 2x rescale

    state.ttd_clock_units = 0;  // not set yet: a model without turbo
    state.hw_turbo_ratio_applied = 1;
    EXPECT_EQ(state.TtdTInFrame(1234), 1234u);
}

TEST(TimeTravelManager_TurboClock_Test, ModelsWithoutTurboKeepTStates)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    EXPECT_EQ(context->emulatorState.ttd_clock_units, 1u);
    EXPECT_EQ(context->pTimeTravelManager->FrameSpan(), context->config.frame);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

class TimeTravelManager_TurboSwitch_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;

    static constexpr uint16_t kTarget = 0x9000;

    void SetUp() override
    {
        // Scorpion ZS-256 Turbo+: IN from the #7FFD family switches to 7 MHz
        // and IN from the #1FFD family back to 3.5 MHz, both mid-frame
        _emulator = EmulatorTestHelper::CreateStandardEmulator("SCORPION", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        _memory = _context->pMemory;
        ASSERT_EQ(_context->emulatorState.ttd_clock_units, 2u);

        FeatureManager* fm = _emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();

        // Write #11 at 7 MHz late enough in the frame, drop to 3.5 MHz (z80.t
        // halves), write #22: the second write has the smaller z80.t
        const uint8_t program[] = {
            0xF3,              // DI
            0x01, 0xFD, 0x7F,  // LD BC,#7FFD
            0xED, 0x78,        // IN A,(C)      - 7 MHz
            0x06, 0x00,        // LD B,0
            0x10, 0xFE,        // DJNZ $        - ~3300 T-states
            0x21, 0x00, 0x90,  // LD HL,#9000
            0x36, 0x11,        // LD (HL),#11
            0x01, 0xFD, 0x1F,  // LD BC,#1FFD
            0xED, 0x78,        // IN A,(C)      - 3.5 MHz
            0x36, 0x22,        // LD (HL),#22
            0x18, 0xFE,        // JR $
        };
        for (size_t i = 0; i < sizeof(program); ++i)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _memory->DirectWriteToZ80Memory(kTarget, 0x00);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0xBFF0;

        _ttd->SetEnableWriteJournal(true);   // the tests read the journal's times
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The journal's writes to kTarget, oldest first
    std::vector<ttd::TTDWriteRecord> TargetWrites() const
    {
        std::vector<ttd::TTDWriteRecord> writes;
        const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
        for (uint64_t seq = journal->SeqTail(); seq < journal->SeqHead(); ++seq)
        {
            const ttd::TTDWriteRecord& rec = journal->RecordAt(seq);
            if (!rec.isIo && rec.addr == kTarget)
                writes.push_back(rec);
        }
        return writes;
    }

    ttd::TTDTimePoint PositionOf(uint64_t globalT) const
    {
        const uint32_t span = _ttd->FrameSpan();
        return {globalT / span, static_cast<uint32_t>(globalT % span)};
    }

    std::optional<ttd::TTDSearchResult> FindLastTargetWrite()
    {
        ttd::TTDSearchQuery q;
        q.access = ttd::TTDAccessType::Write;
        q.addrFrom = kTarget;
        q.addrTo = kTarget;
        return _ttd->FindLastAccess(q);
    }
};

TEST_F(TimeTravelManager_TurboSwitch_Test, JournalTimeOnlyGrowsThroughASwitchDown)
{
    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr);
    ASSERT_GT(journal->Size(), 0u);
    for (uint64_t seq = journal->SeqTail() + 1; seq < journal->SeqHead(); ++seq)
        ASSERT_GE(uint64_t(journal->RecordAt(seq).globalT), uint64_t(journal->RecordAt(seq - 1).globalT))
            << "record " << seq << " goes back in time";

    const auto writes = TargetWrites();
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].value, 0x11);
    EXPECT_EQ(writes[1].value, 0x22);
    EXPECT_GT(uint64_t(writes[1].globalT), uint64_t(writes[0].globalT));
}

TEST_F(TimeTravelManager_TurboSwitch_Test, SeekLandsOnEachWriteNotPastIt)
{
    const auto writes = TargetWrites();
    ASSERT_EQ(writes.size(), 2u);

    // With z80.t as the position, the first write's time comes around again
    // after the switch down and the seek ran on past the second write
    ASSERT_TRUE(_ttd->SeekTo(PositionOf(writes[0].globalT)));
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(kTarget), 0x11);

    ASSERT_TRUE(_ttd->SeekTo(PositionOf(writes[1].globalT)));
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(kTarget), 0x22);
}

TEST_F(TimeTravelManager_TurboSwitch_Test, FindLastAnswersTheSameLiveAndReloaded)
{
    const auto live = FindLastTargetWrite();
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(live->value, 0x22);
    EXPECT_EQ(_ttd->GlobalT(live->time), uint64_t(TargetWrites()[1].globalT));

    std::stringstream file;
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(file, err)) << err;
    ASSERT_TRUE(_ttd->DeserializeSession(file, err)) << err;

    const auto reloaded = FindLastTargetWrite();
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->value, live->value);
    EXPECT_EQ(reloaded->time.frame, live->time.frame);
    EXPECT_EQ(reloaded->time.tInFrame, live->time.tInFrame);
}
