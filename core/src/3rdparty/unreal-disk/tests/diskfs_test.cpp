// DiskFs on its own: the volumes of floppies (TR-DOS, iS-DOS, FAT12 of a Sprinter DSS disk, +3DOS), an iS-DOS device on
// a hard disk at a cylinder boundary, reading with the TR-DOS sector slack, and writing, deleting, renaming on every file
// system (refused on a device that takes no writes)

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/fatfs.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"

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

/// A hard disk in memory
class MemoryDevice : public BlockDevice
{
public:
    explicit MemoryDevice(std::vector<uint8_t> bytes, bool writable = false) : _bytes(std::move(bytes)), _writable(writable) {}
    uint64_t SectorCount() const override { return _bytes.size() / kSectorSize; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        if (lba >= SectorCount())
            return false;
        std::memcpy(dst, _bytes.data() + lba * kSectorSize, kSectorSize);
        return true;
    }
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        if (!_writable || lba >= SectorCount())
            return false;
        std::memcpy(_bytes.data() + lba * kSectorSize, src, kSectorSize);
        return true;
    }
    bool IsWritable() const override { return _writable; }
    std::string Describe() const override { return "memory"; }
    uint64_t ContentId() const override { return 0; }

private:
    std::vector<uint8_t> _bytes;
    bool _writable = false;
};

std::vector<uint8_t> ReadFile(DiskFs& fs, const std::string& volume, const std::string& path)
{
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_TRUE(fs.Read(volume, path, data, error)) << path << ": " << error;
    return data;
}

bool Exists(DiskFs& fs, const std::string& path)
{
    DiskFsEntry entry;
    std::string error;
    return fs.Stat("", path, entry, error);
}
}  // namespace

TEST(DiskFs_Test, FloppyVolumes)
{
    struct Case
    {
        const char* image;
        const char* id;
        const char* system;
        bool writable;
    };
    for (const Case& c : {Case{"loaders/trd/EyeAche.trd", "trdos", "TR-DOS", true},
                          Case{"loaders/isdos/ISDOS50.FDI", "isdos", "iS-DOS Classic", true},
                          Case{"machines/sprinter/dss_1_62_92.img", "fat", "FAT12", true},
                          Case{"loaders/dsk/plus3-blank.dsk", "plus3", "+3DOS", true}})
    {
        auto disk = Load(c.image);
        ASSERT_NE(disk, nullptr);
        DiskFs fs(*disk);
        ASSERT_EQ(fs.Volumes().size(), 1u) << c.image;
        EXPECT_EQ(fs.Volumes()[0].id, c.id) << c.image;
        EXPECT_EQ(fs.Volumes()[0].system, c.system) << c.image;
        EXPECT_EQ(fs.Volumes()[0].writable, c.writable) << c.image;
    }
}

TEST(DiskFs_Test, IsdosDeviceAtACylinder)
{
    const std::vector<uint8_t> device = Bytes("loaders/isdos/HDDNEMO.img");
    ASSERT_EQ(device.size(), 32768u);
    const size_t cylinder = 16 * 63 * 512;
    std::vector<uint8_t> disk(4 * cylinder, 0);
    std::memcpy(disk.data() + 2 * cylinder, device.data(), device.size());
    MemoryDevice hdd(disk);
    DiskFs fs(hdd);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].id, "isdos-2");
    EXPECT_EQ(fs.Volumes()[0].offset, 2 * cylinder);

    std::vector<uint8_t> data;
    std::string error;
    ASSERT_TRUE(fs.Read("isdos-2", "HDD_NEMO/HELP/ide+5.hlp", data, error)) << error;
    EXPECT_EQ(data.size(), 2498u);

    MemoryDevice alone(device);
    DiskFs whole(alone);
    ASSERT_EQ(whole.Volumes().size(), 1u);
    EXPECT_EQ(whole.Volumes()[0].id, "isdos");
}

TEST(DiskFs_Test, TrdosWriteReadsBack)
{
    auto disk = std::make_unique<DiskImage>(uint8_t{80}, uint8_t{2});
    LoaderTRD trd({}, "");
    ASSERT_TRUE(trd.format(disk.get()));
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);

    const std::vector<uint8_t> code = {1, 2, 3, 4, 5};
    DiskFsWriteOptions options;
    options.start = 0x8000;
    std::string error;
    ASSERT_TRUE(fs.Write("", "HELLO.C", code, options, error)) << error;
    ASSERT_TRUE(fs.Write("trdos", "ODD.\\x01", code, {}, error)) << error;  // a type byte that is no letter

    std::vector<DiskFsEntry> entries;
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, "HELLO.C");
    EXPECT_EQ(entries[0].start, 0x8000u);
    EXPECT_EQ(entries[0].Field("type"), 'C');
    EXPECT_EQ(entries[1].name, "ODD.\\x01");
    EXPECT_EQ(entries[1].Field("type"), 1);

    std::vector<uint8_t> data, slack;
    ASSERT_TRUE(fs.Read("", "hello.c", data, error, &slack)) << error;
    EXPECT_EQ(data, code);
    EXPECT_EQ(slack.size(), 256u - code.size());  // the rest of the last sector

    EXPECT_FALSE(fs.Write("", "TOOLONGNAME.C", code, {}, error));
    EXPECT_FALSE(fs.Write("", "NOTYPE", code, {}, error));
}

TEST(DiskFs_Test, Plus3WriteAndReadOnlyRefusal)
{
    auto plus3 = Load("loaders/dsk/plus3-blank.dsk");
    ASSERT_NE(plus3, nullptr);
    DiskFs fs(*plus3);
    const std::vector<uint8_t> code(300, 0xA5);
    std::string error;
    ASSERT_TRUE(fs.Write("plus3", "PROG.BIN", code, {}, error)) << error;
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "PROG.BIN", data, error)) << error;
    EXPECT_EQ(data, code);

    // A hard disk that takes no writes: every volume on it is read only
    MemoryDevice hdd(Bytes("loaders/isdos/HDDNEMO.img"));
    DiskFs ro(hdd);
    ASSERT_EQ(ro.Volumes().size(), 1u);
    EXPECT_FALSE(ro.Volumes()[0].writable);
    EXPECT_FALSE(ro.Write("", "new.txt", code, {}, error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
    EXPECT_FALSE(ro.Delete("", "HDD_NEMO/HELP/ide+5.hlp", error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
}

TEST(DiskFs_Test, TrdosAndPlus3DeleteAndRename)
{
    auto disk = std::make_unique<DiskImage>(uint8_t{80}, uint8_t{2});
    LoaderTRD trd({}, "");
    ASSERT_TRUE(trd.format(disk.get()));
    DiskFs fs(*disk);
    const std::vector<uint8_t> code = {1, 2, 3};
    std::string error;
    ASSERT_TRUE(fs.Write("", "ONE.C", code, {}, error)) << error;
    ASSERT_TRUE(fs.Write("", "TWO.C", code, {}, error)) << error;
    ASSERT_TRUE(fs.Rename("", "ONE.C", "THREE.B", error)) << error;
    EXPECT_FALSE(Exists(fs, "ONE.C"));
    EXPECT_EQ(ReadFile(fs, "", "THREE.B"), code);
    ASSERT_TRUE(fs.Rename("", "THREE.B", "/FOUR.B", error)) << error;  // the root is the only directory
    ASSERT_TRUE(fs.Rename("", "FOUR.B", "THREE.B", error)) << error;
    EXPECT_FALSE(fs.Rename("", "THREE.B", "TWO.C", error));
    EXPECT_NE(error.find("already"), std::string::npos) << error;
    EXPECT_FALSE(fs.Rename("", "THREE.B", "\x01X.C", error));  // a first byte #01 would mark it deleted
    ASSERT_TRUE(fs.Delete("", "TWO.C", error)) << error;
    EXPECT_FALSE(Exists(fs, "TWO.C"));
    EXPECT_FALSE(fs.Delete("", "TWO.C", error));
    EXPECT_NE(error.find("no file"), std::string::npos) << error;

    auto plus3 = Load("loaders/dsk/plus3-blank.dsk");
    ASSERT_NE(plus3, nullptr);
    DiskFs p3(*plus3);
    ASSERT_TRUE(p3.Write("", "PROG.BIN", std::vector<uint8_t>(20000, 7), {}, error)) << error;  // two entries
    ASSERT_TRUE(p3.Rename("", "PROG.BIN", "GAME.COD", error)) << error;
    EXPECT_EQ(ReadFile(p3, "", "GAME.COD"), std::vector<uint8_t>(20000, 7));
    EXPECT_FALSE(Exists(p3, "PROG.BIN"));
    EXPECT_FALSE(p3.Rename("", "GAME.COD", "BAD*.COD", error));
    EXPECT_FALSE(p3.Write("", "TOOLONGNAME.BIN", {1}, {}, error));
    ASSERT_TRUE(p3.Delete("", "GAME.COD", error)) << error;
    EXPECT_FALSE(Exists(p3, "GAME.COD"));
}

TEST(DiskFs_Test, FatOnAFloppyAndAHardDisk)
{
    // The Sprinter DSS floppy, then the same bytes as a superfloppy hard disk
    auto floppy = Load("machines/sprinter/dss_1_62_92.img");
    ASSERT_NE(floppy, nullptr);
    MemoryDevice hdd(Bytes("machines/sprinter/dss_1_62_92.img"), true);
    std::unique_ptr<DiskFs> filesystems[] = {std::make_unique<DiskFs>(*floppy), std::make_unique<DiskFs>(hdd)};
    for (auto& fs : filesystems)
    {
        ASSERT_EQ(fs->Volumes().size(), 1u);
        ASSERT_TRUE(fs->Volumes()[0].writable);
        const int64_t free = fs->Volumes()[0].freeBytes;
        std::vector<DiskFsEntry> root;
        std::string error;
        ASSERT_TRUE(fs->List("", "", false, root, error)) << error;

        const std::vector<uint8_t> text(5000, 'x');
        DiskFsWriteOptions options;
        options.mtime = 1767225600;  // 2026-01-01 00:00
        ASSERT_TRUE(fs->Write("", "New Long Name.txt", text, options, error)) << error;
        EXPECT_EQ(ReadFile(*fs, "", "new long name.txt"), text);
        DiskFsEntry entry;
        ASSERT_TRUE(fs->Stat("", "New Long Name.txt", entry, error)) << error;
        EXPECT_EQ(entry.mtime, 1767225600);
        ASSERT_TRUE(fs->Write("", "New Long Name.txt", {1, 2}, {}, error)) << error;  // replaced
        EXPECT_EQ(ReadFile(*fs, "", "New Long Name.txt"), (std::vector<uint8_t>{1, 2}));

        // A directory of the disk: a bare new name stays there, a path moves the file
        std::string dir;
        for (const DiskFsEntry& e : root)
            if (e.directory)
                dir = e.path;
        ASSERT_FALSE(dir.empty());
        ASSERT_TRUE(fs->Rename("", "New Long Name.txt", dir + "/MOVED.TXT", error)) << error;
        EXPECT_FALSE(Exists(*fs, "New Long Name.txt"));
        ASSERT_TRUE(fs->Rename("", dir + "/MOVED.TXT", "AGAIN.TXT", error)) << error;
        EXPECT_EQ(ReadFile(*fs, "", dir + "/AGAIN.TXT"), (std::vector<uint8_t>{1, 2}));
        ASSERT_TRUE(fs->Rename("", dir + "/AGAIN.TXT", "/ROOT.TXT", error)) << error;  // "/": to the root
        EXPECT_EQ(ReadFile(*fs, "", "ROOT.TXT"), (std::vector<uint8_t>{1, 2}));
        ASSERT_TRUE(fs->Rename("", "ROOT.TXT", dir + "/AGAIN.TXT", error)) << error;
        EXPECT_FALSE(fs->Delete("", dir, error));  // not empty
        EXPECT_FALSE(fs->Write("", "NOWHERE/A.TXT", {1}, {}, error));
        EXPECT_FALSE(fs->Write("", dir, {1}, {}, error));
        EXPECT_NE(error.find("directory"), std::string::npos) << error;
        ASSERT_TRUE(fs->Delete("", dir + "/AGAIN.TXT", error)) << error;
        EXPECT_FALSE(Exists(*fs, dir + "/AGAIN.TXT"));
        EXPECT_FALSE(fs->Delete("", "NOTHING.TXT", error));
        EXPECT_NE(error.find("not found"), std::string::npos) << error;

        // The space is back; a file larger than the free space is refused before anything changes
        DiskFs again = fs.get() == filesystems[0].get() ? DiskFs(*floppy) : DiskFs(hdd);
        EXPECT_EQ(again.Volumes()[0].freeBytes, free);
        EXPECT_FALSE(again.Write("", "HUGE.BIN", std::vector<uint8_t>(static_cast<size_t>(free) + 1), {}, error));
        EXPECT_NE(error.find("free"), std::string::npos) << error;
        EXPECT_FALSE(Exists(again, "HUGE.BIN"));
    }
}

/// FAT directories: made (with the missing parents), moved with their contents (not into themselves), deleted with
/// everything in them; a read-only entry keeps the whole tree
TEST(DiskFs_Test, FatDirectories)
{
    MemoryDevice hdd(Bytes("machines/sprinter/dss_1_62_92.img"), true);
    DiskFs fs(hdd);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    const int64_t free = fs.Volumes()[0].freeBytes;
    std::string error;
    DiskFsEntry entry;

    ASSERT_TRUE(fs.MakeDirectory("", "Work", false, 1767225600, error)) << error;
    ASSERT_TRUE(fs.Stat("", "work", entry, error)) << error;
    EXPECT_TRUE(entry.directory);
    EXPECT_EQ(entry.mtime, 1767225600);
    EXPECT_FALSE(fs.MakeDirectory("", "Work", false, 0, error));
    EXPECT_NE(error.find("already"), std::string::npos) << error;
    EXPECT_FALSE(fs.MakeDirectory("", "NONE/SUB", false, 0, error));  // no parents
    ASSERT_TRUE(fs.MakeDirectory("", "Work/a/b/c", true, 0, error)) << error;
    ASSERT_TRUE(fs.MakeDirectory("", "Work/a/b/c", true, 0, error)) << error;  // there already: fine with parents
    ASSERT_TRUE(fs.Write("", "Work/a/b/c/deep.txt", {1, 2, 3}, {}, error)) << error;
    ASSERT_TRUE(fs.Write("", "Work/a/top.txt", {4}, {}, error)) << error;
    EXPECT_FALSE(fs.MakeDirectory("", "Work/a/top.txt/x", true, 0, error));
    EXPECT_NE(error.find("is a file"), std::string::npos) << error;

    // Moved with its contents; ".." follows (listing the moved tree works)
    ASSERT_TRUE(fs.MakeDirectory("", "Archive", false, 0, error)) << error;
    ASSERT_TRUE(fs.Rename("", "Work/a", "/Archive/a", error)) << error;
    EXPECT_FALSE(Exists(fs, "Work/a"));
    EXPECT_EQ(ReadFile(fs, "", "Archive/a/b/c/deep.txt"), (std::vector<uint8_t>{1, 2, 3}));
    std::vector<DiskFsEntry> tree;
    ASSERT_TRUE(fs.List("", "Archive", true, tree, error)) << error;
    EXPECT_EQ(tree.size(), 5u);  // a, a/b, a/b/c, a/b/c/deep.txt, a/top.txt
    EXPECT_FALSE(fs.Rename("", "Archive/a", "/Archive/a/b/a", error));
    EXPECT_NE(error.find("into itself"), std::string::npos) << error;
    ASSERT_TRUE(fs.Rename("", "Archive/a", "renamed", error)) << error;  // a bare name: in place
    ASSERT_TRUE(fs.Stat("", "Archive/renamed/b", entry, error)) << error;

    // Not empty: refused without recursive. A read-only file keeps the whole tree
    EXPECT_FALSE(fs.Delete("", "Archive", error));
    {
        FatFs raw(hdd);
        ASSERT_TRUE(raw.Open());
        FatNode node;
        ASSERT_TRUE(raw.Resolve("/Archive/renamed/top.txt", node));
        ASSERT_TRUE(raw.SetAttributes(node, FatNode::kReadOnly));
    }
    DiskFs again(hdd);
    EXPECT_FALSE(again.Delete("", "Archive", error, true));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
    EXPECT_TRUE(Exists(again, "Archive/renamed/b/c/deep.txt"));
    {
        FatFs raw(hdd);
        ASSERT_TRUE(raw.Open());
        FatNode node;
        ASSERT_TRUE(raw.Resolve("/Archive/renamed/top.txt", node));
        ASSERT_TRUE(raw.SetAttributes(node, 0));
    }
    DiskFs third(hdd);
    ASSERT_TRUE(third.Delete("", "Archive", error, true)) << error;
    ASSERT_TRUE(third.Delete("", "Work", error, true)) << error;
    EXPECT_FALSE(Exists(third, "Archive"));
    EXPECT_EQ(DiskFs(hdd).Volumes()[0].freeBytes, free);

    // +3DOS has no directories
    std::unique_ptr<DiskImage> plus3 = Load("loaders/dsk/plus3-blank.dsk");
    ASSERT_NE(plus3, nullptr);
    DiskFs flat(*plus3);
    EXPECT_FALSE(flat.MakeDirectory("", "DIR", false, 0, error));
    EXPECT_NE(error.find("no directories"), std::string::npos) << error;
}

TEST(DiskFs_Test, IsdosOnAWritableHardDisk)
{
    const std::vector<uint8_t> device = Bytes("loaders/isdos/HDDNEMO.img");
    const size_t cylinder = 16 * 63 * 512;
    std::vector<uint8_t> disk(2 * cylinder, 0);
    std::memcpy(disk.data() + cylinder, device.data(), device.size());
    MemoryDevice hdd(disk, true);
    DiskFs fs(hdd);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    ASSERT_TRUE(fs.Volumes()[0].writable);
    std::string error;
    // The device is full: a file deleted for the room. Blocks of 256 bytes in sectors of 512: the other half of
    // each sector stays
    const std::vector<uint8_t> help = ReadFile(fs, "", "HDD_NEMO/HELP/uni_con.hlp");
    ASSERT_TRUE(fs.Delete("", "HDD_NEMO/HELP/ide+5.hlp", error)) << error;
    ASSERT_TRUE(fs.Write("", "HDD_NEMO/odd.bin", std::vector<uint8_t>(300, 0x5A), {}, error)) << error;
    EXPECT_EQ(ReadFile(fs, "", "HDD_NEMO/odd.bin"), std::vector<uint8_t>(300, 0x5A));
    EXPECT_EQ(ReadFile(fs, "", "HDD_NEMO/HELP/uni_con.hlp"), help);
    DiskFs fresh(hdd);
    EXPECT_EQ(ReadFile(fresh, "", "HDD_NEMO/odd.bin"), std::vector<uint8_t>(300, 0x5A));
    ASSERT_TRUE(fresh.Delete("", "HDD_NEMO/odd.bin", error)) << error;
    EXPECT_FALSE(Exists(fresh, "HDD_NEMO/odd.bin"));
}
