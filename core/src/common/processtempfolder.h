#pragma once

/// @file processtempfolder.h
/// @brief Folders of a process's own in the system's temporary folder, named "<prefix><pid>-<n>": made empty, removed
/// by their owner when done, and by the start-up cleanup once their process is gone (killed, crashed).
///
/// Worked example: a headless VDAC4 board makes "<temp>/unreal-ng-vdac4-4711-0" for its ESP flash and removes it
/// with the board. Process 4711 is killed instead; at the next start, the step from OrphanedFoldersCleanupStep finds
/// the folder, sees no process 4711 and removes it, while "unreal-ng-vdac4-<own pid>-0" of a running instance stays.

#include <filesystem>
#include <string>

struct CleanupStep;

namespace ProcessTempFolder
{
/// A new empty folder "<temp>/<prefix><this process id>-<n>"; empty path when none can be made
std::filesystem::path Make(const std::string& prefix);

/// The start-up step (CleanupManager) that removes "<temp>/<prefix><pid>-*" folders whose process is gone
CleanupStep OrphanedFoldersCleanupStep(const std::string& name, const std::string& prefix);
}  // namespace ProcessTempFolder
