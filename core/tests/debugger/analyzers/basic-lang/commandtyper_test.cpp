#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"

using Outcome = CommandTyper::Outcome;
using Failure = CommandTyper::Failure;

/// CommandTyper on every editor of the matrix (input-verification.md §10):
/// commands are typed through the keyboard matrix and every step is proven by
/// the ROM's control points. Frames run synchronously; each command takes only
/// the frames the ROM needs (idle, take, insert, per key).
class CommandTyper_Test : public RomEditorFixture, public ::testing::WithParamInterface<std::string>
{
protected:
    CommandTyper* _typer = nullptr;

    void Ready()
    {
        BootEditor(GetParam());
        ASSERT_FALSE(HasFatalFailure());
        _typer = _context->pDebugManager->GetCommandTyper();
        ASSERT_NE(_typer, nullptr);
        // BootEditor left a `0` on the line: clear it the verified way
        Run("", Options(false));
    }

    static CommandTyper::Options Options(bool enter, bool waitForReport = false)
    {
        CommandTyper::Options options;
        options.pressEnter = enter;
        options.waitForReport = waitForReport;
        return options;
    }

    CommandTyper::Result Run(const std::string& command, const CommandTyper::Options& options)
    {
        EXPECT_TRUE(_typer->Request(command, options));
        RunUntil([&] { return _typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        EXPECT_EQ(_typer->GetStatus(), CommandTyper::Status::Done) << "typer still running: " << command;
        return _typer->GetResult();
    }

    CommandTyper::Result Run(const std::string& command) { return Run(command, Options(true)); }

    bool IsTrDos() const { return GetParam().find("TRDOS") != std::string::npos; }
    /// The 128K and +3 editors: keywords spelled, tokenised at ENTER
    bool Is128Editor() const
    {
        return GetParam().find("128BASIC") != std::string::npos || GetParam() == "Plus3-3BASIC";
    }

    static std::string Describe(const CommandTyper::Result& r)
    {
        std::ostringstream out;
        out << CommandTyper::OutcomeName(r.outcome) << " / " << CommandTyper::FailureName(r.failure) << " '"
            << r.message << "' editor=" << ROMControlPoints::RomName(r.editor) << " typed=" << r.bytesTyped
            << " frames=" << r.frames << " ERR_NR=" << int(r.errNr) << "\n";
        for (const EditorMonitor::Event& e : r.cyclogram)
        {
            out << "  f" << e.frame << " " << ROMControlPoints::PointName(e.point) << " A=" << int(e.a)
                << " LAST_K=" << int(e.lastK) << " ERR_NR=" << int(e.errNr);
            if (e.repeats)
                out << " x" << e.repeats + 1;
            out << "\n";
        }
        return out.str();
    }
};

INSTANTIATE_TEST_SUITE_P(Editors, CommandTyper_Test, ::testing::ValuesIn(RomEditorFixture::RomEditors()),
                         RomEditorFixture::ParamName);

TEST_P(CommandTyper_Test, LoadStartsTheTapeLoader)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS LOAD is a disk command";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("LOAD \"\"");
    EXPECT_EQ(r.outcome, Outcome::Started) << Describe(r);
    EXPECT_EQ(r.bytesTyped, Is128Editor() ? 7u : 3u) << Describe(r);  // LOAD token + "" / L,O,A,D, ,",""
}

TEST_P(CommandTyper_Test, DirectCommandFinishesWithZeroOk)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS: see TrDosCommand";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("PRINT 7", Options(true, true));
    EXPECT_EQ(r.outcome, Outcome::Finished) << Describe(r);
    EXPECT_EQ(r.errNr, 0xFF) << "report is not 0 OK\n" << Describe(r);
    RunFrames(2);
    EXPECT_TRUE(ScreenHas("7")) << Screen();
}

TEST_P(CommandTyper_Test, RepeatedCharactersQuotesAndCapitals)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS: see TrDosCommand";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("PRINT \"AAbb\"\"1100\"", Options(true, true));
    EXPECT_EQ(r.outcome, Outcome::Finished) << Describe(r);
    EXPECT_EQ(r.errNr, 0xFF) << Describe(r);
    RunFrames(2);
    EXPECT_TRUE(ScreenHas("AAbb\"1100")) << Screen();
}

TEST_P(CommandTyper_Test, NumberedLineIsStored)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS has no program lines";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("10 PRINT 7");
    EXPECT_EQ(r.outcome, Outcome::Stored) << Describe(r);
}

TEST_P(CommandTyper_Test, SyntaxErrorIsReportedAndNothingRuns)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS: see TrDosCommand";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("PRINT 7+");
    EXPECT_EQ(r.outcome, Outcome::SyntaxError) << Describe(r);
    EXPECT_NE(r.errNr, 0xFF) << Describe(r);
}

TEST_P(CommandTyper_Test, KeywordThroughExtendedMode)
{
    if (IsTrDos() || Is128Editor())
        GTEST_SKIP() << "48K editor keywords only (the 128K editor spells them)";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    // CODE is E mode + I
    const CommandTyper::Result r = Run("PRINT CODE \"A\"", Options(true, true));
    EXPECT_EQ(r.outcome, Outcome::Finished) << Describe(r);
    RunFrames(2);
    EXPECT_TRUE(ScreenHas("65")) << Screen();
}

TEST_P(CommandTyper_Test, UntypableTextIsRefused)
{
    if (IsTrDos() || Is128Editor())
        GTEST_SKIP() << "the 48K editor's K mode";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    // A statement cannot start with a lower-case letter: K mode gives keywords
    const CommandTyper::Result r = Run("a=1");
    EXPECT_EQ(r.outcome, Outcome::Failed) << Describe(r);
    EXPECT_EQ(r.failure, Failure::CannotType) << Describe(r);
    EXPECT_EQ(r.bytesTyped, 0u);
}

TEST_P(CommandTyper_Test, RunningProgramIsBusy)
{
    if (IsTrDos())
        GTEST_SKIP() << "TR-DOS has no program lines";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(Run("10 GO TO 10").outcome, Outcome::Stored);
    ASSERT_EQ(Run("RUN").outcome, Outcome::Started);

    CommandTyper::Options options = Options(true);
    options.idleFrames = 25;
    const CommandTyper::Result r = Run("PRINT 7", options);
    EXPECT_EQ(r.outcome, Outcome::Failed) << Describe(r);
    EXPECT_EQ(r.failure, Failure::Busy) << Describe(r);
    EXPECT_EQ(r.bytesTyped, 0u);
}

TEST_P(CommandTyper_Test, TrDosCommandIsRecognised)
{
    if (!IsTrDos())
        GTEST_SKIP() << "TR-DOS only";
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));

    const CommandTyper::Result r = Run("CAT");
    EXPECT_EQ(r.outcome, Outcome::TrDosCommand) << Describe(r);
    EXPECT_TRUE(r.trdos) << Describe(r);
}

// No disk: TR-DOS activates the drive before it looks the command up, and the
// failure is reported with TR-DOS's error code rather than as success
TEST_P(CommandTyper_Test, TrDosWithoutDiskIsRejected)
{
    if (!IsTrDos())
        GTEST_SKIP() << "TR-DOS only";
    Ready();
    ASSERT_FALSE(HasFatalFailure());

    const CommandTyper::Result r = Run("CAT");
    EXPECT_EQ(r.outcome, Outcome::Failed) << Describe(r);
    EXPECT_EQ(r.failure, Failure::TrDosRejected) << Describe(r);
    EXPECT_EQ(r.errNr, 26) << Describe(r);
}

/// TypeAndWait: the automation-thread entry (WebAPI basic/run, CLI) on a
/// running emulator thread. This one drives the real emulator thread, so it
/// waits on the wall clock (TestWait) but only until the outcome arrives.
TEST(CommandTyperThreaded_Test, TypeAndWaitOnARunningEmulator)
{
    MessageCenter::DisposeDefaultMessageCenter();
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("typer-thread", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    context->config.reset_rom = RM_SOS;
    emulator->Reset();
    context->pFeatureManager->setFeature(Features::kScreenHQ, false);
    context->pFeatureManager->setFeature(Features::kSoundHQ, false);
    emulator->EnableTurboMode(false);
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator->GetState() == StateRun; }, std::chrono::seconds(5)));

    CommandTyper::Options options;
    options.waitForReport = true;
    const CommandTyper::Result r = CommandTyper::TypeAndWait(*emulator, "PRINT 7", options, 20000);
    EXPECT_EQ(r.outcome, Outcome::Finished) << CommandTyper::FailureName(r.failure) << " " << r.message;
    EXPECT_EQ(r.errNr, 0xFF);

    // Paused: nothing can be typed or proven, and the answer comes at once
    emulator->Pause();
    ASSERT_TRUE(emulator->WaitForPauseConfirmation(2000));
    const CommandTyper::Result paused = CommandTyper::TypeAndWait(*emulator, "PRINT 7", CommandTyper::Options{}, 20000);
    EXPECT_EQ(paused.outcome, Outcome::Failed);
    EXPECT_EQ(paused.failure, Failure::EmulatorPaused);

    emulator->Stop();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    MessageCenter::DisposeDefaultMessageCenter();
}

/// From the 128K main menu: the menu reads keys in the editor's loop, so the
/// typer first leaves it for 128 BASIC (cursor to the item, ENTER), each key
/// proven, then types. Also from a menu whose highlight was moved below.
class CommandTyperMenu_Test : public RomEditorFixture, public ::testing::WithParamInterface<std::string>
{
};

INSTANTIATE_TEST_SUITE_P(Machines, CommandTyperMenu_Test, ::testing::Values("128k", "PENTAGON", "SCORPION", "PLUS3"));

TEST_P(CommandTyperMenu_Test, CommandFromTheMainMenu)
{
    Boot(GetParam(), RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());
    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();

    CommandTyper::Options options;
    options.waitForReport = true;
    ASSERT_TRUE(typer->Request("PRINT 7", options));
    RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
    const CommandTyper::Result r = typer->GetResult();
    EXPECT_EQ(r.outcome, Outcome::Finished) << CommandTyper::FailureName(r.failure) << " " << r.message;
    EXPECT_EQ(r.editor, GetParam() == "PLUS3" ? ROMControlPoints::RomKind::Plus3Rom0 : ROMControlPoints::RomKind::Editor128);
    EXPECT_EQ(r.errNr, 0xFF);
}

TEST_P(CommandTyperMenu_Test, CommandFromAMenuHighlightedBelow)
{
    Boot(GetParam(), RM_128, "48 BASIC");
    ASSERT_FALSE(HasFatalFailure());
    for (int i = 0; i < 3; i++)
        TapUntilTaken(ZXKEY_EXT_DOWN, 0x0A);  // highlight "48 BASIC"
    ASSERT_FALSE(HasFatalFailure());
    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();

    ASSERT_TRUE(typer->Request("PRINT 7", CommandTyper::Options{}));
    RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
    const CommandTyper::Result r = typer->GetResult();
    EXPECT_EQ(r.outcome, Outcome::Started) << CommandTyper::FailureName(r.failure) << " " << r.message;
    EXPECT_EQ(r.editor, GetParam() == "PLUS3" ? ROMControlPoints::RomKind::Plus3Rom0 : ROMControlPoints::RomKind::Editor128);
}
