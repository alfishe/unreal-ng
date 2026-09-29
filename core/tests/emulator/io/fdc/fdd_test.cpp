// FDD mechanics: the disk track under the head, 40-track (48 tpi) media in an
// 80-track (96 tpi) drive, the head's travel. And TR-DOS on the real ROM
// loading from 40-track disks, which steps twice per track in an 80-track drive

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/trdostesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "loaders/disk/loader_hobeta.h"

namespace
{
    class FDD_Test : public ::testing::Test
    {
    protected:
        EmulatorContext* _context = nullptr;

        void SetUp() override { _context = new EmulatorContext(LoggerLevel::LogError); }
        void TearDown() override
        {
            delete _context;
            MessageCenter::DisposeDefaultMessageCenter();
        }
    };

    /// A BASIC "boot" as a Hobeta file: 10 POKE 60000,42, autorun line 10
    std::string BootHobeta()
    {
        const std::vector<uint8_t> program = {0x00, 0x0A, 0x16, 0x00, 0xF4, '6', '0', '0', '0', '0', 0x0E, 0x00, 0x00,
                                              0x60, 0xEA, 0x00, ',', '4', '2', 0x0E, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x0D};
        LoaderHobeta::Header header;
        std::memcpy(header.name, "boot    ", 8);
        header.type = 'B';
        header.start = static_cast<uint16_t>(program.size());
        header.length = static_cast<uint16_t>(program.size());
        header.sectors = 1;
        std::string file(LoaderHobeta::HEADER_SIZE + 256, '\0');
        LoaderHobeta::serializeHeader(header, reinterpret_cast<uint8_t*>(file.data()));
        std::memcpy(file.data() + LoaderHobeta::HEADER_SIZE, program.data(), program.size());
        const uint8_t autorun[] = {0x80, 0xAA, 10, 0};
        std::memcpy(file.data() + LoaderHobeta::HEADER_SIZE + program.size(), autorun, sizeof(autorun));
        return file;
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// RUN "boot" on the real TR-DOS ROM; true when the program ran
    bool RunBoot(Emulator* emulator)
    {
        Memory& memory = *emulator->GetContext()->pMemory;
        memory.DirectWriteToZ80Memory(60000, 0);
        TRDOSTestHelper trdos(emulator);
        trdos.startCommand("RUN \"boot\"");
        return trdos.runUntil([&memory] { return memory.DirectReadFromZ80Memory(60000) == 42; },
                              TRDOSTestHelper::MAX_EXECUTION_CYCLES, 10);
    }
}  // namespace

/// 48 tpi media in a 96 tpi drive: head position p reads cylinder p / 2, an
/// odd position sits between two tracks. A 40-track drive reads them 1:1
TEST_F(FDD_Test, FortyTrackMediumInAnEightyTrackDrive)
{
    DiskImage disk(40, 1);
    disk.setFortyTrack(true);
    FDD fdd(_context);
    fdd.insertDisk(&disk);
    ASSERT_EQ(fdd.getDriveCylinders(), 80) << "Beta 128 drives are 80-track";

    fdd.setTrack(0);
    EXPECT_EQ(fdd.trackUnderHead(0), disk.getTrackForCylinderAndSide(0, 0));
    fdd.setTrack(2);
    EXPECT_EQ(fdd.trackUnderHead(0), disk.getTrackForCylinderAndSide(1, 0));
    fdd.setTrack(3);
    EXPECT_EQ(fdd.trackUnderHead(0), nullptr) << "between two 48 tpi tracks";
    fdd.setTrack(78);
    EXPECT_EQ(fdd.trackUnderHead(0), disk.getTrackForCylinderAndSide(39, 0));

    disk.setFortyTrack(false);  // an 80-track medium: 1:1
    fdd.setTrack(3);
    EXPECT_EQ(fdd.trackUnderHead(0), disk.getTrackForCylinderAndSide(3, 0));

    disk.setFortyTrack(true);
    fdd.setDriveCylinders(40);  // the +3's 3" drive: 48 tpi itself
    EXPECT_EQ(fdd.trackUnderHead(0), disk.getTrackForCylinderAndSide(3, 0));
    fdd.ejectDisk();
}

/// The head stops at the drive's mechanical end: TR-DOS tells a 40-track drive
/// from an 80-track one by seeking to 50 and back to 2, then testing TRK00
TEST_F(FDD_Test, HeadTravelFollowsTheDriveMechanics)
{
    FDD fdd(_context);
    fdd.setTrack(60);
    EXPECT_EQ(fdd.getTrack(), 60);
    fdd.setDriveCylinders(40);
    fdd.setTrack(60);
    EXPECT_EQ(fdd.getTrack(), 42);
    fdd.setTrack(-1);
    EXPECT_TRUE(fdd.isTrack00());
}

/// The real TR-DOS ROM steps twice per track for a 40-track disk in its
/// 80-track drive (it reads the disk type in sector 9): files past cylinder 0
/// load from single- and double-sided 40-track disks, built from a folder or
/// reloaded from a .trd file.
/// Real-ROM TR-DOS runs: slower than 50 ms by nature
TEST(FDD_Trdos_Test, FortyTrackDisksLoadPastCylinderZero)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaManager& manager = *emulator->GetContext()->pMediaManager;

    struct Case
    {
        const char* manifest;
        const char* what;
    };
    // pad.bin (32 sectors) comes first, so boot starts at logical track 3:
    // cylinder 3 on one side, cylinder 1 side 1 on two
    for (const Case& c : {Case{"disk: {format: trd, tracks: 40, sides: 1}\norder: [pad.bin]\n", "40 x 1"},
                          Case{"disk: {format: trd, tracks: 40, sides: 2}\norder: [pad.bin]\n", "40 x 2"}})
    {
        ScratchFolder folder("fdd-forty-track");
        folder.File("pad.bin", std::string(32 * 256, 'p'));
        folder.File("boot.$B", BootHobeta());
        folder.File(".unreal-media.yaml", c.manifest);
        MediaSource source;
        source.path = Utf8(folder.Path());
        InsertOptions options;
        options.disposition = Disposition::Discard;
        ASSERT_TRUE(manager.Insert("fdd.a", source, options).Ok()) << c.what;
        EXPECT_TRUE(RunBoot(emulator)) << c.what << ": boot past cylinder 0 of a folder-built disk";

        // The same disk as a .trd file (its type byte gives the geometry back)
        const std::string trd = Utf8(folder.Path() / "saved.trd");
        const FloppySaveResult saved = FloppyFormats::Save(emulator->GetContext(),
                                                           *manager.GetMedium("fdd.a")->Floppy(), trd, false);
        ASSERT_TRUE(saved.saved) << saved.reason;
        ASSERT_TRUE(emulator->LoadDisk(trd, 0));
        EXPECT_TRUE(manager.GetMedium("fdd.a")->Floppy()->isFortyTrack()) << c.what;
        EXPECT_TRUE(RunBoot(emulator)) << c.what << ": boot past cylinder 0 of a .trd file";
        ASSERT_TRUE(emulator->EjectDisk(0, /*force*/ true));
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
