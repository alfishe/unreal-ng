// FatImageSource: a FAT disk image as a layer (multi-source phases/c3-image-sources.md §4)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/fatimagesource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/fat/fatvolumereader.h"

namespace
{
    std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++)
            data[i] = static_cast<uint8_t>(seed + i * 13 + i / 512);
        return data;
    }

    /// The bytes of the node at `path`, read sector by sector through ExtentReader
    std::vector<uint8_t> ReadNode(SourcePool& pool, const FileTree& tree, const std::string& path)
    {
        const uint32_t index = tree.Find(path);
        EXPECT_NE(index, FileTree::kNone) << path;
        if (index == FileTree::kNone)
            return {};
        const FileData& data = tree.Node(index).data;
        ExtentReader reader(pool, tree.Extents());
        std::vector<uint8_t> bytes;
        uint8_t sector[512];
        for (uint64_t s = 0; s * 512 < data.bytes; s++)
        {
            EXPECT_TRUE(reader.ReadFileSector(data, s, sector));
            bytes.insert(bytes.end(), sector, sector + std::min<uint64_t>(512, data.bytes - s * 512));
        }
        return bytes;
    }

    std::vector<std::string> Names(const FileTree& tree, uint32_t dir)
    {
        std::vector<std::string> names;
        for (uint32_t child : tree.Node(dir).children)
            names.push_back(tree.Node(child).name);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool Has(const std::vector<std::string>& report, const std::string& text)
    {
        return std::any_of(report.begin(), report.end(), [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }
}  // namespace

/// FAT12, FAT16 and FAT32 images as sources: the tree, sizes and bytes through ExtentReader
TEST(FatImageSource_Test, Fat12Fat16Fat32Sources)
{
    const std::vector<uint8_t> small = Bytes(1500, 3);
    const std::vector<uint8_t> big = Bytes(70000, 5);

    SourcePool pool;
    FileTree floppy;
    const uint16_t f = pool.AddDevice(Fat12Floppy({{"SMALL.BIN", small}, {"BIG.BIN", big}, {"EMPTY.TXT", {}}}));
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(f, {}, pool, floppy, nullptr, &error)) << error;
    EXPECT_EQ(Names(floppy, FileTree::kRoot), (std::vector<std::string>{"BIG.BIN", "EMPTY.TXT", "SMALL.BIN"}));
    EXPECT_EQ(ReadNode(pool, floppy, "/SMALL.BIN"), small);
    EXPECT_EQ(ReadNode(pool, floppy, "/BIG.BIN"), big);
    EXPECT_EQ(floppy.Node(floppy.Find("/EMPTY.TXT")).data.storage, FileData::Storage::Zero);

    ScratchFolder folder("fis-types");
    folder.File("small.bin", std::string(small.begin(), small.end()));
    folder.File("dir/big.bin", std::string(big.begin(), big.end()));
    for (FatType type : {FatType::Fat16, FatType::Fat32})
    {
        const FatSourceImage image = FolderToFatDisk(folder.Path(), type);
        ASSERT_TRUE(image.ok()) << image.error;
        FileTree tree;
        const uint16_t d = pool.AddDevice(image.disk);
        ASSERT_TRUE(FatImageSource::Enumerate(d, {}, pool, tree, nullptr, &error)) << error;
        EXPECT_EQ(ReadNode(pool, tree, "/small.bin"), small) << (type == FatType::Fat32 ? "FAT32" : "FAT16");
        EXPECT_EQ(ReadNode(pool, tree, "/dir/big.bin"), big) << (type == FatType::Fat32 ? "FAT32" : "FAT16");
    }
}

/// Long names, nesting, the hidden attribute and DOS times come through
TEST(FatImageSource_Test, SubdirectoriesLongNamesAttributesTimes)
{
    ScratchFolder folder("fis-names");
    folder.File("Long File Name.txt", "long");
    folder.File("a/b/c/deep.dat", "deep");
    folder.File(".hidden", "dot");
    const FatSourceImage image = FolderToFatDisk(folder.Path(), FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;

    SourcePool pool;
    FileTree tree;
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(pool.AddDevice(image.disk), {}, pool, tree, nullptr, &error)) << error;
    const uint32_t longName = tree.Find("/Long File Name.txt");
    ASSERT_NE(longName, FileTree::kNone);
    EXPECT_EQ(tree.Node(longName).mtimeUtc, 1767268800) << "the source's DOS time, read as UTC";
    EXPECT_EQ(ReadNode(pool, tree, "/a/b/c/deep.dat"), (std::vector<uint8_t>{'d', 'e', 'e', 'p'}));
    EXPECT_TRUE(tree.Node(tree.Find("/a/b")).isDirectory);
    const uint32_t hidden = tree.Find("/.hidden");
    ASSERT_NE(hidden, FileTree::kNone);
    EXPECT_EQ(tree.Node(hidden).attributes & 0x02, 0x02);
}

/// Short names in CP1251 read right with codepage cp1251, and differ when read as CP866
TEST(FatImageSource_Test, CodePagePerLayer)
{
    ScratchFolder folder("fis-cp");
    folder.File("\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2.TXT", "hello");  // ПРИВЕТ.TXT
    const FatSourceImage image = FolderToFatDisk(folder.Path(), FatType::Fat16, CodePage::Cp1251);
    ASSERT_TRUE(image.ok()) << image.error;

    FatVolumeReader cp1251;
    ASSERT_TRUE(cp1251.Open(*image.disk, CodePage::Cp1251));
    std::vector<FatDirEntryInfo> entries;
    ASSERT_TRUE(cp1251.List("/", entries));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].shortName, "\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2.TXT");
    FatVolumeReader cp866;
    ASSERT_TRUE(cp866.Open(*image.disk, CodePage::Cp866));
    ASSERT_TRUE(cp866.List("/", entries));
    EXPECT_NE(entries[0].shortName, "\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2.TXT") << "the bytes are CP1251";

    SourcePool pool;
    FileTree tree;
    FatImageSourceOptions options;
    options.codePage = CodePage::Cp1251;
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(pool.AddDevice(image.disk), options, pool, tree, nullptr, &error)) << error;
    EXPECT_NE(tree.Find("/\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2.TXT"), FileTree::kNone);
}

/// A deleted entry and an orphan long-name entry do not become files
TEST(FatImageSource_Test, IgnoresDeletedAndOrphanLfn)
{
    auto disk = Fat12Floppy({{"A.TXT", {'a'}}, {"B.TXT", {'b'}}, {"C.TXT", {'c'}}, {"D.TXT", {'d'}}});
    uint8_t s[512];
    ASSERT_TRUE(disk->ReadSector(19, s));
    s[1 * 32] = 0xE5;  // B deleted
    uint8_t* lfn = s + 2 * 32;  // C becomes a long-name piece whose checksum fits nothing
    lfn[0] = 0x41;
    lfn[11] = 0x0F;
    lfn[13] = 0x00;
    ASSERT_TRUE(disk->WriteSector(19, s));

    SourcePool pool;
    FileTree tree;
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(pool.AddDevice(disk), {}, pool, tree, nullptr, &error)) << error;
    EXPECT_EQ(Names(tree, FileTree::kRoot), (std::vector<std::string>{"A.TXT", "D.TXT"}));
}

/// `from`, exclude and include as for folders; a file with a broken chain is left out with a report line
TEST(FatImageSource_Test, FromIncludeExcludeAndBrokenChain)
{
    ScratchFolder folder("fis-filters");
    folder.File("GAMES/elite.trd", "elite");
    folder.File("GAMES/exolon.scl", "exolon");
    folder.File("GAMES/notes.txt", "notes");
    folder.File("GAMES/OLD/old.trd", "old");
    folder.File("readme.txt", "root");
    const FatSourceImage image = FolderToFatDisk(folder.Path(), FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;

    SourcePool pool;
    const uint16_t device = pool.AddDevice(image.disk);
    FileTree tree;
    FatImageSourceOptions options;
    options.from = "/GAMES";
    options.include = {"*.trd", "*.scl"};
    options.exclude = {"OLD"};
    std::vector<std::string> report;
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(device, options, pool, tree, &report, &error)) << error;
    EXPECT_EQ(Names(tree, FileTree::kRoot), (std::vector<std::string>{"elite.trd", "exolon.scl"}));
    EXPECT_TRUE(Has(report, "/notes.txt: not included"));
    EXPECT_TRUE(Has(report, "/OLD: skipped, excluded"));

    FileTree none;
    options.from = "/NOPE";
    EXPECT_FALSE(FatImageSource::Enumerate(device, options, pool, none, nullptr, &error));
    EXPECT_NE(error.find("/NOPE"), std::string::npos) << error;

    // A.BIN's chain points past the volume: skipped, B.BIN still there
    auto floppy = Fat12Floppy({{"A.BIN", std::vector<uint8_t>(1024, 1)}, {"B.BIN", {'b'}}});
    uint8_t fat[512];
    ASSERT_TRUE(floppy->ReadSector(1, fat));
    fat[3] = 0x00;  // cluster 2 -> 0xF00 (bytes 3..4 hold entries 2 and 3)
    fat[4] = static_cast<uint8_t>((fat[4] & 0xF0) | 0x0F);
    ASSERT_TRUE(floppy->WriteSector(1, fat));
    FileTree broken;
    report.clear();
    ASSERT_TRUE(FatImageSource::Enumerate(pool.AddDevice(floppy), {}, pool, broken, &report, &error)) << error;
    EXPECT_EQ(Names(broken, FileTree::kRoot), (std::vector<std::string>{"B.BIN"}));
    EXPECT_TRUE(Has(report, "/A.BIN: skipped, the cluster chain leaves the volume")) << (report.empty() ? "" : report[0]);
}

/// An explicit partition becomes one SubRangeDevice per image and partition, shared by the layers naming it
TEST(FatImageSource_Test, PartitionWindowsAreShared)
{
    ScratchFolder folder("fis-part");
    folder.File("x.txt", "x");
    const FatSourceImage image = FolderToFatDisk(folder.Path(), FatType::Fat16, CodePage::Cp866, true);
    ASSERT_TRUE(image.ok()) << image.error;

    SourcePool pool;
    const uint16_t device = pool.AddDevice(image.disk, "card.img");
    FatImageSourceOptions options;
    options.partition = 1;
    FileTree a;
    FileTree b;
    std::string error;
    ASSERT_TRUE(FatImageSource::Enumerate(device, options, pool, a, nullptr, &error)) << error;
    ASSERT_TRUE(FatImageSource::Enumerate(device, options, pool, b, nullptr, &error)) << error;
    EXPECT_EQ(pool.DeviceCount(), 2u) << "the image and one window of it";
    EXPECT_EQ(ReadNode(pool, b, "/x.txt"), (std::vector<uint8_t>{'x'}));

    options.partition = 2;
    FileTree c;
    EXPECT_FALSE(FatImageSource::Enumerate(device, options, pool, c, nullptr, &error));
    EXPECT_NE(error.find("partition 2"), std::string::npos) << error;
}
