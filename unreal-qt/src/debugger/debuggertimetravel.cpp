#include "debugger/debuggertimetravel.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cstdio>
#include <set>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/remotecontrolguard.h"

namespace
{
QString Hex(uint16_t address)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%04X", static_cast<unsigned>(address));
    return QString::fromLatin1(text);
}
}  // namespace

DebuggerTimeTravel::DebuggerTimeTravel(QWidget* window, QToolBar* toolbar, QAction* before)
    : QObject(window), _window(window)
{
    _commands = std::make_unique<ttdgui::BranchCommands>(window, [this]() { return _emulator; });
    _commands->moved = [this]() { emit moved(); };
    _commands->changed = [this]() { refresh(); };

    auto place = [&](QAction* action) {
        toolbar->insertAction(before, action);
        _group.push_back(action);
        return action;
    };
    auto add = [&](const QString& text, const QString& tip, const QKeySequence& key, void (DebuggerTimeTravel::*slot)()) {
        QAction* a = new QAction(text, window);
        a->setToolTip(key.isEmpty() ? tip : tip + QStringLiteral(" (") + key.toString(QKeySequence::NativeText) + ")");
        if (!key.isEmpty())
        {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WindowShortcut);
        }
        connect(a, &QAction::triggered, this, slot);
        return place(a);
    };

    place(toolbar->insertSeparator(before));
    _stepBack = add(tr("Step Back"), tr("Back one instruction through the recorded history"),
                    QKeySequence(Qt::SHIFT | Qt::Key_F7), &DebuggerTimeTravel::stepBack);
    _stepOverBack = add(tr("Step Over Back"),
                        tr("Back one instruction; back over a whole routine when that instruction is its return"),
                        QKeySequence(Qt::SHIFT | Qt::Key_F8), &DebuggerTimeTravel::stepOverBack);
    _runBack = add(tr("Run Back"), tr("Back to the last time an enabled execution breakpoint was reached"),
                   QKeySequence(Qt::SHIFT | Qt::Key_F9), &DebuggerTimeTravel::runBack);
    _frameBack = add(tr("Frame Back"), tr("Back one frame (the same moment inside it)"), QKeySequence(),
                     &DebuggerTimeTravel::frameBack);
    _branchHere = add(tr("Branch Here"),
                      tr("Record from here: in the past the recorded future is kept as a branch"), QKeySequence(),
                      &DebuggerTimeTravel::branchHere);

    _moment = new QLabel(toolbar);
    _moment->setObjectName(QStringLiteral("debuggerTtdMoment"));
    _moment->setContentsMargins(6, 0, 6, 0);
    place(toolbar->insertWidget(before, _moment));

    _branchMenu = new QMenu(toolbar);
    connect(_branchMenu, &QMenu::aboutToShow, this, &DebuggerTimeTravel::buildBranchMenu);
    _branchButton = new QToolButton(toolbar);
    _branchButton->setObjectName(QStringLiteral("debuggerTtdBranches"));
    _branchButton->setMenu(_branchMenu);
    _branchButton->setPopupMode(QToolButton::InstantPopup);
    _branchButton->setToolTip(tr("The session's branches: switch, manage"));
    _branchButtonAction = place(toolbar->insertWidget(before, _branchButton));

    // The published info is read without the machine (WebAPI reads never park): cheap enough to follow a run
    _timer = new QTimer(this);
    _timer->setInterval(250);
    connect(_timer, &QTimer::timeout, this, [this]() {
        if (_window && _window->isVisible())
            refresh();
    });
    _timer->start();
    setShown(false);
}

DebuggerTimeTravel::~DebuggerTimeTravel() = default;

void DebuggerTimeTravel::setEmulator(std::shared_ptr<Emulator> emulator)
{
    _emulator = std::move(emulator);
    _lastResult.clear();
    refresh();
}

void DebuggerTimeTravel::setShown(bool shown)
{
    if (_shown == shown)
        return;
    _shown = shown;
    for (QAction* a : _group)
        a->setVisible(shown);
}

std::string DebuggerTimeTravel::RunBackPcs(const BreakpointManager& breakpoints)
{
    std::set<uint16_t> pcs;
    for (const auto& [id, bp] : breakpoints.GetAllBreakpoints())
    {
        if (!bp || !bp->active || !BreakpointManager::IsUserBreakpoint(*bp) || bp->type != BRK_MEMORY ||
            !(bp->memoryType & BRK_MEM_EXECUTE))
            continue;
        for (uint32_t a = bp->z80address; a <= bp->EndAddress() && pcs.size() < kMaxRunBackPcs; ++a)
            pcs.insert(static_cast<uint16_t>(a));
    }
    std::string text;
    for (const uint16_t pc : pcs)
        text += (text.empty() ? "" : ",") + Hex(pc).toStdString();
    return text;
}

void DebuggerTimeTravel::refresh()
{
    ttd::TTDSessionInfo info;
    uint64_t frame = 0;
    std::string pcs;
    {
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
        {
            setShown(false);
            return;
        }
        const ttd::TTDSessionRef session(context);
        info = session->GetPublishedSessionInfo();
        frame = session->CurrentPosition().frame;
        if (const BreakpointManager* breakpoints = _emulator->GetBreakpointManager())
            pcs = RunBackPcs(*breakpoints);
    }
    const ttdgui::MomentText moment = ttdgui::DescribeMoment(info, frame);
    // Rule 1: without a recorded history the debugger looks as before
    setShown(!moment.state.isEmpty());
    if (!_shown)
        return;

    const bool locked = RemoteControlGuard::Locked();
    const bool atStart = info.state == ttd::TTDSessionState::Detached && frame <= info.sessionStartFrame;
    _stepBack->setEnabled(!locked && !atStart);
    _stepOverBack->setEnabled(!locked && !atStart);
    _frameBack->setEnabled(!locked && !atStart);
    _runBack->setEnabled(!locked && !pcs.empty());
    _runBack->setToolTip(pcs.empty() ? tr("Run Back needs an enabled execution breakpoint (Shift+F9)")
                                     : tr("Back to the last time one of %1 was reached (Shift+F9)")
                                           .arg(QString::fromStdString(pcs.size() > 40 ? pcs.substr(0, 40) + "..." : pcs)));
    // As the TTD panel's button: only once the recording is stopped; with a recorded future after the moment it
    // forks, at the end (or live past it) it goes on recording
    const bool recording = info.state == ttd::TTDSessionState::Recording || info.recordingPaused;
    const bool forks = info.state == ttd::TTDSessionState::Detached && frame < info.currentEndFrame;
    _branchHere->setEnabled(!locked && !recording);
    _branchHere->setText(forks ? tr("Branch Here") : tr("Continue Rec"));
    _branchHere->setToolTip(forks ? tr("Record from here: the recorded future is kept as a branch")
                                  : tr("Go on recording from the end of the recording"));

    QString state = moment.state;
    if (!moment.fork.isEmpty())
        state += QStringLiteral(" · ") + moment.fork;
    _moment->setText(state);
    _moment->setToolTip(_lastResult);
    // Rule 2: the trunk alone shows nothing about branches
    _branchButtonAction->setVisible(!moment.branch.isEmpty());
    _branchButton->setText(moment.branch + QStringLiteral(" ▾"));
}

bool DebuggerTimeTravel::move(const std::string& verb, std::map<std::string, std::string> options, const QString& title,
                              ttd::TTDReply* out)
{
    if (!_emulator || RemoteControlGuard::Refuse(_window))
        return false;
    ttd::TTDReply reply;
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
            return false;
        reply = ttd::TTDControl(context).Execute({verb, std::move(options)});
    }
    if (!reply.Ok())
    {
        QMessageBox::warning(_window, title, QString::fromStdString(reply.message));
        refresh();
        return false;
    }
    const StateNode* text = reply.body.find("text");
    _lastResult = text ? QString::fromStdString(text->s) : QString();
    if (out)
        *out = reply;
    emit moved();
    refresh();
    return true;
}

void DebuggerTimeTravel::stepBack()
{
    move("step-instruction", {{"dir", "back"}}, tr("Step Back"));
}

void DebuggerTimeTravel::stepOverBack()
{
    move("reverse-step-over", {}, tr("Step Over Back"));
}

void DebuggerTimeTravel::frameBack()
{
    move("step-back", {}, tr("Frame Back"));
}

void DebuggerTimeTravel::runBack()
{
    std::string pcs;
    if (_emulator)
        if (const BreakpointManager* breakpoints = _emulator->GetBreakpointManager())
            pcs = RunBackPcs(*breakpoints);
    if (pcs.empty())
    {
        QMessageBox::information(_window, tr("Run Back"), tr("Run Back stops at the enabled execution breakpoints: "
                                                            "there is none."));
        return;
    }
    // Not found: the machine stays where it is and the reply says so (matched false)
    ttd::TTDReply reply;
    if (!move("reverse-continue", {{"pcs", pcs}}, tr("Run Back"), &reply))
        return;
    const StateNode* matched = reply.body.find("matched");
    const StateNode* pc = reply.body.find("pc");
    if (matched && matched->b && pc)
        _lastResult = tr("Run Back: at the breakpoint %1").arg(Hex(static_cast<uint16_t>(pc->i)));
    else
    {
        _lastResult = tr("Run Back: no enabled execution breakpoint was reached earlier in the history");
        QMessageBox::information(_window, tr("Run Back"), _lastResult);
    }
    refresh();
}

void DebuggerTimeTravel::branchHere()
{
    if (RemoteControlGuard::Refuse(_window))
        return;
    // Rule 7: this is the explicit act; the debugger stays paused for the next step
    _commands->ResumeRecordingHere(_branchHere->text(), /*runAfter=*/false);
    emit moved();
}

void DebuggerTimeTravel::buildBranchMenu()
{
    _branchMenu->clear();
    ttd::TTDSessionInfo info;
    uint64_t frame = 0;
    {
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
            return;
        const ttd::TTDSessionRef session(context);
        info = session->GetPublishedSessionInfo();
        frame = session->CurrentPosition().frame;
    }
    if (!info.branches)
        return;
    const std::shared_ptr<const ttd::TTDBranchesView> view = info.branches;

    // The active lineage first (trunk to the active branch), then the others by id
    std::vector<uint32_t> order;
    std::set<uint32_t> listed;
    for (const ttd::TTDLineageSpan& span : view->lineage)
        if (listed.insert(span.branch).second)
            order.push_back(span.branch);
    for (const ttd::TTDBranchInfo& b : view->branches)
        if (listed.insert(b.id).second)
            order.push_back(b.id);

    auto label = [&](const ttd::TTDBranchInfo& b) {
        QString text = QString::fromStdString(b.meta.name.empty() ? std::to_string(b.id) : b.meta.name);
        if (b.parent != ttd::kTTDNoBranch)
            text += tr("  (from frame %1, %2 frames)").arg(b.fork.frame).arg(b.Length());
        if (b.meta.pinned)
            text += tr("  [pinned]");
        return text;
    };

    QActionGroup* group = new QActionGroup(_branchMenu);
    for (const uint32_t id : order)
    {
        const ttd::TTDBranchInfo* b = view->Find(id);
        if (!b)
            continue;
        QAction* a = _branchMenu->addAction(label(*b));
        a->setCheckable(true);
        a->setChecked(id == info.activeBranch);
        a->setActionGroup(group);
        a->setToolTip(QString::fromStdString(b->meta.note));
        connect(a, &QAction::triggered, this, [this, id, active = info.activeBranch]() {
            if (id != active)
                _commands->Switch(id);
        });
    }

    if (const std::vector<ttdgui::ForkLeave> forks = ttdgui::ForksAt(info, frame); !forks.empty())
    {
        _branchMenu->addSection(tr("Branches leaving here"));
        for (const ttdgui::ForkLeave& leave : forks)
        {
            QAction* a = _branchMenu->addAction(
                tr("%1  (%2 frames)").arg(QString::fromStdString(leave.name)).arg(leave.frames));
            connect(a, &QAction::triggered, this, [this, id = leave.branch]() { _commands->Switch(id); });
        }
    }

    _branchMenu->addSeparator();
    QMenu* manage = _branchMenu->addMenu(tr("Manage"));
    for (const uint32_t id : order)
    {
        const ttd::TTDBranchInfo* b = view->Find(id);
        if (!b)
            continue;
        QMenu* sub = manage->addMenu(QString::fromStdString(b->meta.name.empty() ? std::to_string(id) : b->meta.name));
        QAction* sw = sub->addAction(tr("Switch"), this, [this, id]() { _commands->Switch(id); });
        sw->setEnabled(id != info.activeBranch);
        sub->addAction(tr("Rename..."), this, [this, id]() { _commands->Rename(id); });
        sub->addAction(tr("Tags..."), this, [this, id]() { _commands->Tags(id); });
        sub->addAction(tr("Note..."), this, [this, id]() { _commands->Note(id); });
        sub->addAction(b->meta.pinned ? tr("Unpin") : tr("Pin"), this, [this, id]() { _commands->TogglePin(id); });
        QAction* del = sub->addAction(tr("Delete..."), this, [this, id]() { _commands->Delete(id); });
        // The trunk goes only with the whole history (Clear in the TTD panel)
        del->setEnabled(id != ttd::kTTDTrunk);
        if (id == ttd::kTTDTrunk)
            del->setToolTip(tr("The trunk goes only with the whole history (Clear in the TTD panel)"));
        sub->setToolTipsVisible(true);
    }
    _branchMenu->addAction(tr("Save Lineage..."), this, [this]() { _commands->SaveLineage(); });
    _branchMenu->addAction(tr("Show in the TTD panel"), this, [this]() { emit showInTtdPanel(); });
    _branchMenu->setToolTipsVisible(true);
}
