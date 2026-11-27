#include "debugger/debuggercardrun.h"

#include <QAction>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QTabBar>
#include <QToolBar>
#include <QWidget>

#include <algorithm>

#include "debugger/debuggercpu.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/remotecontrolguard.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{
const execution::Requester kGui{execution::Origin::Gui, {}};
}  // namespace

DebuggerCardRun::DebuggerCardRun(QWidget* window, DebuggerCpuTabs* tabs, QMenu* runTo, QToolBar* toolbar,
                                 QAction* before, Hooks hooks)
    : QObject(window), _window(window), _tabs(tabs), _hooks(std::move(hooks))
{
    auto entry = [this, runTo](const QString& text, const QString& tip, void (DebuggerCardRun::*slot)()) {
        QAction* action = new QAction(text, this);
        action->setToolTip(tip);
        action->setStatusTip(tip);
        connect(action, &QAction::triggered, this, slot);
        if (runTo)
            runTo->addAction(action);
        return action;
    };
    if (runTo)
    {
        _separator = runTo->addSeparator();
        runTo->setToolTipsVisible(true);
    }
    _cardCycle = entry(tr("Card cycle..."), tr("Run until the card CPU reaches a card cycle of the frame"),
                       &DebuggerCardRun::runToCardCycle);
    _mainT = entry(tr("Main T..."),
                   tr("Run the main CPU to a T-state of the frame (the card keeps its own position: it catches up at "
                      "its next port access or the frame end)"),
                   &DebuggerCardRun::runToMainT);
    _cardInterrupt = entry(tr("Next card interrupt"), tr("Run until the card CPU accepts its next interrupt"),
                           &DebuggerCardRun::runToCardInterrupt);
    _hostCommand = entry(tr("Next host command"), tr("Run until the main CPU writes the card a command"),
                         &DebuggerCardRun::runToHostCommand);

    // Step other: the toolbar's, beside the steps
    _stepOther = new QAction(tr("Step main"), window);
    _stepOther->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F11));
    _stepOther->setShortcutContext(Qt::WindowShortcut);
    connect(_stepOther, &QAction::triggered, this, &DebuggerCardRun::stepOther);
    if (toolbar)
        toolbar->insertAction(before, _stepOther);
    else if (window)
        window->addAction(_stepOther);

    // The CPU shortcuts: the window's, and the tab bar's context menu lists them
    auto cpuAction = [this, window](const QString& text, const QKeySequence& key, void (DebuggerCardRun::*slot)()) {
        QAction* action = new QAction(text, window);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        if (window)
            window->addAction(action);
        if (_tabs && _tabs->bar())
            _tabs->bar()->addAction(action);
        return action;
    };
    _mainCpu = cpuAction(tr("Main CPU"), QKeySequence(Qt::CTRL | Qt::Key_1), &DebuggerCardRun::selectMainCpu);
    _cardCpu = cpuAction(tr("Card CPU"), QKeySequence(Qt::CTRL | Qt::Key_2), &DebuggerCardRun::selectCardCpu);
    if (_tabs && _tabs->bar())
        _tabs->bar()->setContextMenuPolicy(Qt::ActionsContextMenu);
    update();
}

void DebuggerCardRun::setEmulator(std::shared_ptr<Emulator> emulator)
{
    _emulator = std::move(emulator);
    update();
}

DebugCpuId DebuggerCardRun::card() const
{
    if (!_tabs || !_emulator)
        return DebugCpuId::Main;
    if (IsCardCpu(_tabs->current()))
        return _tabs->current();
    for (const DebugCpuId cpu : _tabs->cpus())
        if (IsCardCpu(cpu))
            return cpu;
    return DebugCpuId::Main;
}

void DebuggerCardRun::update()
{
    const DebugCpuId cardCpu = card();
    const bool fitted = cardCpu != DebugCpuId::Main;
    const bool cardInFront = fitted && _tabs && _tabs->current() == cardCpu;
    QString name;
    if (fitted && _emulator)
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        if (lease)
            name = debuggercpu::Title(lease.get(), cardCpu);
    }
    if (name.isEmpty())
        name = tr("card");

    if (_separator)
        _separator->setVisible(fitted);
    for (QAction* action : runToActions())
    {
        action->setVisible(fitted);
        action->setEnabled(fitted && _enabled);
    }
    const QString key = _stepOther->shortcut().toString(QKeySequence::NativeText);
    _stepOther->setVisible(fitted);
    _stepOther->setEnabled(fitted && _enabled);
    _stepOther->setText(cardInFront ? tr("Step main") : tr("Step %1").arg(name));
    _stepOther->setToolTip(cardInFront ? tr("Step the main CPU one instruction, the card shown (%1)").arg(key)
                                       : tr("Step the %1 CPU one instruction, the main CPU shown (%2)").arg(name, key));
    // Without a card Ctrl+1 / Ctrl+2 stay the disassembly's marks; with one, too while the disassembly has the focus
    // (DisassemblyView::event)
    _mainCpu->setEnabled(fitted);
    _mainCpu->setVisible(fitted);
    _cardCpu->setEnabled(fitted);
    _cardCpu->setVisible(fitted);
    _cardCpu->setText(fitted ? tr("Card CPU (%1)").arg(name) : tr("Card CPU"));
}

void DebuggerCardRun::setEnabled(bool enabled)
{
    _enabled = enabled;
    update();
}

bool DebuggerCardRun::ask(const QString& title, const QString& label, int value, int min, int max, int& out)
{
    if (_hooks.ask)
        return _hooks.ask(title, label, value, min, max, out);
    bool ok = false;
    out = QInputDialog::getInt(_window, title, label, std::clamp(value, min, max), min, max, 1, &ok);
    return ok;
}

void DebuggerCardRun::tell(const QString& message)
{
    _lastNotice = message;
    if (_hooks.notice && !message.isEmpty())
        _hooks.notice(message);
}

bool DebuggerCardRun::ready()
{
    if (!_emulator || card() == DebugCpuId::Main)
        return false;
    return !RemoteControlGuard::Refuse(_window);  // an automation session holds the machine
}

bool DebuggerCardRun::runTo(const std::string& until, uint64_t value)
{
    if (!ready())
        return false;
    if (_hooks.before)
        _hooks.before();
    _lastNotice.clear();
    const execution::RunReply reply = execution::RunControl(_emulator).CardRunTo(kGui, card(), until, value);
    bool hit = false;
    if (reply.Ok())
    {
        const StateNode* reached = reply.body.find("hit");
        hit = reached && reached->b;
    }
    if (!hit)
        tell(QString::fromStdString(reply.message.empty() ? reply.error : reply.message));
    if (_hooks.after)
        _hooks.after();
    return hit;
}

void DebuggerCardRun::runToCardCycle()
{
    if (!ready())
        return;
    int last = 0;
    int now = 0;
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        GeneralSoundCard* gs = lease ? debuggercpu::Card(lease.get(), card()) : nullptr;
        if (!gs)
            return;
        const int64_t frame = gs->cardCycles(gs->cardFrameLength());
        last = static_cast<int>(std::clamp<int64_t>(frame - 1, 0, INT32_MAX));
        now = static_cast<int>(std::clamp<int64_t>(gs->cardCycles(gs->cardFrameTime()), 0, last));
    }
    int cycle = 0;
    if (!ask(tr("Run to card cycle"), tr("Card cycle of the frame (0..%1):").arg(last), now, 0, last, cycle))
        return;
    runTo("cycle", static_cast<uint64_t>(cycle));
}

void DebuggerCardRun::runToMainT()
{
    if (!ready())
        return;
    int last = 0;
    int now = 0;
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        Z80* z80 = lease && lease.get()->pCore ? lease.get()->pCore->GetZ80() : nullptr;
        if (!z80 || z80->_frameLimit == 0)
            return;
        last = static_cast<int>(z80->_frameLimit - 1);
        now = static_cast<int>(std::min<uint32_t>(z80->t, z80->_frameLimit - 1));
    }
    int t = 0;
    if (!ask(tr("Run to main T"), tr("Main CPU T-state of the frame (0..%1):").arg(last), now, 0, last, t))
        return;
    runTo("main_t", static_cast<uint64_t>(t));
}

void DebuggerCardRun::runToCardInterrupt()
{
    runTo("int", 0);
}

void DebuggerCardRun::runToHostCommand()
{
    runTo("command", 0);
}

void DebuggerCardRun::stepOther()
{
    if (!ready())
        return;
    const DebugCpuId cardCpu = card();
    const bool cardInFront = _tabs && _tabs->current() == cardCpu;
    if (_hooks.before)
        _hooks.before();
    _lastNotice.clear();
    if (cardInFront)
    {
        // The main CPU's own step, as the window's Step In with the main CPU in front; the card stays in front
        _emulator->RunSingleCPUCycle(true);
    }
    else
    {
        // The card's step; its stop would bring the card's tab forward: the main CPU stays in front
        const execution::RunReply reply = execution::RunControl(_emulator).CardStep(kGui, cardCpu, "in", 1);
        if (!reply.Ok())
            tell(QString::fromStdString(reply.message.empty() ? reply.error : reply.message));
        if (_tabs)
        {
            _tabs->followStop();
            _tabs->select(DebugCpuId::Main);
        }
    }
    if (_hooks.after)
        _hooks.after();
}

void DebuggerCardRun::selectMainCpu()
{
    if (_tabs)
        _tabs->select(DebugCpuId::Main);
}

void DebuggerCardRun::selectCardCpu()
{
    if (!_tabs)
        return;
    std::vector<DebugCpuId> cards;
    for (const DebugCpuId cpu : _tabs->cpus())
        if (IsCardCpu(cpu))
            cards.push_back(cpu);
    if (cards.empty())
        return;
    const auto at = std::find(cards.begin(), cards.end(), _tabs->current());
    const DebugCpuId next = at == cards.end() || at + 1 == cards.end() ? cards.front() : *(at + 1);
    _tabs->select(next);
}

#include "moc_debuggercardrun.cpp"
