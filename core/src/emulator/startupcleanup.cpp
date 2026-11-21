#include "emulator/startupcleanup.h"

#include "common/cleanupmanager.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "emulator/platforms/tsconf/vdac4/vdac4board.h"

void StartStartupCleanup()
{
    CleanupManager& manager = CleanupManager::Instance();
    manager.AddStep(ttd::CrashedRecordingsCleanupStep());
    manager.AddStep(Vdac4Board::OwnCacheCleanupStep());
    manager.RunAsync();
}

void StopStartupCleanup()
{
    CleanupManager::Instance().Stop();
}
