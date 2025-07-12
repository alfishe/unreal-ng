#pragma once

#include <memory>
#include <string>
#include "dto/snapshot_dto.h"
#include "serializers/isnapshotserializer.h" // TODO: Restore when serializer interface is implemented
#include "emulator/emulatorcontext.h"


// LoaderUNS: Main snapshot manager for Unreal NG Snapshots (.uns)
class LoaderUNS {
public:
    // Construct with emulator context and snapshot file path
    LoaderUNS(EmulatorContext* context, const std::string& unsFilePath);
    ~LoaderUNS();

    // Load the snapshot (returns true on success)
    bool load();

    // Save a snapshot to a .uns file (not implemented yet)
    // bool save(const SnapshotDTO& snapshot); // (optional, for future)

    // Last error message (if any)
    std::string lastError() const;

    static bool recursiveCleanup(const std::filesystem::path& dir);
    static bool compressAndSaveDirectory(const std::filesystem::path& snapshotDir, const std::filesystem::path& unsFilePath);

private:
    EmulatorContext* _context = nullptr;        // Emulator context
    std::string _unsFilePath;                   // Path to .uns file
    std::string _lastError;                     // Last error message

    // Helper: Extract .uns (7z) to temp folder, return path
    std::string extractArchive(const std::string& unsFilePath);
    // Helper: Pack temp folder to .uns (7z)
    // bool packArchive(const std::string& folderPath); // (optional, for future)
    // Helper: Cleanup temp folder
    void cleanupTempFolder(const std::string& folderPath);
}; 