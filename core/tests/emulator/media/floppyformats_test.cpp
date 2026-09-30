// FloppyFormats: content-first format detection, loading and writing of floppy images

#include <gtest/gtest.h>

#include <algorithm>
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

/// Raw PC floppies (720 KB, 1.44 MB) have no signature: the size names them, whatever the extension, and the
/// MGT rule for .img (819 200 bytes) is untouched. The files are grown sparse: Probe reads only the head
TEST(FloppyFormats_Test, RawPcFloppyIsRecognizedBySize)
{
    ScratchFolder folder("floppy-rawpc-probe");
    auto sized = [&](const char* name, size_t size, const std::string& head)
    {
        const std::filesystem::path path = folder.File(name, head);
        std::filesystem::resize_file(path, size);
        return Utf8(path);
    };
    std::string head(0x900, '\xF6');
    head[0x8E7] = '\x10';  // where a TR-DOS disk has its id: a PC disk may carry the byte too

    EXPECT_EQ(FloppyFormats::Probe(sized("dss.img", 1474560, head)), "rawpc");
    EXPECT_EQ(FloppyFormats::Probe(sized("cpm.ima", 737280, head)), "rawpc");
    EXPECT_EQ(FloppyFormats::Probe(sized("disk.dat", 737280, "")), "rawpc");
    EXPECT_EQ(FloppyFormats::Probe(sized("mgt-sized.img", 819200, "")), "mgt");
    EXPECT_EQ(FloppyFormats::Probe(sized("big.img", 1474560 + 512, "")), "") << "only the exact sizes";
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/mgt/synthetic.img")), "mgt");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/mgt/synthetic.mgt")), "mgt");
    EXPECT_EQ(FloppyFormats::Probe(Fixture("testdata/loaders/trd/EyeAche.trd")), "trd");

    const std::vector<std::string> extensions = FloppyFormats::Extensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "img"), extensions.end());
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "ima"), extensions.end());
}

/// Load names the format; Save to .img writes a raw PC disk back as a raw dump (not MGT), and a disk the guest
/// made irregular is refused with the reason, or goes to <stem>.udi when retargeting is allowed
TEST(FloppyFormats_Test, RawPcFloppyLoadsAndSavesThroughTheRegistry)
{
    ScratchFolder folder("floppy-rawpc-save");
    std::string dd(737280, '\0');
    for (size_t i = 0; i < dd.size(); i++)
        dd[i] = static_cast<char>((i / 512) * 7 + i);
    const std::string source = Utf8(folder.File("cpm.img", dd));

    std::unique_ptr<DiskImage> disk;
    std::string format;
    const MediaResult loaded = FloppyFormats::Load(nullptr, source, disk, format);
    ASSERT_TRUE(loaded.Ok()) << loaded.message;
    EXPECT_EQ(format, "rawpc");
    ASSERT_NE(disk, nullptr);
    EXPECT_EQ(disk->getCylinders(), 80);
    EXPECT_FALSE(disk->isFortyTrack());

    const std::string copy = Utf8(folder.Path() / "copy.img");
    const FloppySaveResult saved = FloppyFormats::Save(nullptr, *disk, copy, /*allowRetarget*/ false);
    ASSERT_TRUE(saved.saved) << saved.reason;
    EXPECT_EQ(ReadAll(copy), dd);

    disk->getTrackForCylinderAndSide(5, 1)->formatTrack(5, 1, DiskImage::TrackFormatSpec::plusD());  // 10 sectors
    const FloppySaveResult refused = FloppyFormats::Save(nullptr, *disk, copy, /*allowRetarget*/ false);
    EXPECT_FALSE(refused.saved);
    EXPECT_NE(refused.reason.find("cylinder 5 side 1"), std::string::npos) << refused.reason;
    const FloppySaveResult retargeted = FloppyFormats::Save(nullptr, *disk, copy, /*allowRetarget*/ true);
    ASSERT_TRUE(retargeted.saved) << retargeted.reason;
    EXPECT_TRUE(retargeted.retargeted);
    EXPECT_EQ(retargeted.savedPath, Utf8(folder.Path() / "copy.udi"));
    EXPECT_EQ(ReadAll(copy), dd) << "the refused file is left alone";
}

/// .img is shared with MGT: a +D disk saved as .img stays an MGT dump
TEST(FloppyFormats_Test, MgtDiskSavedAsImgStaysMgt)
{
    ScratchFolder folder("floppy-mgt-img");
    std::unique_ptr<DiskImage> mgt;
    std::string format;
    ASSERT_TRUE(FloppyFormats::Load(nullptr, Fixture("testdata/loaders/mgt/synthetic.img"), mgt, format).Ok());
    EXPECT_EQ(format, "mgt");
    const std::string copy = Utf8(folder.Path() / "mgt-copy.img");
    const FloppySaveResult saved = FloppyFormats::Save(nullptr, *mgt, copy, false);
    ASSERT_TRUE(saved.saved) << saved.reason;
    EXPECT_EQ(ReadAll(copy), ReadAll(Fixture("testdata/loaders/mgt/synthetic.img")));
}
