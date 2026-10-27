// The iS-DOS file system: the Classic ISDOS50.FDI and TASiS 5.40 system floppies and an HDD device dump read to the same
// tree and bytes as the zxarc reader of the software catalogue (paths, counts, a CRC-32 over every path and body);
// TR-DOS and +3 disks are not taken; damage is reported, never fatal

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

#include "testdata.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/isdosfs.h"

using namespace unrealdisk;
using unrealdisk::testing::TestDataPath;

namespace
{
std::unique_ptr<DiskImage> Load(const std::string& relative)
{
    std::unique_ptr<DiskImage> disk;
    std::string format;
    EXPECT_TRUE(FloppyFormats::Load({}, TestDataPath(relative), disk, format).Ok()) << relative;
    return disk;
}

std::vector<uint8_t> Bytes(const std::string& relative)
{
    std::ifstream in(TestDataPath(relative), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t size)
{
    crc = ~crc;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

struct Tree
{
    size_t files = 0;
    size_t catalogues = 0;
    size_t bytes = 0;
    uint32_t crc = 0;       ///< over each file's path (UTF-8) and body, in listing order (zxarc's golden.py)
    std::vector<std::string> root;
    std::map<std::string, uint32_t> each;   ///< a CRC-32 per file body
};

Tree Walk(const IsdosFileSystem& fs)
{
    Tree tree;
    std::vector<IsdosFile> entries;
    std::vector<std::string> notes;
    std::string error;
    EXPECT_TRUE(fs.List(entries, &notes, error)) << error;
    EXPECT_TRUE(notes.empty()) << notes.front();
    for (const IsdosFile& f : entries)
    {
        if (f.path.find('/') == std::string::npos)
            tree.root.push_back(f.path);
        if (f.directory)
        {
            tree.catalogues++;
            EXPECT_TRUE(f.error.empty()) << f.path << ": " << f.error;
            continue;
        }
        std::vector<uint8_t> body;
        EXPECT_TRUE(fs.Read(f, body, error)) << f.path << ": " << error;
        EXPECT_EQ(body.size(), f.length) << f.path;
        tree.files++;
        tree.bytes += body.size();
        tree.crc = Crc32(tree.crc, reinterpret_cast<const uint8_t*>(f.path.data()), f.path.size());
        tree.crc = Crc32(tree.crc, body.data(), body.size());
        tree.each[f.path] = Crc32(0, body.data(), body.size());
    }
    return tree;
}
}  // namespace

TEST(IsdosFs_Test, ClassicSystemFloppy)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    const IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    ASSERT_TRUE(fs.IsIsdos());
    const IsdosBoot& boot = fs.Boot();
    EXPECT_FALSE(boot.chic);
    EXPECT_EQ(boot.title, "is-DOS94");
    EXPECT_EQ(boot.blocks, 3200);
    EXPECT_EQ(boot.root, 4);
    EXPECT_EQ(boot.sectorBlocks, 4);
    EXPECT_EQ(boot.sectors, 5);
    EXPECT_EQ(boot.sectorTable, (std::vector<uint8_t>{0, 1, 2, 3, 8}));
    EXPECT_EQ(boot.date, (14 << 9) | (5 << 5) | 17);  // 1994-05-17

    const Tree tree = Walk(fs);
    EXPECT_EQ(tree.files, 537u);
    EXPECT_EQ(tree.catalogues, 54u);
    EXPECT_EQ(tree.bytes, 647055u);
    EXPECT_EQ(tree.crc, 0xE16003B4u);
    ASSERT_GE(tree.root.size(), 12u);
    EXPECT_EQ(std::vector<std::string>(tree.root.begin(), tree.root.begin() + 12),
              (std::vector<std::string>{"device.sys", "boot.sys", "SHELL", "UTIL", "RES", "EDIT", "SERVICE", "INSTALL", "PRINT",
                                        "HELP", "TXTARC", "\xD0\xA0\xD0\x95\xD0\x9A\xD0\x9B\xD0\x90\xD0\x9C\xD0\x90"}));  // РЕКЛАМА

    uint32_t free = 0;
    ASSERT_TRUE(fs.FreeBlocks(free));
    EXPECT_EQ(free, 205u);

    // A continuous and a segmented file by path (case ignored), and the system entries
    std::string error;
    IsdosFile f;
    std::vector<uint8_t> body;
    ASSERT_TRUE(fs.Find("READ_ME.TXT", f, error)) << error;
    EXPECT_FALSE(f.Segmented());
    ASSERT_TRUE(fs.Read(f, body, error)) << error;
    EXPECT_EQ(body.size(), 1641u);
    EXPECT_EQ(Crc32(0, body.data(), body.size()), 0xCBB6596Eu);
    ASSERT_TRUE(fs.Find("/SHELL/HELP/exit.hlp", f, error)) << error;
    EXPECT_TRUE(f.Segmented());
    EXPECT_EQ(f.path, "SHELL/HELP/exit.hlp");
    ASSERT_TRUE(fs.Read(f, body, error)) << error;
    EXPECT_EQ(Crc32(0, body.data(), body.size()), 0xFE98E512u);
    ASSERT_TRUE(fs.Find("device.sys", f, error));
    EXPECT_TRUE(f.system);
    EXPECT_FALSE(f.directory);

    std::vector<IsdosFile> help;
    ASSERT_TRUE(fs.ListDirectory("SHELL/HELP", help, error)) << error;
    EXPECT_FALSE(help.empty());
    EXPECT_EQ(help.front().path.rfind("SHELL/HELP/", 0), 0u);
    EXPECT_FALSE(fs.ListDirectory("read_me.txt", help, error));
    EXPECT_FALSE(fs.Find("SHELL/nothing.com", f, error));
    EXPECT_NE(error.find("no SHELL/nothing.com"), std::string::npos) << error;
}

TEST(IsdosFs_Test, TasisSystemFloppy)
{
    auto disk = Load("loaders/isdos/tasis540.fdi");
    ASSERT_NE(disk, nullptr);
    const IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    ASSERT_TRUE(fs.IsIsdos());
    EXPECT_TRUE(fs.Boot().chic);
    EXPECT_EQ(fs.Boot().title, "TASiS v5.40");
    const Tree tree = Walk(fs);
    EXPECT_EQ(tree.files, 485u);
    EXPECT_EQ(tree.catalogues, 46u);
    EXPECT_EQ(tree.bytes, 718738u);
    EXPECT_EQ(tree.crc, 0x42E3BC08u);
}

TEST(IsdosFs_Test, HddDeviceDump)
{
    // HDDNEMO.img (iS-DOS HDD driver kit, 2004): a 128-block device as a linear dump, one block per sector
    const std::vector<uint8_t> bytes = Bytes("loaders/isdos/HDDNEMO.img");
    ASSERT_EQ(bytes.size(), 32768u);
    const IsdosFileSystem fs = IsdosFileSystem::OnBytes(bytes.data(), bytes.size());
    ASSERT_TRUE(fs.IsIsdos());
    EXPECT_EQ(fs.Boot().title, "HDDNEMO");
    EXPECT_EQ(fs.Boot().blocks, 128);
    const Tree tree = Walk(fs);
    EXPECT_EQ(tree.files, 12u);
    EXPECT_EQ(tree.catalogues, 2u);
    EXPECT_EQ(tree.bytes, 30014u);
    EXPECT_EQ(tree.crc, 0x9593C0AFu);
    EXPECT_EQ(tree.root, (std::vector<std::string>{"device.sys", "HDD_NEMO", "new_cats.com", "old_cats.com"}));
}

TEST(IsdosFs_Test, OtherDisksAreNotIsdos)
{
    for (const char* path : {"loaders/trd/EyeAche.trd", "loaders/dsk/plus3-blank.dsk", "loaders/scl/eyeache2.scl"})
    {
        auto disk = Load(path);
        if (!disk)
            continue;
        EXPECT_FALSE(IsdosFileSystem::OnDisk(*disk).IsIsdos()) << path;
    }
    // The extended DSK header carries "DSK" at +13
    std::vector<uint8_t> header(1024, 0);
    std::memcpy(header.data(), "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
    EXPECT_FALSE(IsdosFileSystem::OnBytes(header.data(), header.size()).IsIsdos());
}

TEST(IsdosFs_Test, DamageIsReported)
{
    std::vector<uint8_t> bytes = Bytes("loaders/isdos/HDDNEMO.img");
    ASSERT_FALSE(bytes.empty());
    std::vector<IsdosFile> entries;
    std::string error;
    ASSERT_TRUE(IsdosFileSystem::OnBytes(bytes.data(), bytes.size()).List(entries, nullptr, error));
    const auto dir = std::find_if(entries.begin(), entries.end(), [](const IsdosFile& f) { return f.directory; });
    const auto file = std::find_if(entries.begin(), entries.end(), [](const IsdosFile& f) { return !f.directory && !f.system; });
    ASSERT_NE(dir, entries.end());
    ASSERT_NE(file, entries.end());

    // The dump cut before a file's last block: its prefix and the block
    const std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + (file->first + (file->length - 1) / 256) * 256);
    const IsdosFileSystem cutFs = IsdosFileSystem::OnBytes(cut.data(), cut.size());
    std::vector<uint8_t> body;
    EXPECT_FALSE(cutFs.Read(*file, body, error));
    EXPECT_NE(error.find("unreadable"), std::string::npos) << error;
    EXPECT_EQ(body.size(), (file->length - 1) / 256 * 256);

    // A sub-catalogue naming the root as its block 0: a loop, cut with a note
    std::vector<uint8_t> loop = bytes;
    const uint16_t root = static_cast<uint16_t>(loop[20] | loop[21] << 8);
    for (size_t i = 1; i < 8; i++)  // the root's first block
    {
        uint8_t* e = loop.data() + root * 256 + i * 32;
        if (std::memcmp(e, dir->rawName, 11) == 0)
        {
            e[19] = static_cast<uint8_t>(root);
            e[20] = static_cast<uint8_t>(root >> 8);
        }
    }
    std::vector<std::string> notes;
    ASSERT_TRUE(IsdosFileSystem::OnBytes(loop.data(), loop.size()).List(entries, &notes, error)) << error;
    ASSERT_FALSE(notes.empty());
    EXPECT_NE(notes.front().find("catalogue loop"), std::string::npos) << notes.front();
}

namespace
{
std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; i++)
        bytes[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 8));
    return bytes;
}

std::vector<uint8_t> ReadBack(const IsdosFileSystem& fs, const std::string& path)
{
    IsdosFile f;
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_TRUE(fs.Find(path, f, error) && fs.Read(f, data, error)) << path << ": " << error;
    return data;
}
}  // namespace

TEST(IsdosFs_Test, WrittenFilesReadBackAsTheKernelLaysThemOut)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    ASSERT_TRUE(fs.Writable());
    const Tree before = Walk(fs);
    uint32_t free0 = 0;
    ASSERT_TRUE(fs.FreeBlocks(free0));
    std::string error;

    // A continuous file in a deleted slot of the root: FSUM = 0 - the sum of its bytes
    const std::vector<uint8_t> text = Pattern(300, 1);
    IsdosFileSystem::WriteOptions options;
    options.load = 0x6000;
    options.date = 0x5B5A;
    ASSERT_TRUE(fs.Write("new.txt", text, options, error)) << error;
    IsdosFile f;
    ASSERT_TRUE(fs.Find("NEW.TXT", f, error)) << error;
    EXPECT_EQ(f.status, 0x41);
    EXPECT_EQ(f.load, 0x6000);
    EXPECT_EQ(f.date, 0x5B5A);
    uint16_t sum = 0;
    for (const uint8_t b : text)
        sum = static_cast<uint16_t>(sum + b);
    EXPECT_EQ(static_cast<uint16_t>(f.checksum + sum), 0);
    EXPECT_EQ(ReadBack(fs, "new.txt"), text);
    uint32_t free1 = 0;
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free0 - free1, 2u);

    // Written again: the same slot, the old blocks given back
    const std::vector<uint8_t> longer = Pattern(1000, 9);
    ASSERT_TRUE(fs.Write("NEW.TXT", longer, {}, error)) << error;
    EXPECT_EQ(ReadBack(fs, "new.txt"), longer);
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free0 - free1, 4u);

    // A small catalogue grows by a block of its own past its 16 slots
    for (int i = 0; i < 20; i++)
        ASSERT_TRUE(fs.Write("EDIT/KRYPTON/HELP/f" + std::to_string(i) + ".hlp", Pattern(10 + i, static_cast<uint8_t>(i)), {}, error))
            << i << ": " << error;
    for (int i = 0; i < 20; i++)
        EXPECT_EQ(ReadBack(fs, "EDIT/KRYPTON/HELP/f" + std::to_string(i) + ".hlp"), Pattern(10 + i, static_cast<uint8_t>(i))) << i;

    // Renamed in place, moved to another catalogue, deleted: the blocks come back
    ASSERT_TRUE(fs.Rename("new.txt", "renamed.bin", error)) << error;
    EXPECT_FALSE(fs.Find("new.txt", f, error));
    ASSERT_TRUE(fs.Rename("renamed.bin", "SHELL/moved.bin", error)) << error;
    EXPECT_FALSE(fs.Find("renamed.bin", f, error));
    EXPECT_EQ(ReadBack(fs, "SHELL/moved.bin"), longer);
    ASSERT_TRUE(fs.Rename("SHELL/moved.bin", "/top.bin", error)) << error;  // "/": from the root
    EXPECT_EQ(ReadBack(fs, "top.bin"), longer);
    ASSERT_TRUE(fs.Rename("top.bin", "SHELL/moved.bin", error)) << error;
    ASSERT_TRUE(fs.Delete("SHELL/moved.bin", error)) << error;
    for (int i = 0; i < 20; i++)
        ASSERT_TRUE(fs.Delete("EDIT/KRYPTON/HELP/f" + std::to_string(i) + ".hlp", error)) << error;
    uint32_t free2 = 0;
    ASSERT_TRUE(fs.FreeBlocks(free2));
    EXPECT_EQ(free2, free0 - 1);  // the catalogue keeps the block it grew by

    // The rest of the disk is as it was
    const Tree after = Walk(fs);
    EXPECT_EQ(after.files, before.files);
    EXPECT_EQ(after.catalogues, before.catalogues);
    // device.sys spans blocks 0-2: the descriptor and the bitmap, which keeps the block the catalogue grew by
    for (const auto& [path, crc] : before.each)
        if (path != "device.sys")
            EXPECT_EQ(after.each.count(path) ? after.each.at(path) : 0, crc) << path;

    // More than 255 blocks: segmented (the disk's help files deleted for the room)
    std::vector<IsdosFile> help;
    ASSERT_TRUE(fs.ListDirectory("RES/HELP", help, error)) << error;
    for (const IsdosFile& h : help)
        ASSERT_TRUE(fs.Delete(h.path, error)) << h.path << ": " << error;
    ASSERT_TRUE(fs.FreeBlocks(free1));
    ASSERT_GE(free1, 280u);
    const std::vector<uint8_t> big = Pattern(70000, 5);
    ASSERT_TRUE(fs.Write("RES/HELP/big.bin", big, {}, error)) << error;
    ASSERT_TRUE(fs.Find("RES/HELP/big.bin", f, error)) << error;
    EXPECT_TRUE(f.Segmented());
    EXPECT_EQ(ReadBack(fs, "RES/HELP/big.bin"), big);
    ASSERT_TRUE(fs.FreeBlocks(free2));
    EXPECT_EQ(free1 - free2, (70000u + 255) / 256 + 1);  // and its segment table
    ASSERT_TRUE(fs.Write("RES/HELP/empty", {}, {}, error)) << error;  // an empty file: a segment table of none
    EXPECT_TRUE(ReadBack(fs, "RES/HELP/empty").empty());
    ASSERT_TRUE(fs.Delete("RES/HELP/big.bin", error)) << error;
    ASSERT_TRUE(fs.Delete("RES/HELP/empty", error)) << error;
    ASSERT_TRUE(fs.Delete("RES/HELP", error)) << error;  // an empty catalogue
    EXPECT_FALSE(fs.Find("RES/HELP", f, error));
    Walk(fs);
}

TEST(IsdosFs_Test, RefusalsSayWhy)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    const std::vector<uint8_t> bytes = Pattern(10, 0);
    std::string error;
    EXPECT_FALSE(fs.Write("toolongname.txt", bytes, {}, error));
    EXPECT_FALSE(fs.Write("bad*.txt", bytes, {}, error));
    EXPECT_FALSE(fs.Write("NOWHERE/a.txt", bytes, {}, error));
    EXPECT_NE(error.find("NOWHERE"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("SHELL", bytes, {}, error));
    EXPECT_NE(error.find("catalogue"), std::string::npos) << error;
    EXPECT_FALSE(fs.Delete("device.sys", error));
    EXPECT_NE(error.find("protected"), std::string::npos) << error;
    EXPECT_FALSE(fs.Rename("boot.sys", "x.sys", error));
    EXPECT_FALSE(fs.Delete("SHELL", error));
    EXPECT_NE(error.find("not empty"), std::string::npos) << error;
    EXPECT_FALSE(fs.Rename("SHELL/coca.com", "copy.com", error));
    EXPECT_NE(error.find("already"), std::string::npos) << error;
    EXPECT_FALSE(fs.Rename("SHELL", "SHELL/HELP/SHELL", error));
    EXPECT_NE(error.find("into itself"), std::string::npos) << error;
    EXPECT_FALSE(fs.Delete("SHELL/nothing.txt", error));
    EXPECT_NE(error.find("no file"), std::string::npos) << error;

    // A catalogue renamed: its own header names it too; Cyrillic names go in as CP866
    ASSERT_TRUE(fs.Rename("SHELL", "shell2", error)) << error;
    IsdosFile f;
    ASSERT_TRUE(fs.Find("SHELL2/coca.com", f, error)) << error;
    ASSERT_TRUE(fs.Write("SHELL2/файл.txt", bytes, {}, error)) << error;
    EXPECT_FALSE(fs.Find("SHELL2/ФАЙЛ.txt", f, error));  // ASCII case only: Cyrillic is matched as stored
    EXPECT_EQ(ReadBack(fs, "SHELL2/файл.txt"), bytes);

    // Read only without a writer
    const std::vector<uint8_t> device = Bytes("loaders/isdos/HDDNEMO.img");
    IsdosFileSystem dump = IsdosFileSystem::OnBytes(device.data(), device.size());
    EXPECT_FALSE(dump.Writable());
    EXPECT_FALSE(dump.Write("a.txt", bytes, {}, error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
}

TEST(IsdosFs_Test, AWrittenFloppySavesAndLoads)
{
    auto disk = Load("loaders/isdos/tasis540.fdi");
    ASSERT_NE(disk, nullptr);
    std::string error;
    // The disk is full: a file deleted for the room
    IsdosFileSystem edit = IsdosFileSystem::OnDisk(*disk);
    ASSERT_TRUE(edit.Delete("BOOT/cr_boot.com", error)) << error;
    const std::vector<uint8_t> bytes = Pattern(700, 3);  // 3 blocks of its 4
    ASSERT_TRUE(edit.Write("saved.bin", bytes, {}, error)) << error;
    const std::string path = (unrealdisk::testing::ScratchFolder("isdos-save") / "saved.fdi").string();
    ASSERT_TRUE(FloppyFormats::Save({}, *disk, path, false).saved);
    auto again = std::unique_ptr<DiskImage>();
    std::string format;
    ASSERT_TRUE(FloppyFormats::Load({}, path, again, format).Ok());
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*again);
    EXPECT_EQ(ReadBack(fs, "saved.bin"), bytes);
    Walk(fs);  // every file still reads, no notes
}

namespace
{
/// Block `block` of the device as it is stored (read as a 256-byte continuous file there)
std::vector<uint8_t> RawBlock(const IsdosFileSystem& fs, uint16_t block)
{
    IsdosFile f;
    f.status = 0x41;
    f.first = block;
    f.length = 256;
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_TRUE(fs.Read(f, data, error)) << error;
    data.resize(256);
    return data;
}

uint16_t Word(const std::vector<uint8_t>& b, size_t at)
{
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

/// The entry named `name11` (live or deleted) of the catalogue at block `catalogue`, its blocks taken in a row
std::vector<uint8_t> EntryIn(const IsdosFileSystem& fs, uint16_t catalogue, const char* name11)
{
    const uint16_t blocks = Word(RawBlock(fs, catalogue), 14) / 256;
    for (uint16_t k = 0; k < blocks; k++)
    {
        const std::vector<uint8_t> b = RawBlock(fs, static_cast<uint16_t>(catalogue + k));
        for (size_t i = k == 0 ? 1 : 0; i < 8; i++)
            if (std::memcmp(b.data() + i * 32, name11, 11) == 0)
                return {b.begin() + i * 32, b.begin() + i * 32 + 32};
    }
    ADD_FAILURE() << name11 << " not in catalogue " << catalogue;
    return std::vector<uint8_t>(32);
}
}  // namespace

/// Catalogues as mkdir.com makes them on a Classic disk (level at +16), moved with their tree (levels follow),
/// deleted as delete.com leaves them; the rest of the disk stays as it was
TEST(IsdosFs_Test, CataloguesAsMkdirMakesThem)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    ASSERT_FALSE(fs.Boot().chic);
    const Tree before = Walk(fs);
    uint32_t free0 = 0, free1 = 0;
    ASSERT_TRUE(fs.FreeBlocks(free0));
    std::string error;
    IsdosFileSystem::WriteOptions options;
    options.date = 0x5B5A;

    ASSERT_TRUE(fs.MakeCatalogue("work", options, error)) << error;  // upper-cased
    IsdosFile work;
    ASSERT_TRUE(fs.Find("WORK", work, error)) << error;
    EXPECT_TRUE(work.directory);
    EXPECT_EQ(work.status, 0x21);
    EXPECT_EQ(work.length, 512u);
    EXPECT_EQ(work.date, 0x5B5A);
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free0 - free1, 3u);  // the table and 2 blocks
    const std::vector<uint8_t> parentEntry = EntryIn(fs, fs.Boot().root, "WORK       ");
    const uint16_t table = Word(parentEntry, 17);
    EXPECT_EQ(Word(parentEntry, 19), work.first);
    const std::vector<uint8_t> t = RawBlock(fs, table);
    EXPECT_EQ(std::vector<uint8_t>(t.begin(), t.begin() + 5),
              (std::vector<uint8_t>{1, static_cast<uint8_t>(work.first), static_cast<uint8_t>(work.first >> 8), 2, 0}));
    const std::vector<uint8_t> head = RawBlock(fs, work.first);
    EXPECT_EQ(std::memcmp(head.data(), "WORK       \x21", 12), 0);
    EXPECT_EQ(Word(head, 12), fs.Boot().root);  // the parent
    EXPECT_EQ(Word(head, 14), 512);
    EXPECT_EQ(head[16], 1);   // the level, Classic
    EXPECT_EQ(head[21], 1);   // NFILE
    EXPECT_EQ(head[22], 0);   // NEFIL
    EXPECT_EQ(head[23], 0);
    EXPECT_EQ(Word(head, 30), 0x5B5A);

    // Files go in; refusals
    ASSERT_TRUE(fs.Write("WORK/a.txt", Pattern(600, 4), {}, error)) << error;
    EXPECT_EQ(ReadBack(fs, "work/a.txt"), Pattern(600, 4));
    EXPECT_FALSE(fs.MakeCatalogue("WORK", {}, error));
    EXPECT_NE(error.find("already"), std::string::npos) << error;
    EXPECT_FALSE(fs.MakeCatalogue("WORK/A.TXT", {}, error));  // no type
    EXPECT_FALSE(fs.MakeCatalogue("NONE/SUB", {}, error));

    // Six levels deep at most
    std::string deep = "WORK";
    for (const char* name : {"L2", "L3", "L4", "L5", "L6"})
    {
        deep += std::string("/") + name;
        ASSERT_TRUE(fs.MakeCatalogue(deep, {}, error)) << deep << ": " << error;
    }
    EXPECT_FALSE(fs.MakeCatalogue(deep + "/L7", {}, error));
    EXPECT_NE(error.find("levels"), std::string::npos) << error;
    IsdosFile l6;
    ASSERT_TRUE(fs.Find(deep, l6, error)) << error;
    EXPECT_EQ(RawBlock(fs, l6.first)[16], 6);

    // Moved with its tree: the parent and every level follow; not into itself, not too deep
    ASSERT_TRUE(fs.Rename("WORK/L2/L3", "/TOP", error)) << error;
    IsdosFile top, l4, l5;
    ASSERT_TRUE(fs.Find("TOP", top, error)) << error;
    ASSERT_TRUE(fs.Find("TOP/L4", l4, error)) << error;
    ASSERT_TRUE(fs.Find("TOP/L4/L5/L6", l6, error)) << error;
    EXPECT_FALSE(fs.Find("WORK/L2/L3", top, error));
    ASSERT_TRUE(fs.Find("TOP", top, error)) << error;
    const std::vector<uint8_t> topHead = RawBlock(fs, top.first);
    EXPECT_EQ(std::memcmp(topHead.data(), "TOP        ", 11), 0);
    EXPECT_EQ(Word(topHead, 12), fs.Boot().root);
    EXPECT_EQ(topHead[16], 1);
    EXPECT_EQ(RawBlock(fs, l4.first)[16], 2);
    EXPECT_EQ(RawBlock(fs, l6.first)[16], 4);
    EXPECT_FALSE(fs.Rename("TOP", "TOP/L4/TOP", error));
    EXPECT_NE(error.find("into itself"), std::string::npos) << error;
    ASSERT_TRUE(fs.MakeCatalogue("WORK/L2/X3", {}, error)) << error;
    EXPECT_FALSE(fs.Rename("TOP", "WORK/L2/X3/TOP", error));  // TOP at 4, L6 at 7
    EXPECT_NE(error.find("levels"), std::string::npos) << error;
    ASSERT_TRUE(fs.Rename("TOP", "WORK/L2/TOP", error)) << error;  // back at 3: L6 at 6
    ASSERT_TRUE(fs.Find("WORK/L2/TOP/L4/L5/L6", l6, error)) << error;
    EXPECT_EQ(RawBlock(fs, l6.first)[16], 6);

    // Deleted as delete.com leaves it: the parent entry without bits 0 and 5 and the level byte, entry 0 without bit 0
    ASSERT_TRUE(fs.Find("WORK/L2/TOP/L4/L5", l5, error)) << error;
    EXPECT_FALSE(fs.Delete("WORK/L2/TOP/L4/L5", error));
    EXPECT_NE(error.find("not empty"), std::string::npos) << error;
    ASSERT_TRUE(fs.Delete("WORK/L2/TOP/L4/L5/L6", error)) << error;
    const std::vector<uint8_t> gone = EntryIn(fs, l5.first, "L6         ");
    EXPECT_EQ(gone[11], 0x00);
    EXPECT_EQ(gone[16], 0);
    EXPECT_EQ(RawBlock(fs, l6.first)[11] & 1, 0);
    for (const char* path : {"WORK/L2/TOP/L4/L5", "WORK/L2/TOP/L4", "WORK/L2/TOP", "WORK/L2/X3", "WORK/L2", "WORK/a.txt", "WORK"})
        ASSERT_TRUE(fs.Delete(path, error)) << path << ": " << error;
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free1, free0);
    const Tree after = Walk(fs);
    EXPECT_EQ(after.root, before.root);
    EXPECT_EQ(after.files, before.files);
    EXPECT_EQ(after.catalogues, before.catalogues);
}

/// TASiS mkdir.com /c /s<n>: a continuous catalogue is one run of n blocks with no segment table (status #61, +17 and
/// +19 its first block, as on the TASiS 5.40 floppy); /s sizes a segmented one too
TEST(IsdosFs_Test, ContinuousAndSizedCataloguesAsTasisMkdirMakesThem)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    const Tree before = Walk(fs);
    uint32_t free0 = 0, free1 = 0;
    ASSERT_TRUE(fs.FreeBlocks(free0));
    std::string error;
    IsdosFileSystem::WriteOptions options;
    options.date = 0x5B5A;
    options.continuous = true;
    options.blocks = 2;

    ASSERT_TRUE(fs.MakeCatalogue("CONT", options, error)) << error;
    IsdosFile cont;
    ASSERT_TRUE(fs.Find("CONT", cont, error)) << error;
    EXPECT_TRUE(cont.directory);
    EXPECT_EQ(cont.status, 0x61);
    EXPECT_EQ(cont.Attributes()[0], '.');
    EXPECT_EQ(cont.length, 512u);
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free0 - free1, 2u) << "no segment table";
    const std::vector<uint8_t> parentEntry = EntryIn(fs, fs.Boot().root, "CONT       ");
    EXPECT_EQ(Word(parentEntry, 17), cont.first);
    EXPECT_EQ(Word(parentEntry, 19), cont.first);
    const std::vector<uint8_t> head = RawBlock(fs, cont.first);
    EXPECT_EQ(std::memcmp(head.data(), "CONT       \x61", 12), 0);
    EXPECT_EQ(Word(head, 12), fs.Boot().root);
    EXPECT_EQ(Word(head, 14), 512);
    EXPECT_EQ(Word(head, 17), cont.first);
    EXPECT_EQ(head[16], 1);
    EXPECT_EQ(head[21], 1);
    EXPECT_EQ(Word(head, 30), 0x5B5A);
    EXPECT_EQ(RawBlock(fs, cont.first + 1), std::vector<uint8_t>(256, 0));

    // 15 files fill its 16 entries (entry 0 its own; growing is AFullContinuousCatalogueGrows)
    for (int k = 0; k < 15; k++)
        ASSERT_TRUE(
            fs.Write("CONT/f" + std::to_string(k) + ".bin", Pattern(10 + k, static_cast<uint8_t>(k)), {}, error))
            << k << ": " << error;
    for (int k = 0; k < 15; k++)
        EXPECT_EQ(ReadBack(fs, "CONT/f" + std::to_string(k) + ".bin"), Pattern(10 + k, static_cast<uint8_t>(k))) << k;
    EXPECT_EQ(RawBlock(fs, cont.first)[11], 0x61);
    EXPECT_EQ(RawBlock(fs, cont.first)[21], 16);

    // /s alone: a segmented catalogue of n blocks; 1-16 only
    IsdosFileSystem::WriteOptions sized;
    sized.blocks = 5;
    ASSERT_TRUE(fs.MakeCatalogue("BIG", sized, error)) << error;
    IsdosFile big;
    ASSERT_TRUE(fs.Find("BIG", big, error)) << error;
    EXPECT_EQ(big.status, 0x21);
    EXPECT_EQ(big.length, 1280u);
    const std::vector<uint8_t> t = RawBlock(fs, Word(EntryIn(fs, fs.Boot().root, "BIG        "), 17));
    EXPECT_EQ(t[0], 1);
    EXPECT_EQ(t[3], 5);
    sized.blocks = 17;
    EXPECT_FALSE(fs.MakeCatalogue("TOOBIG", sized, error));
    EXPECT_NE(error.find("1 to 16"), std::string::npos) << error;

    ASSERT_TRUE(fs.Delete("BIG", error)) << error;
    for (int k = 0; k < 15; k++)
        ASSERT_TRUE(fs.Delete("CONT/f" + std::to_string(k) + ".bin", error)) << error;
    ASSERT_TRUE(fs.Delete("CONT", error)) << error;
    ASSERT_TRUE(fs.FreeBlocks(free1));
    EXPECT_EQ(free1, free0);
    const Tree after = Walk(fs);
    EXPECT_EQ(after.files, before.files);
    EXPECT_EQ(after.catalogues, before.catalogues);
}

/// A Chic device (HDDNEMO): the level at +23, a catalogue of 3 blocks
TEST(IsdosFs_Test, ChicCatalogueOnAHardDiskDevice)
{
    std::vector<uint8_t> bytes = Bytes("loaders/isdos/HDDNEMO.img");
    IsdosFileSystem fs = IsdosFileSystem::OnBytes(bytes.data(), bytes.size());
    fs.SetWriter([&bytes](uint32_t block, const uint8_t* src) {
        std::memcpy(bytes.data() + block * 256u, src, 256);
        return true;
    });
    ASSERT_TRUE(fs.Boot().chic);
    std::string error;
    IsdosFile help;
    ASSERT_TRUE(fs.Find("HDD_NEMO/HELP", help, error)) << error;
    EXPECT_EQ(RawBlock(fs, help.first)[23], 2) << "the disk's own catalogues keep the level at +23";
    ASSERT_TRUE(fs.Delete("HDD_NEMO/HELP/ide+5.hlp", error)) << error;  // the device is full

    ASSERT_TRUE(fs.MakeCatalogue("HDD_NEMO/NEW", {}, error)) << error;
    IsdosFile made;
    ASSERT_TRUE(fs.Find("HDD_NEMO/NEW", made, error)) << error;
    EXPECT_EQ(made.length, 768u);
    std::vector<uint8_t> head = RawBlock(fs, made.first);
    EXPECT_EQ(head[16], 0);
    EXPECT_EQ(head[23], 2);
    ASSERT_TRUE(fs.Write("HDD_NEMO/NEW/x.bin", Pattern(100, 1), {}, error)) << error;
    ASSERT_TRUE(fs.Rename("HDD_NEMO/NEW", "/NEW", error)) << error;
    ASSERT_TRUE(fs.Find("NEW", made, error)) << error;
    head = RawBlock(fs, made.first);
    EXPECT_EQ(head[23], 1);
    EXPECT_EQ(ReadBack(fs, "NEW/x.bin"), Pattern(100, 1));
    Walk(fs);
}

/// A catalogue's attributes and time go into its parent entry and its own entry 0; the device name into block 0
TEST(IsdosFs_Test, AttributesTimeAndTitle)
{
    auto disk = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    std::string error;
    IsdosFile shell;
    ASSERT_TRUE(fs.Find("SHELL", shell, error)) << error;
    ASSERT_TRUE(fs.SetAttributes("SHELL", IsdosFileSystem::kHide | IsdosFileSystem::kDeleteProtect, error)) << error;
    ASSERT_TRUE(fs.SetTime("SHELL", 0x5B5A, 0x1234, error)) << error;
    const std::vector<uint8_t> outer = EntryIn(fs, fs.Boot().root, "SHELL      ");
    const std::vector<uint8_t> head = RawBlock(fs, shell.first);
    EXPECT_EQ(outer[11], 0x21 | 0x10 | 0x80);
    EXPECT_EQ(head[11], 0x21 | 0x10 | 0x80);
    EXPECT_EQ(Word(outer, 30), 0x5B5A);
    EXPECT_EQ(Word(head, 30), 0x5B5A);
    EXPECT_EQ(Word(head, 28), 0x1234);
    ASSERT_TRUE(fs.SetAttributes("SHELL", 0, error)) << error;
    EXPECT_EQ(RawBlock(fs, shell.first)[11], 0x21);

    ASSERT_TRUE(fs.SetTitle("work", error)) << error;
    EXPECT_EQ(fs.Boot().title, "work");
    EXPECT_EQ(std::memcmp(RawBlock(fs, 0).data() + 2, "work    ", 8), 0);
    EXPECT_EQ(IsdosFileSystem::OnDisk(*disk).Boot().title, "work");
    EXPECT_FALSE(fs.SetTitle("123456789", error));
    Walk(fs);
}

/// A full continuous catalogue grows (TASiS makes them; the kernel leaves them full): into the free block after it,
/// staying continuous, else it becomes segmented, its parent's entry following
TEST(IsdosFs_Test, AFullContinuousCatalogueGrows)
{
    auto disk = Load("loaders/isdos/tasis540.fdi");
    ASSERT_NE(disk, nullptr);
    IsdosFileSystem fs = IsdosFileSystem::OnDisk(*disk);
    const Tree before = Walk(fs);
    std::string error;
    std::vector<IsdosFile> help;
    ASSERT_TRUE(fs.List(help, nullptr, error)) << error;
    std::vector<std::string> files;
    for (const IsdosFile& f : help)
        if (f.path.rfind("SHELL/HELP/", 0) == 0 && !f.directory && f.name != "residenthlp" && files.size() < 17)
            files.push_back(f.path);
    ASSERT_EQ(files.size(), 17u);
    uint32_t room = 0;
    ASSERT_TRUE(fs.FreeBlocks(room));
    ASSERT_EQ(room, 0u) << "a full disk";

    // MY_GAMES: one block (143) of 8 entries, autoexec.bat in block 144 after it
    std::vector<uint8_t> head = RawBlock(fs, 143);
    ASSERT_EQ(head[11], 0x61);
    ASSERT_EQ(head[21], 1);
    ASSERT_TRUE(fs.Delete("autoexec.bat", error)) << error;
    for (size_t k = 0; k < 8; k++)
        ASSERT_TRUE(fs.Rename(files[k], "/MY_GAMES/" + files[k].substr(11), error)) << files[k] << ": " << error;
    head = RawBlock(fs, 143);
    EXPECT_EQ(head[11], 0x61) << "still continuous";
    EXPECT_EQ(Word(head, 14), 288);
    EXPECT_EQ(head[21], 9);
    EXPECT_EQ(std::memcmp(RawBlock(fs, 144).data(), files[7].substr(11).c_str(), 4), 0) << "entry 8 in block 144";

    // MY_DOCS/SOURCES: one block (140), SHELL/HELP/residenthlp in block 141; the 8th entry a catalogue of MY_DOCS
    // (the entry moved and the parent's entry of SOURCES in the same catalogue block). Becoming segmented needs two
    // free blocks: none until a file goes
    for (size_t k = 8; k < 15; k++)
        ASSERT_TRUE(fs.Rename(files[k], "/MY_DOCS/SOURCES/" + files[k].substr(11), error)) << files[k] << ": " << error;
    EXPECT_FALSE(fs.Rename("MY_DOCS/TEXTS", "/MY_DOCS/SOURCES/TEXTS", error));
    EXPECT_NE(error.find("cannot grow"), std::string::npos) << error;
    ASSERT_TRUE(fs.Delete(files[16], error)) << error;
    ASSERT_EQ(RawBlock(fs, 140)[21], 8);
    ASSERT_TRUE(fs.Rename("MY_DOCS/TEXTS", "/MY_DOCS/SOURCES/TEXTS", error)) << error;
    head = RawBlock(fs, 140);
    EXPECT_EQ(head[11], 0x21) << "segmented now";
    EXPECT_EQ(Word(head, 14), 288);
    EXPECT_EQ(Word(head, 19), 140);
    const uint16_t table = Word(head, 17);
    const std::vector<uint8_t> t = RawBlock(fs, table);
    ASSERT_EQ(t[0], 2);
    EXPECT_EQ(Word(t, 1), 140);
    EXPECT_EQ(t[3], 1);
    EXPECT_EQ(t[6], 1);
    const uint16_t grown = Word(t, 4);
    EXPECT_NE(grown, 141);
    const std::vector<uint8_t> outer = EntryIn(fs, 130, "SOURCES    ");
    EXPECT_EQ(outer[11], 0x21) << "the parent's entry says segmented";
    EXPECT_EQ(Word(outer, 17), table);
    EXPECT_EQ(Word(outer, 19), 140);
    IsdosFile texts;
    ASSERT_TRUE(fs.Find("MY_DOCS/SOURCES/TEXTS", texts, error)) << error;
    EXPECT_TRUE(texts.directory);

    // One more fits the grown block; everything reads, the free space counted
    ASSERT_TRUE(fs.Rename(files[15], "/MY_DOCS/SOURCES/" + files[15].substr(11), error)) << error;
    EXPECT_EQ(Word(RawBlock(fs, 140), 14), 320);
    const Tree after = Walk(fs);
    EXPECT_EQ(after.files, before.files - 2);
    EXPECT_EQ(after.catalogues, before.catalogues);
    std::vector<IsdosFile> sources;
    ASSERT_TRUE(fs.List(sources, nullptr, error)) << error;
    size_t inSources = 0;
    for (const IsdosFile& f : sources)
        inSources += f.path.rfind("MY_DOCS/SOURCES/", 0) == 0 && f.path.find('/', 16) == std::string::npos;
    EXPECT_EQ(inSources, 9u);
    EXPECT_TRUE(fs.Find("MY_GAMES/" + files[7].substr(11), texts, error)) << error;
}
