#include "emulator/startupcleanup.h"

#include "common/cleanupmanager.h"
#include "debugger/ttd/ttdrecordingfolders.h"

void StartStartupCleanup()
{
    CleanupManager& manager = CleanupManager::Instance();
    manager.AddStep(ttd::CrashedRecordingsCleanupStep());
    manager.RunAsync();
}

void StopStartupCleanup()
{
    CleanupManager::Instance().Stop();
}
