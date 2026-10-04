#pragma once

/// @file startupcleanup.h
/// @brief The application's housekeeping at startup: registers every
/// subsystem's cleanup step with CleanupManager::Instance() and runs the due
/// ones in the background (common/cleanupmanager.h). Called once by each
/// application (unreal-qt, the headless automation host) right after it
/// starts; StopStartupCleanup() at exit asks a running step to finish.
///
/// Steps today: "ttd-crashed-recordings" (debugger/ttd/ttdrecordingfolders.h).

void StartStartupCleanup();
void StopStartupCleanup();
