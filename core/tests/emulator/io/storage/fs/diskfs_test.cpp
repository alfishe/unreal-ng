// DiskFs: the file systems of floppies (TR-DOS, iS-DOS, FAT12 of a Sprinter DSS disk) and of hard disks (an MBR FAT
// partition next to an iS-DOS device at a cylinder, a whole-disk iS-DOS device), and the media verbs volumes / files /
// read on a machine's drive

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fatimagebuilder.h"
#include "_helpers/isoimagebuilder.h"
#include "_helpers/testpathhelper.h"
#include "common/base64.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fs/diskfs.h"
#include "emulator/media/mediacontrol.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/trdosfs.h"

namespace
{
std::unique_ptr<DiskImage> LoadFloppy(const std::string& relative)
{
    std::unique_ptr<DiskImage> disk;
    std::string format;
    EXPECT_TRUE(unrealdisk::FloppyFormats::Load({}, TestPathHelper::GetTestDataPath(relative), disk, format).Ok()) << relative;
    return disk;
}

std::vector<uint8_t> Bytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
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

size_t Count(const std::vector<DiskFsEntry>& entries, bool directories)
{
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [&](const DiskFsEntry& e) { return e.directory == directories; }));
}
}  // namespace

TEST(DiskFs_Test, TrdosFloppy)
{
    auto disk = LoadFloppy("loaders/trd/EyeAche.trd");
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].id, "trdos");
    EXPECT_EQ(fs.Volumes()[0].system, "TR-DOS");

    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    ASSERT_FALSE(entries.empty());
    std::vector<unrealdisk::TrdosFile> files;
    ASSERT_TRUE(unrealdisk::TrdosFileSystem(*disk).List(files, error));
    ASSERT_EQ(entries.size(), files.size());
    EXPECT_EQ(entries[0].name, files[0].TrimmedName() + "." + files[0].type);
    EXPECT_EQ(entries[0].size, files[0].length);

    std::vector<uint8_t> data, expected;
    ASSERT_TRUE(fs.Read("trdos", entries[0].path, data, error)) << error;
    ASSERT_TRUE(unrealdisk::TrdosFileSystem(*disk).Read(files[0], expected, nullptr, error));
    EXPECT_EQ(data, expected);
    EXPECT_FALSE(fs.Read("", "NOFILE.C", data, error));
    EXPECT_FALSE(fs.List("isdos", "", false, entries, error));
    EXPECT_NE(error.find("volumes: trdos"), std::string::npos) << error;
}

TEST(DiskFs_Test, IsdosFloppy)
{
    auto disk = LoadFloppy("loaders/isdos/ISDOS50.FDI");
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    const DiskFsVolume& v = fs.Volumes()[0];
    EXPECT_EQ(v.id, "isdos");
    EXPECT_EQ(v.system, "iS-DOS Classic");
    EXPECT_EQ(v.label, "is-DOS94");
    EXPECT_EQ(v.bytes, 3200u * 256);
    EXPECT_EQ(v.freeBytes, 205 * 256);

    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("", "", true, entries, error)) << error;
    EXPECT_EQ(Count(entries, false), 537u);
    EXPECT_EQ(Count(entries, true), 54u);
    ASSERT_TRUE(fs.List("", "/SHELL/HELP/", false, entries, error)) << error;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries[0].path.rfind("SHELL/HELP/", 0), 0u);

    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "read_me.txt", data, error)) << error;
    EXPECT_EQ(data.size(), 1641u);
    EXPECT_EQ(Crc32(data), 0xCBB6596Eu);
    DiskFsEntry entry;
    ASSERT_TRUE(fs.Stat("", "SHELL/HELP/exit.hlp", entry, error)) << error;
    EXPECT_EQ(entry.size, 663u);
    EXPECT_EQ(entry.attributes.front(), 'S');  // segmented
}

TEST(DiskFs_Test, SprinterDssFloppy)
{
    auto disk = LoadFloppy("machines/sprinter/dss_1_62_92.img");
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].id, "fat");
    EXPECT_EQ(fs.Volumes()[0].system, "FAT12");
    EXPECT_EQ(fs.Volumes()[0].freeBytes, 6144);

    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("fat", "", true, entries, error)) << error;
    EXPECT_EQ(Count(entries, false), 108u);
    EXPECT_EQ(Count(entries, true), 24u);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "pics/sample.bmp", data, error)) << error;
    EXPECT_EQ(data.size(), 100408u);
    EXPECT_EQ(Crc32(data), 0x30A9F481u);
}

TEST(DiskFs_Test, HardDiskFatPartitionAndIsdosDevice)
{
    // A 3 MiB FAT16 partition at LBA 2048, then an iS-DOS device at cylinder 9 (16 heads x 63 sectors)
    FatImageSpec spec;
    spec.sizeBytes = 4u << 20;
    spec.label = "NEDOOS";
    spec.files = {{"AUTOEXEC.BAT", {'c', 'm', 'd', '\r', '\n'}}};
    ScratchFatImage fat("diskfs-hdd.img", spec);
    ASSERT_TRUE(fat.ok()) << fat.error();
    std::vector<uint8_t> bytes = Bytes(fat.path());
    const std::vector<uint8_t> device = Bytes(TestPathHelper::GetTestDataPath("loaders/isdos/HDDNEMO.img"));
    ASSERT_EQ(device.size(), 32768u);
    const size_t at = 9 * 16 * 63 * 512;
    bytes.resize(at + 16 * 63 * 512, 0);
    std::memcpy(bytes.data() + at, device.data(), device.size());
    BytesDisk disk(bytes);

    DiskFs fs(disk);
    const std::vector<DiskFsVolume>& volumes = fs.Volumes();
    ASSERT_EQ(volumes.size(), 2u);
    EXPECT_EQ(volumes[0].id, "p1");
    EXPECT_EQ(volumes[0].system, "FAT16");
    EXPECT_EQ(volumes[0].label, "NEDOOS");
    EXPECT_EQ(volumes[0].offset, 2048u * 512);
    EXPECT_EQ(volumes[1].id, "isdos-9");
    EXPECT_EQ(volumes[1].label, "HDDNEMO");
    EXPECT_EQ(volumes[1].offset, at);

    std::vector<uint8_t> data;
    std::string error;
    ASSERT_TRUE(fs.Read("p1", "autoexec.bat", data, error)) << error;
    EXPECT_EQ(std::string(data.begin(), data.end()), "cmd\r\n");
    std::vector<DiskFsEntry> entries;
    ASSERT_TRUE(fs.List("isdos-9", "", true, entries, error)) << error;
    EXPECT_EQ(Count(entries, false), 12u);
    EXPECT_EQ(Count(entries, true), 2u);
    ASSERT_TRUE(fs.Read("isdos-9", "HDD_NEMO/HELP/ide+5.hlp", data, error)) << error;
    EXPECT_EQ(data.size(), 2498u);

    // The device alone is the whole disk
    BytesDisk alone(device);
    DiskFs whole(alone);
    ASSERT_EQ(whole.Volumes().size(), 1u);
    EXPECT_EQ(whole.Volumes()[0].id, "isdos");
}

/// The media verbs on a Pentagon's drive A: every automation plane runs these
TEST(DiskFs_Test, MediaVerbsOnADrive)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaControl control(emulator->GetContext());
    auto run = [&](std::string verb, std::string path, std::map<std::string, std::string> options = {}) {
        return control.Execute(MediaRequest{std::move(verb), "A", std::move(path), std::move(options)});
    };

    MediaReply reply = run("volumes", "");
    EXPECT_FALSE(reply.result.Ok());  // empty drive
    reply = run("insert", TestPathHelper::GetTestDataPath("loaders/isdos/ISDOS50.FDI"));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;

    reply = run("volumes", "");
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    const StateNode* volumes = reply.body.find("volumes");
    ASSERT_NE(volumes, nullptr);
    ASSERT_EQ(volumes->items.size(), 1u);
    EXPECT_EQ(volumes->items[0].find("system")->s, "iS-DOS Classic");

    reply = run("files", "SHELL", {{"recursive", ""}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.body.find("volume")->s, "isdos");
    EXPECT_FALSE(reply.body.find("entries")->items.empty());

    reply = run("read", "read_me.txt");
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.body.find("size")->i, 1641);
    std::vector<uint8_t> data;
    ASSERT_TRUE(base64::Decode(reply.body.find("data")->s, data));
    EXPECT_EQ(Crc32(data), 0xCBB6596Eu);

    const std::string out = TestPathHelper::GetUniqueTestScratchPath("read_me.txt");
    reply = run("read", "read_me.txt", {{"out", out}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(Bytes(out), data);
    EXPECT_EQ(reply.body.find("data"), nullptr);

    reply = run("read", "nothing.txt");
    EXPECT_EQ(reply.result.error, MediaError::UnreadableSource);
    reply = run("files", "", {{"volume", "fat"}});
    EXPECT_NE(reply.result.message.find("volumes: isdos"), std::string::npos) << reply.result.message;
    reply = run("files", "", {{"bogus", "1"}});
    EXPECT_EQ(reply.result.error, MediaError::BadRequest);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// media write on a Pentagon's drive A: a file onto a blank TR-DOS disk read back, the refusals
TEST(DiskFs_Test, MediaWriteOnADrive)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaControl control(emulator->GetContext());
    auto run = [&](std::string verb, std::string path, std::map<std::string, std::string> options = {}) {
        return control.Execute(MediaRequest{std::move(verb), "A", std::move(path), std::move(options)});
    };
    const std::vector<uint8_t> code = {0xF3, 0xC9, 0x00, 0x01};

    MediaReply reply = run("write", "HELLO.C", {{"data", base64::Encode(code)}});
    EXPECT_EQ(reply.result.error, MediaError::NotSupported);  // empty drive
    reply = run("create", "", {{"format", "trdos"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;

    reply = run("write", "HELLO.C", {{"data", base64::Encode(code)}, {"start", "#8000"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.body.find("volume")->s, "trdos");
    EXPECT_EQ(reply.body.find("size")->i, 4);
    EXPECT_FALSE(reply.body.find("changes")->s.empty());
    reply = run("files", "");
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    ASSERT_EQ(reply.body.find("entries")->items.size(), 1u);
    EXPECT_EQ(reply.body.find("entries")->items[0].find("start")->i, 0x8000);
    reply = run("read", "HELLO.C");
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    std::vector<uint8_t> data;
    ASSERT_TRUE(base64::Decode(reply.body.find("data")->s, data));
    EXPECT_EQ(data, code);
    reply = run("info", "");
    const StateNode* medium = reply.body.find("info") ? reply.body.find("info")->find("medium") : nullptr;
    ASSERT_NE(medium, nullptr);
    EXPECT_TRUE(medium->find("dirty")->b);

    // A host file, over the older file of that name
    const std::string in = TestPathHelper::GetUniqueTestScratchPath("hello.bin");
    std::ofstream(in, std::ios::binary) << "hello";
    reply = run("write", "HELLO.C", {{"in", in}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(run("files", "").body.find("entries")->items.size(), 1u);
    EXPECT_EQ(run("read", "HELLO.C").body.find("size")->i, 5);

    EXPECT_EQ(run("write", "HELLO.C", {}).result.error, MediaError::BadRequest);  // no bytes
    EXPECT_EQ(run("write", "HELLO.C", {{"data", "AA=="}, {"in", in}}).result.error, MediaError::BadRequest);
    EXPECT_EQ(run("write", "HELLO.C", {{"data", "AA=="}, {"start", "70000"}}).result.error, MediaError::BadRequest);
    EXPECT_EQ(run("write", "TOOLONGNAME.C", {{"data", "AA=="}}).result.error, MediaError::BadRequest);
    ASSERT_TRUE(run("protect", "", {{"on", "true"}}).result.Ok());
    EXPECT_EQ(run("write", "NEW.C", {{"data", "AA=="}}).result.error, MediaError::NotSupported);
    ASSERT_TRUE(run("protect", "", {{"on", "false"}}).result.Ok());

    // iS-DOS reads, does not write
    ASSERT_TRUE(run("insert", TestPathHelper::GetTestDataPath("loaders/isdos/ISDOS50.FDI"), {{"discard", "true"}}).result.Ok());
    reply = run("write", "new.txt", {{"data", "AA=="}});
    EXPECT_EQ(reply.result.error, MediaError::NotSupported);
    EXPECT_NE(reply.result.message.find("read only"), std::string::npos) << reply.result.message;

    EmulatorTestHelper::CleanupEmulator(emulator);
}
