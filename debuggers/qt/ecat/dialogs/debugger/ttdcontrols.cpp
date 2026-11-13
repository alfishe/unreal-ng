// unreal-debugger (not in ecat): the machine's time travel in the debugger window

#include "ttdcontrols.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <set>

namespace {

//The trunk's id (ttd::kTTDTrunk): it goes only with the whole history
constexpr unsigned int kTrunk = 0;
//Run Back's addresses at most (reverse-continue's pcs)
constexpr size_t kMaxRunBackPcs = 1024;

QString hex4(unsigned int value)
{
    return QString("#%1").arg(value & 0xFFFF, 4, 16, QChar('0')).toUpper();
}

unsigned int id_of(const QJsonObject &b)
{
    return (unsigned int)b.value("id").toInteger();
}

QString name_of(const QJsonObject &b)
{
    const QString name = b.value("name").toString();
    return name.isEmpty() ? QString::number(id_of(b)) : name;
}

uint64_t frame_of(const QJsonValue &point)
{
    return (uint64_t)point.toObject().value("frame").toInteger();
}

} // namespace

TtdControls::TtdControls(QWidget * window, QToolBar * toolbar, Emulator * e, std::function<CPU *()> main_cpu)
    : QObject(window), m_window(window), e(e), m_main_cpu(std::move(main_cpu))
{
    confirm = [this](const QString &title, const QString &text) {
        return QMessageBox::question(m_window, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
               QMessageBox::Yes;
    };

    m_group.push_back(toolbar->addSeparator());
    auto add = [this, toolbar](const QString &text, const QKeySequence &key, void (TtdControls::*slot)()) {
        QAction * a = toolbar->addAction(text);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WindowShortcut);
            a->setToolTip(text + " (" + key.toString(QKeySequence::NativeText) + ")");
            m_window->addAction(a);
        }
        connect(a, &QAction::triggered, this, slot);
        m_group.push_back(a);
        return a;
    };
    m_step_back = add(tr("Step Back"), QKeySequence(Qt::SHIFT | Qt::Key_F7), &TtdControls::step_back);
    m_step_over_back = add(tr("Step Over Back"), QKeySequence(Qt::SHIFT | Qt::Key_F8), &TtdControls::step_over_back);
    m_run_back = add(tr("Run Back"), QKeySequence(Qt::SHIFT | Qt::Key_F9), &TtdControls::run_back);
    m_frame_back = add(tr("Frame Back"), QKeySequence(), &TtdControls::frame_back);
    m_branch_here = add(tr("Continue Rec"), QKeySequence(), &TtdControls::branch_here);
    m_step_back->setStatusTip(tr("One instruction back in the recorded history"));
    m_step_over_back->setStatusTip(tr("Back over a routine whose return is the instruction before"));
    m_run_back->setStatusTip(tr("Back to the last time a breakpoint's address ran"));
    m_frame_back->setStatusTip(tr("One frame back in the recorded history"));

    m_branch_menu = new QMenu(window);
    m_branch_menu->setObjectName("ecatTtdBranchMenu");
    connect(m_branch_menu, &QMenu::aboutToShow, this, &TtdControls::build_branch_menu);
    m_branch_button = new QToolButton(toolbar);
    m_branch_button->setObjectName("ecatTtdBranches");
    m_branch_button->setPopupMode(QToolButton::InstantPopup);
    m_branch_button->setMenu(m_branch_menu);
    m_branch_button->setToolTip(tr("The branches of the history: switch, rename, tag, pin, delete"));
    m_branch_button_action = toolbar->addWidget(m_branch_button);
    m_group.push_back(m_branch_button_action);

    set_shown(false);
}

bool TtdControls::call(const std::string &verb, std::map<std::string, std::string> options, QJsonObject * out,
                       const QString &title)
{
    const Emulator::TtdAnswer a = e->ttd_call(verb, options);
    const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(a.json)).object();
    if (out) *out = o;
    if (!a.ok && !title.isEmpty())
        QMessageBox::warning(m_window, title, o.value("message").toString(tr("The emulator did not answer.")));
    return a.ok;
}

void TtdControls::poll(bool force)
{
    if (!force && ++m_tick < 5) return;
    m_tick = 0;
    const QString before = moment();
    m_state.clear();
    m_branch_part.clear();
    m_fork_part.clear();
    m_branches.clear();
    m_branch_list = QJsonObject();

    //A history: the position (a read the companions poll fifty times a second), then the session
    bool history = e->ttd_supported() && e->debug_online() && call("position", {}, &m_position) &&
                   m_position.value("history").toBool() && call("status", {}, &m_status) &&
                   m_status.value("checkpoint_count").toInteger() != 0 &&
                   m_status.value("unavailable_reason").toString().isEmpty();
    if (history) {
        const QString state = m_status.value("state").toString();
        const bool paused = m_status.value("recording_paused").toBool();
        const uint64_t frame = frame_of(m_position.value("current"));
        if (state == "recording" && !paused)
            m_state = tr("Recording");
        else if (state == "detached" || paused)
            m_state = tr("History: frame %1 of %2, replaying")
                          .arg(frame)
                          .arg(m_status.value("current_end_frame").toInteger());
        else
            m_state = tr("Live, not recorded");   //a stopped recording: the machine goes on past its end (D-3)

        //Branches: only once the history is more than the trunk
        if (m_status.value("branch_count").toInteger() > 1 && call("branches", {}, &m_branch_list)) {
            const QJsonArray list = m_branch_list.value("branches").toArray();
            std::set<unsigned int> listed;
            auto take = [&](unsigned int id) {
                for (const QJsonValue &v : list)
                    if (id_of(v.toObject()) == id && listed.insert(id).second) m_branches.push_back(v.toObject());
            };
            //The active lineage first (trunk to the active branch), then the others by id
            for (const QJsonValue &span : m_branch_list.value("lineage").toArray())
                take((unsigned int)span.toObject().value("branch").toInteger());
            for (const QJsonValue &v : list) take(id_of(v.toObject()));
            if (const QJsonObject * viewed = branch((unsigned int)m_branch_list.value("viewed").toInteger()))
                m_branch_part = tr("on '%1' (%2 branches)").arg(name_of(*viewed)).arg(list.size());
            const qsizetype forks = m_branch_list.value("forks_here").toArray().size();
            if (forks != 0)
                m_fork_part = forks == 1 ? tr("fork here: 1 other future") : tr("fork here: %1 other futures").arg(forks);
        }
    }
    set_shown(history);
    update_actions();
    if (moment() != before) emit moment_changed();
}

QString TtdControls::moment() const
{
    QStringList parts;
    for (const QString &p : {m_state, m_branch_part, m_fork_part})
        if (!p.isEmpty()) parts << p;
    return parts.join(QString(" ") + QChar(0x00B7) + " ");
}

void TtdControls::set_live(bool live)
{
    if (m_live == live) return;
    m_live = live;
    update_actions();
}

void TtdControls::set_shown(bool shown)
{
    if (m_shown == shown) return;
    m_shown = shown;
    for (QAction * a : m_group) a->setVisible(shown);
}

void TtdControls::update_actions()
{
    const QString state = m_status.value("state").toString();
    const bool detached = state == "detached";
    const bool recording = state == "recording" && !m_status.value("recording_paused").toBool();
    const uint64_t frame = frame_of(m_position.value("current"));
    const uint64_t end = (uint64_t)m_status.value("current_end_frame").toInteger();
    const bool live = m_live && m_shown;
    //At the start of the history nothing is earlier
    const bool at_start = detached && frame <= (uint64_t)m_status.value("session_start_frame").toInteger();
    CPU * cpu = m_main_cpu ? m_main_cpu() : nullptr;
    const bool any_breakpoint = cpu != nullptr && !cpu->get_breakpoints().empty();

    m_step_back->setEnabled(live && !at_start);
    m_step_over_back->setEnabled(live && !at_start);
    m_frame_back->setEnabled(live && !at_start);
    m_run_back->setEnabled(live && !at_start && any_breakpoint);
    //Records on from here: never while it records already (rule 7: only this button and Start Rec record)
    m_branch_here->setEnabled(live && !recording);
    const bool fork = detached && frame < end;
    m_branch_here->setText(fork ? tr("Branch Here") : tr("Continue Rec"));
    m_branch_here->setToolTip(fork ? tr("A new branch of the history from this moment: it records from here on")
                                   : tr("The recording goes on from its end"));

    m_branch_button->setText(m_branch_part);
    m_branch_button_action->setVisible(m_shown && !m_branch_part.isEmpty());
    m_branch_button->setEnabled(m_live);
    QStringList tip;
    for (const QString &p : {m_state, m_fork_part, m_last_result})
        if (!p.isEmpty()) tip << p;
    m_branch_button->setToolTip(tip.join('\n'));
}

bool TtdControls::move(const std::string &verb, std::map<std::string, std::string> options, const QString &title,
                       QJsonObject * out)
{
    QJsonObject answer;
    if (!call(verb, std::move(options), &answer, title)) {
        poll(true);
        return false;
    }
    m_last_result = answer.value("text").toString().trimmed();
    if (out) *out = answer;
    poll(true);
    emit moved();
    return true;
}

void TtdControls::step_back()
{
    if (m_step_back->isEnabled()) move("step-instruction", {{"dir", "back"}}, tr("Step Back"));
}

void TtdControls::step_over_back()
{
    if (m_step_over_back->isEnabled()) move("reverse-step-over", {}, tr("Step Over Back"));
}

void TtdControls::frame_back()
{
    if (m_frame_back->isEnabled()) move("step-back", {}, tr("Frame Back"));
}

void TtdControls::run_back()
{
    if (!m_shown || !m_live) return;
    CPU * cpu = m_main_cpu ? m_main_cpu() : nullptr;
    std::vector<unsigned int> addresses = cpu ? cpu->get_breakpoints() : std::vector<unsigned int>{};
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
    if (addresses.size() > kMaxRunBackPcs) addresses.resize(kMaxRunBackPcs);
    QStringList pcs;
    for (unsigned int a : addresses) pcs << hex4(a);
    if (pcs.isEmpty()) {
        QMessageBox::information(m_window, tr("Run Back"), tr("Run Back stops at the breakpoints: there is none."));
        return;
    }
    //Not found: the machine stays where it is and the answer says so (matched false)
    QJsonObject answer;
    if (!move("reverse-continue", {{"pcs", pcs.join(',').toStdString()}}, tr("Run Back"), &answer)) return;
    if (answer.value("matched").toBool())
        m_last_result = tr("Run Back: at the breakpoint %1").arg(hex4((unsigned int)answer.value("pc").toInteger()));
    else {
        m_last_result = tr("Run Back: no breakpoint was reached earlier in the history");
        QMessageBox::information(m_window, tr("Run Back"), m_last_result);
    }
    update_actions();
}

void TtdControls::branch_here()
{
    if (!m_branch_here->isEnabled()) return;
    const QString title = m_branch_here->text();
    poll(true);
    //From where the machine stands inside the history; a machine that ran on past the end of a stopped recording
    //goes back to that end first (what ran after the stop was not recorded)
    if (m_status.value("state").toString() != "detached") {
        QJsonObject seek;
        const std::string end = std::to_string(m_status.value("current_end_frame").toInteger());
        if (!call("seek", {{"frame", end}}, &seek) || !seek.value("reached").toBool()) {
            QMessageBox::warning(m_window, title,
                                 tr("Could not go back to the end of the recording: %1")
                                     .arg(seek.value("message").toString()));
            poll(true);
            return;
        }
        emit moved();
    }
    //A medium that cannot go back there (another medium in the slot, a write beyond its undo log) needs the user's
    //decision, as in the TTD panel
    std::map<std::string, std::string> options{{"media", "ask"}};
    QJsonObject answer;
    bool ok = call("resume", options, &answer);
    if (answer.value("needs_decision").toBool()) {
        QString list;
        for (const QJsonValue &v : answer.value("media").toArray())
            list += QString("\n- %1: %2").arg(v.toObject().value("slot").toString(), v.toObject().value("reason").toString());
        QMessageBox box(QMessageBox::Question, title,
                        tr("These media cannot go back to this point:%1\n\nThe recording goes on from here either "
                           "way. What should happen to them?")
                            .arg(list),
                        QMessageBox::NoButton, m_window);
        QPushButton * empty = box.addButton(tr("Empty the slots"), QMessageBox::AcceptRole);
        QPushButton * keep = box.addButton(tr("Keep them as they are"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(empty);
        box.exec();
        const QAbstractButton * clicked = box.clickedButton();
        if (clicked != empty && clicked != keep) return;   //cancelled: nothing changed
        options["media"] = clicked == empty ? "empty" : "keep";
        ok = call("resume", options, &answer);
    }
    if (!ok || !answer.value("resumed").toBool())
        QMessageBox::warning(m_window, title,
                             ok ? tr("The recording could not go on from here.") : answer.value("message").toString());
    //The machine stays paused here: the debugger's Run goes on (and records now)
    poll(true);
    emit moved();
}

const QJsonObject * TtdControls::branch(unsigned int id) const
{
    for (const QJsonObject &b : m_branches)
        if (id_of(b) == id) return &b;
    return nullptr;
}

bool TtdControls::switch_branch(unsigned int id)
{
    //The verb parks the machine and leaves it paused there, as a seek does
    return move("branch-switch", {{"branch", std::to_string(id)}}, tr("Switch Branch"));
}

void TtdControls::rename_branch(unsigned int id)
{
    const QJsonObject * b = branch(id);
    if (b == nullptr) return;
    bool ok = false;
    const QString name = QInputDialog::getText(m_window, tr("Rename Branch"), tr("Name:"), QLineEdit::Normal,
                                               b->value("name").toString(), &ok);
    if (ok && !name.trimmed().isEmpty())
        call("branch-update", {{"branch", std::to_string(id)}, {"name", name.trimmed().toStdString()}}, nullptr,
             tr("Rename Branch"));
    poll(true);
}

void TtdControls::tag_branch(unsigned int id)
{
    const QJsonObject * b = branch(id);
    if (b == nullptr) return;
    QStringList tags;
    for (const QJsonValue &t : b->value("tags").toArray()) tags << t.toString();
    bool ok = false;
    const QString text = QInputDialog::getText(m_window, tr("Branch Tags"), tr("Tags (comma separated):"),
                                               QLineEdit::Normal, tags.join(", "), &ok);
    if (ok)
        call("branch-update", {{"branch", std::to_string(id)}, {"tags", text.toStdString()}}, nullptr,
             tr("Branch Tags"));
    poll(true);
}

void TtdControls::note_branch(unsigned int id)
{
    const QJsonObject * b = branch(id);
    if (b == nullptr) return;
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(m_window, tr("Branch Note"), tr("Note:"),
                                                        b->value("note").toString(), &ok);
    if (ok)
        call("branch-update", {{"branch", std::to_string(id)}, {"note", note.toStdString()}}, nullptr,
             tr("Branch Note"));
    poll(true);
}

void TtdControls::pin_branch(unsigned int id)
{
    const QJsonObject * b = branch(id);
    if (b == nullptr) return;
    call("branch-update", {{"branch", std::to_string(id)}, {"pinned", b->value("pinned").toBool() ? "false" : "true"}},
         nullptr, tr("Pin Branch"));
    poll(true);
}

bool TtdControls::delete_branch(unsigned int id)
{
    const QJsonObject * found = branch(id);
    //The trunk goes only with the whole history (Clear in the TTD panel)
    if (found == nullptr || id == kTrunk) return false;
    const QJsonObject b = *found;
    //Always asked: the question names what goes with it (the branches leaving it, a pin)
    size_t descendants = 0;
    bool pinned = b.value("pinned").toBool();
    std::vector<unsigned int> stack{id};
    while (!stack.empty()) {
        const unsigned int parent = stack.back();
        stack.pop_back();
        for (const QJsonObject &c : m_branches) {
            const QJsonValue p = c.value("parent");
            if (!p.isNull() && (unsigned int)p.toInteger() == parent && id_of(c) != parent) {
                descendants++;
                pinned = pinned || c.value("pinned").toBool();
                stack.push_back(id_of(c));
            }
        }
    }
    const QString name = b.value("name").toString();
    QString what = tr("Delete branch '%1'?").arg(name);
    if (descendants != 0)
        what = tr("Delete branch '%1' and the %2 branch(es) leaving it?").arg(name).arg(descendants);
    else if (b.value("pinned").toBool())
        what = tr("Delete the pinned branch '%1'?").arg(name);
    if (pinned && !b.value("pinned").toBool()) what += tr(" A branch leaving it is pinned.");
    //A branch the timeline runs on (the active one or one of its ancestors) goes after a switch to its parent
    const bool on_lineage = b.value("on_lineage").toBool();
    const QJsonValue parent_value = b.value("parent");
    const QJsonObject * parent = parent_value.isNull() ? nullptr : branch((unsigned int)parent_value.toInteger());
    const unsigned int parent_id = parent ? id_of(*parent) : kTrunk;
    if (on_lineage && parent)
        what += tr("\n\nThe timeline runs on it: it switches to '%1' first.").arg(parent->value("name").toString());
    if (!confirm || !confirm(tr("Delete Branch"), what)) return false;
    if (on_lineage && parent && !switch_branch(parent_id)) return false;
    const bool ok = call("branch-delete", {{"branch", std::to_string(id)}, {"force", pinned ? "true" : "false"}},
                         nullptr, tr("Delete Branch"));
    poll(true);
    return ok;
}

void TtdControls::save_lineage()
{
    //The path is the emulator's: the same computer for a debugger in process or on this host
    const QString file = QFileDialog::getSaveFileName(m_window, tr("Save Lineage"), QString(),
                                                      tr("Time Travel Session (*.ttd)"));
    if (file.isEmpty()) return;
    if (!e->debug_paused()) e->debug_pause();
    if (call("dump", {{"path", file.toUtf8().toStdString()}, {"lineage", "true"}}, nullptr, tr("Save Lineage")))
        QMessageBox::information(m_window, tr("Save Lineage"), tr("The history shown saved to %1").arg(file));
}

void TtdControls::build_branch_menu()
{
    m_branch_menu->clear();
    poll(true);
    if (m_branches.empty()) return;
    const unsigned int active = (unsigned int)m_branch_list.value("active").toInteger();

    auto label = [](const QJsonObject &b) {
        QString text = name_of(b);
        if (!b.value("parent").isNull())
            text += tr("  (from frame %1, %2 frames)").arg(frame_of(b.value("fork"))).arg(b.value("length").toInteger());
        if (b.value("pinned").toBool()) text += tr("  [pinned]");
        return text;
    };

    QActionGroup * group = new QActionGroup(m_branch_menu);
    for (const QJsonObject &b : m_branches) {
        const unsigned int id = id_of(b);
        QAction * a = m_branch_menu->addAction(label(b));
        a->setCheckable(true);
        a->setChecked(id == active);
        a->setActionGroup(group);
        a->setToolTip(b.value("note").toString());
        connect(a, &QAction::triggered, this, [this, id, active]() {
            if (id != active) switch_branch(id);
        });
    }

    const QJsonArray forks = m_branch_list.value("forks_here").toArray();
    if (!forks.isEmpty()) {
        m_branch_menu->addSection(tr("Branches leaving here"));
        for (const QJsonValue &v : forks) {
            const QJsonObject leave = v.toObject();
            const unsigned int id = id_of(leave);
            QAction * a = m_branch_menu->addAction(
                tr("%1  (%2 frames)").arg(name_of(leave)).arg(leave.value("frames").toInteger()));
            connect(a, &QAction::triggered, this, [this, id]() { switch_branch(id); });
        }
    }

    m_branch_menu->addSeparator();
    QMenu * manage = m_branch_menu->addMenu(tr("Manage"));
    for (const QJsonObject &b : m_branches) {
        const unsigned int id = id_of(b);
        QMenu * sub = manage->addMenu(name_of(b));
        QAction * sw = sub->addAction(tr("Switch"), this, [this, id]() { switch_branch(id); });
        sw->setEnabled(id != active);
        sub->addAction(tr("Rename..."), this, [this, id]() { rename_branch(id); });
        sub->addAction(tr("Tags..."), this, [this, id]() { tag_branch(id); });
        sub->addAction(tr("Note..."), this, [this, id]() { note_branch(id); });
        sub->addAction(b.value("pinned").toBool() ? tr("Unpin") : tr("Pin"), this, [this, id]() { pin_branch(id); });
        QAction * del = sub->addAction(tr("Delete..."), this, [this, id]() { delete_branch(id); });
        del->setEnabled(id != kTrunk);
        if (id == kTrunk) del->setToolTip(tr("The trunk goes only with the whole history (Clear in the TTD panel)"));
        sub->setToolTipsVisible(true);
    }
    m_branch_menu->addAction(tr("Save Lineage..."), this, [this]() { save_lineage(); });
    m_branch_menu->setToolTipsVisible(true);
}
