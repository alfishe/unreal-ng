// The Embedded debugger's time controls (TTD branches debuggers.md §4): nothing shows without a recorded history;
// with one, the back steps, Run Back to the enabled execution breakpoints, Branch Here / Continue Rec named as in
// the TTD panel, the moment ("History: frame ... replaying", "Live, not recorded", "fork here"), and with branches
// the drop-down: switch, the branches leaving here, the management (a delete asks; never the trunk). At a position on
// a sound card's CPU (gs-debugger design §8) the moment names both CPUs: the window shows only the main CPU

#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debuggertimetravel.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotcontrol.h"
#include "widgets/ttdbranchcommands.h"

class DebuggerTimeTravel_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settingsDir.path());
        _machine = EmulatorManager::GetInstance()->CreateEmulatorWithModel("debugger-ttd", "48K", LoggerLevel::LogError);
        ASSERT_NE(_machine, nullptr);
        _machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _machine->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _ttd = _machine->GetContext()->pTimeTravelController;
        _toolbar = _window.addToolBar(QStringLiteral("debugger"));
        _tt = std::make_unique<DebuggerTimeTravel>(&_window, _toolbar, nullptr);
        _tt->commands().confirm = [this](const QString&, const QString& question) {
            _asked = question;
            return _answer;
        };
    }

    void TearDown() override
    {
        _tt.reset();
        if (_ttd)
            _ttd->StopRecording();
        _machine.reset();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    void Record(int frames)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _machine->GetContext()->emulatorState.frame_counter;
        _machine->RunNFrames(frames, true);
        _ttd->StopRecording();
    }

    /// 20 frames, then a resume at the 8th: 6 frames on branch 1, the trunk keeps its old future
    void RecordAFork()
    {
        Record(20);
        ASSERT_TRUE(_ttd->SeekTo({_start + 8, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 8, 0}));
        _machine->RunNFrames(6, true);
        _ttd->StopRecording();
    }

    uint64_t Frame() const { return _ttd->CurrentPosition().frame; }
    /// A toolbar widget's action is shown (the window is never shown: the toolbar lays nothing out)
    bool Visible(QWidget* w) const
    {
        for (QAction* a : _toolbar->actions())
            if (_toolbar->widgetForAction(a) == w)
                return a->isVisible();
        return false;
    }
    size_t Branches() const
    {
        const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
        return info.branches ? info.branches->branches.size() : 0;
    }
    QAction* MenuItem(QMenu* menu, const QString& prefix) const
    {
        for (QAction* a : menu->actions())
            if (a->text().startsWith(prefix))
                return a;
        return nullptr;
    }
    QMenu* OpenedBranchMenu()
    {
        emit _tt->branchMenu()->aboutToShow();
        return _tt->branchMenu();
    }

    QTemporaryDir _settingsDir;
    std::shared_ptr<Emulator> _machine;
    ttd::TimeTravelController* _ttd = nullptr;
    QMainWindow _window;
    QToolBar* _toolbar = nullptr;
    std::unique_ptr<DebuggerTimeTravel> _tt;
    uint64_t _start = 0;
    QString _asked;
    bool _answer = true;
};

TEST_F(DebuggerTimeTravel_Test, NothingShowsWithoutAHistory)
{
    _tt->setEmulator(_machine);
    EXPECT_FALSE(_tt->shown());
    EXPECT_FALSE(_tt->stepBackAction()->isVisible());
    EXPECT_FALSE(Visible(_tt->momentLabel()));

    // A stopped recording: the controls, the machine live past it, recording again is Continue Rec (D-3, rule 7)
    Record(5);
    _tt->refresh();
    EXPECT_TRUE(_tt->shown());
    EXPECT_TRUE(_tt->stepBackAction()->isVisible());
    EXPECT_EQ(_tt->momentLabel()->text(), QStringLiteral("Live, not recorded"));
    EXPECT_EQ(_tt->branchHereAction()->text(), QStringLiteral("Continue Rec"));
    EXPECT_TRUE(_tt->branchHereAction()->isEnabled());
    // Rule 2: the trunk alone shows nothing about branches
    EXPECT_FALSE(Visible(_tt->branchButton()));
    EXPECT_EQ(_tt->stepBackAction()->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F7));
    EXPECT_EQ(_tt->stepOverBackAction()->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F8));
    EXPECT_EQ(_tt->runBackAction()->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F9));
}

TEST_F(DebuggerTimeTravel_Test, InThePastItReplaysAndBranchHereForks)
{
    Record(10);
    ASSERT_TRUE(_ttd->SeekTo({_start + 4, 0}));
    _tt->setEmulator(_machine);
    EXPECT_TRUE(_tt->momentLabel()->text().startsWith(QStringLiteral("History: frame %1 of").arg(_start + 4)))
        << _tt->momentLabel()->text().toStdString();
    EXPECT_TRUE(_tt->momentLabel()->text().contains(QStringLiteral("replaying")));
    EXPECT_EQ(_tt->branchHereAction()->text(), QStringLiteral("Branch Here"));
}

TEST_F(DebuggerTimeTravel_Test, TheBackStepsMoveTheMachine)
{
    Record(10);
    ASSERT_TRUE(_ttd->SeekTo({_start + 6, 0}));
    _tt->setEmulator(_machine);
    int moves = 0;
    QObject::connect(_tt.get(), &DebuggerTimeTravel::moved, [&moves]() { ++moves; });

    _tt->frameBack();
    EXPECT_EQ(Frame(), _start + 5);
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    _tt->stepBack();
    EXPECT_NE(_ttd->CurrentPosition().tInFrame + (_ttd->CurrentPosition().frame << 20),
              before.tInFrame + (before.frame << 20));
    _tt->stepOverBack();
    EXPECT_FALSE(_tt->momentLabel()->toolTip().isEmpty()) << "what the step did";
    EXPECT_TRUE(_tt->momentLabel()->toolTip().startsWith(QStringLiteral("Stepped back")))
        << _tt->momentLabel()->toolTip().toStdString();
    EXPECT_EQ(moves, 3);
}

TEST_F(DebuggerTimeTravel_Test, RunBackStopsAtTheEnabledExecutionBreakpoints)
{
    BreakpointManager& bm = *_machine->GetBreakpointManager();
    EXPECT_EQ(DebuggerTimeTravel::RunBackPcs(bm), "");
    bm.AddExecutionBreakpoint(0x0038);
    const uint16_t off = bm.AddExecutionBreakpoint(0x1234);
    bm.DeactivateBreakpoint(off);
    bm.AddMemReadBreakpoint(0x4000);
    EXPECT_EQ(DebuggerTimeTravel::RunBackPcs(bm), "#0038") << "enabled execution breakpoints only";

    // An instruction the history ran: back to the last time it ran, from the end
    Record(5);
    ASSERT_TRUE(_ttd->SeekTo({_start + 2, 0}));
    const uint16_t pc = _machine->GetContext()->pCore->GetZ80()->pc;
    ASSERT_TRUE(_ttd->SeekTo({_start + 4, 0}));
    bm.ClearUserBreakpoints();
    bm.AddExecutionBreakpoint(pc);
    _tt->setEmulator(_machine);
    EXPECT_TRUE(_tt->runBackAction()->isEnabled());
    _tt->runBack();
    EXPECT_EQ(_machine->GetContext()->pCore->GetZ80()->pc, pc);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%04X", static_cast<unsigned>(pc));
    EXPECT_EQ(_tt->momentLabel()->toolTip(), QStringLiteral("Run Back: at the breakpoint ") + hex);
}

TEST_F(DebuggerTimeTravel_Test, RunBackHonoursBreakpointConditions)
{
    BreakpointManager& bm = *_machine->GetBreakpointManager();
    Record(5);
    ASSERT_TRUE(_ttd->SeekTo({_start + 2, 0}));
    const uint16_t pc = _machine->GetContext()->pCore->GetZ80()->pc;
    ASSERT_TRUE(_ttd->SeekTo({_start + 4, 0}));
    bm.ClearUserBreakpoints();
    const uint16_t id = bm.AddExecutionBreakpoint(pc);
    EXPECT_EQ(DebuggerTimeTravel::RunBackCondition(bm), "") << "no condition: plain pcs";

    // A condition that never holds: Run Back finds nothing and the machine stays
    std::string error;
    ASSERT_TRUE(bm.SetBreakpointCondition(id, "FRAME > 100000000", error)) << error;
    EXPECT_NE(DebuggerTimeTravel::RunBackCondition(bm).find("FRAME"), std::string::npos)
        << DebuggerTimeTravel::RunBackCondition(bm);
    _tt->setEmulator(_machine);
    QString said;
    _tt->inform = [&](const QString&, const QString& text, bool) { said = text; };
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    _tt->runBack();
    EXPECT_EQ(_ttd->CurrentPosition().frame, before.frame) << "the condition held nowhere: no move";
    EXPECT_FALSE(said.isEmpty());

    // One that holds in the history: back to that hit
    ASSERT_TRUE(bm.SetBreakpointCondition(id, "FRAME >= " + std::to_string(_start), error)) << error;
    _tt->runBack();
    EXPECT_EQ(_machine->GetContext()->pCore->GetZ80()->pc, pc);
}

TEST_F(DebuggerTimeTravel_Test, AtAForkTheDropDownNamesTheBranchAndWhatLeavesHere)
{
    RecordAFork();
    ASSERT_TRUE(_ttd->SeekTo({_start + 8, 0}));
    _tt->setEmulator(_machine);
    ASSERT_TRUE(Visible(_tt->branchButton()));
    EXPECT_TRUE(_tt->branchButton()->text().contains(QStringLiteral("(2 branches)")))
        << _tt->branchButton()->text().toStdString();
    EXPECT_TRUE(_tt->momentLabel()->text().contains(QStringLiteral("fork here: 1 other future")))
        << _tt->momentLabel()->text().toStdString();

    QMenu* menu = OpenedBranchMenu();
    QAction* trunk = MenuItem(menu, QStringLiteral("trunk"));
    ASSERT_NE(trunk, nullptr);
    EXPECT_FALSE(trunk->isChecked()) << "the active branch is the fork";
    bool section = false;
    for (QAction* a : menu->actions())
        section = section || (a->isSeparator() && a->text() == QStringLiteral("Branches leaving here"));
    EXPECT_TRUE(section);
    QAction* manage = MenuItem(menu, QStringLiteral("Manage"));
    ASSERT_NE(manage, nullptr);
    ASSERT_NE(manage->menu(), nullptr);
    QAction* trunkMenu = MenuItem(manage->menu(), QStringLiteral("trunk"));
    ASSERT_NE(trunkMenu, nullptr);
    QAction* del = MenuItem(trunkMenu->menu(), QStringLiteral("Delete"));
    ASSERT_NE(del, nullptr);
    EXPECT_FALSE(del->isEnabled()) << "never the trunk";
    EXPECT_NE(MenuItem(menu, QStringLiteral("Show in the TTD panel")), nullptr);

    // Picking the trunk switches to it
    trunk->trigger();
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, ttd::kTTDTrunk);
}

TEST_F(DebuggerTimeTravel_Test, ADeleteAsksAndSwitchesOffTheTimelineFirst)
{
    RecordAFork();
    _tt->setEmulator(_machine);
    _answer = false;
    EXPECT_FALSE(_tt->commands().Delete(1));
    EXPECT_TRUE(_asked.contains(QStringLiteral("switches to 'trunk' first"))) << _asked.toStdString();
    EXPECT_EQ(Branches(), 2u);
    _answer = true;
    EXPECT_TRUE(_tt->commands().Delete(1));
    EXPECT_LT(Branches(), 2u) << "the trunk alone";
    _tt->refresh();
    EXPECT_FALSE(Visible(_tt->branchButton()));
}

TEST_F(DebuggerTimeTravel_Test, BranchHereRecordsANewBranchAndStaysPaused)
{
    Record(10);
    ASSERT_TRUE(_ttd->SeekTo({_start + 4, 0}));
    _tt->setEmulator(_machine);
    _tt->branchHere();
    EXPECT_EQ(_ttd->ReadSessionInfo().state, ttd::TTDSessionState::Recording);
    EXPECT_EQ(Branches(), 2u);
    EXPECT_FALSE(_machine->IsRunning() && !_machine->IsPaused()) << "the debugger leaves the machine for the next step";
    _tt->refresh();
    EXPECT_EQ(_tt->momentLabel()->text().left(9), QStringLiteral("Recording"));
    EXPECT_FALSE(_tt->branchHereAction()->isEnabled()) << "nothing to continue while recording";
}

TEST(TtdGui_Test, TheMomentInParts)
{
    ttd::TTDSessionInfo info;
    EXPECT_TRUE(ttdgui::DescribeMoment(info, 0).Line().isEmpty()) << "no history: nothing";
    info.checkpointCount = 3;
    info.currentEndFrame = 120;
    info.state = ttd::TTDSessionState::Recording;
    EXPECT_EQ(ttdgui::DescribeMoment(info, 120).Line(), QStringLiteral("Recording"));
    info.state = ttd::TTDSessionState::Detached;
    EXPECT_EQ(ttdgui::DescribeMoment(info, 50).Line(), QStringLiteral("History: frame 50 of 120, replaying"));
    info.state = ttd::TTDSessionState::Idle;
    EXPECT_EQ(ttdgui::DescribeMoment(info, 130).Line(), QStringLiteral("Live, not recorded"));
}

TEST(DebuggerTimeTravelCard_Test, TheCardMomentNamesBothCpus)
{
    ttd::TTDCardPosition p;
    p.cpu = DebugCpuId::GS;
    p.known = true;
    p.instruction = 1200;
    p.pc = 0x0A3C;
    p.mainPc = 0x8009;
    EXPECT_TRUE(DebuggerTimeTravel::CardMoment({p}).isEmpty()) << "no card position: the moment is the main CPU's";
    p.cardPosition = true;
    p.caughtUp = true;
    p.inside = true;
    p.tOffset = 7;
    EXPECT_EQ(DebuggerTimeTravel::CardMoment({p}),
              QStringLiteral("gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the next instruction"));
    p.inside = false;
    p.tOffset = 0;
    p.mainPc = 0x800B;
    EXPECT_EQ(DebuggerTimeTravel::CardMoment({p}),
              QStringLiteral("gs at instruction 1200, PC #0A3C; main CPU PC #800B, at its instruction boundary"));
    // The MultiSound's GS by its name; a card without counts never stands at a position
    ttd::TTDCardPosition ms = p;
    ms.cpu = DebugCpuId::MultiSoundGS;
    ttd::TTDCardPosition other;
    other.cpu = DebugCpuId::GS;
    EXPECT_TRUE(DebuggerTimeTravel::CardMoment({other, ms}).startsWith(QStringLiteral("ms at instruction 1200")));
    ms.known = false;
    EXPECT_TRUE(DebuggerTimeTravel::CardMoment({ms}).isEmpty());
}

/// A Pentagon with the General Sound card (its own Z80), time travel on: a card move made elsewhere (the CLI, eCat,
/// MCP) puts the machine at a card position, and the Embedded debugger's moment says where both CPUs stand.
/// Runtime: 3 recorded frames, two card seeks (~0.1 s)
TEST(DebuggerTimeTravelCard_Test, ACardPositionShowsInTheMoment)
{
    QTemporaryDir settings;
    ASSERT_TRUE(settings.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    std::function<void(CONFIG&)> slotOverride;
    std::string error;
    ASSERT_TRUE(SlotControl::CreateOverride({{"zxbus.1", "gs"}}, slotOverride, error)) << error;
    std::shared_ptr<Emulator> machine = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "debugger-ttd-card", "PENTAGON", LoggerLevel::LogError, &error, slotOverride);
    ASSERT_NE(machine, nullptr) << error;
    machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    machine->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* controller = machine->GetContext()->pTimeTravelController;
    machine->RunNFrames(2, true);
    ASSERT_TRUE(controller->StartRecording());
    machine->RunNFrames(3, true);
    controller->StopRecording();

    QMainWindow window;
    QToolBar* toolbar = window.addToolBar(QStringLiteral("debugger"));
    DebuggerTimeTravel tt(&window, toolbar, nullptr);
    tt.setEmulator(machine);
    EXPECT_FALSE(tt.momentLabel()->text().contains(QStringLiteral("gs at"))) << "live past the end: no card position";

    const ttd::TTDCardPosition now = controller->CardPositionNow(DebugCpuId::GS);
    ASSERT_TRUE(now.known) << "the card counts its instructions";
    ASSERT_GT(now.instruction, 3000u);
    // A card position, as a CLI `ttd seek --cpu gs N` makes it; a few back until the card is inside a main
    // instruction (the T offset shows)
    auto execute = [&](const std::string& verb, const std::map<std::string, std::string>& options) {
        const Emulator::ContextLease lease = machine->LeaseContext();
        return ttd::TTDControl(lease.get()).Execute({verb, options});
    };
    ttd::TTDReply seek = execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(now.instruction - 2000)}});
    ASSERT_TRUE(seek.Ok()) << seek.message;
    ttd::TTDCardPosition at = controller->CardPositionNow(DebugCpuId::GS);
    for (int i = 0; i < 40 && !at.inside; ++i)
    {
        ASSERT_TRUE(execute("step-instruction", {{"cpu", "gs"}, {"dir", "back"}}).Ok());
        at = controller->CardPositionNow(DebugCpuId::GS);
    }
    ASSERT_TRUE(at.cardPosition && at.inside);
    tt.refresh();
    const QString label = tt.momentLabel()->text();
    EXPECT_TRUE(label.startsWith(QStringLiteral("History: frame"))) << label.toStdString();
    EXPECT_TRUE(label.contains(DebuggerTimeTravel::CardMoment({at}))) << label.toStdString();
    char text[96];
    std::snprintf(text, sizeof(text), "gs at instruction %llu, PC #%04X; main CPU PC #%04X, +%u T",
                  static_cast<unsigned long long>(at.instruction), static_cast<unsigned>(at.pc),
                  static_cast<unsigned>(at.mainPc), static_cast<unsigned>(at.tOffset));
    EXPECT_TRUE(label.contains(QString::fromLatin1(text))) << label.toStdString() << " / " << text;
    EXPECT_EQ(machine->GetContext()->pCore->GetZ80()->pc, at.mainPc) << "the main CPU shown at its last boundary";

    // A main-CPU move ends the card position: the moment is the main CPU's again
    tt.stepBack();
    EXPECT_FALSE(controller->CardPositionNow(DebugCpuId::GS).cardPosition);
    EXPECT_FALSE(tt.momentLabel()->text().contains(QStringLiteral("gs at"))) << tt.momentLabel()->text().toStdString();

    controller->StopRecording();
    machine.reset();
    for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
        EmulatorManager::GetInstance()->RemoveEmulator(id);
}

/// With the card's tab in front the group's steps are the card's: Step Over Back is enabled and moves the card back
/// (one card instruction, or over a whole card routine); the window's Step Over / Step Out in the past are the card's
/// steps through the history (stepForward "over" / "out", Run to Cursor "to": forward by replay, the main CPU untouched
/// by a live run);
/// at the history's end stepForward hands over (false: the window steps the card live, D-3) without a message.
/// Runtime: 3 recorded frames, a few card moves (~0.3 s)
TEST(DebuggerTimeTravelCard_Test, TheCardsTabStepsOverBackAndThroughThePast)
{
    QTemporaryDir settings;
    ASSERT_TRUE(settings.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    std::function<void(CONFIG&)> slotOverride;
    std::string error;
    ASSERT_TRUE(SlotControl::CreateOverride({{"zxbus.1", "gs"}}, slotOverride, error)) << error;
    std::shared_ptr<Emulator> machine = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "debugger-ttd-card-steps", "PENTAGON", LoggerLevel::LogError, &error, slotOverride);
    ASSERT_NE(machine, nullptr) << error;
    machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    machine->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* controller = machine->GetContext()->pTimeTravelController;
    machine->RunNFrames(2, true);
    ASSERT_TRUE(controller->StartRecording());
    machine->RunNFrames(3, true);
    controller->StopRecording();

    QMainWindow window;
    QToolBar* toolbar = window.addToolBar(QStringLiteral("debugger"));
    DebuggerTimeTravel tt(&window, toolbar, nullptr);
    std::vector<QString> said;
    tt.inform = [&said](const QString& title, const QString& text, bool) { said.push_back(title + ": " + text); };
    tt.setEmulator(machine);
    tt.setCpu([]() { return DebugCpuId::GS; });
    auto execute = [&](const std::string& verb, const std::map<std::string, std::string>& options) {
        const Emulator::ContextLease lease = machine->LeaseContext();
        return ttd::TTDControl(lease.get()).Execute({verb, options});
    };
    const uint64_t now = controller->CardPositionNow(DebugCpuId::GS).instruction;
    const uint64_t start = now - 3000;
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(start)}}).Ok());
    tt.refresh();
    EXPECT_TRUE(tt.stepOverBackAction()->isEnabled()) << "the card's step over back";
    EXPECT_TRUE(tt.stepOverBackAction()->toolTip().contains(QStringLiteral("gs instruction back")))
        << tt.stepOverBackAction()->toolTip().toStdString();

    tt.stepOverBack();
    ttd::TTDCardPosition at = controller->CardPositionNow(DebugCpuId::GS);
    EXPECT_TRUE(at.cardPosition);
    EXPECT_LT(at.instruction, start);
    EXPECT_TRUE(said.empty()) << said.front().toStdString();

    // Step Over / Step Out in the past: forward through the history
    ASSERT_TRUE(tt.stepForward("over"));
    const uint64_t overAt = controller->CardPositionNow(DebugCpuId::GS).instruction;
    EXPECT_GT(overAt, at.instruction);
    EXPECT_TRUE(tt.lastResult().startsWith(QStringLiteral("Step Over: gs at instruction"))) << tt.lastResult().toStdString();
    ASSERT_TRUE(tt.stepForward("out"));
    EXPECT_GT(controller->CardPositionNow(DebugCpuId::GS).instruction, overAt);
    EXPECT_TRUE(tt.lastResult().startsWith(QStringLiteral("Step Out: gs"))) << tt.lastResult().toStdString();
    EXPECT_TRUE(said.empty()) << said.front().toStdString();

    // Run to Cursor in the past: the card's next instruction at the cursor; one never reached says so, nothing moves
    // From well inside the history (the step out may have gone to its end: the firmware runs no routine there)
    const uint64_t here = start;
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(here + 1)}}).Ok());
    const uint16_t cursor = controller->CardPositionNow(DebugCpuId::GS).pc;
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(here)}}).Ok());
    ASSERT_TRUE(tt.stepForward("to", cursor));
    EXPECT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, here + 1);
    EXPECT_TRUE(tt.lastResult().startsWith(QStringLiteral("Run to Cursor: gs at instruction")))
        << tt.lastResult().toStdString();
    EXPECT_TRUE(said.empty()) << said.front().toStdString();
    ASSERT_TRUE(tt.stepForward("to", 0xFFFF)) << "an answer in the past, not a live run";
    EXPECT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, here + 1) << "not reached: nothing moved";
    ASSERT_EQ(said.size(), 1u);
    EXPECT_NE(said.front().indexOf(QStringLiteral("#FFFF")), -1) << said.front().toStdString();
    said.clear();

    // At the history's end the window's own (live) step: no message, nothing moved
    const ttd::TTDTimePoint end = controller->SessionEndPosition();
    ASSERT_TRUE(controller->SeekTo({end.frame, 0}));
    const uint64_t endCount = controller->CardPositionNow(DebugCpuId::GS).instruction;
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(endCount)}}).Ok());
    for (const char* kind : {"in", "over", "out", "to"})
    {
        EXPECT_FALSE(tt.stepForward(kind, 0x4000)) << kind << ": at the end the card steps on live";
        EXPECT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, endCount) << kind;
    }
    EXPECT_TRUE(said.empty()) << said.front().toStdString();

    controller->StopRecording();
    machine.reset();
    for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
        EmulatorManager::GetInstance()->RemoveEmulator(id);
}
