// Iso9660Reader against images written by the independent IsoImageBuilder
// (multi-source phases/c5-iso.md §7)

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/isoimagebuilder.h"
#include "emulator/io/storage/cd/iso9660reader.h"

namespace
{
    std::vector<std::string> Names(Iso9660Reader& reader, const std::string& path)
    {
        IsoDirEntry dir;
        EXPECT_TRUE(reader.Stat(path, dir)) << path;
        std::vector<IsoDirEntry> entries;
        EXPECT_TRUE(reader.List(dir, entries)) << path;
        std::vector<std::string> names;
        for (const IsoDirEntry& e : entries)
            names.push_back(e.name);
        return names;
    }

    std::string Read(Iso9660Reader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }
}  // namespace

/// ISO names without ";1", a nested tree, hidden files, dates, file bytes across blocks
TEST(Iso9660Reader_Test, ReadsBuilderImages)
{
    IsoImageBuilder iso;
    iso.Add("/README.TXT", "hello");
    iso.Add("/GAMES/ELITE.TRD", std::string(5000, 'e'));
    iso.Add("/GAMES/DEEP/ER/FILE.BIN", "deep");
    iso.Add("/SECRET.DAT", "s", {}, true);
    BytesDisk disk(iso.Build());

    Iso9660Reader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(disk, &error)) << error;
    EXPECT_FALSE(reader.HasJoliet());
    EXPECT_EQ(reader.VolumeId(), "TESTISO");
    EXPECT_EQ(Names(reader, "/"), (std::vector<std::string>{"GAMES", "README.TXT", "SECRET.DAT"}));
    EXPECT_EQ(Names(reader, "/GAMES"), (std::vector<std::string>{"DEEP", "ELITE.TRD"}));
    EXPECT_EQ(Read(reader, "/README.TXT"), "hello");
    EXPECT_EQ(Read(reader, "/games/elite.trd"), std::string(5000, 'e')) << "paths match ASCII case-insensitively";
    EXPECT_EQ(Read(reader, "/GAMES/DEEP/ER/FILE.BIN"), "deep");
    IsoDirEntry secret;
    ASSERT_TRUE(reader.Stat("/SECRET.DAT", secret));
    EXPECT_TRUE(secret.hidden);
    EXPECT_EQ(secret.isoName, "SECRET.DAT;1");
    EXPECT_EQ(secret.mtimeUtc, 1767268800);
}

/// With a Joliet tree the long names come from it; useJoliet = false reads the ISO tree
TEST(Iso9660Reader_Test, JolietNamesPreferred)
{
    IsoImageBuilder iso;
    iso.joliet = true;
    iso.Add("/README.TXT", "r", "Read Me Please.txt");
    iso.Add("/LONGDIR/A.TXT", "a", "a.txt");
    iso.DirectoryJoliet("/LONGDIR", "A Long Directory");
    BytesDisk disk(iso.Build());

    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(disk));
    EXPECT_TRUE(reader.HasJoliet());
    EXPECT_EQ(Names(reader, "/"), (std::vector<std::string>{"A Long Directory", "Read Me Please.txt"}));
    EXPECT_EQ(Read(reader, "/A Long Directory/a.txt"), "a");

    Iso9660Reader plain;
    ASSERT_TRUE(plain.Open(disk, nullptr, false));
    EXPECT_EQ(Names(plain, "/"), (std::vector<std::string>{"LONGDIR", "README.TXT"}));
}

/// A directory whose records fill several blocks (records never cross a block: zero padding at block ends)
TEST(Iso9660Reader_Test, RecordsAcrossSectors)
{
    IsoImageBuilder iso;
    std::vector<std::string> expected;
    for (int i = 0; i < 150; i++)
    {
        char name[16];
        std::snprintf(name, sizeof name, "F%07d.TXT", i);
        iso.Add(std::string("/") + name, std::to_string(i));
        expected.push_back(name);
    }
    BytesDisk disk(iso.Build());
    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(disk));
    EXPECT_GT(reader.Root().size, 2048u) << "the root spans blocks";
    EXPECT_EQ(Names(reader, "/"), expected);
    EXPECT_EQ(Read(reader, "/F0000149.TXT"), "149");
}

TEST(Iso9660Reader_Test, NotAnIso)
{
    BytesDisk disk(std::vector<uint8_t>(64 * 2048, 0));
    Iso9660Reader reader;
    std::string error;
    EXPECT_FALSE(reader.Open(disk, &error));
    EXPECT_NE(error.find("CD001"), std::string::npos) << error;
}
