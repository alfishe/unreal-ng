// VDAC4 board: the ESP's flash (module cache, path.cfg) of a board with no [VDAC4] EspCache and no desktop default
// (tests, headless instances) is an empty folder of the board's own, removed with it, so every run of a program
// starts from the same flash; the start-up cleanup removes those a killed process left behind.

#ifdef ENABLE_VDAC2

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "common/cleanupmanager.h"
#include "common/processtempfolder.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/platforms/tsconf/vdac4/vdac4board.h"
#include "platform/processinfo.h"

namespace
{
namespace fs = std::filesystem;

/// This process's own ESP flash folders in the temporary folder
int OwnFolders()
{
    const std::string prefix =
        std::string(Vdac4Board::kOwnCachePrefix) + std::to_string(platform::CurrentProcessId()) + "-";
    int count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(fs::temp_directory_path(ec), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().string().rfind(prefix, 0) == 0)
            ++count;
    return count;
}
}  // namespace

TEST(Vdac4Board_Test, HeadlessBoardHasAnOwnFlashRemovedWithIt)
{
    const int before = OwnFolders();
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("vdac4-flash", "TSL-VDAC4", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EXPECT_EQ(OwnFolders(), before + 1);
    manager->RemoveEmulator(emulator->GetUUID());
    emulator.reset();
    EXPECT_EQ(OwnFolders(), before);
}

TEST(Vdac4Board_Test, CleanupRemovesOnlyFoldersOfGoneProcesses)
{
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    // a process id no system hands out (Windows ids are multiples of 4, POSIX ones stay far below)
    const fs::path gone = temp / (std::string(Vdac4Board::kOwnCachePrefix) + "2147483646-0");
    const fs::path alive =
        temp / (std::string(Vdac4Board::kOwnCachePrefix) + std::to_string(platform::CurrentProcessId()) + "-test");
    const fs::path other = temp / (std::string(Vdac4Board::kOwnCachePrefix) + "x");
    for (const fs::path& folder : {gone, alive, other})
    {
        fs::create_directories(folder / "zm", ec);
        std::ofstream(folder / "zm" / "index.bin") << "x";
    }

    std::atomic<bool> stop{false};
    CleanupContext context(stop);
    ProcessTempFolder::OrphanedFoldersCleanupStep("test", Vdac4Board::kOwnCachePrefix).run(context);

    EXPECT_FALSE(fs::exists(gone));
    EXPECT_TRUE(fs::exists(alive));
    EXPECT_TRUE(fs::exists(other));
    fs::remove_all(alive, ec);
    fs::remove_all(other, ec);
}

#endif  // ENABLE_VDAC2
