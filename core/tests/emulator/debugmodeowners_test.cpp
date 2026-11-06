/// @file debugmodeowners_test.cpp
/// @brief Debug mode has several users: the debugger window (Emulator::DebugOn / DebugOff), the BASIC editor monitor
/// (it switches it on through its breakpoints and restores it), a memory or call trace session an automation client
/// started. Who switched it on for its own needs and no longer needs it must not switch it off under a session that
/// still captures: the counters would stop silently (found driving the memory counters over the WebAPI: unreal-qt's
/// window adopted the new instance with the debugger hidden and switched debug mode off).

#include <gtest/gtest.h>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/editormonitor.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace
{
class DebugModeOwners_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("debugmode-owners", "48K", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _tracker = &_context->pMemory->GetAccessTracker();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        MessageCenter::DisposeDefaultMessageCenter();
    }

    /// What POST /profiler/memory/start does
    void StartMemorySession()
    {
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kMemoryTracking, true);
        _tracker->UpdateFeatureCache();
        _tracker->StartMemorySession();
        ASSERT_TRUE(_tracker->IsMemoryCapturing()) << "test precondition";
    }

    bool DebugMode() const { return _context->pFeatureManager->isEnabled(Features::kDebugMode); }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    MemoryAccessTracker* _tracker = nullptr;
};
}  // namespace

TEST_F(DebugModeOwners_Test, DebugOffLeavesDebugModeToACapturingSession)
{
    StartMemorySession();
    _emulator->DebugOff();
    EXPECT_TRUE(DebugMode()) << "a capturing memory session keeps the debug memory interface";
    EXPECT_TRUE(_tracker->IsMemoryCapturing());

    _tracker->StopMemorySession();
    _emulator->DebugOff();
    EXPECT_FALSE(DebugMode()) << "with no session it switches off as before";
}

TEST_F(DebugModeOwners_Test, EditorMonitorDisarmLeavesDebugModeToASessionStartedMeanwhile)
{
    EditorMonitor* monitor = _context->pDebugManager->GetEditorMonitor();
    ASSERT_NE(monitor, nullptr);
    ASSERT_FALSE(DebugMode()) << "test precondition";

    monitor->Arm();
    StartMemorySession();
    monitor->Disarm();
    EXPECT_TRUE(DebugMode()) << "the session started after Arm() still needs it";
    EXPECT_TRUE(_tracker->IsMemoryCapturing());
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kBreakpoints)) << "breakpoints are restored as before";
}
