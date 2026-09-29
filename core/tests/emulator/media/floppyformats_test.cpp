// FloppyFormats: content-first format detection, loading and writing of floppy images

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/floppyformats.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Fixture(const char* relative)
    {
        return Utf8(TestPathHelper::FindProjectRoot() / relative);
    }

    std::string ReadAll(const std::string& path)
    {
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
}  // namespace

/// Signatures decide, whatever the file is called
TEST(FloppyFormats_Test, ContentDecidesTheFormat)
{
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/trd/EyeAche.trd")), "trd");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/scl/insult.scl")), "scl");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/fdi/VORON1.FDI")), "fdi");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/udi/VORON1.UDI")), "udi");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/dsk/plus3-blank.dsk")), "dsk");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/td0/trdos-sample.td0")), "td0");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/mgt/synthetic.img")), "mgt");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/hobeta/hello.$C")), "hobeta");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/hobeta/bad-checksum.$C")), "")
        << "a Hobeta file whose checksum fails is nothing";

    // The old rule was the extension: a TRD and an SCL under misleading names
    ScratchFolder folder("floppy-probe");
    folder.File("game.dat", ReadAll(Fixture("testdata/loaders/trd/EyeAche.trd")));
    folder.File("disk.trd", ReadAll(Fixture("testdata/loaders/scl/insult.scl")));
    folder.File("notes.txt", "not a disk");
    EXPECT_EQ(FloppyFormats::Probe(Utf8(folder.Path() / "game.dat")), "trd");
    EXPECT_EQ(FloppyFormats::Probe(Utf8(folder.Path() / "disk.trd")), "scl");
    EXPECT_EQ(FloppyFormats::Probe(Utf8(folder.Path() / "notes.txt")), "");
}

TEST(FloppyFormats_Test, LoadNamesTheFormatAndRefusesWhatIsNotADisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    std::unique_ptr<DiskImage> disk;
    std::string format;
    ASSERT_TRUE(FloppyFormats::Load(context, Fixture("testdata/loaders/hobeta/hello.$C"), disk, format).Ok());
    EXPECT_EQ(format, "hobeta");
    ASSERT_NE(disk, nullptr);
    EXPECT_EQ(disk->getCylinders(), 80) << "a Hobeta file lands on a blank DS80 TR-DOS disk";

    ScratchFolder folder("floppy-load");
    folder.File("notes.txt", "not a disk");
    const MediaResult refused = FloppyFormats::Load(context, Utf8(folder.Path() / "notes.txt"), disk, format);
    EXPECT_EQ(refused.error, MediaError::UnknownFormat);
    EXPECT_NE(refused.message.find(".trd"), std::string::npos) << "the error lists what a drive takes: " << refused.message;
    EXPECT_EQ(disk, nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A disk TRD cannot hold goes to <stem>.udi and the original stays as it was;
/// a Hobeta file is never a target
TEST(FloppyFormats_Test, SaveRetargetsToUdiAndNeverWritesHobeta)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    // A +3DOS disk (9 x 512) has no TR-DOS tracks
    DiskImage plus3(40, 1, DiskImage::TrackFormatSpec::plus3());
    ScratchFolder folder("floppy-save");
    const std::string trd = Utf8(folder.Path() / "plus3.trd");
    folder.File("plus3.trd", "original");

    const FloppySaveResult strict = FloppyFormats::Save(context, plus3, trd, /*allowRetarget*/ false);
    EXPECT_FALSE(strict.saved);
    EXPECT_FALSE(strict.reason.empty());

    const FloppySaveResult retargeted = FloppyFormats::Save(context, plus3, trd, /*allowRetarget*/ true);
    ASSERT_TRUE(retargeted.saved) << retargeted.reason;
    EXPECT_TRUE(retargeted.retargeted);
    EXPECT_EQ(retargeted.savedPath, Utf8(folder.Path() / "plus3.udi"));
    EXPECT_EQ(ReadAll(trd), "original") << "the refused file is left alone";
    EXPECT_EQ(FloppyFormats::Probe(retargeted.savedPath), "udi");

    const FloppySaveResult hobeta = FloppyFormats::Save(context, plus3, Utf8(folder.Path() / "file.$C"), true);
    EXPECT_FALSE(hobeta.saved);
    EXPECT_NE(hobeta.reason.find("Hobeta"), std::string::npos) << hobeta.reason;
    EmulatorTestHelper::CleanupEmulator(emulator);
}
