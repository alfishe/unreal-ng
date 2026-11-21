#include "emulator/startupcleanup.h"

#include "common/cleanupmanager.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "common/processtempfolder.h"

void StartStartupCleanup()
{
    CleanupManager& manager = CleanupManager::Instance();
    manager.AddStep(ttd::CrashedRecordingsCleanupStep());
    // The ESP flash of headless VDAC4 boards (Vdac4Board::kOwnCachePrefix) left by killed processes
    manager.AddStep(ProcessTempFolder::OrphanedFoldersCleanupStep("vdac4-esp-flash", "unreal-ng-vdac4-"));
    manager.RunAsync();
}

void StopStartupCleanup()
{
    CleanupManager::Instance().Stop();
}
