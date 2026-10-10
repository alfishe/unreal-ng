/// @file timetravelhooks_test.cpp
/// @brief Phase 5, Step 1 (layer 2): the core reaches time travel through
/// EmulatorContext::pTimeTravelHooks. Until the engine takes over, the hooks are
/// v1's manager, and the typed load / configuration / model-transfer hooks keep
/// v1's behavior: they drop a stopped session's history and keep the reason

#include <gtest/gtest.h>

#include <string>

#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

class TimeTravelHooks_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    ttd::ITimeTravelHooks* _hooks = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _ttd = _emulator->GetContext()->pTimeTravelController;
        _hooks = _emulator->GetContext()->pTimeTravelHooks;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_hooks, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    // A stopped session with history, as a user has after pressing Stop
    void RecordAndStop()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_TRUE(_hooks->HasHistory());
    }
};

TEST_F(TimeTravelHooks_Test, TheCoreSeesTheManagerThroughTheHooks)
{
    EXPECT_EQ(_hooks, static_cast<ttd::ITimeTravelHooks*>(_ttd));
    EXPECT_FALSE(_hooks->HasHistory());
    EXPECT_EQ(_hooks->GetState(), ttd::TTDSessionState::Idle);

    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_TRUE(_hooks->IsRecording());
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_hooks->HasHistory());
}

// A snapshot load is not one of them: it ends a recording like a reset and the history of the machine stays (the one rule)
TEST_F(TimeTravelHooks_Test, ASnapshotLoadKeepsTheHistoryOfAStoppedSession)
{
    RecordAndStop();
    _hooks->OnLoad(ttd::TTDLoadKind::Snapshot, "snapshot-load");
    EXPECT_TRUE(_hooks->HasHistory());
    EXPECT_TRUE(_ttd->GetSessionInfo().lastDropReason.empty());
}

TEST_F(TimeTravelHooks_Test, LoadsConfigurationChangesAndTransfersDropAStoppedSessionWithTheirReason)
{
    struct Case
    {
        const char* reason;
        void (*call)(ttd::ITimeTravelHooks&, const char*);
    };
    const Case cases[] = {
        {"tape-load", [](ttd::ITimeTravelHooks& h, const char* r) { h.OnLoad(ttd::TTDLoadKind::Tape, r); }},
        {"media-change", [](ttd::ITimeTravelHooks& h, const char* r) { h.OnLoad(ttd::TTDLoadKind::Media, r); }},
        {"rom-reload",
         [](ttd::ITimeTravelHooks& h, const char* r) { h.OnConfigurationChange(ttd::TTDConfigChangeKind::RomReload, r); }},
        {"state-transfer", [](ttd::ITimeTravelHooks& h, const char* r) { h.OnModelTransfer(r); }},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.reason);
        RecordAndStop();
        c.call(*_hooks, c.reason);
        EXPECT_FALSE(_hooks->HasHistory());
        EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, c.reason);
    }
}
