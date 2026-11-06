#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../../../../automation/webapi/src/common/upload_sweep.h"
#include "_helpers/testpathhelper.h"
#include "platform/processinfo.h"

/// SweepUploadSessions: the staged uploads of a process that crashed or was killed go when the next WebAPI starts;
/// a running process's folder and names that are not a session's stay

namespace fs = std::filesystem;

TEST(UploadSweep_Test, RemovesTheFoldersOfGoneProcessesOnly)
{
    const fs::path uploads = TestPathHelper::GetUniqueTestScratchPath("upload-sweep/uploads");
    const std::string mine = std::to_string(platform::CurrentProcessId()) + "-0123456789abcdef";
    // No system hands out this id (Linux and macOS cap theirs far below, Windows ids are multiples of 4)
    const std::string gone = "4000000001-0123456789abcdef";
    for (const std::string& name : {mine, gone, std::string("0123456789abcdef"), std::string("notes-1")})
    {
        fs::create_directories(uploads / name);
        std::ofstream(uploads / name / "1_game.tap") << "x";
    }

    api::v1::SweepUploadSessions(uploads.string());

    EXPECT_TRUE(fs::exists(uploads / mine / "1_game.tap")) << "this process's session";
    EXPECT_FALSE(fs::exists(uploads / gone)) << "a session whose process is gone";
    EXPECT_TRUE(fs::exists(uploads / "0123456789abcdef")) << "no owner in the name: not a session of this code";
    EXPECT_TRUE(fs::exists(uploads / "notes-1")) << "not a session name";
}

TEST(UploadSweep_Test, AMissingFolderIsNothingToDo)
{
    api::v1::SweepUploadSessions(TestPathHelper::GetUniqueTestScratchPath("upload-sweep-none/uploads"));
}
