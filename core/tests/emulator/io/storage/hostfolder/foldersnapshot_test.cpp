// FolderSnapshot: one stat-only scan of a host folder, deterministic order,
// UTC times, every skipped entry reported (technical design §6.0, §6.4)

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"

namespace fs = std::filesystem;

namespace
{
    bool WasSkipped(const FolderSnapshot& snapshot, const std::string& path, const std::string& reasonPart)
    {
        return std::any_of(snapshot.Skipped().begin(), snapshot.Skipped().end(), [&](const SkippedEntry& s) {
            return s.path == path && s.reason.find(reasonPart) != std::string::npos;
        });
    }
}  // namespace

TEST(FolderSnapshot_Test, TreeOrderSizesAndUtcTimes)
{
    ScratchFolder folder("snapshot-tree");
    folder.File("SD_BOOT.$C", std::string(1297, 'x'), 1767268800);  // 2026-01-01 12:00:00 UTC
    folder.File("games/EYEACHE.TRD", std::string(1000, 't'));
    folder.File("Длинное имя.txt", "0123456789");
    folder.File("empty.bin", "");
    folder.File(".profile", "dot-files are ordinary files");
    folder.Folder("zzz-empty");

    FolderSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot, &error)) << error;

    const auto& root = snapshot.Root().children;
    std::vector<std::string> names;
    for (const auto& e : root)
        names.push_back(e.name);
    // folders first, then files, byte-wise sorted (UTF-8 Cyrillic sorts after ASCII)
    EXPECT_EQ(names, (std::vector<std::string>{"games", "zzz-empty", ".profile", "SD_BOOT.$C", "empty.bin", "Длинное имя.txt"}));

    EXPECT_TRUE(root[0].isDirectory);
    ASSERT_EQ(root[0].children.size(), 1u);
    EXPECT_EQ(root[0].children[0].name, "EYEACHE.TRD");
    EXPECT_EQ(root[0].children[0].size, 1000u);
    EXPECT_EQ(root[3].size, 1297u);
    EXPECT_EQ(root[3].mtimeUtc, 1767268800) << "seconds since the epoch, UTC";
    EXPECT_EQ(root[4].size, 0u) << "a 0-byte file is kept";

    EXPECT_EQ(snapshot.TotalFileBytes(), 1297u + 1000u + 10u + 0u + 28u);
    EXPECT_EQ(snapshot.EntryCount(), 7u);
    EXPECT_TRUE(snapshot.Skipped().empty());
}

TEST(FolderSnapshot_Test, ServiceFilesExcludesAndReasonsAreReported)
{
    ScratchFolder folder("snapshot-skips");
    folder.File("game.trd", "g");
    folder.File(".DS_Store", "junk");
    folder.File("Thumbs.db", "junk");
    folder.File(".git/HEAD", "ref");
    folder.File("notes.txt", "n");
    folder.File("sub/._game.trd", "junk");

    FolderScanOptions options;
    options.excludePatterns = {"*.txt"};
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), options, snapshot));

    // Host-OS housekeeping is filtered without a trace: the host drops it into
    // the folder at any moment, so it must not surface anywhere
    EXPECT_FALSE(WasSkipped(snapshot, ".DS_Store", ""));
    EXPECT_FALSE(WasSkipped(snapshot, "Thumbs.db", ""));
    EXPECT_FALSE(WasSkipped(snapshot, "sub/._game.trd", ""));
    // Project metadata stays a reported decision
    EXPECT_TRUE(WasSkipped(snapshot, ".git", "service (vcs)"));
    EXPECT_TRUE(WasSkipped(snapshot, "notes.txt", "excluded"));
    EXPECT_EQ(snapshot.EntryCount(), 2u) << "game.trd and the (now empty) sub folder";
}

TEST(FolderSnapshot_Test, TopLevelOnlyReportsSubfolders)
{
    ScratchFolder folder("snapshot-flat");
    folder.File("boot.$B", "b");
    folder.File("more/game.$C", "c");

    FolderScanOptions options;
    options.recursive = false;  // disk images and tapes take one folder
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), options, snapshot));
    ASSERT_EQ(snapshot.Root().children.size(), 1u);
    EXPECT_EQ(snapshot.Root().children[0].name, "boot.$B");
    EXPECT_TRUE(WasSkipped(snapshot, "more", "subfolder"));
}

TEST(FolderSnapshot_Test, LimitsAreReportedNotSilent)
{
    ScratchFolder folder("snapshot-limits");
    for (int i = 0; i < 5; i++)
        folder.File("f" + std::to_string(i), "x");
    folder.File("d1/d2/d3/deep.bin", "x");

    FolderScanOptions options;
    options.maxEntries = 3;
    options.maxDepth = 1;
    options.maxFileSize = 0;  // everything with contents is "too big"
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), options, snapshot));

    EXPECT_TRUE(WasSkipped(snapshot, "f0", "larger than 0 bytes"));
    EXPECT_TRUE(WasSkipped(snapshot, "d1/d2", "deeper than 1"));

    FolderScanOptions few;
    few.maxEntries = 3;
    FolderSnapshot limited;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), few, limited));
    EXPECT_EQ(limited.EntryCount(), 3u);
    EXPECT_TRUE(std::any_of(limited.Skipped().begin(), limited.Skipped().end(),
                            [](const SkippedEntry& s) { return s.reason.find("more than 3 entries") != std::string::npos; }));
}

TEST(FolderSnapshot_Test, SymlinksAreSkippedByDefault)
{
    ScratchFolder folder("snapshot-links");
    folder.File("real.bin", "r");
    std::error_code ec;
    fs::create_symlink(folder.Path() / "real.bin", folder.Path() / "link.bin", ec);
    if (ec)
        GTEST_SKIP() << "this host cannot create symbolic links: " << ec.message();

    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));
    EXPECT_TRUE(WasSkipped(snapshot, "link.bin", "symbolic link"));
    EXPECT_EQ(snapshot.EntryCount(), 1u);
}

TEST(FolderSnapshot_Test, IdentityFollowsNamesSizesAndTimes)
{
    ScratchFolder folder("snapshot-identity");
    folder.File("a.bin", "aaaa");
    FolderSnapshot first;
    FolderSnapshot second;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, first));
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, second));
    EXPECT_EQ(first.Identity(), second.Identity()) << "an unchanged folder has a stable identity";

    folder.File("a.bin", "aaaaa");  // size changes
    FolderSnapshot changed;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, changed));
    EXPECT_NE(changed.Identity(), first.Identity());

    FolderSnapshot missing;
    std::string error;
    EXPECT_FALSE(FolderSnapshot::Scan(folder.Path() / "nope", {}, missing, &error));
    EXPECT_FALSE(error.empty());
}

/// BUGS.md #3: a caller scanning off the UI thread needs to abort a large or
/// slow/network folder and show progress while it runs
TEST(FolderSnapshot_Test, OnProgressCountsEveryEntryVisited)
{
    ScratchFolder folder("snapshot-progress");
    folder.File("a.bin", "a");
    folder.File("b.bin", "b");
    folder.File("sub/c.bin", "c");

    std::vector<uint64_t> seen;
    std::vector<uint64_t> bytesSeen;
    FolderScanOptions options;
    options.onProgress = [&](uint64_t count, uint64_t bytes) {
        seen.push_back(count);
        bytesSeen.push_back(bytes);
    };

    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), options, snapshot));

    // a.bin, b.bin, sub (top level) + c.bin (inside sub) = 4 entries visited
    ASSERT_EQ(seen.size(), 4u);
    EXPECT_TRUE(std::is_sorted(seen.begin(), seen.end())) << "a strictly increasing running count";
    EXPECT_EQ(seen.back(), 4u);

    // bytesSeen lags by one file (reported before the current entry is added
    // to the running total - see foldersnapshot.h's onProgress comment):
    // a.bin and b.bin are 1 byte each, sub is a directory (0 bytes), c.bin 1
    // byte - so the running byte total only reaches 2 once c.bin itself is
    // being visited, never 3 (c.bin's own byte is never reflected back to it)
    EXPECT_EQ(bytesSeen.back(), 2u);
    EXPECT_TRUE(std::is_sorted(bytesSeen.begin(), bytesSeen.end()));
}

TEST(FolderSnapshot_Test, CancelRequestedStopsTheWalkAndFailsWithCancelled)
{
    ScratchFolder folder("snapshot-cancel");
    folder.File("a.bin", "a");
    folder.File("b.bin", "b");
    folder.File("c.bin", "c");

    int visited = 0;
    FolderScanOptions options;
    // Checked at the top of every iteration, before that entry's onProgress:
    // becoming true after the first visited entry stops the walk at the
    // second one, one onProgress call short of all three files
    options.cancelRequested = [&visited]() { return visited >= 1; };
    options.onProgress = [&visited](uint64_t, uint64_t) { ++visited; };

    FolderSnapshot snapshot;
    std::string error;
    EXPECT_FALSE(FolderSnapshot::Scan(folder.Path(), options, snapshot, &error));
    EXPECT_EQ(error, FolderSnapshot::kCancelledError);
    EXPECT_EQ(visited, 1) << "cancelled before the second entry's onProgress";
    // Scan() never populates `snapshot` on a cancelled walk - the caller has
    // no half-built tree to accidentally treat as a result
    EXPECT_EQ(snapshot.EntryCount(), 0u);
}

/// Cancelling while scanning a subfolder must unwind every recursion level,
/// not just the innermost one - a sibling top-level folder scanned after the
/// cancelled one would otherwise make the abort pointless under a real stall
TEST(FolderSnapshot_Test, CancelRequestedUnwindsNestedRecursion)
{
    ScratchFolder folder("snapshot-cancel-nested");
    folder.File("sub1/a.bin", "a");
    folder.File("sub1/b.bin", "b");
    folder.File("sub2/c.bin", "c");  // must never be visited if cancelled inside sub1

    bool cancelFromNow = false;
    std::vector<uint64_t> seen;
    FolderScanOptions options;
    options.onProgress = [&](uint64_t count, uint64_t) {
        seen.push_back(count);
        if (count == 2)        // right after entering sub1, before its first file
            cancelFromNow = true;
    };
    options.cancelRequested = [&cancelFromNow]() { return cancelFromNow; };

    FolderSnapshot snapshot;
    std::string error;
    EXPECT_FALSE(FolderSnapshot::Scan(folder.Path(), options, snapshot, &error));
    EXPECT_EQ(error, FolderSnapshot::kCancelledError);
    // Whichever of sub1/sub2 directory_iterator visits first (unspecified
    // order): its own entry (count 1) + its first file (count 2, where the
    // test's cancel flag is armed) is as far as the walk gets either way -
    // the second subfolder is never entered, matching the recursion-unwind
    // contract (every level's `if (_cancelled) break;`)
    EXPECT_EQ(seen.size(), 2u);
}
