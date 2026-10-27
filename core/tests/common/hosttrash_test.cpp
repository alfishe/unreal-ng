// HostTrash (multi-source phases/c8d-writeback-tails.md §2): the freedesktop.org names and the moves into a trash
// under a scratch home (never the user's own trash).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "_helpers/scratchfolder.h"
#include "common/hosttrash.h"

TEST(HostTrash_Test, TrashInfoAndTopDir)
{
    const std::string info = HostTrash::TrashInfo("/home/zx/My Games/ELITE #1.TRD", "2026-10-06T12:30:00");
    EXPECT_EQ(info, "[Trash Info]\nPath=/home/zx/My%20Games/ELITE%20%231.TRD\nDeletionDate=2026-10-06T12:30:00\n");
    EXPECT_EQ(HostTrash::TopDirTrash("/media/usb", 1000), std::filesystem::path("/media/usb/.Trash-1000"));
}

TEST(HostTrash_Test, MovesIntoTheTrashAndAvoidsClashes)
{
#if defined(_WIN32)
    GTEST_SKIP() << "the Recycle Bin is the user's own: not filled by a test";
#else
    ScratchFolder folder("hosttrash");
    const auto home = folder.Path() / "home";
    std::filesystem::create_directories(home);
    const char* oldXdg = std::getenv("XDG_DATA_HOME");
    const char* oldHome = std::getenv("HOME");
    const std::string keepXdg = oldXdg ? oldXdg : "", keepHome = oldHome ? oldHome : "";
    setenv("XDG_DATA_HOME", (home / "data").c_str(), 1);
    setenv("HOME", home.c_str(), 1);

    folder.File("a/GAME.TRD", "first");
    folder.File("b/GAME.TRD", "second");
    std::string error;
    const bool first = HostTrash::Move(folder.Path() / "a/GAME.TRD", &error);
    const bool second = HostTrash::Move(folder.Path() / "b/GAME.TRD", &error);
    const bool missing = HostTrash::Move(folder.Path() / "a/NONE.TRD", &error);

    if (oldXdg)
        setenv("XDG_DATA_HOME", keepXdg.c_str(), 1);
    else
        unsetenv("XDG_DATA_HOME");
    setenv("HOME", keepHome.c_str(), 1);

    ASSERT_TRUE(first) << error;
    ASSERT_TRUE(second) << error;
    EXPECT_FALSE(missing);
    EXPECT_FALSE(std::filesystem::exists(folder.Path() / "a/GAME.TRD"));
    EXPECT_FALSE(std::filesystem::exists(folder.Path() / "b/GAME.TRD"));
#if defined(__APPLE__)
    EXPECT_TRUE(std::filesystem::exists(home / ".Trash" / "GAME.TRD"));
    EXPECT_TRUE(std::filesystem::exists(home / ".Trash" / "GAME.2.TRD")) << "a clash gets a new name";
#else
    const auto trash = home / "data" / "Trash";
    EXPECT_TRUE(std::filesystem::exists(trash / "files" / "GAME.TRD"));
    EXPECT_TRUE(std::filesystem::exists(trash / "files" / "GAME.2.TRD")) << "a clash gets a new name";
    EXPECT_TRUE(std::filesystem::exists(trash / "info" / "GAME.TRD.trashinfo"));
    EXPECT_TRUE(std::filesystem::exists(trash / "info" / "GAME.2.TRD.trashinfo"));
#endif
#endif
}
