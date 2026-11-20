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
#include "debugger/targets/debugcpuid.h"
#include "debugger/ttd/timetravelcontroller.h"
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
    inform = [window](const QString& title, const QString& text, bool warning) {
        if (warning)
            QMessageBox::warning(window, title, text);
        else
            QMessageBox::information(window, title, text);
    };

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

void DebuggerTimeTravel::setCpu(std::function<DebugCpuId()> cpu)
{
    _cpu = std::move(cpu);
    refresh();
}

DebugCpuId DebuggerTimeTravel::cpu() const
{
    return _cpu ? _cpu() : DebugCpuId::Main;
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

QString DebuggerTimeTravel::CardReadout(const ttd::TTDCardPosition& card)
{
    const QString name = QString::fromLatin1(DebugCpuName(card.cpu));
    if (!card.known)
        return tr("%1: no instruction counts in this recording").arg(name);
    return tr("%1 at instruction %2, PC %3").arg(name).arg(static_cast<qulonglong>(card.instruction)).arg(Hex(card.pc));
}

QString DebuggerTimeTravel::CardMoment(const std::vector<ttd::TTDCardPosition>& cards)
{
    for (const ttd::TTDCardPosition& p : cards)
    {
        if (!p.cardPosition || !p.known)
            continue;
        QString text = CardReadout(p);
        // The main CPU at its last completed instruction; inside the next one by the T-states the card is ahead
        if (p.inside)
            text += tr("; main CPU PC %1, +%2 T into the next instruction").arg(Hex(p.mainPc)).arg(p.tOffset);
        else
            text += tr("; main CPU PC %1, at its instruction boundary").arg(Hex(p.mainPc));
        return text;
    }
    return QString();
}

void DebuggerTimeTravel::refresh()
{
    ttd::TTDSessionInfo info;
    uint64_t frame = 0;
    std::string pcs;
    std::vector<ttd::TTDCardPosition> cards;
    const DebugCpuId front = cpu();
    const bool onCard = IsCardCpu(front);
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
        // A card position exists only in the history with the machine paused (its first run leaves it): the
        // published readout then (a read under the controller's lock, never a session operation). A card's tab in
        // front reads where its card stands at any paused moment
        if ((info.state == ttd::TTDSessionState::Detached || onCard) &&
            (!_emulator->IsRunning() || _emulator->IsPaused()) && context->pTimeTravelController)
            cards = context->pTimeTravelController->PublishedCardPositions();
    }
    const ttdgui::MomentText moment = ttdgui::DescribeMoment(info, frame);
    // Rule 1: without a recorded history the debugger looks as before
    setShown(!moment.state.isEmpty());
    if (!_shown)
        return;

    const bool locked = RemoteControlGuard::Locked();
    const bool atStart = info.state == ttd::TTDSessionState::Detached && frame <= info.sessionStartFrame;
    // A card's tab in front: the back steps are the card's. The frame of the main CPU's start is no limit for them
    // (the card may have run in it); the core answers at the card's first position (at_start)
    const QString card = QString::fromLatin1(DebugCpuName(front));
    _stepBack->setEnabled(!locked && (onCard || !atStart));
    _stepOverBack->setEnabled(!locked && (onCard || !atStart));
    _frameBack->setEnabled(!locked && !atStart);
    // The card's breakpoints of any kind count for the card (the core refuses when none is active)
    _runBack->setEnabled(!locked && (onCard || !pcs.empty()));
    if (onCard)
    {
        _stepBack->setToolTip(tr("Step Back: one %1 instruction back in the recorded history (Shift+F7)").arg(card));
        _stepOverBack->setToolTip(tr("Step Over Back: one %1 instruction back; back over a whole card routine when "
                                     "that instruction is its return (Shift+F8)")
                                      .arg(card));
        _runBack->setToolTip(tr("Run Back: back to the previous hit of a %1 breakpoint (Shift+F9)").arg(card));
    }
    else
    {
        _stepBack->setToolTip(tr("Back one instruction through the recorded history (Shift+F7)"));
        _stepOverBack->setToolTip(
            tr("Back one instruction; back over a whole routine when that instruction is its return (Shift+F8)"));
        _runBack->setToolTip(pcs.empty() ? tr("Run Back needs an enabled execution breakpoint (Shift+F9)")
                                         : tr("Back to the last time one of %1 was reached (Shift+F9)")
                                               .arg(QString::fromStdString(pcs.size() > 40 ? pcs.substr(0, 40) + "..."
                                                                                           : pcs)));
    }
    // As the TTD panel's button: only once the recording is stopped; with a recorded future after the moment it
    // forks, at the end (or live past it) it goes on recording
    const bool recording = info.state == ttd::TTDSessionState::Recording || info.recordingPaused;
    const bool forks = info.state == ttd::TTDSessionState::Detached && frame < info.currentEndFrame;
    _branchHere->setEnabled(!locked && !recording);
    _branchHere->setText(forks ? tr("Branch Here") : tr("Continue Rec"));
    _branchHere->setToolTip(forks ? tr("Record from here: the recorded future is kept as a branch")
                                  : tr("Go on recording from the end of the recording"));

    QString state = moment.state;
    // A card position names both CPUs whatever tab is in front; else the card in front says where it stands
    QString cardPart = CardMoment(cards);
    if (cardPart.isEmpty() && onCard)
        for (const ttd::TTDCardPosition& p : cards)
            if (p.cpu == front)
                cardPart = CardReadout(p);
    if (!cardPart.isEmpty())
        state += QStringLiteral(" · ") + cardPart;
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
        if (inform)
            inform(title, QString::fromStdString(reply.message), true);
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

bool DebuggerTimeTravel::cardMove(DebugCpuId card, const std::string& verb, std::map<std::string, std::string> options,
                                  const QString& title, bool* atEnd)
{
    const QString name = QString::fromLatin1(DebugCpuName(card));
    options["cpu"] = DebugCpuName(card);
    ttd::TTDReply reply;
    if (!_emulator || RemoteControlGuard::Refuse(_window))
        return false;
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
            return false;
        reply = ttd::TTDControl(context).Execute({verb, std::move(options)});
    }
    const StateNode* message = reply.body.find("message");
    const QString said = message ? QString::fromStdString(message->s) : QString::fromStdString(reply.message);
    // A refusal (no card, no counts, no history, no breakpoints): its text is the core's
    if (!reply.Ok())
    {
        _lastResult = title + QStringLiteral(": ") + said;
        if (inform)
            inform(title, said, true);
        refresh();
        return false;
    }
    // An answer that moved nothing (at_start, at_end, no_hit): the machine stays where it was
    const StateNode* reached = reply.body.find("reached");
    const StateNode* status = reply.body.find("status");
    if ((!reached || !reached->b) && atEnd && status && status->s == "at_end")
    {
        *atEnd = true;
        return false;
    }
    if (!reached || !reached->b)
    {
        _lastResult = title + QStringLiteral(": ") + (said.isEmpty() ? tr("nothing moved") : said);
        if (inform)
            inform(title, said.isEmpty() ? tr("Nothing moved.") : said, false);
        refresh();
        return false;
    }
    const StateNode* position = reply.body.find("card");
    const StateNode* instruction = position ? position->find("instruction") : nullptr;
    const StateNode* from = reply.body.find("from");
    const qulonglong at = instruction ? static_cast<qulonglong>(instruction->i) : 0;
    _lastResult = tr("%1: %2 at instruction %3 (from %4)")
                      .arg(title, name)
                      .arg(at)
                      .arg(from ? static_cast<qulonglong>(from->i) : 0);
    if (const StateNode* hit = reply.body.find("breakpoint"))
        if (const StateNode* address = hit->find("address"))
            _lastResult = tr("%1: %2 at the breakpoint %3 (instruction %4)")
                              .arg(title, name, Hex(static_cast<uint16_t>(address->i)))
                              .arg(at);
    if (const StateNode* over = reply.body.find("over_call"); over && over->b)
        if (const StateNode* skipped = reply.body.find("skipped"))
            _lastResult += tr(", back over a routine of %1 instruction(s)").arg(static_cast<qulonglong>(skipped->i));
    if (const StateNode* reach = reply.body.find("call_out_of_reach"); reach && reach->b)
        _lastResult += tr(", on the return: the routine's start is out of reach");
    if (const StateNode* end = reply.body.find("history_end"); end && end->b)
        _lastResult += reply.body.find("kind")
                           ? tr(", the step did not end in the history: at its end (the next step runs live)")
                           : tr(", from past the history (stepped on live): at its end");
    else if (const StateNode* cut = reply.body.find("truncated"); cut && cut->b && reply.body.find("kind"))
        _lastResult += tr(", the step did not end in the frames searched (step again to go on)");
    emit moved();
    refresh();
    return true;
}

bool DebuggerTimeTravel::stepForward(const std::string& kind, uint16_t address)
{
    const DebugCpuId front = cpu();
    if (!IsCardCpu(front) || !_emulator)
        return false;
    // In the past only (rule 3: forward actions there replay); live, the card's own step runs
    {
        const Emulator::ContextLease lease = _emulator->LeaseContext();
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
            return false;
        if (ttd::TTDSessionRef(context)->GetPublishedSessionInfo().state != ttd::TTDSessionState::Detached)
            return false;
    }
    // At the history's end the card steps on live, unrecorded (D-3, as the main CPU's steps): the window's own step
    std::map<std::string, std::string> options{{"dir", "forward"}};
    if (kind != "in")
        options["kind"] = kind;
    if (kind == "to")
        options["pc"] = std::to_string(address);
    const QString title = kind == "over" ? tr("Step Over")
                          : kind == "out" ? tr("Step Out")
                          : kind == "to"  ? tr("Run to Cursor")
                                          : tr("Step");
    bool atEnd = false;
    cardMove(front, "step-instruction", options, title, &atEnd);
    return !atEnd;
}

void DebuggerTimeTravel::stepBack()
{
    if (const DebugCpuId front = cpu(); IsCardCpu(front))
        cardMove(front, "step-instruction", {{"dir", "back"}}, tr("Step Back"));
    else
        move("step-instruction", {{"dir", "back"}}, tr("Step Back"));
}

void DebuggerTimeTravel::stepOverBack()
{
    // The card's: one card instruction back, or back past a whole card routine when that instruction is its return
    if (const DebugCpuId front = cpu(); IsCardCpu(front))
    {
        cardMove(front, "reverse-step-over", {}, tr("Step Over Back"));
        return;
    }
    move("reverse-step-over", {}, tr("Step Over Back"));
}

void DebuggerTimeTravel::frameBack()
{
    move("step-back", {}, tr("Frame Back"));
}

void DebuggerTimeTravel::runBack()
{
    // The card's: back to the previous hit of its own breakpoints (found by replay; their hit counts stay)
    if (const DebugCpuId front = cpu(); IsCardCpu(front))
    {
        cardMove(front, "reverse-continue", {}, tr("Run Back"));
        return;
    }
    std::string pcs;
    if (_emulator)
        if (const BreakpointManager* breakpoints = _emulator->GetBreakpointManager())
            pcs = RunBackPcs(*breakpoints);
    if (pcs.empty())
    {
        if (inform)
            inform(tr("Run Back"), tr("Run Back stops at the enabled execution breakpoints: there is none."), false);
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
        if (inform)
            inform(tr("Run Back"), _lastResult, false);
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
