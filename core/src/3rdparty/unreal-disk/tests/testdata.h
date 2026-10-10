#pragma once

// The repository's disk images (testdata/loaders/...) and a scratch folder for files the tests write

#include <filesystem>
#include <string>

namespace unrealdisk::testing
{
inline std::string TestDataPath(const std::string& relative)
{
    return std::string(UNREAL_DISK_TESTDATA_DIR) + "/" + relative;
}

/// A fresh folder under the system temp directory for one test
inline std::filesystem::path ScratchFolder(const std::string& name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "unreal-disk-tests" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}
}  // namespace unrealdisk::testing
