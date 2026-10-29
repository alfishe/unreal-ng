// ATM CP/M: the floppies (a directory at #2000, an empty TR-DOS catalogue in cylinder 0 is no volume then), a hard disk
// whose configuration record and .CDS descriptor give two partitions, 8-bit block numbers, Scorpion MOA CP/M refused;
// files written, deleted, renamed and their attributes set on a floppy, a partition and 8-bit block numbers

#include "unrealdisk/cpmfs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_pro.h"

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

uint32_t Crc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = ~0u;
    for (const uint8_t b : data)
    {
        crc ^= b;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

std::vector<uint8_t> ReadFile(DiskFs& fs, const std::string& volume, const std::string& path)
{
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_TRUE(fs.Read(volume, path, data, error)) << path << ": " << error;
    return data;
}

class MemoryDevice : public BlockDevice
{
public:
    explicit MemoryDevice(std::vector<uint8_t> bytes, bool writable = false)
        : _bytes(std::move(bytes)), _writable(writable)
    {
    }
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
    const std::vector<uint8_t>& Data() const
    {
        return _bytes;
    }
    std::string Describe() const override
    {
        return "memory";
    }
    uint64_t ContentId() const override
    {
        return 0;
    }

private:
    std::vector<uint8_t> _bytes;
    bool _writable = false;
};

std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> out(size);
    for (size_t i = 0; i < size; i++)
        out[i] = static_cast<uint8_t>(i * 7 + seed + (i >> 8));
    return out;
}

/// A directory entry: user 0, NAME.TYP, extent 0, `records`, 16-bit block numbers
void PutEntry(uint8_t* e, const char* name11, uint8_t records, std::initializer_list<uint16_t> blocks)
{
    std::memset(e, 0, 32);
    std::memcpy(e + 1, name11, 11);
    e[15] = records;
    int k = 0;
    for (const uint16_t b : blocks)
    {
        e[16 + 2 * k] = static_cast<uint8_t>(b);
        e[17 + 2 * k] = static_cast<uint8_t>(b >> 8);
        k++;
    }
}
}  // namespace

TEST(CpmFs_Test, AtmFloppiesListAndRead)
{
    auto prince = Load("machines/atm/cpm/prince.trd");
    ASSERT_NE(prince, nullptr);
    DiskFs fs(*prince);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].id, "cpm");
    EXPECT_EQ(fs.Volumes()[0].system, "ATM CP/M");
    EXPECT_TRUE(fs.Volumes()[0].writable);
    EXPECT_EQ(fs.Volumes()[0].attributes, "RSA");
    EXPECT_EQ(fs.Volumes()[0].bytes, 655360u);

    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("", "", true, entries, error)) << error;
    ASSERT_EQ(entries.size(), 44u);  // 42 files of user 0, the directory USER15 and its CONFIG.DAT
    EXPECT_EQ(entries[0].path, "TITLE1.DAT");
    EXPECT_EQ(entries[0].size, 39936u);
    EXPECT_EQ(entries[0].Field("extents"), 3);
    EXPECT_EQ(entries[42].path, "USER15");
    EXPECT_TRUE(entries[42].directory);
    EXPECT_EQ(entries[43].path, "USER15/CONFIG.DAT");
    EXPECT_EQ(entries[43].attributes, "RS.");

    // A file of three extents; a text of one; the same name in user 15
    EXPECT_EQ(Crc32(ReadFile(fs, "", "TITLE1.DAT")), 0x5C771324u);
    const std::vector<uint8_t> doc = ReadFile(fs, "", "prince.doc");
    ASSERT_EQ(doc.size(), 11904u);
    EXPECT_EQ(Crc32(doc), 0x8FD3694Bu);
    EXPECT_EQ(std::string(doc.begin() + 7, doc.begin() + 23), "Prince of Persia");
    EXPECT_EQ(Crc32(ReadFile(fs, "cpm", "USER15/CONFIG.DAT")), 0x6E17B5DEu);
    std::vector<uint8_t> data;
    EXPECT_FALSE(fs.Read("", "CONFIG.DAT", data, error));

    // The system disk keeps an empty TR-DOS catalogue in cylinder 0: only the CP/M volume
    auto sys = Load("machines/atm450/cpm/sys.trd");
    ASSERT_NE(sys, nullptr);
    DiskFs system(*sys);
    ASSERT_EQ(system.Volumes().size(), 1u);
    EXPECT_EQ(system.Volumes()[0].id, "cpm");
    EXPECT_EQ(Crc32(ReadFile(system, "", "DOC.COM")), 0xBF5555B3u);
    entries.clear();
    ASSERT_TRUE(system.List("", "", true, entries, error)) << error;
    EXPECT_EQ(entries.size(), 65u);
}

TEST(CpmFs_Test, OtherDisksAreNoAtmCpm)
{
    // Scorpion MOA CP/M has another layout; a TR-DOS disk has file data at #2000
    for (const char* image : {"machines/scorpion/cpm/moa-cpm-disk01.trd", "loaders/trd/EyeAche.trd"})
    {
        auto disk = Load(image);
        ASSERT_NE(disk, nullptr);
        std::string why;
        EXPECT_FALSE(CpmFileSystem::AtmFloppy(*disk).IsCpm(&why)) << image;
        EXPECT_FALSE(why.empty());
        DiskFs fs(*disk);
        for (const DiskFsVolume& v : fs.Volumes())
            EXPECT_NE(v.fs, "cpm") << image;
    }
}

TEST(CpmFs_Test, HardDiskPartitionsFromTheConfigurationRecordAndCdsFiles)
{
    // 16 heads x 63 sectors, 5 cylinders: D: at cylinder 1 (2 cylinders, sys.trd's files), E: at cylinder 3 from
    // D:HDDE.CDS (prince.trd's files)
    constexpr size_t kCylinder = 16 * 63 * 512;
    std::vector<uint8_t> disk(5 * kCylinder, 0xE5);
    uint8_t* record = disk.data();
    std::memset(record, 0, 512);
    static const uint8_t head[] = {0xC9, 0x00, 0x00, 0x55, 0xAA};
    std::memcpy(record, head, sizeof(head));
    // ASSIGN D: channel 3: DTYP 4, DUS 0, DDTYP 64, 16 heads, 0, 2 cylinders, 63 sectors, 512 bytes, 1 reserved track,
    // start cylinder 1, 8064 records, 252 records a track, 0, 0, 128 entries
    static const uint8_t assign[] = {0x03, 26, 3, 3, 4,    0,    64,  16, 0, 2, 0, 63,  0x00,
                                     0x02, 1,  1, 0, 0x80, 0x1F, 252, 0,  0, 0, 0, 128, 0};
    std::memcpy(record + 5, assign, sizeof(assign));
    static const char go[] =
        "\x06\x11"
        "D:@ D:AUTOEXEC";
    std::memcpy(record + 5 + sizeof(assign), go, 16);

    const std::vector<uint8_t> sys = Bytes("machines/atm450/cpm/sys.trd");
    const std::vector<uint8_t> prince = Bytes("machines/atm/cpm/prince.trd");
    ASSERT_EQ(sys.size(), 655360u);
    ASSERT_EQ(prince.size(), 655360u);
    const size_t d = kCylinder + 63 * 512;  // D:'s directory past its reserved track
    const size_t e = 3 * kCylinder + 63 * 512;
    std::memcpy(disk.data() + d, sys.data() + 0x2000, sys.size() - 0x2000);
    std::memcpy(disk.data() + e, prince.data() + 0x2000, prince.size() - 0x2000);

    // HDDE.CDS in a free entry and block 400 of D:; a descriptor of another unit is passed over
    const std::string cds =
        "DRIVER = 4\r\nUNIT = 0\r\nSECTORS = 63\r\nTRACKS = 2\r\nSECSIZE = 512\r\nHEADSR = 0\r\n"
        "HEADSF = 16\r\nSYS = 1\r\nDIR = 128 ; entries\r\nPATBEG = 3\r\nWORD 13 = 50660\r\nBYTE 2 = 64\r\n\x1A";
    const std::string slave =
        "DRIVER = 4\r\nUNIT = 1\r\nSECTORS = 63\r\nTRACKS = 2\r\nHEADSF = 16\r\nSYS = 1\r\nDIR = 128\r\nPATBEG = 3\r\n";
    PutEntry(disk.data() + d + 120 * 32, "HDDE    CDS", 2, {400});
    PutEntry(disk.data() + d + 121 * 32, "HDDS    CDS", 2, {401});
    std::memcpy(disk.data() + d + 400 * 2048, cds.data(), cds.size());
    std::memcpy(disk.data() + d + 401 * 2048, slave.data(), slave.size());

    MemoryDevice hdd(disk);
    DiskFs fs(hdd);
    ASSERT_EQ(fs.Volumes().size(), 2u);
    EXPECT_EQ(fs.Volumes()[0].id, "cpm-1");
    EXPECT_EQ(fs.Volumes()[0].label, "D:");
    EXPECT_EQ(fs.Volumes()[0].offset, kCylinder);
    EXPECT_EQ(fs.Volumes()[0].bytes, 8064u * 128);
    EXPECT_EQ(fs.Volumes()[1].id, "cpm-3");
    EXPECT_EQ(fs.Volumes()[1].label, "HDDE.CDS");
    EXPECT_EQ(fs.Volumes()[1].offset, 3 * kCylinder);
    EXPECT_EQ(Crc32(ReadFile(fs, "cpm-1", "DOC.COM")), 0xBF5555B3u);
    EXPECT_EQ(Crc32(ReadFile(fs, "cpm-3", "TITLE1.DAT")), 0x5C771324u);
    // (8064 - 252) / 16 = 488 blocks: the directory 2, sys.trd 301, the two .CDS files 2
    EXPECT_EQ(fs.Volumes()[0].freeBytes, (488 - 2 - 301 - 2) * 2048);

    // Without the record the disk has no CP/M volume
    std::memset(disk.data(), 0, 512);
    MemoryDevice bare(disk);
    DiskFs none(bare);
    EXPECT_TRUE(none.Volumes().empty());
}

TEST(CpmFs_Test, EightBitBlockNumbersHoldTwoExtentsAnEntry)
{
    // 200 blocks: 8-bit block numbers, 16 an entry, EXM 1. A file of 40 KB: extents 0-1 (32 KB) and extent 2 (8 KB)
    std::vector<uint8_t> volume(200 * 2048, 0xE5);
    uint8_t* e = volume.data();
    std::memset(e, 0, 64);
    std::memcpy(e + 1, "BIG     BIN", 11);
    e[12] = 1;
    e[15] = 128;
    for (int k = 0; k < 16; k++)
        e[16 + k] = static_cast<uint8_t>(10 + k);
    std::memcpy(e + 33, "BIG     BIN", 11);
    e[32 + 12] = 2;
    e[32 + 15] = 64;
    for (int k = 0; k < 4; k++)
        e[48 + k] = static_cast<uint8_t>(30 + k);
    for (int b = 0; b < 20; b++)
        std::memset(volume.data() + (b < 16 ? 10 + b : 30 + b - 16) * 2048, b + 1, 2048);

    CpmGeometry geometry;
    geometry.blocks = 200;
    geometry.entries = 64;
    CpmFileSystem fs(
        [&volume](uint64_t offset, uint8_t* dst, uint32_t size) {
            if (offset + size > volume.size())
                return false;
            std::memcpy(dst, volume.data() + offset, size);
            return true;
        },
        geometry);
    ASSERT_TRUE(fs.IsCpm());
    std::vector<CpmFile> files;
    std::string error;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].FullName(), "BIG.BIN");
    EXPECT_EQ(files[0].size, 40960u);
    EXPECT_EQ(files[0].extents, 2u);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(files[0], data, error)) << error;
    ASSERT_EQ(data.size(), 40960u);
    for (int b = 0; b < 20; b++)
        EXPECT_EQ(data[b * 2048], b + 1) << b;
    uint32_t free = 0;
    ASSERT_TRUE(fs.FreeBlocks(free));
    EXPECT_EQ(free, 200u - 1 - 20);

    // A block used twice is no directory
    e[48] = 10;
    std::string why;
    EXPECT_FALSE(fs.IsCpm(&why));
    EXPECT_NE(why.find("used twice"), std::string::npos) << why;
}

TEST(CpmFs_Test, CdsText)
{
    AtmCpmChannel c;
    ASSERT_TRUE(CpmFileSystem::ParseCds(
        "DRIVER = 4\r\nUNIT = 0\r\nSECTORS = 63\r\nTRACKS = 16\r\nSECSIZE = 512\r\nHEADSR = 0\r\n"
        "HEADSF = 16\r\nSYS = 1\r\nDIR = 512\r\nPATBEG = 6\r\nWORD 13 = 50660\r\n",
        c));
    EXPECT_EQ(c.startCylinder, 6u);
    EXPECT_EQ(c.records, 16u * 16 * 63 * 4);
    EXPECT_EQ(c.Geometry().blocks, 4016u);  // DSM 4015 as on ATM_HDD.HDD's E:
    EXPECT_EQ(c.Geometry().entries, 512u);
    ASSERT_TRUE(
        CpmFileSystem::ParseCds("DRIVER = 3\r\nUNIT = 1\r\nSECTORS = 16\r\nTRACKS = 80\r\nSECSIZE = 256\r\n", c));
    EXPECT_EQ(c.driver, 3);
    // A hard disk without DIR, a line without '='
    EXPECT_FALSE(
        CpmFileSystem::ParseCds("DRIVER = 4\r\nSECTORS = 44\r\nTRACKS = 6\r\nHEADSF = 15\r\nPATBEG = 0\r\n", c));
    EXPECT_FALSE(CpmFileSystem::ParseCds("DRIVER 4\r\n", c));
}

TEST(CpmFs_Test, FloppyFilesWrittenDeletedRenamedAndAttributes)
{
    auto prince = Load("machines/atm/cpm/prince.trd");
    ASSERT_NE(prince, nullptr);
    std::string error;
    const std::vector<uint8_t> big = Pattern(40000, 3);
    int64_t free = 0;
    {
        DiskFs fs(*prince);
        free = fs.Volumes()[0].freeBytes;
        // 313 records in 20 blocks, three entries of 16 KB; the last record padded with #1A
        ASSERT_TRUE(fs.Write("", "new.bin", big, {}, error)) << error;
        ASSERT_TRUE(fs.Write("", "EMPTY", {}, {}, error)) << error;
        ASSERT_TRUE(fs.Write("", "USER3/A.COM", Pattern(300, 9), {}, error)) << error;
        // Written over: the older blocks are not reused, they come free with the directory write
        ASSERT_TRUE(fs.Write("", "USER3/A.COM", Pattern(5000, 1), {}, error)) << error;
    }
    DiskFs fs(*prince);
    EXPECT_EQ(fs.Volumes()[0].freeBytes, free - (20 + 3) * 2048);
    std::vector<uint8_t> data = ReadFile(fs, "", "NEW.BIN");
    ASSERT_EQ(data.size(), 40064u);
    EXPECT_TRUE(std::equal(big.begin(), big.end(), data.begin()));
    EXPECT_EQ(data.back(), 0x1A);
    DiskFsEntry entry;
    ASSERT_TRUE(fs.Stat("", "NEW.BIN", entry, error)) << error;
    EXPECT_EQ(entry.Field("extents"), 3);
    ASSERT_TRUE(fs.Stat("", "EMPTY", entry, error)) << error;
    EXPECT_EQ(entry.size, 0u);
    EXPECT_EQ(entry.Field("block"), 0);
    data = ReadFile(fs, "", "USER3/A.COM");
    ASSERT_EQ(data.size(), 5120u);
    EXPECT_EQ(Crc32({data.begin(), data.begin() + 5000}), Crc32(Pattern(5000, 1)));
    ASSERT_TRUE(fs.Stat("", "USER3", entry, error)) << error;
    EXPECT_TRUE(entry.directory);

    // Renamed in its user area, moved to user 0 and back; a taken name and a bad one refused
    ASSERT_TRUE(fs.Rename("", "USER3/A.COM", "B.COM", error)) << error;
    ASSERT_TRUE(fs.Stat("", "USER3/B.COM", entry, error)) << error;
    ASSERT_TRUE(fs.Rename("", "USER3/B.COM", "/B.COM", error)) << error;
    ASSERT_TRUE(fs.Stat("", "B.COM", entry, error)) << error;
    EXPECT_FALSE(fs.Stat("", "USER3", entry, error));
    EXPECT_FALSE(fs.Rename("", "B.COM", "PRINCE.DOC", error));
    EXPECT_NE(error.find("there already"), std::string::npos) << error;
    EXPECT_FALSE(fs.Rename("", "B.COM", "TOO-LONG-NAME", error));
    EXPECT_FALSE(fs.Write("", "USER16/X", {1}, {}, error));
    EXPECT_NE(error.find("USER1..USER15"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("", "A*.COM", {1}, {}, error));

    // Read only: not deleted, written over or renamed; USER15/CONFIG.DAT (RS.) keeps its user area
    ASSERT_TRUE(fs.SetAttributes("", "B.COM", "+RA", error)) << error;
    ASSERT_TRUE(fs.Stat("", "B.COM", entry, error)) << error;
    EXPECT_EQ(entry.attributes, "R.A");
    EXPECT_FALSE(fs.Delete("", "B.COM", error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("", "B.COM", {1}, {}, error));
    EXPECT_FALSE(fs.Rename("", "B.COM", "C.COM", error));
    EXPECT_FALSE(fs.Delete("", "USER15", error, true));
    EXPECT_FALSE(fs.Delete("", "USER15", error));
    EXPECT_NE(error.find("not empty"), std::string::npos) << error;
    ASSERT_TRUE(fs.SetAttributes("", "B.COM", "-R", error)) << error;
    ASSERT_TRUE(fs.Delete("", "B.COM", error)) << error;
    ASSERT_TRUE(fs.SetAttributes("", "USER15/CONFIG.DAT", "-R", error)) << error;
    ASSERT_TRUE(fs.Delete("", "USER15", error, true)) << error;
    EXPECT_FALSE(fs.Stat("", "USER15", entry, error));

    // More than the disk holds
    EXPECT_FALSE(fs.Write("", "HUGE.BIN", Pattern(700000, 0), {}, error));
    EXPECT_NE(error.find("not enough free blocks"), std::string::npos) << error;

    // The directory still passes the strict check; the other files read as before
    EXPECT_TRUE(CpmFileSystem::AtmFloppy(*prince).IsCpm(&error)) << error;
    DiskFs again(*prince);
    EXPECT_EQ(Crc32(ReadFile(again, "", "TITLE1.DAT")), 0x5C771324u);
    EXPECT_EQ(again.Volumes()[0].freeBytes, free - 20 * 2048 + 2048);  // NEW.BIN and EMPTY stay, CONFIG.DAT went
}

TEST(CpmFs_Test, EightBitBlockNumbersWrittenTwoExtentsAnEntry)
{
    // 200 blocks: 16 block numbers an entry, EXM 1. 40 KB + 1 record: an entry of extents 0-1 (EX 1, RC 128) and one
    // of extent 2 (EX 2, RC 65)
    std::vector<uint8_t> volume(200 * 2048, 0xE5);
    CpmGeometry geometry;
    geometry.blocks = 200;
    geometry.entries = 64;
    CpmFileSystem fs(
        [&volume](uint64_t offset, uint8_t* dst, uint32_t size) {
            if (offset + size > volume.size())
                return false;
            std::memcpy(dst, volume.data() + offset, size);
            return true;
        },
        geometry);
    std::string error;
    EXPECT_FALSE(fs.Write(0, "X", "", {1}, error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
    fs.SetWriter([&volume](uint64_t offset, const uint8_t* src, uint32_t size) {
        if (offset + size > volume.size())
            return false;
        std::memcpy(volume.data() + offset, src, size);
        return true;
    });
    const std::vector<uint8_t> bytes = Pattern(40960 + 128, 5);
    ASSERT_TRUE(fs.Write(2, "BIG", "BIN", bytes, error)) << error;
    const uint8_t* e = volume.data();
    EXPECT_EQ(e[0], 2);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(e + 1), 11), "BIG     BIN");
    EXPECT_EQ(e[12], 1);
    EXPECT_EQ(e[15], 128);
    EXPECT_EQ(e[16], 1);  // the first block past the directory
    EXPECT_EQ(e[31], 16);
    EXPECT_EQ(e[32 + 12], 2);
    EXPECT_EQ(e[32 + 15], 65);
    EXPECT_EQ(e[48], 17);
    EXPECT_EQ(e[48 + 4], 21);
    EXPECT_EQ(e[48 + 5], 0);
    ASSERT_TRUE(fs.IsCpm(&error)) << error;
    std::vector<CpmFile> files;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].size, bytes.size());
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(files[0], data, error)) << error;
    EXPECT_EQ(data, bytes);
    uint32_t free = 0;
    ASSERT_TRUE(fs.FreeBlocks(free));
    EXPECT_EQ(free, 200u - 1 - 21);
}

TEST(CpmFs_Test, HardDiskPartitionWritten)
{
    // One partition at cylinder 1 of 16 heads x 63 sectors (prince.trd's files), its directory past a reserved track
    constexpr size_t kCylinder = 16 * 63 * 512;
    std::vector<uint8_t> disk(4 * kCylinder, 0xE5);
    std::memset(disk.data(), 0, 512);
    static const uint8_t head[] = {0xC9, 0x00, 0x00, 0x55, 0xAA};
    std::memcpy(disk.data(), head, sizeof(head));
    static const uint8_t assign[] = {0x03, 26, 3, 3, 4,    0,    64,  16, 0, 2, 0, 63,  0x00,
                                     0x02, 1,  1, 0, 0x80, 0x1F, 252, 0,  0, 0, 0, 128, 0};
    std::memcpy(disk.data() + 5, assign, sizeof(assign));
    const std::vector<uint8_t> prince = Bytes("machines/atm/cpm/prince.trd");
    ASSERT_EQ(prince.size(), 655360u);
    std::memcpy(disk.data() + kCylinder + 63 * 512, prince.data() + 0x2000, prince.size() - 0x2000);

    MemoryDevice readOnly(disk);
    DiskFs locked(readOnly);
    ASSERT_EQ(locked.Volumes().size(), 1u);
    EXPECT_FALSE(locked.Volumes()[0].writable);

    MemoryDevice hdd(disk, true);
    std::string error;
    {
        DiskFs fs(hdd);
        ASSERT_EQ(fs.Volumes().size(), 1u);
        EXPECT_TRUE(fs.Volumes()[0].writable);
        // An odd size: the last sector of the data is read, patched and written back
        ASSERT_TRUE(fs.Write("cpm-1", "HDD.TXT", Pattern(3000, 7), {}, error)) << error;
        ASSERT_TRUE(fs.Rename("cpm-1", "PRINCE.DOC", "USER2/PRINCE.DOC", error)) << error;
    }
    DiskFs fs(hdd);
    const std::vector<uint8_t> data = ReadFile(fs, "cpm-1", "HDD.TXT");
    ASSERT_EQ(data.size(), 3072u);
    EXPECT_EQ(Crc32({data.begin(), data.begin() + 3000}), Crc32(Pattern(3000, 7)));
    EXPECT_EQ(Crc32(ReadFile(fs, "cpm-1", "USER2/PRINCE.DOC")), 0x8FD3694Bu);
    // Nothing past the partition changed
    EXPECT_TRUE(std::equal(disk.begin(), disk.begin() + kCylinder, hdd.Data().begin()));
    EXPECT_TRUE(std::equal(disk.begin() + 3 * kCylinder, disk.end(), hdd.Data().begin() + 3 * kCylinder));
}

TEST(CpmFs_Test, ProfiFloppyFromAProDumpReadsTheSystemTrackSector)
{
    // A .pro dump: BOOT.COM in block 2 = bytes 4096-6143, the fifth sector of track 0 (ID 9 on a Profi disk) and the
    // first of track 1 (cylinder 0 side 1)
    std::vector<uint8_t> dump(80 * 2 * 5 * 1024, 0xE5);
    PutEntry(dump.data(), "BOOT    COM", 16, {2});
    const std::vector<uint8_t> boot = Pattern(2048, 3);
    std::copy(boot.begin(), boot.end(), dump.begin() + 4096);

    LoaderPRO loader({}, "");
    std::vector<std::string> warnings;
    std::unique_ptr<DiskImage> disk(loader.parse(dump.data(), dump.size(), warnings));
    ASSERT_NE(disk, nullptr);
    EXPECT_NE(disk->getTrackForCylinderAndSide(0, 0)->findSector(9), nullptr);
    EXPECT_EQ(disk->getTrackForCylinderAndSide(0, 0)->findSector(5), nullptr);
    EXPECT_NE(disk->getTrackForCylinderAndSide(0, 1)->findSector(5), nullptr);

    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].system, "Profi CP/M");
    EXPECT_EQ(fs.Volumes()[0].freeBytes, (400 - 2 - 1) * 2048);
    EXPECT_EQ(ReadFile(fs, "", "BOOT.COM"), boot);

    // A write goes through the sector of ID 9 too; the dump comes back with the new file and BOOT.COM in place
    std::string error;
    const std::vector<uint8_t> note = Pattern(3000, 9);
    ASSERT_TRUE(fs.Write("", "NOTE.TXT", note, {}, error)) << error;
    std::vector<uint8_t> saved;
    ASSERT_TRUE(loader.serialize(disk.get(), saved, warnings));
    ASSERT_EQ(saved.size(), dump.size());
    EXPECT_TRUE(std::equal(boot.begin(), boot.end(), saved.begin() + 4096));
    std::unique_ptr<DiskImage> back(loader.parse(saved.data(), saved.size(), warnings));
    ASSERT_NE(back, nullptr);
    DiskFs again(*back);
    std::vector<uint8_t> read = ReadFile(again, "", "NOTE.TXT");
    ASSERT_EQ(read.size(), 3072u);  // Whole records, padded with #1A
    EXPECT_TRUE(std::equal(note.begin(), note.end(), read.begin()));
    EXPECT_EQ(ReadFile(again, "", "BOOT.COM"), boot);
}

TEST(CpmFs_Test, EmptyProfiFloppyIsAVolumeOnlyWithTheSystemTrack)
{
    // FORMAT.COM's empty disk: the system track marks it
    const std::vector<uint8_t> dump(80 * 2 * 5 * 1024, 0xE5);
    LoaderPRO loader({}, "");
    std::vector<std::string> warnings;
    std::unique_ptr<DiskImage> marked(loader.parse(dump.data(), dump.size(), warnings));
    ASSERT_NE(marked, nullptr);
    DiskFs fs(*marked);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].system, "Profi CP/M");
    EXPECT_EQ(fs.Volumes()[0].freeBytes, (400 - 2) * 2048);

    // The same five 1024-byte sectors, 1..5 on track 0 too: nothing says CP/M
    DiskImage plain(80, 2);
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
        for (uint8_t side = 0; side < 2; side++)
            plain.getTrackForCylinderAndSide(cylinder, side)->formatTrack(cylinder, side, LoaderPRO::trackSpec());
    EXPECT_TRUE(DiskFs(plain).Volumes().empty());
}

TEST(CpmFs_Test, KlugCpmFloppyHasItsDirectoryAfterTwoSystemCylinders)
{
    // Klug CP/M 2.3: the directory at cylinder 2 (byte 20480), 390 blocks; the system tracks hold no directory
    std::vector<uint8_t> dump(82 * 2 * 5 * 1024, 0xE5);
    constexpr size_t kDirectory = 4 * 5 * 1024;
    std::fill(dump.begin(), dump.begin() + kDirectory, 0x00);
    PutEntry(dump.data() + kDirectory, "PIP     COM", 16, {389});
    const std::vector<uint8_t> pip = Pattern(2048, 5);
    std::copy(pip.begin(), pip.end(), dump.begin() + kDirectory + 389 * 2048);

    LoaderPRO loader({}, "");
    std::vector<std::string> warnings;
    std::unique_ptr<DiskImage> disk(loader.parse(dump.data(), dump.size(), warnings));
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].system, "Klug CP/M");
    EXPECT_EQ(fs.Volumes()[0].freeBytes, (390 - 2 - 1) * 2048);
    EXPECT_EQ(ReadFile(fs, "", "PIP.COM"), pip);
}
