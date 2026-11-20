// The Embedded debugger's processor tabs (gs-debugger design §7.3; TTD branches debuggers.md §8): Main CPU and one
// tab per sound card CPU fitted, no bar with the main CPU alone. The tab in front is the machine's selected CPU while
// the window is shown; the panes read that CPU (its registers, its 64 KB, its disassembly with its own marks); a
// card's stop brings its tab to the front once per stop and a move in time does not; with a card in front the time
// group's Step Back / Step / Run Back are the card's, with the main CPU in front they are as before

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include <QLabel>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>

#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/debuggercpu.h"
#include "debugger/debuggertimetravel.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/disassemblywindow.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/targets/cpuview.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{
/// A machine with the card the slot override fits ({} none), debug mode on
std::shared_ptr<Emulator> Machine(const char* id, const char* model,
                                  const std::vector<std::pair<std::string, std::string>>& cards, bool timeTravel = false)
{
    std::function<void(CONFIG&)> slotOverride;
    std::string error;
    if (!cards.empty())
    {
        EXPECT_TRUE(SlotControl::CreateOverride(cards, slotOverride, error)) << error;
    }
    std::shared_ptr<Emulator> machine =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, model, LoggerLevel::LogError, &error, slotOverride);
    EXPECT_NE(machine, nullptr) << error;
    if (machine)
    {
        machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        if (timeTravel)
            machine->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    }
    return machine;
}

class DebuggerCpuTabs_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(_settings.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settings.path());
    }
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    QTemporaryDir _settings;
};
}  // namespace

/// Without a card the window is as it always was: the main CPU alone, no bar. Runtime: one machine, no frames
TEST_F(DebuggerCpuTabs_Test, TheMainCpuAloneShowsNoBar)
{
    std::shared_ptr<Emulator> machine = Machine("cputabs-48k", "48K", {});
    ASSERT_NE(machine, nullptr);
    QTabBar bar;
    DebuggerCpuTabs tabs(&bar);
    std::vector<DebugCpuId> changed;
    QObject::connect(&tabs, &DebuggerCpuTabs::cpuChanged, [&changed](DebugCpuId cpu) { changed.push_back(cpu); });
    tabs.setEmulator(machine);
    EXPECT_EQ(tabs.cpus(), std::vector<DebugCpuId>{DebugCpuId::Main});
    EXPECT_EQ(tabs.current(), DebugCpuId::Main);
    EXPECT_FALSE(bar.isVisibleTo(nullptr)) << "no tab bar with the main CPU alone";
    EXPECT_EQ(bar.count(), 1);
    EXPECT_FALSE(tabs.select(DebugCpuId::GS)) << "no GS in this machine";
    EXPECT_FALSE(tabs.followStop());
    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::Main);
}

/// The General Sound card gets a tab; switching it puts the card in front: the machine's selected CPU while the
/// window is shown, the main CPU again once it is hidden. The panes' readers follow the CPU in front: registers,
/// bytes, the 16 KB windows' names and the disassembly (the card's own, as CpuView::Disasm gives it).
/// Runtime: 3 frames
TEST_F(DebuggerCpuTabs_Test, AFittedCardGetsATabAndTheReadersFollowIt)
{
    std::shared_ptr<Emulator> machine = Machine("cputabs-gs", "PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(machine, nullptr);
    machine->RunNFrames(3, true);
    EmulatorContext* context = machine->GetContext();
    GeneralSoundCard* card = debuggercpu::Card(context, DebugCpuId::GS);
    ASSERT_NE(card, nullptr);

    QTabBar bar;
    DebuggerCpuTabs tabs(&bar);
    std::vector<DebugCpuId> changed;
    QObject::connect(&tabs, &DebuggerCpuTabs::cpuChanged, [&changed](DebugCpuId cpu) { changed.push_back(cpu); });
    tabs.setEmulator(machine);
    ASSERT_EQ(tabs.cpus(), (std::vector<DebugCpuId>{DebugCpuId::Main, DebugCpuId::GS}));
    EXPECT_TRUE(bar.isVisibleTo(nullptr));
    EXPECT_EQ(bar.tabText(0), QStringLiteral("Main CPU"));
    EXPECT_EQ(bar.tabText(1), card->implementation() == GSCardImplementation::NGS ? QStringLiteral("NeoGS")
                                                                                   : QStringLiteral("GS"));
    EXPECT_EQ(tabs.current(), DebugCpuId::Main) << "the main CPU in front at first";
    EXPECT_TRUE(changed.empty());

    // A click on the tab: the card in front, told once, the machine's selected CPU
    bar.setCurrentIndex(1);
    EXPECT_EQ(tabs.current(), DebugCpuId::GS);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed.at(0), DebugCpuId::GS);
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::GS);
    EXPECT_TRUE(tabs.select(DebugCpuId::GS));
    EXPECT_EQ(changed.size(), 1u) << "the same tab again: no change";
    tabs.setActive(false);
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::Main) << "a hidden window selects nothing";
    tabs.setActive(true);
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::GS);

    // Registers: the card's are its own, not the main Z80's
    Z80Registers main{};
    Z80Registers gs{};
    ASSERT_TRUE(debuggercpu::Registers(context, DebugCpuId::Main, main));
    ASSERT_TRUE(debuggercpu::Registers(context, DebugCpuId::GS, gs));
    EXPECT_EQ(main.pc, context->pCore->GetZ80()->pc);
    Z80CpuRegisters cardRegisters{};
    ASSERT_TRUE(card->cardRegisters(cardRegisters));
    EXPECT_EQ(gs.pc, cardRegisters.pc);
    EXPECT_EQ(gs.sp, cardRegisters.sp);

    // Bytes and windows: the card's 64 KB
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(CpuView::Read(context, DebugCpuId::GS, gs.pc, 16, bytes));
    for (uint16_t i = 0; i < 16; ++i)
        EXPECT_EQ(debuggercpu::Peek(context, DebugCpuId::GS, static_cast<uint16_t>(gs.pc + i)), bytes[i]) << i;
    EXPECT_EQ(debuggercpu::Peek(context, DebugCpuId::Main, 0x0000),
              context->pMemory->DirectReadFromZ80Memory(0x0000));
    const QString window0 = debuggercpu::CardWindowName(context, DebugCpuId::GS, 0);
    EXPECT_TRUE(window0.startsWith(QStringLiteral("ROM"))) << window0.toStdString() << ": the card boots from its ROM";
    EXPECT_TRUE(debuggercpu::CardWindowName(context, DebugCpuId::Main, 0).isEmpty());

    // The disassembly: the card's bytes and the card's PC as an anchor, the same rows as CpuView::Disasm
    debuggercpu::CardSource source(context, DebugCpuId::GS);
    ASSERT_TRUE(source.Valid());
    ASSERT_NE(source.Registers(), nullptr);
    EXPECT_EQ(source.Registers()->pc, gs.pc);
    disasm::Marks marks;
    source.MarksOf(gs.pc, gs.pc, marks);
    EXPECT_EQ(marks.anchors, std::vector<uint16_t>{gs.pc});
    Z80Disassembler disassembler(context);
    disasm::Window window(disassembler, source);
    const std::vector<disasm::Row> rows = window.Rows(gs.pc, 6);
    const StateNode reference = CpuView::Disasm(context, DebugCpuId::GS, gs.pc, 6);
    const StateNode* instructions = reference.find("instructions");
    ASSERT_NE(instructions, nullptr);
    ASSERT_EQ(rows.size(), 6u);
    ASSERT_GE(instructions->items.size(), 6u);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        EXPECT_EQ(rows[i].address, instructions->items[i].find("address")->i) << i;
        EXPECT_EQ(rows[i].length, instructions->items[i].find("size")->i) << i;
    }
    EXPECT_FALSE(debuggercpu::CardSource(context, DebugCpuId::MultiSoundGS).Valid()) << "no MultiSound here";

    // Another machine: its CPUs, the main CPU in front, the old one's selection undone
    std::shared_ptr<Emulator> plain = Machine("cputabs-plain", "48K", {});
    ASSERT_NE(plain, nullptr);
    tabs.setEmulator(plain);
    EXPECT_EQ(tabs.current(), DebugCpuId::Main);
    EXPECT_EQ(tabs.cpus(), std::vector<DebugCpuId>{DebugCpuId::Main});
    EXPECT_FALSE(bar.isVisibleTo(nullptr));
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::Main);
    EXPECT_EQ(changed.size(), 2u);
}

/// The MultiSound's GS is a tab of its own, named so. Runtime: one machine, no frames
TEST_F(DebuggerCpuTabs_Test, TheMultiSoundsGsHasItsTab)
{
    std::shared_ptr<Emulator> machine =
        Machine("cputabs-ms", "PENTAGON", {{"zxbus.1", "multisound"}, {"zxbus.1.dip", "ym,saa,gs"}});
    ASSERT_NE(machine, nullptr);
    QTabBar bar;
    DebuggerCpuTabs tabs(&bar);
    tabs.setEmulator(machine);
    ASSERT_EQ(tabs.cpus(), (std::vector<DebugCpuId>{DebugCpuId::Main, DebugCpuId::MultiSoundGS}));
    EXPECT_EQ(bar.tabText(1), QStringLiteral("MultiSound GS"));
    EXPECT_TRUE(tabs.select(DebugCpuId::MultiSoundGS));
    EXPECT_EQ(machine->GetDebugManager()->SelectedCpu(), DebugCpuId::MultiSoundGS);
}

/// A card's step stops the machine: its tab comes to the front, once for that stop (the user may go back to the
/// main CPU and stays there). A move in time is no stop: the tab in front stays. Runtime: a turbo boot of 3 frames,
/// one card step, 2 recorded frames and one seek (~0.1 s)
TEST_F(DebuggerCpuTabs_Test, ACardStopBringsItsTabOnceAndATimeMoveDoesNot)
{
    std::shared_ptr<Emulator> machine = Machine("cputabs-stop", "PENTAGON", {{"zxbus.1", "gs"}}, true);
    ASSERT_NE(machine, nullptr);
    EmulatorContext* context = machine->GetContext();
    QTabBar bar;
    DebuggerCpuTabs tabs(&bar);
    tabs.setEmulator(machine);
    ASSERT_EQ(tabs.cpus().size(), 2u);

    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    ASSERT_TRUE(controller->StartRecording());
    machine->EnableTurboMode();
    machine->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter > 3; }));
    machine->Pause();
    ASSERT_TRUE(TestWait::For([&] { return machine->IsEmulationParked(); }));
    EXPECT_FALSE(tabs.followStop()) << "a pause names no CPU";
    EXPECT_EQ(tabs.current(), DebugCpuId::Main);

    // The card's step (as the window's Step In with the card in front, or a client's): the card's tab comes forward
    const execution::RunReply reply = execution::RunControl(machine).CardStep(
        execution::Requester{execution::Origin::Gui, {}}, DebugCpuId::GS, "in", 1);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(machine->LastStop().breakpoint.cpu, DebugCpuId::GS);
    EXPECT_TRUE(tabs.followStop());
    EXPECT_EQ(tabs.current(), DebugCpuId::GS);
    // The user goes back to the main CPU: the same stop does not pull the card forward again
    EXPECT_TRUE(tabs.select(DebugCpuId::Main));
    EXPECT_FALSE(tabs.followStop());
    EXPECT_EQ(tabs.current(), DebugCpuId::Main);

    // A move in time with the card in front: no stop, the tab stays
    machine->Stop();
    controller->StopRecording();
    EXPECT_TRUE(tabs.select(DebugCpuId::GS));
    const uint64_t serial = machine->LastStop().serial;
    {
        const Emulator::ContextLease lease = machine->LeaseContext();
        const ttd::TTDReply back = ttd::TTDControl(lease.get()).Execute({"step-back", {}});
        ASSERT_TRUE(back.Ok()) << back.message;
    }
    EXPECT_EQ(machine->LastStop().serial, serial) << "a move in time is no stop";
    EXPECT_FALSE(tabs.followStop());
    EXPECT_EQ(tabs.current(), DebugCpuId::GS);
    EXPECT_TRUE(tabs.select(DebugCpuId::Main));
    EXPECT_FALSE(tabs.followStop());
    EXPECT_EQ(tabs.current(), DebugCpuId::Main) << "the main tab stays in front after a move";
}

/// The time group with the card's tab in front: Step Back is one card instruction back through the recorded history
/// (it lands on the instruction before, at the PC the card had there), Step in the past one forward, Run Back the
/// card's breakpoints; the main CPU's tab steps the main CPU as before. Runtime: 3 recorded frames, a few card seeks
/// (~0.1 s)
TEST_F(DebuggerCpuTabs_Test, ACardStepBackLandsOnTheCardsPreviousInstruction)
{
    std::shared_ptr<Emulator> machine = Machine("cputabs-ttd", "PENTAGON", {{"zxbus.1", "gs"}}, true);
    ASSERT_NE(machine, nullptr);
    ttd::TimeTravelController* controller = machine->GetContext()->pTimeTravelController;
    machine->RunNFrames(2, true);
    ASSERT_TRUE(controller->StartRecording());
    machine->RunNFrames(3, true);
    controller->StopRecording();

    QMainWindow window;
    QToolBar* toolbar = window.addToolBar(QStringLiteral("debugger"));
    DebuggerTimeTravel tt(&window, toolbar, nullptr);
    std::vector<QString> told;
    tt.inform = [&told](const QString& title, const QString& text, bool) { told.push_back(title + ": " + text); };
    DebugCpuId front = DebugCpuId::Main;
    tt.setCpu([&front]() { return front; });
    tt.setEmulator(machine);

    auto execute = [&](const std::string& verb, const std::map<std::string, std::string>& options) {
        const Emulator::ContextLease lease = machine->LeaseContext();
        return ttd::TTDControl(lease.get()).Execute({verb, options});
    };
    const ttd::TTDCardPosition now = controller->CardPositionNow(DebugCpuId::GS);
    ASSERT_TRUE(now.known);
    ASSERT_GT(now.instruction, 3000u);
    // The card's PC at instruction N - 1, then the machine at N (as a CLI `ttd seek --cpu gs` puts it)
    const uint64_t n = now.instruction - 1500;
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(n - 1)}}).Ok());
    const ttd::TTDCardPosition expected = controller->CardPositionNow(DebugCpuId::GS);
    ASSERT_TRUE(expected.cardPosition);
    ASSERT_EQ(expected.instruction, n - 1);
    ASSERT_TRUE(execute("seek", {{"cpu", "gs"}, {"instruction", std::to_string(n)}}).Ok());
    ASSERT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, n);

    // The card in front: the time group is the card's
    front = DebugCpuId::GS;
    tt.refresh();
    EXPECT_TRUE(tt.stepBackAction()->isEnabled());
    EXPECT_TRUE(tt.stepOverBackAction()->isEnabled()) << "Step Over Back is the card's too";
    EXPECT_TRUE(tt.stepOverBackAction()->toolTip().contains(QStringLiteral("gs instruction")));
    EXPECT_TRUE(tt.stepBackAction()->toolTip().contains(QStringLiteral("gs instruction")));
    tt.stepBack();
    ttd::TTDCardPosition at = controller->CardPositionNow(DebugCpuId::GS);
    EXPECT_TRUE(at.cardPosition);
    EXPECT_EQ(at.instruction, n - 1);
    EXPECT_EQ(at.pc, expected.pc);
    Z80Registers gs{};
    ASSERT_TRUE(debuggercpu::Registers(machine->GetContext(), DebugCpuId::GS, gs));
    EXPECT_EQ(gs.pc, expected.pc) << "the card's registers the panes read are at that instruction";
    EXPECT_EQ(tt.lastResult(),
              QStringLiteral("Step Back: gs at instruction %1 (from %2)").arg(n - 1).arg(static_cast<qulonglong>(n)));
    EXPECT_TRUE(tt.momentLabel()->text().contains(QStringLiteral("gs at instruction %1").arg(n - 1)))
        << tt.momentLabel()->text().toStdString();

    // Step in the past: one card instruction forward
    EXPECT_TRUE(tt.stepForward());
    EXPECT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, n);
    // Run Back without a card breakpoint: the core's refusal, told, nothing moved
    tt.runBack();
    EXPECT_EQ(controller->CardPositionNow(DebugCpuId::GS).instruction, n);
    ASSERT_FALSE(told.empty());
    EXPECT_TRUE(told.back().startsWith(QStringLiteral("Run Back: "))) << told.back().toStdString();

    // The main CPU's tab: Step Back is the main CPU's as before (the card position ends), Step is no card step
    front = DebugCpuId::Main;
    tt.refresh();
    EXPECT_FALSE(tt.stepForward()) << "the main CPU's Step In replays as before";
    tt.stepBack();
    EXPECT_FALSE(controller->CardPositionNow(DebugCpuId::GS).cardPosition);
    EXPECT_FALSE(tt.momentLabel()->text().contains(QStringLiteral("gs at"))) << tt.momentLabel()->text().toStdString();
    controller->StopRecording();
}
