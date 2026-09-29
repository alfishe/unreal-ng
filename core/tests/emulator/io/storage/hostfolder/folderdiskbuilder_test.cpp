// FolderDiskBuilder: a host folder built into a TR-DOS disk image
// (integration-floppy.md §4)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/trdostesthelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/emulator.h"
#include "emulator/io/storage/hostfolder/folderdiskbuilder.h"
#include "loaders/disk/loader_hobeta.h"

namespace
{
    /// A Hobeta file: 17-byte header (checksum included), then whole sectors
    std::string Hobeta(const char* name8, char type, uint16_t start, size_t length, uint8_t fill)
    {
        LoaderHobeta::Header header;
        std::memcpy(header.name, name8, 8);
        header.type = static_cast<uint8_t>(type);
        header.start = start;
        header.length = static_cast<uint16_t>(length);
        header.sectors = static_cast<uint8_t>((length + 255) / 256);
        std::string file(LoaderHobeta::HEADER_SIZE + header.sectors * 256, static_cast<char>(fill));
        LoaderHobeta::serializeHeader(header, reinterpret_cast<uint8_t*>(file.data()));
        return file;
    }

    std::string Label(DiskImage& disk)
    {
        const DiskImage::Sector* volume = disk.getTrackForCylinderAndSide(0, 0)->getSector(8);
        return std::string(reinterpret_cast<const char*>(volume->data) + 0xF5, 8);
    }

    bool Reported(const MediaResult& result, const std::string& fragment)
    {
        return std::any_of(result.report.begin(), result.report.end(),
                           [&fragment](const std::string& line) { return line.find(fragment) != std::string::npos; });
    }

    std::string Report(const MediaResult& result)
    {
        std::string text;
        for (const std::string& line : result.report)
            text += line + "\n";
        return text;
    }

    class FolderDiskBuilder_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;

        void SetUp() override
        {
            // The TR-DOS formatter reads the machine's sector interleave setting
            _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
        }

        void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

        MediaResult Build(const ScratchFolder& folder, std::unique_ptr<DiskImage>& disk)
        {
            return FolderDiskBuilder::BuildTrd(_emulator->GetContext(), folder.Path(), disk);
        }
    };
}  // namespace

/// The worked example of integration-floppy.md §4.3
TEST_F(FolderDiskBuilder_Test, WorkedExample)
{
    ScratchFolder folder("folder-disk-example");
    folder.File("boot.$B", Hobeta("boot    ", 'B', 100, 100, 0x11));
    folder.File("game.$C", Hobeta("game    ", 'C', 24576, 40 * 1024, 0x22));
    folder.File("intro.scr", std::string(6912, '\x33'));
    folder.File("notes.txt", "never on the disk");
    folder.File(".DS_Store", "host junk");
    folder.File(".unreal-media.yaml", "label: DEMO\norder: [boot.$B, intro.scr]\nexclude: [notes.txt]\n");

    std::unique_ptr<DiskImage> disk;
    const MediaResult result = Build(folder, disk);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_NE(disk, nullptr);

    const auto files = LoaderHobeta::listFiles(disk.get());
    ASSERT_EQ(files.size(), 3u) << Report(result);
    EXPECT_EQ(files[0].nameString(), "boot    ");
    EXPECT_EQ(files[0].entry.Type, 'B');
    EXPECT_EQ(files[1].nameString(), "intro   ");
    EXPECT_EQ(files[1].entry.Type, 'C');
    EXPECT_EQ(files[1].entry.Start, 16384) << "a 6912-byte screen loads at the screen";
    EXPECT_EQ(files[1].entry.SizeInSectors, 27);
    EXPECT_EQ(files[2].nameString(), "game    ");
    EXPECT_EQ(files[2].entry.Start, 24576) << "a Hobeta header's start is kept";
    EXPECT_EQ(files[2].entry.SizeInSectors, 160);
    EXPECT_EQ(Label(*disk), "DEMO    ");

    EXPECT_TRUE(Reported(result, ".DS_Store")) << Report(result);
    EXPECT_TRUE(Reported(result, "notes.txt")) << Report(result);
    EXPECT_FALSE(disk->isDirty()) << "a freshly built disk has nothing to save";
}

TEST_F(FolderDiskBuilder_Test, CompatibleNamesTypesAndCollisions)
{
    EXPECT_EQ(FolderDiskBuilder::CompatibleName("intro"), "intro   ") << "case kept";
    EXPECT_EQ(FolderDiskBuilder::CompatibleName("a\"b"), "a_b     ") << "a quote breaks BASIC commands";
    EXPECT_EQ(FolderDiskBuilder::CompatibleName("\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0"), "____    ")
        << "one '_' per non-ASCII character, not per byte";
    EXPECT_EQ(FolderDiskBuilder::CompatibleName("verylongname"), "verylong");

    EXPECT_EQ(DiskTypeMap::TypeFor("bas"), 'B');
    EXPECT_EQ(DiskTypeMap::TypeFor("B"), 'B');
    EXPECT_EQ(DiskTypeMap::TypeFor("scr"), 'C');
    EXPECT_EQ(DiskTypeMap::TypeFor("D"), 'D');
    EXPECT_EQ(DiskTypeMap::TypeFor("#"), '#');
    EXPECT_EQ(DiskTypeMap::TypeFor("txt"), 'C') << "anything else is code";

    ScratchFolder folder("folder-disk-names");
    folder.File("longname-a.bin", "a");
    folder.File("longname-b.bin", "b");
    folder.File("longname-c.bas", "c");  // same name, other type: no clash
    std::unique_ptr<DiskImage> disk;
    ASSERT_TRUE(Build(folder, disk).Ok());
    const auto files = LoaderHobeta::listFiles(disk.get());
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0].nameString(), "longname");
    EXPECT_EQ(files[1].nameString(), "longnam1") << "the last character becomes 1";
    EXPECT_EQ(files[2].nameString(), "longname");
    EXPECT_EQ(files[2].entry.Type, 'B');
}

/// Files are placed in order; one that does not fit is reported and the next
/// one still goes on
TEST_F(FolderDiskBuilder_Test, WhatDoesNotFitIsReportedAndSkipped)
{
    ScratchFolder folder("folder-disk-full");
    // One side, 40 tracks: 39 x 16 = 624 free sectors
    folder.File(".unreal-media.yaml", "disk: {format: trd, tracks: 40, sides: 1}\n");
    folder.File("a.bin", std::string(60000, 'a'));  // 235 sectors
    folder.File("b.bin", std::string(60000, 'b'));  // 235
    folder.File("c.bin", std::string(60000, 'c'));  // 235: only 154 left
    folder.File("d.bin", std::string(1000, 'd'));   // 4: fits after the skip
    folder.File("huge.bin", std::string(70000, 'h'));
    folder.Folder("sub");

    std::unique_ptr<DiskImage> disk;
    const MediaResult result = Build(folder, disk);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(disk->getCylinders(), 40);
    EXPECT_EQ(disk->getSides(), 1);

    const auto files = LoaderHobeta::listFiles(disk.get());
    std::vector<std::string> names;
    for (const auto& f : files)
        names.push_back(f.nameString());
    EXPECT_EQ(names, (std::vector<std::string>{"a       ", "b       ", "d       "})) << Report(result);
    EXPECT_TRUE(Reported(result, "c.bin: skipped, does not fit")) << Report(result);
    EXPECT_TRUE(Reported(result, "huge.bin: skipped, larger than a TR-DOS file")) << Report(result);
    EXPECT_TRUE(Reported(result, "sub")) << "subfolders are reported: " << Report(result);
}

TEST_F(FolderDiskBuilder_Test, ManifestOverridesNameTypeStartAndAutorunLine)
{
    ScratchFolder folder("folder-disk-override");
    folder.File(".unreal-media.yaml",
                "files:\n"
                "  loader.bin: {name: LOADER, type: C, start: 24576}\n"
                "  prog.bin: {name: run, type: B, line: 10}\n");
    folder.File("loader.bin", std::string(300, 'L'));
    folder.File("prog.bin", std::string(500, 'P'));

    std::unique_ptr<DiskImage> disk;
    const MediaResult result = Build(folder, disk);
    ASSERT_TRUE(result.Ok()) << result.message;
    const auto files = LoaderHobeta::listFiles(disk.get());
    ASSERT_EQ(files.size(), 2u) << Report(result);
    EXPECT_EQ(files[0].nameString(), "LOADER  ");
    EXPECT_EQ(files[0].entry.Start, 24576);
    EXPECT_EQ(files[1].nameString(), "run     ");
    EXPECT_EQ(files[1].entry.Type, 'B');
    EXPECT_EQ(files[1].entry.Length, 500);
    EXPECT_EQ(files[1].entry.SizeInSectors, 2) << "500 bytes + the 4-byte autorun record: 2 sectors";

    // The autorun record follows the program: #80 #AA, line 10
    ASSERT_NE(disk->getTrack(files[1].entry.StartTrack), nullptr);
    const uint8_t* second = disk->getTrack(files[1].entry.StartTrack)
                                ->getSector(static_cast<uint8_t>(files[1].entry.StartSector + 1))
                                ->data;
    EXPECT_EQ(second[500 - 256 + 0], 0x80);
    EXPECT_EQ(second[500 - 256 + 1], 0xAA);
    EXPECT_EQ(second[500 - 256 + 2], 10);
    EXPECT_EQ(second[500 - 256 + 3], 0);
}

/// ACC-7 (requirements.md): a folder with Hobeta and plain files and a manifest
/// giving the order, inserted into drive A. The real TR-DOS ROM lists them in
/// that order with compatible names, the file that does not fit is reported and
/// absent, and RUN "boot" runs the BASIC program from the folder.
/// Real-ROM TR-DOS commands (LIST, then RUN): slower than 50 ms by nature
TEST_F(FolderDiskBuilder_Test, Acc7TrdosListsAndRunsAFolder)
{
    // 10 POKE 60000,42 - tokenized, with the TR-DOS autorun record for line 10
    const std::vector<uint8_t> program = {
        0x00, 0x0A, 0x16, 0x00,                          // line 10, 22 bytes
        0xF4, '6', '0', '0', '0', '0',                   // POKE 60000
        0x0E, 0x00, 0x00, 0x60, 0xEA, 0x00, ',',         //   (60000 as a small integer)
        '4', '2', 0x0E, 0x00, 0x00, 0x2A, 0x00, 0x00,    // 42
        0x0D};
    std::string boot = Hobeta("boot    ", 'B', static_cast<uint16_t>(program.size()), program.size(), 0);
    std::memcpy(boot.data() + LoaderHobeta::HEADER_SIZE, program.data(), program.size());
    const uint8_t autorun[] = {0x80, 0xAA, 10, 0};
    std::memcpy(boot.data() + LoaderHobeta::HEADER_SIZE + program.size(), autorun, sizeof(autorun));

    // DS80 holds 2 544 sectors: ten 235-sector files fit beside boot and the
    // picture, the eleventh does not, and the small file after it still goes on
    ScratchFolder folder("folder-disk-acc7");
    folder.File("boot.$B", boot);
    folder.File("Picture.scr", std::string(6912, '\x55'));
    for (int i = 1; i <= 11; i++)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "big-%02d.bin", i);
        folder.File(name, std::string(60000, static_cast<char>('A' + i)));
    }
    folder.File("tail.bin", std::string(1000, 't'));
    folder.File(".unreal-media.yaml", "order: [boot.$B, Picture.scr]\n");

    MediaSource source;
    const auto u8 = folder.Path().u8string();
    source.path.assign(u8.begin(), u8.end());
    const MediaResult inserted = _emulator->GetContext()->pMediaManager->Insert("fdd.a", source);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_TRUE(Reported(inserted, "big-11.bin: skipped, does not fit")) << Report(inserted);

    TRDOSTestHelper trdos(_emulator);
    trdos.startCommand("LIST");
    trdos.runUntil([&trdos] { return trdos.screenText().find("tail") != std::string::npos; },
                   4 * TRDOSTestHelper::MAX_EXECUTION_CYCLES, 10);
    const std::string list = trdos.screenText();
    size_t previous = 0;
    for (const char* name : {"boot", "Picture", "big-01", "big-02", "big-10", "tail"})
    {
        const size_t at = list.find(name);
        ASSERT_NE(at, std::string::npos) << name << " missing: " << list;
        EXPECT_GT(at, previous) << name << " out of order: " << list;
        previous = at;
    }
    EXPECT_EQ(list.find("big-11"), std::string::npos) << "what does not fit is absent: " << list;

    Memory& memory = *_emulator->GetContext()->pMemory;
    memory.DirectWriteToZ80Memory(60000, 0);
    trdos.startCommand("RUN \"boot\"");
    const bool ran = trdos.runUntil([&memory] { return memory.DirectReadFromZ80Memory(60000) == 42; },
                                    TRDOSTestHelper::MAX_EXECUTION_CYCLES, 10);
    EXPECT_TRUE(ran) << "RUN \"boot\" ran the program from the folder: " << trdos.screenText();
}
