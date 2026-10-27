// MDOS: a D80 disk (MDOS3's boot disk cut out of its container), the slots of an MDOS3 container and of an MBR
// partition of type #31, the copies of one file on different disks, a FAT chain that loops

#include "unrealdisk/mdosfs.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_mdos.h"

using namespace unrealdisk;
using unrealdisk::testing::TestDataPath;

namespace
{
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

std::unique_ptr<DiskImage> Parse(const std::vector<uint8_t>& bytes, const std::string& name, std::string& format)
{
    std::unique_ptr<DiskImage> disk;
    const FormatResult result = FloppyFormats::LoadData({}, bytes.data(), bytes.size(), name, disk, format);
    EXPECT_TRUE(result.Ok()) << name << ": " << result.message;
    return disk;
}

std::vector<uint8_t> ReadFile(DiskFs& fs, const std::string& volume, const std::string& path)
{
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_TRUE(fs.Read(volume, path, data, error)) << volume << " " << path << ": " << error;
    return data;
}

class MemoryDevice : public BlockDevice
{
public:
    explicit MemoryDevice(std::vector<uint8_t> bytes) : _bytes(std::move(bytes)) {}
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
    bool WriteSector(uint64_t, const uint8_t*) override
    {
        return false;
    }
    bool IsWritable() const override
    {
        return false;
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
};
}  // namespace

TEST(MdosFs, D80DiskListsAndReads)
{
    const std::vector<uint8_t> bytes = Bytes("loaders/mdos/bootdisk.d80");
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(bytes, "bootdisk.d80", format);
    ASSERT_TRUE(disk);
    EXPECT_EQ(format, "mdos");  // not the 720 KB PC dump of the same size
    EXPECT_EQ(disk->getCylinders(), 80);

    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    const DiskFsVolume& v = fs.Volumes()[0];
    EXPECT_EQ(v.id, "mdos");
    EXPECT_EQ(v.system, "MDOS");
    EXPECT_EQ(v.label, "bootdisk");
    EXPECT_EQ(v.bytes, 737280u);
    EXPECT_EQ(v.freeBytes, 7 * 512);
    EXPECT_FALSE(v.writable);

    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 109u);
    EXPECT_EQ(entries[0].name, "run.P");
    EXPECT_EQ(entries[0].start, 3u);  // the autostart line
    EXPECT_EQ(entries[0].Field("param2"), 94);
    EXPECT_EQ(entries[0].attributes, "....RWED");
    EXPECT_EQ(entries[1].name, "run.B");
    EXPECT_EQ(entries[1].start, 49152u);
    EXPECT_EQ(entries[22].name, "09-60.P");
    EXPECT_EQ(entries[22].attributes, "H...RWED");
    EXPECT_EQ(entries[20].name, "pt351   cd.B");

    // The program: BASIC lines (number, length, the text up to #0D) filling its 94 bytes
    const std::vector<uint8_t> run = ReadFile(fs, "", "run.P");
    ASSERT_EQ(run.size(), 94u);
    size_t at = 0;
    int lines = 0;
    while (at + 4 <= run.size())
    {
        const size_t length = run[at + 2] | run[at + 3] << 8;
        ASSERT_LE(at + 4 + length, run.size());
        EXPECT_EQ(run[at + 3 + length], 0x0D);
        at += 4 + length;
        lines++;
    }
    EXPECT_EQ(at, run.size());
    EXPECT_GT(lines, 0);

    // CRCs of the research script's extract (tools/mdos_ls.py)
    EXPECT_EQ(Crc32(ReadFile(fs, "", "RUN.B")), 0x8007BC17u);
    EXPECT_EQ(Crc32(ReadFile(fs, "", "sql-code.B")), 0x69C0A9F1u);
    EXPECT_EQ(Crc32(ReadFile(fs, "", "pt351   cd.B")), 0xC14262F1u);
    EXPECT_EQ(Crc32(ReadFile(fs, "", "bootconf.B")), 0xD0ECBA7Cu);  // 512 bytes: #E00 ends a full sector

    std::vector<uint8_t> out;
    std::vector<std::string> warnings;
    ASSERT_TRUE(LoaderMDOS({}, "").serialize(disk.get(), out, warnings));
    EXPECT_TRUE(out == bytes);
}

TEST(MdosFs, Mdos3ContainerAndPartitionSlots)
{
    std::vector<uint8_t> md3 = Bytes("loaders/mdos/mdos3-3slots.md3");
    ASSERT_EQ(md3.size(), 3u * 1693 * 512);
    MemoryDevice device(md3);
    DiskFs fs(device);
    const std::vector<DiskFsVolume>& volumes = fs.Volumes();
    ASSERT_EQ(volumes.size(), 3u);
    EXPECT_EQ(volumes[0].id, "mdos-0");
    EXPECT_EQ(volumes[0].label, "BOOT 20.1.2006");
    EXPECT_EQ(volumes[0].offset, 512u);
    EXPECT_EQ(volumes[1].label, "ARTSTUDIO - ARTIST2");
    EXPECT_EQ(volumes[1].freeBytes, 955 * 512);
    EXPECT_EQ(volumes[2].label, "GRAFIKA 1");
    EXPECT_EQ(volumes[2].bytes, 94u * 2 * 9 * 512);  // enlarged by MDOS3 to 94 cylinders
    EXPECT_EQ(volumes[2].freeBytes, 1606 * 512);

    // One file on three disks
    const std::vector<uint8_t> overlays = ReadFile(fs, "mdos-0", "overlays.B");
    ASSERT_EQ(overlays.size(), 14336u);
    EXPECT_EQ(ReadFile(fs, "mdos-1", "overlays.B"), overlays);
    EXPECT_EQ(ReadFile(fs, "mdos-1", "ART128KM.P"), ReadFile(fs, "mdos-0", "ART128KM.P"));
    EXPECT_EQ(Crc32(ReadFile(fs, "mdos-2", "run.B")), 0x8007BC17u);
    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("mdos-2", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[3].name, "tools128.B");
    EXPECT_EQ(entries[3].size, 25438u);

    // The same slots in an MBR partition of type #49 (MDOS3's MAKER) from sector 2048, and its first slot again in a
    // second one: the partition's number names the volumes then
    std::vector<uint8_t> disk(2048 * 512, 0);
    auto partition = [&disk](int number, uint8_t type, uint32_t first, uint32_t count) {
        uint8_t* entry = disk.data() + 446 + number * 16;
        entry[4] = type;
        for (int k = 0; k < 4; k++)
        {
            entry[8 + k] = static_cast<uint8_t>(first >> (8 * k));
            entry[12 + k] = static_cast<uint8_t>(count >> (8 * k));
        }
    };
    partition(0, 0x49, 2048, 3 * 1693);
    disk[510] = 0x55;
    disk[511] = 0xAA;
    std::vector<uint8_t> one = disk;
    one.insert(one.end(), md3.begin(), md3.end());
    MemoryDevice hdd(one);
    DiskFs partitioned(hdd);
    ASSERT_EQ(partitioned.Volumes().size(), 3u);
    EXPECT_EQ(partitioned.Volumes()[1].id, "mdos-1");
    EXPECT_EQ(partitioned.Volumes()[1].offset, (2048u + 1693 + 1) * 512);
    EXPECT_EQ(ReadFile(partitioned, "mdos-1", "overlays.B"), overlays);

    partition(1, 0x49, 2048 + 3 * 1693, 1693);
    std::vector<uint8_t> two = disk;
    two.insert(two.end(), md3.begin(), md3.end());
    two.insert(two.end(), md3.begin(), md3.begin() + 1693 * 512);
    MemoryDevice hdd2(two);
    DiskFs both(hdd2);
    ASSERT_EQ(both.Volumes().size(), 4u);
    EXPECT_EQ(both.Volumes()[2].id, "mdos-p1-2");
    EXPECT_EQ(both.Volumes()[3].id, "mdos-p2-0");
    EXPECT_EQ(both.Volumes()[3].label, "BOOT 20.1.2006");

    // Another type (#31 here) is no MDOS3 partition: the firmware takes the whole disk and finds the slots from
    // the first "SDOS" sector on, wherever it is
    partition(0, 0x31, 2048, 3 * 1693);
    partition(1, 0, 0, 0);
    std::vector<uint8_t> other = disk;
    other.resize(other.size() - 100 * 512);  // the slots 1948 sectors from the start
    other.insert(other.end(), md3.begin(), md3.end());
    MemoryDevice hdd3(other);
    DiskFs searched(hdd3);
    ASSERT_EQ(searched.Volumes().size(), 3u);
    EXPECT_EQ(searched.Volumes()[0].offset, (1948u + 1) * 512);
}

TEST(MdosFs, Mdos3CardOfFat16Files)
{
    // MDOS3's how-to, version 1: a FAT16 partition of 512-byte clusters whose first files are the slots: 0bootdisk,
    // then 1infosec.bin + 1moved.d80 + 1space.bin (the files of my_CF_16M in the MDOS3 2010-03-02 distribution)
    MemoryDevice card(Bytes("loaders/mdos/mdos3-cf-fat16.img"));
    DiskFs fs(card);
    const std::vector<DiskFsVolume>& volumes = fs.Volumes();
    ASSERT_EQ(volumes.size(), 3u);
    EXPECT_EQ(volumes[0].id, "p1");
    EXPECT_EQ(volumes[0].fs, "fat16");
    EXPECT_EQ(volumes[1].id, "mdos-0");
    EXPECT_EQ(volumes[1].label, "bootdisk");
    EXPECT_EQ(volumes[1].offset, (116u + 1) * 512);  // the first cluster of 0bootdisk, its info sector
    EXPECT_EQ(volumes[2].id, "mdos-1");
    EXPECT_EQ(volumes[2].label, "moved disk (.d80 format)");
    EXPECT_EQ(volumes[2].bytes, 737280u);

    // The slot is the D80 file it was copied from, byte for byte, and lists as one
    const std::vector<uint8_t> file = ReadFile(fs, "p1", "1moved.d80");
    ASSERT_EQ(file.size(), 737280u);
    std::vector<uint8_t> slot(file.size());
    for (uint64_t k = 0; k < slot.size() / 512; k++)
        ASSERT_TRUE(card.ReadSector(volumes[2].offset / 512 + k, slot.data() + k * 512));
    EXPECT_TRUE(file == slot);
    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("mdos-1", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 11u);
    EXPECT_EQ(entries[1].name, "intro4yes!.B");
    EXPECT_EQ(ReadFile(fs, "mdos-1", entries[1].path).size(), entries[1].size);
}

TEST(MdosFs, LoopingChainFailsAndOtherDisksAreNoMdos)
{
    std::vector<uint8_t> bytes = Bytes("loaders/mdos/bootdisk.d80");
    // run.B starts at sector 15: its FAT word (odd entry 15 of FAT sector 1) back to 15
    uint8_t* fat = bytes.data() + 512 + (15 / 2) * 3;
    fat[1] = static_cast<uint8_t>((fat[1] & 0xF0) | 0x0);
    fat[2] = 15;
    std::string format;
    std::unique_ptr<DiskImage> disk = Parse(bytes, "loop.d80", format);
    ASSERT_TRUE(disk);
    DiskFs fs(*disk);
    std::vector<uint8_t> data;
    std::string error;
    EXPECT_FALSE(fs.Read("", "run.B", data, error));
    EXPECT_NE(error.find("loops"), std::string::npos) << error;

    // Without the mark the size makes a raw PC floppy, and DiskFs finds no MDOS on it
    std::vector<uint8_t> plain = Bytes("loaders/mdos/bootdisk.d80");
    plain[204] = 'X';
    disk = Parse(plain, "plain.img", format);
    ASSERT_TRUE(disk);
    EXPECT_EQ(format, "rawpc");
    MdosFileSystem mdos = MdosFileSystem::OnDisk(*disk);
    std::string why;
    EXPECT_FALSE(mdos.Open(&why));
    for (const DiskFsVolume& v : DiskFs(*disk).Volumes())
        EXPECT_NE(v.fs, "mdos");
}
