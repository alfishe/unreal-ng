// MB-02: the BS-DOS 308 system disk (its directory tree, every body against the tape copies of the same archive), the
// raw image told from MGT by its boot sector, a FAT chain that loops; files and directories written; the BS-DOS 309
// virtual floppies of a #47 hard disk partition; an MB-01 (BS-DOS 52-105) disk as Busy describes it

#include "unrealdisk/mb02fs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_mb02.h"
#include "unrealdisk/mb01fs.h"

using namespace unrealdisk;
using unrealdisk::testing::TestDataPath;

namespace
{
std::vector<uint8_t> Bytes(const std::string& relative)
{
    std::ifstream in(TestDataPath(relative), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The bodies of a TAP in order (a data block after each header)
std::vector<std::vector<uint8_t>> TapBodies(const std::vector<uint8_t>& tap)
{
    std::vector<std::vector<uint8_t>> out;
    for (size_t i = 0; i + 2 <= tap.size();)
    {
        const size_t n = tap[i] | tap[i + 1] << 8;
        if (n >= 2 && i + 2 + n <= tap.size() && tap[i + 2] == 0xFF)
            out.emplace_back(tap.begin() + i + 3, tap.begin() + i + 1 + n);
        i += 2 + n;
    }
    return out;
}

/// A DD disk (80 x 2 x 5, 800 sectors): both FATs in LSN 2..5 (4 sectors), DIRS at 6, the root at 7, one
/// 1500-byte CODE file "two" at 8..9
std::vector<uint8_t> DdDisk()
{
    std::vector<uint8_t> disk(819200, 0);
    uint8_t* boot = disk.data();
    const uint8_t head[] = {0x18, 0x6E, 0x80, 0x02, 80, 0, 5,    0, 2, 0, 1, 0, 6, 0, 4, 0,
                            0x00, 0x10, 2,    0,    2,  0, 0x00, 0, 0, 0, 3, 3, 4, 4, 5, 5};
    std::memcpy(boot, head, sizeof(head));
    std::memcpy(boot + 0x26, "Tiny      ", 10);
    auto fat = [&](uint32_t lsn, uint16_t value) {
        disk[2 * 1024 + 2 * lsn] = static_cast<uint8_t>(value);
        disk[2 * 1024 + 2 * lsn + 1] = static_cast<uint8_t>(value >> 8);
    };
    fat(0, 0xFF00);
    fat(1, 0xFF00);
    fat(2, 0xC003);
    fat(3, 0xC004);
    fat(4, 0xC005);
    fat(5, 0x8400);
    fat(6, 0x8400);
    fat(7, 0x8400);
    fat(8, 0xC009);
    fat(9, 0x8000 | 476);
    disk[6 * 1024] = 0x80;
    disk[6 * 1024 + 2] = 7;
    uint8_t* root = disk.data() + 7 * 1024;
    root[0] = 0x80;
    std::memcpy(root + 6, "Root      ", 10);
    uint8_t* e = root + 32;
    e[0x00] = 0xB0;
    e[0x01] = 0x00;  // 12:00:00
    e[0x02] = 0x60;
    e[0x03] = 0x5B;  // 2025-10-27
    e[0x04] = 0x5B;
    e[0x05] = 3;
    std::memcpy(e + 6, "two       ", 10);
    e[0x10] = 0xDC;
    e[0x11] = 0x05;
    e[0x12] = 0x00;
    e[0x13] = 0x80;
    e[0x16] = 0x00;
    e[0x17] = 0x80;
    e[0x18] = 0xDC;
    e[0x19] = 0x05;
    e[0x1C] = 0xFF;
    e[0x1E] = 8;
    for (size_t k = 0; k < 1500; k++)
        disk[8 * 1024 + k] = static_cast<uint8_t>(k * 7);
    return disk;
}

class MemoryDevice : public BlockDevice
{
public:
    explicit MemoryDevice(std::vector<uint8_t> bytes, bool writable) : _bytes(std::move(bytes)), _writable(writable) {}
    uint64_t SectorCount() const override
    {
        return _bytes.size() / kSectorSize;
    }
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
    bool IsWritable() const override
    {
        return _writable;
    }
    std::string Describe() const override
    {
        return "memory";
    }
    uint64_t ContentId() const override
    {
        return 0;
    }
    const std::vector<uint8_t>& Data() const
    {
        return _bytes;
    }

private:
    std::vector<uint8_t> _bytes;
    bool _writable = false;
};

std::unique_ptr<DiskImage> Parse(const std::vector<uint8_t>& bytes, const std::string& name, std::string& format)
{
    std::unique_ptr<DiskImage> disk;
    const FormatResult result = FloppyFormats::LoadData({}, bytes.data(), bytes.size(), name, disk, format);
    std::string report;
    for (const std::string& line : result.report)
        report += " " + line;
    EXPECT_TRUE(result.Ok()) << name << ": " << result.message << report;
    return disk;
}
}  // namespace

TEST(Mb02Fs, SystemDiskTreeAndBodiesMatchItsTapes)
{
    const std::vector<uint8_t> bytes = Bytes("loaders/mb02/SYS308.MB2");
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(bytes, "SYS308.MB2", format);
    ASSERT_TRUE(disk);
    EXPECT_EQ(format, "mb02");
    EXPECT_EQ(disk->getCylinders(), 82);

    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    const DiskFsVolume& v = fs.Volumes()[0];
    EXPECT_EQ(v.id, "mb02");
    EXPECT_EQ(v.system, "MB-02 BS-DOS");
    EXPECT_EQ(v.label, "System 308");
    EXPECT_EQ(v.bytes, 1847296u);
    EXPECT_EQ(v.freeBytes, 1586 * 1024);  // SYS308.LST: free 1 624 064
    EXPECT_TRUE(v.writable);
    EXPECT_TRUE(v.times);
    EXPECT_TRUE(v.labels);

    std::vector<DiskFsEntry> root;
    std::string error;
    ASSERT_TRUE(fs.List("", "", false, root, error)) << error;
    ASSERT_EQ(root.size(), 31u);  // 29 files, then "To ramdisk" and "Small demo"
    EXPECT_EQ(root[0].name, "Masterboot");
    EXPECT_EQ(root[0].start, 25600u);
    EXPECT_EQ(root[0].size, 2048u);
    EXPECT_EQ(root[0].Field("type"), 3);
    EXPECT_EQ(root[3].name, "fi");
    EXPECT_EQ(root[3].Field("type"), 0);
    EXPECT_EQ(root[3].Field("param2"), 1514);
    EXPECT_EQ(root[7].name, "tma");
    EXPECT_EQ(root[9].name, "tma~10");  // the second "tma" of the directory
    EXPECT_EQ(root[9].Field("address"), 22528);
    EXPECT_TRUE(root[29].directory);
    EXPECT_EQ(root[29].name, "To ramdisk");
    EXPECT_EQ(root[29].Field("dir"), 1);
    EXPECT_EQ(root[30].name, "Small demo");

    // Every body of directories 0 and 2 against the tapes
    const std::vector<std::vector<uint8_t>> sysdir = TapBodies(Bytes("loaders/mb02/SYSDIR.TAP"));
    ASSERT_EQ(sysdir.size(), 29u);
    for (size_t k = 0; k < 29; k++)
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE(fs.Read("", root[k].path, data, error)) << root[k].path << ": " << error;
        EXPECT_EQ(data, sysdir[k]) << root[k].path;
    }
    std::vector<DiskFsEntry> demo;
    ASSERT_TRUE(fs.List("", "small demo", false, demo, error)) << error;
    const std::vector<std::vector<uint8_t>> mbdemo = TapBodies(Bytes("loaders/mb02/MBDEMO.TAP"));
    ASSERT_EQ(demo.size(), 8u);
    ASSERT_EQ(mbdemo.size(), 8u);
    EXPECT_EQ(demo[7].path, "Small demo/cdebusy");
    for (size_t k = 0; k < 8; k++)
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE(fs.Read("", demo[k].path, data, error)) << demo[k].path << ": " << error;
        EXPECT_EQ(data, mbdemo[k]) << demo[k].path;
    }

    std::vector<DiskFsEntry> all;
    ASSERT_TRUE(fs.List("", "", true, all, error)) << error;
    EXPECT_EQ(all.size(), 29u + 22u + 8u + 2u);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "To ramdisk/tma~8", data, error)) << error;
    EXPECT_EQ(data, sysdir[9]);

    // The raw image writes back byte for byte
    std::vector<uint8_t> out;
    std::vector<std::string> warnings;
    ASSERT_TRUE(LoaderMB02({}, "").serialize(disk.get(), out, warnings));
    EXPECT_TRUE(out == bytes);
}

namespace
{
/// An MB-01 DD disk (80 x 2 x 5) as mb02form.htm lays it out, built here (no MB-01 disk survives): the boot sector
/// at id 2 (LSN 1; id 1 bad), its backup, FAT1 at 3-4 chained in itself, FAT2 at 5-6, the directory list at 7, the
/// file list at 8; "hello" (1500 bytes over 9, 10) at the root, "game" in directory 1 "Games", a deleted entry and a
/// headerless body
std::unique_ptr<DiskImage> Mb01Disk(bool mark = true)
{
    std::vector<uint8_t> lsn(800 * 1024, 0);
    auto sector = [&](uint32_t n) { return lsn.data() + n * 1024; };
    auto word = [](uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    };
    uint8_t* boot = sector(1);
    boot[0] = 0x18;
    boot[2] = 0x80;
    boot[3] = mark ? 0x01 : 0x02;
    std::memcpy(boot + 4, "Busy MB-01", 10);
    boot[14] = 80;
    boot[15] = 5;
    word(boot + 16, 0x6000);  // 12:00:00
    word(boot + 18, 0x5B5B);  // 2025-10-27
    boot[24] = 7;
    boot[25] = 8;
    boot[26] = 2;
    boot[27] = 3;
    boot[31] = 5;
    std::memcpy(sector(2), boot, 1024);
    for (uint32_t copy : {3u, 5u})
    {
        auto fat = [&](uint32_t n, uint16_t v) { word(sector(copy) + 2 * n, v); };
        fat(0, 0x2000);  // bad
        for (uint32_t n : {1u, 2u, 4u, 6u, 7u, 8u})
            fat(n, 0x5400);  // system, the last of its chain, 1024 bytes
        fat(3, 0x1004);
        fat(5, 0x1006);
        fat(9, 10);
        fat(10, 0x4000 | 476);
        fat(11, 0x4000 | 100);
        fat(12, 0x4000 | 10);
    }
    // The FAT's second sector holds LSN 512..799: all free here, so it stays zero
    uint8_t* dir = sector(7) + 16;
    word(dir + 1, 0x6000);
    word(dir + 3, 0x5B5B);
    dir[5] = 1;
    std::memcpy(dir + 6, "Games     ", 10);
    auto file = [&](uint32_t entry, uint8_t directory, uint8_t id, const char* name, uint32_t length, uint16_t first) {
        uint8_t* e = sector(8) + entry * 32;
        e[0] = directory;
        word(e + 1, 0x6000);
        word(e + 3, 0x5B5B);
        e[5] = id;
        std::memcpy(e + 6, name, 10);
        word(e + 16, length);
        word(e + 18, 0x8000);
        word(e + 22, 0x8000);
        word(e + 24, length);
        e[26] = static_cast<uint8_t>(length >> 16);
        e[27] = 0xFF;
        word(e + 30, first);
    };
    file(0, 0, 0x63, "hello     ", 1500, 9);
    file(1, 1, 0x63, "game      ", 100, 11);
    file(2, 0, 0xE3, "old       ", 100, 11);
    file(3, 0, 0x44, "          ", 10, 12);
    for (uint32_t k = 0; k < 1500; k++)
        lsn[9 * 1024 + k] = static_cast<uint8_t>(k * 7);
    std::memset(sector(11), 0x47, 100);
    std::memset(sector(12), 0x48, 10);

    auto disk = std::make_unique<DiskImage>(uint8_t{80}, uint8_t{2});
    const DiskImage::TrackFormatSpec spec = LoaderMB02::trackSpec(5);
    const uint8_t* src = lsn.data();
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
        for (uint8_t side = 0; side < 2; side++)
        {
            DiskImage::Track* track = disk->getTrackForCylinderAndSide(cylinder, side);
            track->formatTrack(cylinder, side, spec);
            for (uint8_t index = 0; index < 5; index++, src += 1024)
            {
                DiskImage::Sector* s = track->getSector(index);
                std::memcpy(s->data, src, 1024);
                s->recalculateDataCRC();
            }
        }
    return disk;
}
}  // namespace

TEST(Mb01Fs, ABusyDescribedDiskReads)
{
    std::unique_ptr<DiskImage> disk = Mb01Disk();
    Mb01FileSystem mb01 = Mb01FileSystem::OnDisk(*disk);
    std::string why;
    ASSERT_TRUE(mb01.Open(&why)) << why;
    EXPECT_EQ(mb01.Boot().lsn, 1u);
    EXPECT_EQ(mb01.Boot().Sectors(), 800u);

    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    const DiskFsVolume& v = fs.Volumes()[0];
    EXPECT_EQ(v.id, "mb01");
    EXPECT_EQ(v.system, "MB-01 BS-DOS");
    EXPECT_EQ(v.label, "Busy MB-01");
    EXPECT_FALSE(v.writable);
    EXPECT_EQ(v.freeBytes, (800 - 13) * 1024);
    std::vector<DiskFsEntry> all;
    std::string error;
    ASSERT_TRUE(fs.List("", "", true, all, error)) << error;
    std::vector<std::string> paths;
    for (const DiskFsEntry& e : all)
        paths.push_back(e.path);
    EXPECT_EQ(paths, (std::vector<std::string>{"hello", "~3", "Games", "Games/game"}));
    EXPECT_EQ(all[0].size, 1500u);
    EXPECT_EQ(all[0].start, 0x8000u);
    EXPECT_EQ(all[0].mtime, 1761566400);  // 2025-10-27 12:00:00 UTC
    EXPECT_TRUE(all[2].directory);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "hello", data, error)) << error;
    ASSERT_EQ(data.size(), 1500u);
    EXPECT_EQ(data[1499], static_cast<uint8_t>(1499 * 7));
    ASSERT_TRUE(fs.Read("", "Games/game", data, error)) << error;
    EXPECT_EQ(data, std::vector<uint8_t>(100, 0x47));
    ASSERT_TRUE(fs.Read("", "~3", data, error)) << error;
    EXPECT_EQ(data, std::vector<uint8_t>(10, 0x48));
    EXPECT_FALSE(fs.Write("", "new", {1}, {}, error));

    // Without the MB-01 mark: none
    std::unique_ptr<DiskImage> other = Mb01Disk(false);
    EXPECT_FALSE(Mb01FileSystem::OnDisk(*other).Open());
}

TEST(Mb02Fs, DdDiskIsNoMgtAndKeepsItsDate)
{
    const std::vector<uint8_t> bytes = DdDisk();
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(bytes, "tiny.mgt", format);
    ASSERT_TRUE(disk);
    EXPECT_EQ(format, "mb02");
    DiskImage::Track* track = disk->getTrackForCylinderAndSide(0, 0);
    ASSERT_TRUE(track);
    EXPECT_EQ(track->sectorCount(), 5u);

    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].label, "Tiny");
    EXPECT_EQ(fs.Volumes()[0].freeBytes, (800 - 10) * 1024);
    std::vector<DiskFsEntry> root;
    std::string error;
    ASSERT_TRUE(fs.List("", "", false, root, error)) << error;
    ASSERT_EQ(root.size(), 1u);
    EXPECT_EQ(root[0].name, "two");
    EXPECT_EQ(root[0].start, 0x8000u);
    EXPECT_EQ(root[0].mtime, 1761566400);  // 2025-10-27 12:00:00 UTC
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "two", data, error)) << error;
    ASSERT_EQ(data.size(), 1500u);
    EXPECT_EQ(data[1499], static_cast<uint8_t>(1499 * 7));

    // Without the MB-02 marks the same size is an MGT disk again
    std::vector<uint8_t> plain = bytes;
    plain[3] = 0;
    EXPECT_EQ(FloppyFormats::ProbeData(plain.data(), plain.size(), "tiny.mgt"), "mgt");
}

TEST(Mb02Fs, LoopingChainFailsAndDamagedBootIsRefused)
{
    std::vector<uint8_t> bytes = DdDisk();
    bytes[2 * 1024 + 2 * 9] = 0x08;  // sector 9 leads back to 8
    bytes[2 * 1024 + 2 * 9 + 1] = 0xC0;
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(bytes, "loop.mb2", format);
    ASSERT_TRUE(disk);
    DiskFs fs(*disk);
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_FALSE(fs.Read("", "two", data, error));
    EXPECT_NE(error.find("loops"), std::string::npos) << error;

    // A FAT that does not reserve the backup boot sector: no MB-02 volume
    std::vector<uint8_t> broken = DdDisk();
    broken[2 * 1024 + 2] = 0;
    broken[2 * 1024 + 3] = 0;
    disk = Parse(broken, "broken.mb2", format);
    ASSERT_TRUE(disk);
    Mb02FileSystem mb02 = Mb02FileSystem::OnDisk(*disk);
    std::string why;
    EXPECT_FALSE(mb02.Open(&why));
    EXPECT_NE(why.find("backup"), std::string::npos) << why;
}

namespace
{
std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> data(size);
    for (size_t k = 0; k < size; k++)
        data[k] = static_cast<uint8_t>(k * 13 + seed + (k >> 10));
    return data;
}
}  // namespace

TEST(Mb02Fs, FilesAndDirectoriesWritten)
{
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(Bytes("loaders/mb02/SYS308.MB2"), "SYS308.MB2", format);
    ASSERT_TRUE(disk);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    std::string error;

    // A file of two sectors and a bit in "Small demo": a CODE header, the start as the body address
    const std::vector<uint8_t> data = Pattern(2 * 1024 + 300, 1);
    DiskFsWriteOptions options;
    options.start = 0x8000;
    options.mtime = 1262347200;  // 2010-01-01 12:00
    ASSERT_TRUE(fs.Write("", "Small demo/hello", data, options, error)) << error;
    std::vector<uint8_t> read;
    ASSERT_TRUE(fs.Read("", "Small demo/hello", read, error)) << error;
    EXPECT_EQ(read, data);
    DiskFsEntry entry;
    ASSERT_TRUE(fs.Stat("", "Small demo/hello", entry, error)) << error;
    EXPECT_EQ(entry.start, 0x8000u);
    EXPECT_EQ(entry.Field("type"), 3);
    EXPECT_EQ(entry.Field("address"), 0x8000);
    EXPECT_EQ(entry.Field("entry"), 9);
    EXPECT_EQ(entry.mtime, 1262347200);
    EXPECT_EQ(entry.Field("flags"), 0xB0);

    // Written over in the same entry; an empty file has no body
    const std::vector<uint8_t> shorter = Pattern(1024, 2);
    ASSERT_TRUE(fs.Write("", "Small demo/hello", shorter, options, error)) << error;
    ASSERT_TRUE(fs.Read("", "Small demo/hello", read, error)) << error;
    EXPECT_EQ(read, shorter);
    ASSERT_TRUE(fs.Stat("", "Small demo/hello", entry, error)) << error;
    EXPECT_EQ(entry.Field("entry"), 9);
    ASSERT_TRUE(fs.Write("", "empty", {}, {}, error)) << error;
    ASSERT_TRUE(fs.Stat("", "empty", entry, error)) << error;
    EXPECT_EQ(entry.Field("flags"), 0x90);

    // A program as BS-DOS saves one (autoexe): its autostart line, its length as param 2, the body at 23755
    DiskFsWriteOptions program;
    program.basicType = 0;
    program.start = 10;
    ASSERT_TRUE(fs.Write("", "run", Pattern(138, 6), program, error)) << error;
    ASSERT_TRUE(fs.Stat("", "run", entry, error)) << error;
    EXPECT_EQ(entry.Field("type"), 0);
    EXPECT_EQ(entry.start, 10u);
    EXPECT_EQ(entry.Field("param2"), 138);
    EXPECT_EQ(entry.Field("address"), 23755);
    program.basicType = 1;  // where BS-DOS loads an array is not known
    EXPECT_FALSE(fs.Write("", "array", {1}, program, error));
    EXPECT_NE(error.find("a program (0), CODE (3)"), std::string::npos) << error;
    program.attributes = "R";
    program.basicType = 3;
    EXPECT_FALSE(fs.Write("", "array", {1}, program, error));
    EXPECT_NE(error.find("no file attributes"), std::string::npos) << error;
    ASSERT_TRUE(fs.Delete("", "run", error)) << error;
    EXPECT_EQ(entry.mtime, 0);

    // The root holds 31 entries a sector: the second file grows it by one
    ASSERT_TRUE(fs.Write("", "more", Pattern(10, 3), {}, error)) << error;
    ASSERT_TRUE(fs.Write("", "grown", Pattern(10, 4), {}, error)) << error;
    ASSERT_TRUE(fs.Stat("", "grown", entry, error)) << error;
    EXPECT_EQ(entry.Field("entry"), 32);

    // A directory: made, a file moved into it, renamed, refused while not empty, then gone
    ASSERT_TRUE(fs.MakeDirectory("", "Small demo/Sub", false, 1262347200, error)) << error;
    ASSERT_TRUE(fs.Stat("", "Small demo/Sub", entry, error)) << error;
    EXPECT_TRUE(entry.directory);
    EXPECT_EQ(entry.Field("dir"), 3);
    EXPECT_EQ(entry.mtime, 1262347200);
    ASSERT_TRUE(fs.Rename("", "more", "/Small demo/Sub/moved", error)) << error;
    ASSERT_TRUE(fs.Read("", "Small demo/Sub/moved", read, error)) << error;
    EXPECT_EQ(read, Pattern(10, 3));
    EXPECT_FALSE(fs.Stat("", "more", entry, error));
    ASSERT_TRUE(fs.Rename("", "Small demo/Sub", "/Top", error)) << error;
    ASSERT_TRUE(fs.Read("", "Top/moved", read, error)) << error;
    EXPECT_FALSE(fs.Rename("", "Top", "/Top/Inner", error));  // not into itself
    EXPECT_FALSE(fs.Delete("", "Top", error));
    EXPECT_NE(error.find("not empty"), std::string::npos) << error;
    ASSERT_TRUE(fs.SetTime("", "Top/moved", 1262347200, error)) << error;
    ASSERT_TRUE(fs.Stat("", "Top/moved", entry, error)) << error;
    EXPECT_EQ(entry.mtime, 1262347200);
    ASSERT_TRUE(fs.Delete("", "Top", error, true)) << error;
    EXPECT_FALSE(fs.Stat("", "Top", entry, error));

    // Too big, bad names
    EXPECT_FALSE(fs.Write("", "huge", Pattern(1600 * 1024, 5), {}, error));
    EXPECT_NE(error.find("not enough free sectors"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("", "elevenchars", data, {}, error));
    EXPECT_FALSE(fs.Write("", "No dir/x", data, {}, error));

    ASSERT_TRUE(fs.SetLabel("", "Renamed", 0, error)) << error;
    EXPECT_EQ(fs.Volumes()[0].label, "Renamed");
    for (const char* name : {"Small demo/hello", "empty", "grown"})
        ASSERT_TRUE(fs.Delete("", name, error)) << name << ": " << error;

    // The saved image: both FAT copies alike, every file of the original there, the sectors free again but the
    // root's second one
    std::vector<uint8_t> out;
    std::vector<std::string> warnings;
    ASSERT_TRUE(LoaderMB02({}, "").serialize(disk.get(), out, warnings));
    EXPECT_TRUE(std::equal(out.begin() + 2 * 1024, out.begin() + 6 * 1024, out.begin() + 6 * 1024));
    std::unique_ptr<DiskImage> again = Parse(out, "again.mb2", format);
    ASSERT_TRUE(again);
    DiskFs reread(*again);
    ASSERT_EQ(reread.Volumes().size(), 1u);
    EXPECT_EQ(reread.Volumes()[0].label, "Renamed");
    EXPECT_EQ(reread.Volumes()[0].freeBytes, (1586 - 1) * 1024);
    std::vector<DiskFsEntry> all;
    ASSERT_TRUE(reread.List("", "", true, all, error)) << error;
    EXPECT_EQ(all.size(), 29u + 22u + 8u + 2u);
}

TEST(Mb02Fs, Bsdos309FloppiesOfTheHardDiskPartition)
{
    // A #47 partition at LBA 64 of three 2 MiB floppies and a bit: @1 SYS308.MB2 (11 sectors a track, as it is),
    // @2 the DD disk of 5 sectors a track placed as the BIOS places it (11 a track), @3 never formatted
    constexpr size_t kDisc = 4096 * 512;
    const std::vector<uint8_t> sys = Bytes("loaders/mb02/SYS308.MB2");
    ASSERT_EQ(sys.size(), 1847296u);
    std::vector<uint8_t> disk((64 + 3 * 4096 + 100) * 512, 0);
    disk[510] = 0x55;
    disk[511] = 0xAA;
    uint8_t* entry = disk.data() + 446 + 16;  // the second slot: the BIOS takes the first #47 one
    entry[4] = 0x47;
    entry[8] = 64;
    const uint32_t count = 3 * 4096 + 100;
    entry[12] = static_cast<uint8_t>(count);
    entry[13] = static_cast<uint8_t>(count >> 8);
    const size_t base = 64 * 512;
    std::memcpy(disk.data() + base, sys.data(), sys.size());
    const std::vector<uint8_t> dd = DdDisk();
    for (uint32_t lsn = 0; lsn < 800; lsn++)
    {
        const uint32_t track = lsn / 10, side = lsn / 5 % 2, sector = lsn % 5;
        std::memcpy(disk.data() + base + kDisc + ((track * 2 + side) * 11 + sector) * 1024, dd.data() + lsn * 1024,
                    1024);
    }

    MemoryDevice hdd(disk, true);
    std::string error;
    {
        DiskFs fs(hdd);
        ASSERT_EQ(fs.Volumes().size(), 2u);
        EXPECT_EQ(fs.Volumes()[0].id, "mb02-1");
        EXPECT_EQ(fs.Volumes()[0].label, "System 308");
        EXPECT_EQ(fs.Volumes()[0].offset, base);
        EXPECT_EQ(fs.Volumes()[0].freeBytes, 1586 * 1024);
        EXPECT_TRUE(fs.Volumes()[0].writable);
        EXPECT_EQ(fs.Volumes()[1].id, "mb02-2");
        EXPECT_EQ(fs.Volumes()[1].label, "Tiny");
        EXPECT_EQ(fs.Volumes()[1].offset, base + kDisc);
        std::vector<uint8_t> data;
        ASSERT_TRUE(fs.Read("mb02-2", "two", data, error)) << error;
        ASSERT_EQ(data.size(), 1500u);
        EXPECT_EQ(data[1499], static_cast<uint8_t>(1499 * 7));
        ASSERT_TRUE(fs.Read("mb02-1", "Small demo/cdebusy", data, error)) << error;
        EXPECT_EQ(data.size(), 768u);
        ASSERT_TRUE(fs.Write("mb02-2", "three", std::vector<uint8_t>(3000, 0x33), {}, error)) << error;
    }
    // Written into @2 only, at the BIOS's places
    DiskFs fs(hdd);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("mb02-2", "three", data, error)) << error;
    EXPECT_EQ(data, std::vector<uint8_t>(3000, 0x33));
    EXPECT_TRUE(std::equal(disk.begin(), disk.begin() + base + kDisc, hdd.Data().begin()));
    EXPECT_TRUE(std::equal(disk.begin() + base + 2 * kDisc, disk.end(), hdd.Data().begin() + base + 2 * kDisc));

    // Not writable on a device that takes no writes; no floppies without the #47 partition
    MemoryDevice locked(disk, false);
    DiskFs readOnly(locked);
    ASSERT_EQ(readOnly.Volumes().size(), 2u);
    EXPECT_FALSE(readOnly.Volumes()[0].writable);
    entry[4] = 0x0B;
    MemoryDevice other(disk, false);
    DiskFs none(other);
    for (const DiskFsVolume& v : none.Volumes())
        EXPECT_NE(v.fs, "mb02");
}
