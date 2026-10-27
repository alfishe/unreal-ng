// DiskFs on its own: the volumes of floppies (TR-DOS, iS-DOS, FAT12 of a Sprinter DSS disk, +3DOS), an iS-DOS device on
// a hard disk at a cylinder boundary, reading with the TR-DOS sector slack, and writing on TR-DOS and +3DOS (refused on
// read-only file systems)

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
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
    explicit MemoryDevice(std::vector<uint8_t> bytes) : _bytes(std::move(bytes)) {}
    uint64_t SectorCount() const override { return _bytes.size() / kSectorSize; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        if (lba >= SectorCount())
            return false;
        std::memcpy(dst, _bytes.data() + lba * kSectorSize, kSectorSize);
        return true;
    }
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::string Describe() const override { return "memory"; }
    uint64_t ContentId() const override { return 0; }

private:
    std::vector<uint8_t> _bytes;
};
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
                          Case{"loaders/isdos/ISDOS50.FDI", "isdos", "iS-DOS Classic", false},
                          Case{"machines/sprinter/dss_1_62_92.img", "fat", "FAT12", false},
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

    auto isdos = Load("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(isdos, nullptr);
    DiskFs ro(*isdos);
    EXPECT_FALSE(ro.Write("", "new.txt", code, {}, error));
    EXPECT_NE(error.find("read only"), std::string::npos) << error;
}
