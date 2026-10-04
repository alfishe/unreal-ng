// TTD recording folders (debugger/ttd/ttdrecordingfolders.h): the startup
// cleanup deletes a recording folder only when its owner process is gone and
// its newest file is older than the keep time; saved recordings and folders
// of a running process are never touched.

#include <gtest/gtest.h>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "platform/processinfo.h"

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t kNoSuchProcess = 0x7FFFFFF0u;   // above every system's pid limit

    struct Root
    {
        std::string path = TestPathHelper::GetUniqueTestScratchPath("ttd-recordings");
        fs::file_time_type now = fs::file_time_type::clock::now();

        Root()
        {
            FileHelper::DeleteFolder(path);
            FileHelper::CreateFolders(path);
        }

        /// A recording folder with one segment file, @p days old, owned by @p pid (0: no owner file)
        std::string Folder(const std::string& name, int days, uint32_t pid)
        {
            const std::string folder = FileHelper::PathCombine(path, name);
            FileHelper::CreateFolders(folder);
            const std::string segment = FileHelper::PathCombine(folder, "segment-0000.ttd");
            std::ofstream(FileHelper::ToFsPath(segment)) << "data";
            if (pid != 0)
                std::ofstream(FileHelper::ToFsPath(FileHelper::PathCombine(folder, ttd::kRecordingOwnerFile))) << pid;
            const fs::file_time_type when = now - std::chrono::hours(24 * days);
            for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(folder)))
                fs::last_write_time(entry.path(), when);
            fs::last_write_time(FileHelper::ToFsPath(folder), when);
            return folder;
        }

        std::vector<std::string> Clean()
        {
            std::atomic<bool> stop{false};
            CleanupContext context(stop);
            ttd::CleanCrashedRecordings(context, path, now, ttd::kCrashedRecordingKeep);
            EXPECT_TRUE(context.FailedItems().empty());
            return context.RemovedItems();
        }
    };
}  // namespace

TEST(TTDRecordingFolders_Test, ProcessIds)
{
    EXPECT_TRUE(platform::IsProcessAlive(platform::CurrentProcessId()));
    EXPECT_FALSE(platform::IsProcessAlive(kNoSuchProcess));
    EXPECT_FALSE(platform::IsProcessAlive(0));
}

/// Crashed and older than a week: deleted. Crashed but recent, or owned by a
/// running process however old: kept. A saved recording file: kept
TEST(TTDRecordingFolders_Test, OnlyOldCrashedRecordingsAreDeleted)
{
    Root root;
    const std::string oldCrashed = root.Folder("2026-09-20-120000-pentagon", 10, kNoSuchProcess);
    const std::string oldNoOwner = root.Folder("2026-09-21-120000-48k", 9, 0);
    const std::string recentCrashed = root.Folder("2026-10-01-120000-atm3", 3, kNoSuchProcess);
    const std::string oldButRunning = root.Folder("2026-09-01-120000-tsl", 30, platform::CurrentProcessId());
    const std::string saved = FileHelper::PathCombine(root.path, "2026-09-01-120000-pentagon.ttd");
    std::ofstream(FileHelper::ToFsPath(saved)) << "saved";
    fs::last_write_time(FileHelper::ToFsPath(saved), root.now - std::chrono::hours(24 * 60));

    const std::vector<std::string> removed = root.Clean();
    EXPECT_EQ(removed.size(), 2u);
    EXPECT_FALSE(FileHelper::FolderExists(oldCrashed));
    EXPECT_FALSE(FileHelper::FolderExists(oldNoOwner));
    EXPECT_TRUE(FileHelper::FolderExists(recentCrashed));
    EXPECT_TRUE(FileHelper::FolderExists(oldButRunning));
    EXPECT_TRUE(FileHelper::FileExists(saved));
}

/// One recent file keeps an old folder: the age is the newest file's
TEST(TTDRecordingFolders_Test, TheNewestFileDecidesTheAge)
{
    Root root;
    const std::string folder = root.Folder("2026-09-20-120000-pentagon", 10, kNoSuchProcess);
    const std::string late = FileHelper::PathCombine(folder, "segment-0001.ttd");
    std::ofstream(FileHelper::ToFsPath(late)) << "data";
    fs::last_write_time(FileHelper::ToFsPath(late), root.now - std::chrono::hours(24));
    EXPECT_TRUE(root.Clean().empty());
    EXPECT_TRUE(FileHelper::FolderExists(folder));
}

/// The owner file round-trips this process's id; a missing root is no error
TEST(TTDRecordingFolders_Test, OwnerFileAndMissingRoot)
{
    Root root;
    const std::string folder = FileHelper::PathCombine(root.path, "rec");
    FileHelper::CreateFolders(folder);
    EXPECT_EQ(ttd::ReadRecordingOwner(folder), 0u);
    ASSERT_TRUE(ttd::WriteRecordingOwner(folder));
    EXPECT_EQ(ttd::ReadRecordingOwner(folder), platform::CurrentProcessId());

    std::atomic<bool> stop{false};
    CleanupContext context(stop);
    ttd::CleanCrashedRecordings(context, FileHelper::PathCombine(root.path, "missing"), root.now,
                                ttd::kCrashedRecordingKeep);
    EXPECT_TRUE(context.RemovedItems().empty());
    EXPECT_TRUE(context.FailedItems().empty());
}

/// A recording's folder: named by local time and model, a second one in the
/// same second gets "-2"; it names this process; save copies the finished
/// segment out, discard deletes the folder
TEST(TTDRecordingFolders_Test, FolderLifecycle)
{
    Root root;
    std::string error;
    const std::time_t when = 1'759'581'012;   // a fixed instant
    auto first = ttd::TTDRecordingFolder::Create(root.path, "PENTAGON", when, error);
    ASSERT_NE(first, nullptr) << error;
    auto second = ttd::TTDRecordingFolder::Create(root.path, "PENTAGON", when, error);
    ASSERT_NE(second, nullptr) << error;
    EXPECT_NE(first->Path().find("-pentagon"), std::string::npos) << first->Path();
    EXPECT_EQ(second->Path(), first->Path() + "-2");
    EXPECT_EQ(ttd::ReadRecordingOwner(first->Path()), platform::CurrentProcessId());

    std::string target = FileHelper::PathCombine(root.path, "saved-\xc3\xa9.ttd");
    EXPECT_FALSE(first->SaveAs(target, false, error)) << "no segment yet";
    std::ofstream(FileHelper::ToFsPath(first->SegmentPath(0)), std::ios::binary) << "session";
    ASSERT_EQ(first->Segments().size(), 1u);
    ASSERT_TRUE(first->SaveAs(target, false, error)) << error;
    EXPECT_TRUE(FileHelper::FileExists(target));
    EXPECT_FALSE(first->SaveAs(target, false, error)) << "an existing file is not overwritten unasked";
    EXPECT_TRUE(first->SaveAs(target, true, error)) << error;

    const std::string path = first->Path();
    EXPECT_TRUE(first->Discard());
    EXPECT_FALSE(FileHelper::FolderExists(path));
    EXPECT_TRUE(FileHelper::FileExists(target)) << "the saved copy stays";
}
