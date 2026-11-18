// The +D system disk (docs/inprogress/2026-11-15-mgt-plus-d/TODO.md P0.4, owner decision D-2): the Plus D System Tape
// v2a's CONFIG run on a 48K-PLUSD with its default answers writes the G+DOS system file onto a blank disk in drive 1.
// The tape is not in the repository: UNREAL_PLUSD_SYSTEM_TAPE names it, UNREAL_PLUSD_SYSTEM_OUT where the disk goes.
// Without them ConfigWritesTheSystemDisk is skipped; the disk it wrote is data/boot/plusd/gdos-2a-system.mgt (recipe in
// data/boot/plusd/README.md), which CatListsDisk boots.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/media/mediamanager.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "emulator/slots/slotmanager.h"

namespace
{

const char* const kSystemDisk = "data/boot/plusd/gdos-2a-system.mgt";

class PlusDSystemDisk_Test : public RomEditorFixture
{
protected:
    CommandTyper* _typer = nullptr;

    void Type(const std::string& command)
    {
        CommandTyper::Options options;
        options.pressEnter = true;
        ASSERT_TRUE(_typer->Request(command, options));
        ASSERT_TRUE(RunUntil([&] { return _typer->GetStatus() == CommandTyper::Status::Done; }, 4000));
    }

    /// A blank 800K .mgt (80 cylinders, 2 sides, 10 sectors of 512 bytes): every sector present, an empty directory
    static std::string BlankMgt(const std::string& name)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const std::vector<char> zeros(819200, 0);
        file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        return path;
    }
};

}  // namespace

/// CONFIG answered with its defaults formats drive 1 and saves +SYS 2a and itself; the screens go to the log
TEST_F(PlusDSystemDisk_Test, ConfigWritesTheSystemDisk)
{
    const char* tape = std::getenv("UNREAL_PLUSD_SYSTEM_TAPE");
    if (tape == nullptr || *tape == '\0')
        GTEST_SKIP() << "UNREAL_PLUSD_SYSTEM_TAPE (Plus D System Tape v2a .tzx) not set";

    Boot("48K-PLUSD", RM_SOS, "1982 Sinclair");
    ASSERT_FALSE(HasFatalFailure());
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);

    const std::string disk = BlankMgt("plusd-system.mgt");
    MediaSource source;
    source.path = disk;
    const MediaResult inserted = _context->pMediaManager->Insert("fdd.plusd1", source, InsertOptions{});
    ASSERT_TRUE(inserted.Ok()) << inserted.message;

    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(tape, &error)) << error;
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kFastTape, true));
    Type("LOAD \"\"");
    ASSERT_FALSE(HasFatalFailure());

    // The answers, by what the screen asks: every question its default (ENTER), the FORMAT confirmation Y, and N to
    // "repeat" at the end (its default starts CONFIG over)
    std::string last;
    auto settle = [&](const std::string& what) {
        RunUntil([&] { return !_keys->IsSequenceRunning(); }, 200);
        // Until the screen has stood still for 3 seconds (a FORMAT runs for about 40)
        std::string screen = Screen();
        for (int stable = 0, frames = 0; stable < 150 && frames < 20000; frames += 25)
        {
            RunFrames(25);
            const std::string now = Screen();
            stable = now == screen ? stable + 25 : 0;
            screen = now;
        }
        if (screen != last)
            std::cout << "---- " << what << "\n" << screen << "\n";
        last = screen;
    };
    settle("loaded");
    bool formatted = false;
    bool finished = false;
    for (int step = 0; step < 120 && !finished; step++)
    {
        char answer = '~';
        if (last.find("Are you SURE") != std::string::npos)
        {
            answer = 'Y';
            formatted = true;
        }
        else if (last.find("REPEAT?") != std::string::npos)
        {
            answer = 'N';
            finished = true;
        }
        _keys->TapKey(answer == '~' ? ZXKEY_ENTER : static_cast<ZXKeysEnum>(answer));
        settle("step " + std::to_string(step) + " '" + answer + "'");
    }
    EXPECT_TRUE(formatted) << "CONFIG never asked to confirm the FORMAT";
    EXPECT_TRUE(finished) << "CONFIG never reached its end:\n" << last;

    const char* outEnv = std::getenv("UNREAL_PLUSD_SYSTEM_OUT");
    const std::string out = outEnv != nullptr && *outEnv != '\0' ? outEnv
                                                                 : TestPathHelper::GetUniqueTestScratchPath("system-out.mgt");
    const MediaResult exported = _context->pMediaManager->Export("fdd.plusd1", out);
    ASSERT_TRUE(exported.Ok()) << exported.message;
    std::cout << "---- disk written to " << out << "\n";
}

/// G+DOS boots from the system disk (RUN loads +SYS) and CAT 1 lists the five files CONFIG saved
TEST_F(PlusDSystemDisk_Test, CatListsDisk)
{
    Boot("48K-PLUSD", RM_SOS, "1982 Sinclair");
    ASSERT_FALSE(HasFatalFailure());
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);

    MediaSource source;
    source.path = (TestPathHelper::FindProjectRoot() / kSystemDisk).string();
    InsertOptions options;
    options.writeProtect = true;
    const MediaResult inserted = _context->pMediaManager->Insert("fdd.plusd1", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;

    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("G+DOS2a"); }, 500)) << "+SYS never loaded (its banner):\n" << Screen();

    Type("CAT 1");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("CONFIG3_C"); }, 500)) << "no catalogue:\n" << Screen();
    for (const char* name : { "+SYS 2a", "CONFIG", "CONFIG1_C", "CONFIG2_C" })
        EXPECT_TRUE(ScreenHas(name)) << name << " is not listed:\n" << Screen();
}

/// The snapshot button on a 48K (long tier, ~2 s): NMI, G+DOS pages in at #0066 and waits on its menu; 4 saves a 48K
/// snapshot (type 5, 97 sectors) one sector per turn of the disk and returns to the program paged out. After NEW,
/// `LOAD d1"Snap F"S` puts the program back: its border loop runs again
TEST_F(PlusDSystemDisk_Test, SnapshotButton48K)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "long tier (UNREAL_LONG_TESTS=1): about 2 s";
    Boot("48K-PLUSD", RM_SOS, "1982 Sinclair");
    ASSERT_FALSE(HasFatalFailure());
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);
    PlusDCard* card = PlusDCard::Find(_context);
    ASSERT_NE(card, nullptr);

    const std::string disk = TestPathHelper::GetUniqueTestScratchPath("plusd-snapshot.mgt");
    std::filesystem::copy_file(TestPathHelper::FindProjectRoot() / kSystemDisk, disk,
                               std::filesystem::copy_options::overwrite_existing);
    MediaSource source;
    source.path = disk;
    const MediaResult inserted = _context->pMediaManager->Insert("fdd.plusd1", source, InsertOptions{});
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("G+DOS2a"); }, 500)) << Screen();
    Type("10 BORDER 1: GO TO 10");
    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    RunFrames(10);
    ASSERT_EQ(_context->emulatorState.border_attr, 1);

    _emulator->RequestMNI();
    ASSERT_TRUE(RunUntil([&] { return card->IsPaged(); }, 5)) << "the NMI pages the +D in";
    _keys->TapKey(ZXKEY_4);
    ASSERT_TRUE(RunUntil([&] { return !card->IsPaged(); }, 3000)) << "the save never returned to the program";

    const std::string out = disk + ".out.mgt";
    const MediaResult exported = _context->pMediaManager->Export("fdd.plusd1", out);
    ASSERT_TRUE(exported.Ok()) << exported.message;
    std::ifstream file(out, std::ios::binary);
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_EQ(d.size(), 819200u);
    // The sixth directory entry (track 0 sector 3, second half), after +SYS, CONFIG and the three CONFIGn_C
    const uint8_t* entry = d.data() + 2 * 512 + 256;
    EXPECT_EQ(entry[0], 5) << "a 48K snapshot";
    EXPECT_EQ(std::string(entry + 1, entry + 11), "Snap F    ");
    EXPECT_EQ(entry[11] * 256 + entry[12], 97);

    // Stop the program, NEW, change the border; the snapshot brings the loop back
    _keys->TapCombo({ZXKEY_CAPS_SHIFT, ZXKEY_SPACE});
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("BREAK"); }, 200)) << Screen();
    Type("NEW");
    Type("BORDER 4");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(_context->emulatorState.border_attr, 4);
    Type("LOAD d1\"Snap F\"S");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(RunUntil([&] { return _context->emulatorState.border_attr == 1 && !card->IsPaged(); }, 1500))
        << "the snapshot did not resume:\n" << Screen();
}

/// Time travel through a disk read (~1 s): record `CAT 1`, seek back to a frame where the WD1772 is in the middle of a
/// command, then forward to the end: the card's blob (paging, latch, RAM, controller, command in flight) and the
/// catalogue on the screen are the ones the recording had
TEST_F(PlusDSystemDisk_Test, TtdSeekDuringRead)
{
    Boot("48K-PLUSD", RM_SOS, "1982 Sinclair");
    ASSERT_FALSE(HasFatalFailure());
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);
    PlusDCard* card = PlusDCard::Find(_context);
    ASSERT_NE(card, nullptr);
    MediaSource source;
    source.path = (TestPathHelper::FindProjectRoot() / kSystemDisk).string();
    InsertOptions options;
    options.writeProtect = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("fdd.plusd1", source, options).Ok());
    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("G+DOS2a"); }, 500)) << Screen();

    _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    ttd::TTDSerializable* blob = devices[0].device;

    // Type CAT 1 and run it, frame by frame: the first frame boundary with a command in flight
    CommandTyper::Options typed;
    typed.pressEnter = true;
    ASSERT_TRUE(_typer->Request("CAT 1", typed));
    uint64_t busyFrame = 0;
    uint64_t busyHash = 0;
    const bool listed = RunUntil(
        [&] {
            if (busyFrame == 0 && (card->Fdc()->PeekRegister(WD1793::REG_COMMAND_STATUS) & 0x01) != 0)
            {
                busyFrame = _context->emulatorState.frame_counter;
                busyHash = blob->TTDHashState();
            }
            return ScreenHas("CONFIG3_C");
        },
        500);
    ASSERT_TRUE(listed) << Screen();
    ASSERT_NE(busyFrame, 0u) << "the controller was never seen busy at a frame boundary";
    RunFrames(2);
    const uint64_t endFrame = _context->emulatorState.frame_counter;
    const uint64_t endHash = blob->TTDHashState();
    const std::string endScreen = Screen();
    ttd->StopRecording();

    ttd::TTDTimePoint target;
    target.frame = busyFrame;
    target.tInFrame = 0;
    ASSERT_TRUE(ttd->SeekTo(target));
    EXPECT_EQ(blob->TTDHashState(), busyHash) << "the card at frame " << busyFrame;
    EXPECT_NE(card->Fdc()->PeekRegister(WD1793::REG_COMMAND_STATUS) & 0x01, 0) << "the command is in flight again";

    target.frame = endFrame;
    ASSERT_TRUE(ttd->SeekTo(target));
    EXPECT_EQ(blob->TTDHashState(), endHash) << "the card at the end of the replay";
    EXPECT_EQ(Screen(), endScreen);
}
