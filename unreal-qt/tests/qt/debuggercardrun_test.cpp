// The Embedded debugger's card run controls (gs-debugger TODO P5.4): Run to's card entries, Step other and the CPU
// shortcuts exist only with a card fitted; Step other steps the CPU not in front and keeps the tab; Ctrl+2 puts the
// card in front; a Run to card cycle through the window's path ends paused with the card at that cycle; a refused run
// says why in the stop line

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidget>

#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/debuggercardrun.h"
#include "debugger/debuggercpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{
class DebuggerCardRun_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(_settings.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settings.path());
    }
    void TearDown() override
    {
        _run.reset();
        _tabs.reset();
        if (_machine)
            _machine->Stop();
        _machine.reset();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    /// A machine with the card the slot fits ({} none), debug mode on, and the controls over it
    void Create(const char* id, const char* model, const std::vector<std::pair<std::string, std::string>>& cards)
    {
        std::function<void(CONFIG&)> slotOverride;
        std::string error;
        if (!cards.empty())
            ASSERT_TRUE(SlotControl::CreateOverride(cards, slotOverride, error)) << error;
        _machine = EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, model, LoggerLevel::LogError, &error,
                                                                          slotOverride);
        ASSERT_NE(_machine, nullptr) << error;
        _machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);

        _tabs = std::make_unique<DebuggerCpuTabs>(&_bar);
        _tabs->setEmulator(_machine);
        DebuggerCardRun::Hooks hooks;
        hooks.notice = [this](const QString& text) { _notices.push_back(text); };
        hooks.after = [this]() { _afters++; };
        hooks.ask = [this](const QString&, const QString&, int, int min, int max, int& out) {
            _askedMax = max;
            out = std::clamp(_answer, min, max);
            return true;
        };
        _run = std::make_unique<DebuggerCardRun>(&_window, _tabs.get(), &_menu, nullptr, nullptr, std::move(hooks));
        _run->setEmulator(_machine);
    }

    /// Running in turbo for a few frames, then paused at a boundary
    void StartPaused()
    {
        EmulatorContext* context = _machine->GetContext();
        _machine->EnableTurboMode();
        _machine->StartAsync();
        ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter > 3; }));
        _machine->Pause();
        ASSERT_TRUE(TestWait::For([&] { return _machine->IsEmulationParked(); }));
    }

    GeneralSoundCard* Card() const { return debuggercpu::Card(_machine->GetContext(), DebugCpuId::GS); }

    QTemporaryDir _settings;
    QWidget _window;
    QTabBar _bar;
    QMenu _menu;
    std::shared_ptr<Emulator> _machine;
    std::unique_ptr<DebuggerCpuTabs> _tabs;
    std::unique_ptr<DebuggerCardRun> _run;
    std::vector<QString> _notices;
    int _afters = 0;
    int _answer = 0;
    int _askedMax = -1;
};
}  // namespace

/// Without a card nothing shows: Run to keeps its entries, no Step other, Ctrl+1 / Ctrl+2 stay the disassembly's
/// marks. Runtime: one machine, no frames
TEST_F(DebuggerCardRun_Test, NothingShowsWithoutACard)
{
    ASSERT_NO_FATAL_FAILURE(Create("cardrun-48k", "48K", {}));
    EXPECT_EQ(_run->card(), DebugCpuId::Main);
    for (QAction* action : _run->runToActions())
    {
        EXPECT_FALSE(action->isVisible()) << action->text().toStdString();
        EXPECT_TRUE(_menu.actions().contains(action));
    }
    EXPECT_FALSE(_run->stepOtherAction()->isVisible());
    EXPECT_FALSE(_run->cardCpuAction()->isEnabled());
    EXPECT_FALSE(_run->mainCpuAction()->isEnabled());
    _run->stepOther();
    EXPECT_EQ(_afters, 0) << "nothing ran";
}

/// A card fitted: the four entries in Run to, Step other with Alt+F11, the CPU shortcuts Ctrl+1 / Ctrl+2; Ctrl+2 puts
/// the card in front, Ctrl+1 the main CPU. Runtime: one machine, no frames
TEST_F(DebuggerCardRun_Test, ACardBringsTheEntriesAndItsShortcut)
{
    ASSERT_NO_FATAL_FAILURE(Create("cardrun-gs", "PENTAGON", {{"zxbus.1", "gs"}}));
    ASSERT_EQ(_tabs->cpus(), (std::vector<DebugCpuId>{DebugCpuId::Main, DebugCpuId::GS}));
    EXPECT_EQ(_run->card(), DebugCpuId::GS);
    const QStringList texts = {"Card cycle...", "Main T...", "Next card interrupt", "Next host command"};
    const std::vector<QAction*> entries = _run->runToActions();
    ASSERT_EQ(entries.size(), 4u);
    for (size_t i = 0; i < entries.size(); i++)
    {
        EXPECT_EQ(entries[i]->text(), texts[static_cast<int>(i)]);
        EXPECT_TRUE(entries[i]->isVisible());
        EXPECT_TRUE(_menu.actions().contains(entries[i]));
    }
    EXPECT_TRUE(_run->stepOtherAction()->isVisible());
    EXPECT_EQ(_run->stepOtherAction()->shortcut(), QKeySequence(Qt::ALT | Qt::Key_F11));
    EXPECT_EQ(_run->stepOtherAction()->text(), "Step GS") << "the main CPU in front: the card's step";

    EXPECT_EQ(_run->cardCpuAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_2));
    EXPECT_EQ(_run->mainCpuAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_1));
    EXPECT_TRUE(_window.actions().contains(_run->cardCpuAction())) << "a window shortcut";
    EXPECT_TRUE(_bar.actions().contains(_run->cardCpuAction())) << "the tab bar's context menu";
    ASSERT_TRUE(_run->cardCpuAction()->isEnabled());
    _run->cardCpuAction()->trigger();
    EXPECT_EQ(_tabs->current(), DebugCpuId::GS);
    _run->update();
    EXPECT_EQ(_run->stepOtherAction()->text(), "Step main");
    _run->cardCpuAction()->trigger();
    EXPECT_EQ(_tabs->current(), DebugCpuId::GS) << "the only card stays in front";
    _run->mainCpuAction()->trigger();
    EXPECT_EQ(_tabs->current(), DebugCpuId::Main);
}

/// Step other steps the CPU not in front and leaves the tabs as they were: the card's step with the main CPU in front
/// (its stop does not pull the card forward), the main CPU's step with the card in front. Runtime: a few turbo frames
TEST_F(DebuggerCardRun_Test, StepOtherStepsTheCpuNotInFront)
{
    ASSERT_NO_FATAL_FAILURE(Create("cardrun-step", "PENTAGON", {{"zxbus.1", "gs"}}));
    ASSERT_NO_FATAL_FAILURE(StartPaused());
    GeneralSoundCard* card = Card();
    ASSERT_NE(card, nullptr);

    // The main CPU in front: the card steps
    ASSERT_EQ(_tabs->current(), DebugCpuId::Main);
    const int64_t cardBefore = card->cardCycles(card->cardTime());
    _run->stepOther();
    EXPECT_EQ(_afters, 1);
    EXPECT_TRUE(_run->lastNotice().isEmpty()) << _run->lastNotice().toStdString();
    EXPECT_EQ(_machine->LastStop().breakpoint.cpu, DebugCpuId::GS) << "the card's step";
    EXPECT_GT(card->cardCycles(card->cardTime()), cardBefore);
    EXPECT_EQ(_tabs->current(), DebugCpuId::Main) << "the main CPU stays in front";
    EXPECT_FALSE(_tabs->followStop()) << "the card's stop is taken";
    EXPECT_TRUE(_machine->IsPaused());

    // The card in front: the main CPU steps
    ASSERT_TRUE(_tabs->select(DebugCpuId::GS));
    _run->update();
    ASSERT_EQ(_run->stepOtherAction()->text(), "Step main");
    Z80State* z80 = _machine->GetZ80State();
    const uint64_t frame = _machine->GetContext()->emulatorState.frame_counter;
    const uint32_t t = z80->t;
    _run->stepOtherAction()->trigger();
    EXPECT_EQ(_afters, 2);
    EXPECT_TRUE(z80->t != t || _machine->GetContext()->emulatorState.frame_counter != frame)
        << "the main CPU ran an instruction";
    _tabs->followStop();
    EXPECT_EQ(_tabs->current(), DebugCpuId::GS) << "the card stays in front";
    EXPECT_TRUE(_machine->IsPaused());
}

/// Run to ▾ Card cycle… asks the cycle (0 .. the frame's last) and runs there: paused, the card at the first boundary
/// at or after it, no notice. A cycle past the frame is refused and the stop line says why. Runtime: a few turbo
/// frames and a part of one
TEST_F(DebuggerCardRun_Test, RunToACardCycleEndsPausedThere)
{
    ASSERT_NO_FATAL_FAILURE(Create("cardrun-cycle", "PENTAGON", {{"zxbus.1", "gs"}}));
    ASSERT_NO_FATAL_FAILURE(StartPaused());
    GeneralSoundCard* card = Card();
    ASSERT_NE(card, nullptr);
    const int64_t frameCycles = card->cardCycles(card->cardFrameLength());
    ASSERT_GT(frameCycles, 10000);
    const int64_t at = card->cardCycles(card->cardFrameTime());
    const bool wraps = at + 3000 >= frameCycles;
    _answer = static_cast<int>(wraps ? 5000 : at + 3000);

    _run->cardCycleAction()->trigger();
    EXPECT_EQ(_askedMax, frameCycles - 1);
    EXPECT_EQ(_afters, 1);
    EXPECT_TRUE(_run->lastNotice().isEmpty()) << _run->lastNotice().toStdString();
    EXPECT_TRUE(_notices.empty());
    EXPECT_TRUE(_machine->IsPaused());
    const int64_t reached = card->cardCycles(card->cardFrameTime());
    EXPECT_GE(reached, _answer) << "the first boundary at or after the cycle";
    EXPECT_LT(reached, _answer + 200) << "one card instruction past it at most";
    EXPECT_EQ(_machine->LastStop().breakpoint.cpu, DebugCpuId::GS);

    // Past the frame: refused, told in the stop line, nothing ran
    EXPECT_FALSE(_run->runTo("cycle", static_cast<uint64_t>(frameCycles)));
    ASSERT_EQ(_notices.size(), 1u);
    EXPECT_FALSE(_notices.front().isEmpty());
    EXPECT_EQ(_run->lastNotice(), _notices.front());
    EXPECT_EQ(card->cardCycles(card->cardFrameTime()), reached);
    EXPECT_TRUE(_machine->IsPaused());
}
