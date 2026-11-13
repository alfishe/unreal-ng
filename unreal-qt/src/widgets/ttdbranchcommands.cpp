#include "widgets/ttdbranchcommands.h"

#include <QAbstractButton>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

#include <algorithm>

#include "common/filehelper.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/remotecontrolguard.h"

namespace ttdgui
{

std::vector<ForkLeave> ForksAt(const ttd::TTDSessionInfo& info, uint64_t frame)
{
    if (!info.branches || info.branches->branches.size() < 2)
        return {};
    const uint64_t end = std::max(info.currentEndFrame, info.sessionStartFrame + 1);
    const std::map<uint64_t, std::vector<ForkLeave>> points = ttd::TTDForkPoints(*info.branches, end);
    const auto it = points.find(frame);
    return it == points.end() ? std::vector<ForkLeave>{} : it->second;
}

MomentText DescribeMoment(const ttd::TTDSessionInfo& info, uint64_t frame)
{
    MomentText out;
    if (info.checkpointCount == 0 || !info.unavailableReason.empty())
        return out;
    if (info.state == ttd::TTDSessionState::Recording && !info.recordingPaused)
        out.state = QObject::tr("Recording");
    else if (info.state == ttd::TTDSessionState::Detached || info.recordingPaused)
        out.state = QObject::tr("History: frame %1 of %2, replaying").arg(frame).arg(info.currentEndFrame);
    else
        out.state = QObject::tr("Live, not recorded");   // a stopped recording: the machine goes on past its end (D-3)

    if (info.branches && info.branches->branches.size() > 1)
    {
        if (const ttd::TTDBranchInfo* on = info.branches->Find(info.viewedBranch))
            out.branch = QObject::tr("on '%1' (%2 branches)")
                             .arg(QString::fromStdString(on->meta.name))
                             .arg(info.branches->branches.size());
        if (const size_t forks = ForksAt(info, frame).size(); forks != 0)
            out.fork = forks == 1 ? QObject::tr("fork here: 1 other future")
                                  : QObject::tr("fork here: %1 other futures").arg(forks);
    }
    return out;
}

QString MomentText::Line() const
{
    QStringList parts;
    for (const QString& part : {state, branch, fork})
        if (!part.isEmpty())
            parts << part;
    return parts.join(QStringLiteral(" \u00B7 "));
}

BranchCommands::BranchCommands(QWidget* parent, std::function<std::shared_ptr<Emulator>()> emulator)
    : _parent(parent), _emulator(std::move(emulator))
{
    confirm = [this](const QString& title, const QString& question) {
        return QMessageBox::question(_parent, title, question) == QMessageBox::Yes;
    };
}

void BranchCommands::Changed() const
{
    if (changed)
        changed();
}

bool BranchCommands::Run(const std::string& verb, std::map<std::string, std::string> options, const QString& title)
{
    const std::shared_ptr<Emulator> emulator = _emulator ? _emulator() : nullptr;
    if (!emulator)
        return false;
    ttd::TTDReply reply;
    {
        // The lease ends before any message box: a modal question must not hold the instance
        const Emulator::ContextLease lease = emulator->LeaseContext();
        EmulatorContext* context = lease.get();
        if (!context || !ttd::HasTimeTravelSession(context))
            return false;
        reply = ttd::TTDControl(context).Execute({verb, std::move(options)});
    }
    if (!reply.Ok())
        QMessageBox::warning(_parent, title, QString::fromStdString(reply.message));
    Changed();
    return reply.Ok();
}

const ttd::TTDBranchInfo* BranchCommands::Published(uint32_t branch,
                                                    std::shared_ptr<const ttd::TTDBranchesView>& keep) const
{
    const std::shared_ptr<Emulator> emulator = _emulator ? _emulator() : nullptr;
    if (!emulator)
        return nullptr;
    const Emulator::ContextLease lease = emulator->LeaseContext();
    EmulatorContext* context = lease.get();
    if (!context || !ttd::HasTimeTravelSession(context))
        return nullptr;
    keep = ttd::TTDSessionRef(context)->GetPublishedSessionInfo().branches;
    return keep ? keep->Find(branch) : nullptr;
}

bool BranchCommands::Switch(uint32_t branch)
{
    // The verb parks the machine and leaves it paused there, as a seek does
    if (!Run("branch-switch", {{"branch", std::to_string(branch)}}, QObject::tr("Switch Branch")))
        return false;
    if (moved)
        moved();
    return true;
}

void BranchCommands::Rename(uint32_t branch)
{
    std::shared_ptr<const ttd::TTDBranchesView> keep;
    const ttd::TTDBranchInfo* b = Published(branch, keep);
    if (!b)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(_parent, QObject::tr("Rename Branch"), QObject::tr("Name:"),
                                               QLineEdit::Normal, QString::fromStdString(b->meta.name), &ok);
    if (ok && !name.trimmed().isEmpty())
        Run("branch-update", {{"branch", std::to_string(branch)}, {"name", name.trimmed().toStdString()}},
            QObject::tr("Rename Branch"));
}

void BranchCommands::Tags(uint32_t branch)
{
    std::shared_ptr<const ttd::TTDBranchesView> keep;
    const ttd::TTDBranchInfo* b = Published(branch, keep);
    if (!b)
        return;
    QStringList tags;
    for (const std::string& t : b->meta.tags)
        tags << QString::fromStdString(t);
    bool ok = false;
    const QString text = QInputDialog::getText(_parent, QObject::tr("Branch Tags"), QObject::tr("Tags (comma separated):"),
                                               QLineEdit::Normal, tags.join(QStringLiteral(", ")), &ok);
    if (ok)
        Run("branch-update", {{"branch", std::to_string(branch)}, {"tags", text.toStdString()}},
            QObject::tr("Branch Tags"));
}

void BranchCommands::Note(uint32_t branch)
{
    std::shared_ptr<const ttd::TTDBranchesView> keep;
    const ttd::TTDBranchInfo* b = Published(branch, keep);
    if (!b)
        return;
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(_parent, QObject::tr("Branch Note"), QObject::tr("Note:"),
                                                        QString::fromStdString(b->meta.note), &ok);
    if (ok)
        Run("branch-update", {{"branch", std::to_string(branch)}, {"note", note.toStdString()}},
            QObject::tr("Branch Note"));
}

void BranchCommands::TogglePin(uint32_t branch)
{
    std::shared_ptr<const ttd::TTDBranchesView> keep;
    const ttd::TTDBranchInfo* b = Published(branch, keep);
    if (!b)
        return;
    Run("branch-update", {{"branch", std::to_string(branch)}, {"pinned", b->meta.pinned ? "false" : "true"}},
        QObject::tr("Pin Branch"));
}

bool BranchCommands::Delete(uint32_t branch)
{
    std::shared_ptr<const ttd::TTDBranchesView> keep;
    const ttd::TTDBranchInfo* b = Published(branch, keep);
    if (!b)
        return false;
    // The trunk goes only with the whole history (Clear)
    if (b->id == ttd::kTTDTrunk || RemoteControlGuard::Refuse(_parent))
        return false;
    // Always asked: the question names what goes with it (the branches leaving it, a pin)
    size_t descendants = 0;
    bool pinned = b->meta.pinned;
    std::vector<uint32_t> stack(b->children.begin(), b->children.end());
    while (!stack.empty())
    {
        const uint32_t c = stack.back();
        stack.pop_back();
        if (const ttd::TTDBranchInfo* child = keep->Find(c))
        {
            ++descendants;
            pinned = pinned || child->meta.pinned;
            stack.insert(stack.end(), child->children.begin(), child->children.end());
        }
    }
    const QString name = QString::fromStdString(b->meta.name);
    QString what = QObject::tr("Delete branch '%1'?").arg(name);
    if (descendants != 0)
        what = QObject::tr("Delete branch '%1' and the %2 branch(es) leaving it?").arg(name).arg(descendants);
    else if (b->meta.pinned)
        what = QObject::tr("Delete the pinned branch '%1'?").arg(name);
    if (pinned && !b->meta.pinned)
        what += QObject::tr(" A branch leaving it is pinned.");
    // A branch the timeline runs on (the active one or one of its ancestors) goes after a switch to its parent: the
    // history shown then ends where the branch left it
    bool onLineage = false;
    for (const ttd::TTDLineageSpan& span : keep->lineage)
        onLineage = onLineage || span.branch == b->id;
    const ttd::TTDBranchInfo* parent = keep->Find(b->parent);
    const uint32_t parentId = parent ? parent->id : ttd::kTTDNoBranch;
    if (onLineage && parent)
        what += QObject::tr("\n\nThe timeline runs on it: it switches to '%1' first.")
                    .arg(QString::fromStdString(parent->meta.name));
    if (!confirm || !confirm(QObject::tr("Delete Branch"), what))
        return false;
    if (onLineage && parentId != ttd::kTTDNoBranch && !Switch(parentId))
        return false;
    return Run("branch-delete", {{"branch", std::to_string(branch)}, {"force", pinned ? "true" : "false"}},
               QObject::tr("Delete Branch"));
}

void BranchCommands::SaveLineage()
{
    const std::shared_ptr<Emulator> emulator = _emulator ? _emulator() : nullptr;
    if (!emulator)
        return;
    const std::string recordings = ttd::RecordingsRoot();
    if (!recordings.empty())
        FileHelper::CreateFolders(recordings);
    const QString fileName = QFileDialog::getSaveFileName(_parent, QObject::tr("Save Lineage"),
                                                          QString::fromStdString(recordings),
                                                          QObject::tr("Time Travel Session (*.ttd)"));
    if (fileName.isEmpty())
        return;
    if (emulator->IsRunning() && !emulator->IsPaused())
        emulator->Pause(execution::Requester{execution::Origin::Gui, {}}, "ttd save lineage");
    if (Run("dump", {{"path", fileName.toUtf8().toStdString()}, {"lineage", "true"}}, QObject::tr("Save Lineage")))
        QMessageBox::information(_parent, QObject::tr("Save Lineage"),
                                 QObject::tr("The history shown saved to %1").arg(fileName));
}

bool BranchCommands::ResumeRecordingHere(const QString& title, bool runAfter)
{
    // A removed instance has nothing to continue: no question, no warning box
    const std::shared_ptr<Emulator> emulator = _emulator ? _emulator() : nullptr;
    if (!emulator || emulator->IsRetired())
        return false;
    EmulatorContext* context = emulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context))
        return false;
    // From where the machine stands inside the history; a machine that ran on past the end of a stopped recording
    // goes back to that end (a session goes on without a gap: what ran after the stop was not recorded). The seek
    // parks the machine first, as the slider's does: the resume itself never waits for a running machine
    std::map<std::string, std::string> options{{"media", "ask"}};
    {
        const ttd::TTDSessionInfo info = ttd::TTDSessionRef(context)->GetPublishedSessionInfo();
        if (info.state != ttd::TTDSessionState::Detached)
        {
            const ttd::TTDReply seek =
                ttd::TTDControl(context).Execute({"seek", {{"frame", std::to_string(info.currentEndFrame)}}});
            const StateNode* reached = seek.body.find("reached");
            if (!seek.Ok() || !reached || !reached->b)
            {
                QMessageBox::warning(_parent, title,
                                     QObject::tr("Could not go back to the end of the recording: %1")
                                         .arg(QString::fromStdString(seek.message)));
                Changed();
                return false;
            }
            if (moved)
                moved();
        }
    }
    // A medium that cannot go back there (another medium in the slot, a write beyond its undo log) needs the user's
    // decision (M5, F11)
    ttd::TTDReply reply = ttd::TTDControl(context).Execute({"resume", options});
    if (const StateNode* needs = reply.body.find("needs_decision"); needs && needs->b)
    {
        QString list;
        if (const StateNode* media = reply.body.find("media"))
            for (const StateNode& item : media->items)
                list += QString("\n- %1: %2").arg(QString::fromStdString(item.find("slot")->s),
                                                   QString::fromStdString(item.find("reason")->s));
        QMessageBox box(QMessageBox::Question, title,
                        QObject::tr("These media cannot go back to this point:%1\n\nThe recording goes on from here "
                                    "either way. What should happen to them?")
                            .arg(list),
                        QMessageBox::NoButton, _parent);
        QPushButton* empty = box.addButton(QObject::tr("Empty the slots"), QMessageBox::AcceptRole);
        QPushButton* keep = box.addButton(QObject::tr("Keep them as they are"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(empty);
        box.exec();
        const QAbstractButton* clicked = box.clickedButton();
        if (clicked != empty && clicked != keep)
            return false;   // cancelled: the machine stays where it is, nothing changed
        options["media"] = clicked == empty ? "empty" : "keep";
        reply = ttd::TTDControl(context).Execute({"resume", options});
    }
    const StateNode* resumed = reply.body.find("resumed");
    if (!reply.Ok() || !resumed || !resumed->b)
    {
        QMessageBox::warning(_parent, title,
                             reply.Ok() ? QObject::tr("The recording could not go on from here.")
                                        : QString::fromStdString(reply.message));
        Changed();
        return false;
    }
    // The machine runs on its own thread: a paused one is resumed. Never Start(): it runs the main loop on the
    // calling thread, and on the GUI thread that froze the window
    if (runAfter && emulator->IsPaused())
        emulator->Resume(execution::Requester{execution::Origin::Gui, {}});
    Changed();
    return true;
}

}  // namespace ttdgui
