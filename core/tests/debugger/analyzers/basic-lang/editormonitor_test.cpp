#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "debugger/analyzers/basic-lang/editormonitor.h"

using ROMControlPoints::Point;
using ROMControlPoints::RomKind;

/// EditorMonitor against the real ROMs (input-verification.md §4): every
/// control point must mean what the table says, on every editor of the matrix.
/// Keys are typed through the matrix and taken by the ROM (TapUntilTaken); the
/// monitor's events are then checked for the expected points in order.
class EditorMonitor_Test : public RomEditorFixture, public ::testing::WithParamInterface<std::string>
{
protected:
    EditorMonitor* _monitor = nullptr;

    void BootAndArm()
    {
        BootEditor(GetParam());
        ASSERT_FALSE(HasFatalFailure());
        _monitor = _context->pDebugManager->GetEditorMonitor();
        ASSERT_NE(_monitor, nullptr);
        _monitor->ClearEvents();
        _monitor->Arm();
    }

    /// The 128K and +3 editors: keywords spelled, tokenised at ENTER
    bool Is128Editor() const
    {
        return GetParam().find("128BASIC") != std::string::npos || GetParam().find("-3BASIC") != std::string::npos;
    }

    /// Types `key` and waits until the ROM has taken it; returns the code the
    /// ROM stored in LAST_K
    uint8_t Type(ZXKeysEnum key)
    {
        _context->pMemory->DirectWriteToZ80Memory(0x5C08, 0x00);
        _keys->TapKey(key);
        RunUntil([&] { return SysVar(0x5C08) != 0 && (SysVar(0x5C3B) & 0x20) == 0 && !_keys->IsSequenceRunning(); },
                 60);
        return SysVar(0x5C08);
    }

    void TypeAll(const std::vector<ZXKeysEnum>& keys)
    {
        for (ZXKeysEnum key : keys)
            Type(key);
    }

    /// Keys for `PRINT 7`: one keyword key in the 48K editor, letters in the
    /// 128K editor (it tokenises at ENTER)
    std::vector<ZXKeysEnum> PrintSeven() const
    {
        if (Is128Editor())
            return { ZXKEY_P, ZXKEY_R, ZXKEY_I, ZXKEY_N, ZXKEY_T, ZXKEY_SPACE, ZXKEY_7 };
        return { ZXKEY_P, ZXKEY_7 };
    }

    std::string Trace() const
    {
        std::ostringstream out;
        for (const EditorMonitor::Event& e : _monitor->Events())
        {
            out << "  f" << e.frame << " " << ROMControlPoints::PointName(e.point) << " ("
                << ROMControlPoints::RomName(e.rom) << ") A=" << int(e.a) << " LAST_K=" << int(e.lastK)
                << " ERR_NR=" << int(e.errNr) << "\n";
        }
        return out.str();
    }

    /// Index of the first `point` at or after `from`, or -1
    int Find(Point point, int from = 0) const
    {
        const auto& events = _monitor->Events();
        for (size_t i = static_cast<size_t>(from); i < events.size(); i++)
        {
            if (events[i].point == point)
                return static_cast<int>(i);
        }
        return -1;
    }

    const EditorMonitor::Event& At(int index) const { return _monitor->Events()[static_cast<size_t>(index)]; }

    /// The points appear in this order (others may be in between)
    void ExpectSequence(const std::vector<Point>& points)
    {
        int at = 0;
        for (Point point : points)
        {
            const int found = Find(point, at);
            ASSERT_GE(found, 0) << "missing " << ROMControlPoints::PointName(point) << " in:\n" << Trace();
            at = found + 1;
        }
    }
};

/// Each test runs on the editors it applies to, none is skipped: keys on every editor, BASIC lines on the
/// editors that run BASIC, the TR-DOS hand-over at the A> prompt
class EditorMonitorBasic_Test : public EditorMonitor_Test
{
};

class EditorMonitorTrDos_Test : public EditorMonitor_Test
{
};

INSTANTIATE_TEST_SUITE_P(Editors, EditorMonitor_Test, ::testing::ValuesIn(RomEditorFixture::RomEditors()),
                         RomEditorFixture::ParamName);
INSTANTIATE_TEST_SUITE_P(Editors, EditorMonitorBasic_Test, ::testing::ValuesIn(RomEditorFixture::BasicEditors()),
                         RomEditorFixture::ParamName);
INSTANTIATE_TEST_SUITE_P(Editors, EditorMonitorTrDos_Test, ::testing::ValuesIn(RomEditorFixture::TrDosEditors()),
                         RomEditorFixture::ParamName);

// A character key: taken, accepted with its code, inserted
TEST_P(EditorMonitor_Test, CharacterKeyIsTakenAndInserted)
{
    BootAndArm();
    ASSERT_FALSE(HasFatalFailure());

    const uint8_t code = Type(ZXKEY_7);
    RunFrames(2);
    ASSERT_EQ(code, '7');

    // From the moment the ROM took the `7` (the readiness `0` of BootEditor can
    // still be finishing on the slower +3 editor)
    int taken = -1;
    for (size_t i = 0; i < _monitor->Events().size(); i++)
    {
        if (At(static_cast<int>(i)).point == Point::KeyTaken && At(static_cast<int>(i)).lastK == '7')
        {
            taken = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(taken, 0) << Trace();
    const int accepted = Find(Point::KeyAccepted, taken);
    const int inserted = Find(Point::CharInserted, taken);
    ASSERT_GE(accepted, 0) << Trace();
    ASSERT_GE(inserted, accepted) << Trace();
    EXPECT_EQ(At(accepted).a, '7') << Trace();
    EXPECT_EQ(At(inserted).a, '7') << Trace();
    EXPECT_EQ(Find(Point::Rasp), -1) << Trace();
}

// A direct command: ENTER, syntax OK, runs, reports 0 OK
TEST_P(EditorMonitorBasic_Test, DirectCommandRunsAndReports)
{
    BootAndArm();
    ASSERT_FALSE(HasFatalFailure());

    // The first `0` of BootEditor is still on the line: delete it
    Type(ZXKEY_EXT_DELETE);
    TypeAll(PrintSeven());
    Type(ZXKEY_ENTER);
    RunFrames(10);

    // The 128K editor has no separate "accepted" point: the syntax result is it
    if (Is128Editor())
        ExpectSequence({ Point::Enter, Point::SyntaxResult, Point::ExecStart, Point::Report });
    else
        ExpectSequence({ Point::Enter, Point::SyntaxResult, Point::LineAccepted, Point::ExecStart, Point::Report });
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(At(Find(Point::SyntaxResult)).errNr, 0xFF) << "syntax check failed:\n" << Trace();
    EXPECT_EQ(At(Find(Point::Report)).errNr, 0xFF) << "report is not 0 OK:\n" << Trace();
    EXPECT_TRUE(ScreenHas("7")) << Screen();
}

// A numbered line is stored, not run
TEST_P(EditorMonitorBasic_Test, NumberedLineIsStored)
{
    BootAndArm();
    ASSERT_FALSE(HasFatalFailure());

    // `0` is on the line already: type `1` first, then the keyword
    Type(ZXKEY_EXT_DELETE);
    Type(ZXKEY_1);
    Type(ZXKEY_0);
    TypeAll(PrintSeven());
    Type(ZXKEY_ENTER);
    RunFrames(10);

    if (Is128Editor())
        ExpectSequence({ Point::Enter, Point::SyntaxResult, Point::LineStored });
    else
        ExpectSequence({ Point::Enter, Point::SyntaxResult, Point::LineAccepted, Point::LineStored });
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(At(Find(Point::SyntaxResult)).errNr, 0xFF) << Trace();
    EXPECT_EQ(Find(Point::ExecStart), -1) << "a numbered line must not run:\n" << Trace();
}

// A syntax error: checked, not accepted, not run
TEST_P(EditorMonitorBasic_Test, SyntaxErrorIsDetectedAndNothingRuns)
{
    BootAndArm();
    ASSERT_FALSE(HasFatalFailure());

    // `0+`: a line number followed by nonsense
    Type(ZXKEY_EXT_PLUS);
    Type(ZXKEY_ENTER);
    RunFrames(20);

    ExpectSequence({ Point::Enter, Point::SyntaxResult });
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_NE(At(Find(Point::SyntaxResult)).errNr, 0xFF) << "syntax error not flagged:\n" << Trace();
    EXPECT_EQ(Find(Point::LineAccepted), -1) << Trace();
    EXPECT_EQ(Find(Point::ExecStart), -1) << Trace();
}

// TR-DOS: the 48K editor hands the line back to TR-DOS, which looks it up
TEST_P(EditorMonitorTrDos_Test, TrDosLineGoesToTheDispatcher)
{
    BootAndArm();
    ASSERT_FALSE(HasFatalFailure());

    Type(ZXKEY_ENTER);
    RunFrames(20);

    ExpectSequence({ Point::Enter, Point::TrDosLineBack, Point::TrDosDispatch });
    ASSERT_FALSE(HasFatalFailure());
}

// RAM at #0000, or a ROM we do not know: nothing is recorded
TEST(EditorMonitorUnit_Test, NothingRecordedWhileDisarmed)
{
    MessageCenter::DisposeDefaultMessageCenter();
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("em-unit", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    context->config.reset_rom = RM_SOS;
    emulator->Reset();
    auto* loop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    EditorMonitor* monitor = context->pDebugManager->GetEditorMonitor();
    ASSERT_NE(monitor, nullptr);

    for (int i = 0; i < 120; i++)
        loop->RunFrame();
    EXPECT_TRUE(monitor->Events().empty());
    ASSERT_FALSE(context->pFeatureManager->isEnabled(Features::kBreakpoints)) << "test precondition";

    monitor->Arm();
    for (int i = 0; i < 5; i++)
        loop->RunFrame();
    EXPECT_FALSE(monitor->Events().empty()) << "the idle 48K editor loop passes WAIT-KEY every frame";

    monitor->Disarm();
    EXPECT_FALSE(context->pFeatureManager->isEnabled(Features::kBreakpoints)) << "Disarm must restore breakpoints";
    EXPECT_FALSE(context->pFeatureManager->isEnabled(Features::kDebugMode)) << "Disarm must restore debug mode";
    monitor->ClearEvents();
    for (int i = 0; i < 5; i++)
        loop->RunFrame();
    EXPECT_TRUE(monitor->Events().empty());

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    MessageCenter::DisposeDefaultMessageCenter();
}
