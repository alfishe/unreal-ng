// The DISCiPLE system disk (docs/inprogress/2026-11-15-mgt-plus-d/TODO.md P7.2): the GDOS 3d system tape's CONFIG run
// on a 48K-DISCIPLE with its default answers writes the GDOS system file onto a blank disk in drive 1. The tape is not
// in the repository: UNREAL_DISCIPLE_SYSTEM_TAPE names it, UNREAL_DISCIPLE_SYSTEM_OUT where the disk goes. Without
// them ConfigWritesTheSystemDisk is skipped; the disk it wrote is data/boot/disciple/gdos-3d-system.mgt (recipe in
// data/boot/disciple/README.md), which BootsGdos3d boots.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "emulator/media/mediamanager.h"
#include "emulator/slots/cards/plusd/plusdcard.h"

namespace
{

const char* const kSystemDisk = "data/boot/disciple/gdos-3d-system.mgt";

class DiscipleSystemDisk_Test : public RomEditorFixture
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

    /// A blank 800K .mgt (80 cylinders, 2 sides, 10 sectors of 512 bytes)
    static std::string BlankMgt(const std::string& name)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const std::vector<char> zeros(819200, 0);
        file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        return path;
    }

    void BootWithSystemDisk(bool menu128)
    {
        if (menu128)
        {
            // The 128K ROM's own reset and menu (the card holds its traps off for the reset), then 128 BASIC
            Boot("128K-DISCIPLE", RM_128, "48 BASIC");
            ASSERT_FALSE(HasFatalFailure());
            Enter128Editor();
        }
        else
        {
            Boot("48K-DISCIPLE", RM_SOS, "1982 Sinclair");
        }
        ASSERT_FALSE(HasFatalFailure());
        _typer = _context->pDebugManager->GetCommandTyper();
        ASSERT_NE(_typer, nullptr);
        MediaSource source;
        source.path = (TestPathHelper::FindProjectRoot() / kSystemDisk).string();
        InsertOptions options;
        options.writeProtect = true;
        const MediaResult inserted = _context->pMediaManager->Insert("fdd.disciple1", source, options);
        ASSERT_TRUE(inserted.Ok()) << inserted.message;
    }
};

}  // namespace

/// CONFIG answered with its defaults formats drive 1 and saves the system file; the screens go to the log
TEST_F(DiscipleSystemDisk_Test, ConfigWritesTheSystemDisk)
{
    const char* tape = std::getenv("UNREAL_DISCIPLE_SYSTEM_TAPE");
    if (tape == nullptr || *tape == '\0')
        GTEST_SKIP() << "UNREAL_DISCIPLE_SYSTEM_TAPE (GDOS 3d system tape .tzx) not set";

    Boot("48K-DISCIPLE", RM_SOS, "1982 Sinclair");
    ASSERT_FALSE(HasFatalFailure());
    _typer = _context->pDebugManager->GetCommandTyper();
    ASSERT_NE(_typer, nullptr);

    const std::string disk = BlankMgt("disciple-system.mgt");
    MediaSource source;
    source.path = disk;
    const MediaResult inserted = _context->pMediaManager->Insert("fdd.disciple1", source, InsertOptions{});
    ASSERT_TRUE(inserted.Ok()) << inserted.message;

    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(tape, &error)) << error;
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kFastTape, true));
    Type("LOAD \"\"");
    ASSERT_FALSE(HasFatalFailure());

    // The answers, by what the screen asks: every question its default (ENTER), a FORMAT confirmation Y, and N to
    // "repeat" at the end
    std::string last;
    auto settle = [&](const std::string& what) {
        RunUntil([&] { return !_keys->IsSequenceRunning(); }, 200);
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
    for (int step = 0; step < 400 && !finished; step++)
    {
        char answer = '~';
        if (last.find("ENSURE DISC") != std::string::npos)
        {
            // "ENSURE DISC IN DRIVE 1 THEN PRESS ENTER": the disk is there (ENTER below)
        }
        else if (last.find("Are you SURE") != std::string::npos || last.find("ARE YOU SURE") != std::string::npos)
        {
            answer = 'Y';
            formatted = true;
        }
        else if (last.find("REPEAT") != std::string::npos || last.find("Repeat") != std::string::npos ||
                 last.find("ANOTHER") != std::string::npos || last.find("Another") != std::string::npos)
        {
            answer = 'N';
            finished = true;
        }
        _keys->TapKey(answer == '~' ? ZXKEY_ENTER : static_cast<ZXKeysEnum>(answer));
        settle("step " + std::to_string(step) + " '" + answer + "'");
    }
    EXPECT_TRUE(formatted) << "CONFIG never asked to confirm the FORMAT";
    EXPECT_TRUE(finished) << "CONFIG never reached its end:\n" << last;

    const char* outEnv = std::getenv("UNREAL_DISCIPLE_SYSTEM_OUT");
    const std::string out = outEnv != nullptr && *outEnv != '\0'
                                ? outEnv
                                : TestPathHelper::GetUniqueTestScratchPath("disciple-system-out.mgt");
    const MediaResult exported = _context->pMediaManager->Export("fdd.disciple1", out);
    ASSERT_TRUE(exported.Ok()) << exported.message;
    std::cout << "---- disk written to " << out << "\n";
}

/// GDOS 3d boots on a 48K from the system disk (RUN loads the system file) and CAT 1 lists what CONFIG saved
TEST_F(DiscipleSystemDisk_Test, BootsGdos3d)
{
    BootWithSystemDisk(false);
    ASSERT_FALSE(HasFatalFailure());
    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("GDOS 3"); }, 500)) << "the system file never loaded:\n" << Screen();
    EXPECT_TRUE(ScreenHas("DOS 3d")) << Screen();

    Type("CAT 1");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("CONFIG"); }, 500)) << "no catalogue:\n" << Screen();
    EXPECT_TRUE(ScreenHas("SYSTEM 3d")) << Screen();
}

/// The same on a 128K: the card's hold-off lets the 128K ROM start, GDOS 3d loads from 128 BASIC and CAT 1 lists
TEST_F(DiscipleSystemDisk_Test, BootsGdos3dOn128K)
{
    BootWithSystemDisk(true);
    ASSERT_FALSE(HasFatalFailure());
    Type("RUN");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("GDOS 3"); }, 500)) << "the system file never loaded:\n" << Screen();
    EXPECT_TRUE(ScreenHas("DOS 3d")) << Screen();
    // GDOS ends with the 48K report "O.K. GDOS 3, 0:1"; the 128 editor takes the next key to clear it and redraws
    _keys->TapKey(ZXKEY_ENTER);
    ASSERT_TRUE(RunUntil([&] { return !ScreenHas("O.K."); }, 300)) << Screen();
    RunFrames(25);
    // The 128 editor takes keywords letter by letter: each key until the ROM has it (LAST_K)
    const std::pair<ZXKeysEnum, uint8_t> keys[] = {{ZXKEY_C, 'c'}, {ZXKEY_A, 'a'}, {ZXKEY_T, 't'},
                                                   {ZXKEY_SPACE, ' '}, {ZXKEY_1, '1'}, {ZXKEY_ENTER, 0x0D}};
    for (const auto& [key, code] : keys)
    {
        TapUntilTaken(key, code);
        ASSERT_FALSE(HasFatalFailure());
    }
    EXPECT_TRUE(RunUntil([&] { return ScreenHas("CONFIG"); }, 500)) << Screen();
    EXPECT_TRUE(ScreenHas("SYSTEM 3d")) << Screen();
}
