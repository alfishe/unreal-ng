// FolderManifest: the optional .unreal-media.yaml / .json of a folder source
// (technical design §6.6)

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"

namespace fs = std::filesystem;

TEST(FolderManifest_Test, YamlSetsEveryKey)
{
    const FolderManifest m = FolderManifest::Parse(R"(
label: MY GAMES
order:
  - boot.$B
  - game.$C
exclude: [notes.txt, "*.psd"]
files:
  loader.bin: {name: loader, type: C, start: 24576}
  intro.scr: {start: 0x4000}
  menu: {type: B, line: 10}
disk: {format: trd, tracks: 80, sides: 2}
tape: {format: tzx, pause: 1000}
)", ".unreal-media.yaml");

    EXPECT_TRUE(m.report.empty()) << m.report.front();
    EXPECT_EQ(m.label.value_or(""), "MY GAMES");
    EXPECT_EQ(m.order, (std::vector<std::string>{"boot.$B", "game.$C"}));
    EXPECT_EQ(m.exclude, (std::vector<std::string>{"notes.txt", "*.psd"}));
    ASSERT_EQ(m.files.count("loader.bin"), 1u);
    EXPECT_EQ(m.files.at("loader.bin").name.value_or(""), "loader");
    EXPECT_EQ(m.files.at("loader.bin").type.value_or('?'), 'C');
    EXPECT_EQ(m.files.at("loader.bin").start.value_or(0), 24576);
    EXPECT_EQ(m.files.at("intro.scr").start.value_or(0), 0x4000) << "hex accepted";
    EXPECT_EQ(m.files.at("menu").line.value_or(0), 10);
    EXPECT_EQ(m.diskFormat.value_or(""), "trd");
    EXPECT_EQ(m.diskTracks.value_or(0), 80);
    EXPECT_EQ(m.diskSides.value_or(0), 2);
    EXPECT_EQ(m.tapeFormat.value_or(""), "tzx");
    EXPECT_EQ(m.tapePauseMs.value_or(0), 1000u);
}

TEST(FolderManifest_Test, JsonIsReadByTheSameParser)
{
    const FolderManifest m = FolderManifest::Parse(
        R"({"label": "JSON DISK", "order": ["a.$C"], "files": {"a.$C": {"start": 32768}}})", ".unreal-media.json");
    EXPECT_TRUE(m.report.empty());
    EXPECT_EQ(m.label.value_or(""), "JSON DISK");
    EXPECT_EQ(m.order, (std::vector<std::string>{"a.$C"}));
    EXPECT_EQ(m.files.at("a.$C").start.value_or(0), 32768);
}

TEST(FolderManifest_Test, ProblemsAreReportedNeverFatal)
{
    const FolderManifest m = FolderManifest::Parse(R"(
label: OK
colour: blue
disk: {tracks: 300, sides: 2}
files:
  x.bin: {type: CODE, start: 70000}
)", ".unreal-media.yaml");
    EXPECT_EQ(m.label.value_or(""), "OK") << "good keys still apply";
    EXPECT_EQ(m.diskSides.value_or(0), 2);
    EXPECT_FALSE(m.diskTracks.has_value());
    ASSERT_GE(m.report.size(), 4u);
    auto has = [&m](const char* part) {
        for (const auto& line : m.report)
            if (line.find(part) != std::string::npos)
                return true;
        return false;
    };
    EXPECT_TRUE(has("colour: unknown key"));
    EXPECT_TRUE(has("disk.tracks"));
    EXPECT_TRUE(has("x.bin.type"));
    EXPECT_TRUE(has("x.bin.start"));

    const FolderManifest broken = FolderManifest::Parse("label: [unclosed", ".unreal-media.yaml");
    EXPECT_FALSE(broken.label.has_value());
    ASSERT_EQ(broken.report.size(), 1u);
    EXPECT_NE(broken.report[0].find("cannot be read"), std::string::npos);

    EXPECT_TRUE(FolderManifest::Parse("   \n", ".unreal-media.yaml").report.empty()) << "empty is fine";
}

TEST(FolderManifest_Test, LoadPrefersYamlAndReportsBoth)
{
    const fs::path folder = TestPathHelper::GetUniqueTestScratchPath("manifest-load");
    fs::remove_all(folder);
    fs::create_directories(folder);

    EXPECT_TRUE(FolderManifest::Load(folder).sourceFile.empty()) << "no metafile: nothing set, nothing reported";

    std::ofstream(folder / ".unreal-media.json") << R"({"label": "FROM JSON"})";
    EXPECT_EQ(FolderManifest::Load(folder).label.value_or(""), "FROM JSON");

    std::ofstream(folder / ".unreal-media.yaml") << "label: FROM YAML\n";
    const FolderManifest both = FolderManifest::Load(folder);
    EXPECT_EQ(both.label.value_or(""), "FROM YAML");
    ASSERT_EQ(both.report.size(), 1u);
    EXPECT_NE(both.report[0].find("takes precedence"), std::string::npos);

    std::error_code ec;
    fs::remove_all(folder, ec);
}
