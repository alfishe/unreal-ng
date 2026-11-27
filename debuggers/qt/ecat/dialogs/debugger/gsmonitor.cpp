// unreal-debugger (not in ecat): the GS monitor tab (gui-card-debugger A5.3, C10; gs-debugger P5.6)

#include "gsmonitor.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QBrush>
#include <QPointer>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "addressmenu.h"
#include "debugstyle.h"

namespace
{
QString hex2(int v)
{
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X", v & 0xFF);
    return QString(b);
}

QString grouped(uint64_t v)
{
    QString digits = QString::number((qulonglong)v);
    for (int i = digits.size() - 3; i > 0; i -= 3) digits.insert(i, ',');
    return digits;
}

QString plural(uint64_t n, const char * one, const char * many)
{
    return grouped(n) + " " + (n == 1 ? one : many);
}

const QString kDot = QString(" ") + QChar(0x00B7) + " ";

//A row's record, kept on its first cell
enum RowRole { RoleFrame = Qt::UserRole, RoleT, RoleCommand, RoleBytes, RoleUpload, RoleHostData };

//The processor of that name ("cpu" the main one, "gs", "ms"), null when none
CPU * cpu_named(Emulator * e, const std::string &name)
{
    if (!e->loaded || e->dm == nullptr) return nullptr;
    for (CPU * c : e->dm->get_cpus())
        if (c->name == name) return c;
    return nullptr;
}

//An event breakpoint: the user's equal one when there is one (enabled again, a hit policy's count reset to count from
//now), else a new one. Its id in id (0 when the shim did not list it); false with the reason in error
bool event_breakpoint(Emulator * e, CPU * c, const std::string &kind, unsigned int value, unsigned int mask,
                      const std::string &hits, unsigned int &id, bool &existed, std::string &error)
{
    existed = false;
    id = 0;
    bool ok = false;
    e->invoke([&]() {
        auto find = [&]() {
            for (const EventBreakpoint &b : c->get_event_breakpoints())
                if (b.kind == kind && b.mask == mask && (b.value & mask) == (value & mask) && b.hits == hits && b.stops) {
                    id = b.id;
                    return true;
                }
            return false;
        };
        if (find()) {
            existed = true;
            for (const EventBreakpoint &b : c->get_event_breakpoints())
                if (b.id == id && !b.enabled) c->enable_event_breakpoint(id, true);
            if (!hits.empty()) c->reset_event_hits(id);
            ok = true;
            return;
        }
        ok = c->add_event_breakpoint(kind, value & (mask ? mask : 0xFF), mask, hits, error);
        if (ok) find();
    });
    return ok;
}
}

GsMonitorView::GsMonitorView(Emulator * e, QWidget *parent):
    QWidget(parent),
    e(e)
{
    setObjectName("card:gs-monitor");
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    m_caption = new QLabel(this);
    m_caption->setObjectName("caption");
    m_caption->hide();
    layout->addWidget(m_caption);

    //1. The protocol strip, with Send... and the machine's Pause / Continue at its right
    QHBoxLayout * top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    m_strip = new QLabel(this);
    m_strip->setObjectName("gsmonitor:strip");
    m_strip->setFont(dbg::font());
    m_strip->setWordWrap(true);
    m_strip->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_strip, 1);
    m_send = new QToolButton(this);
    m_send->setObjectName("gsmonitor:send");
    m_send->setText(tr("Send..."));
    m_send->setToolTip(tr("The GS send tab: send the card a command as the host would (Ctrl+Shift+S)"));
    connect(m_send, &QToolButton::clicked, this, &GsMonitorView::send_requested);
    top->addWidget(m_send, 0, Qt::AlignTop);
    m_machine = new QToolButton(this);
    m_machine->setObjectName("gsmonitor:machine");
    m_machine->setText(tr("Pause"));
    m_machine->setEnabled(false);
    top->addWidget(m_machine, 0, Qt::AlignTop);
    layout->addLayout(top);
    connect(m_machine, &QToolButton::clicked, this, [this]() {
        QAction * a = m_pause != nullptr && m_pause->isEnabled() ? m_pause : m_run;
        if (a != nullptr && a->isEnabled()) a->trigger();
        update_machine_button();
    });

    //2. The error tile
    m_tile_box = new QWidget(this);
    m_tile_box->setObjectName("gsmonitor:tilebox");
    m_tile_box->setAutoFillBackground(true);
    QHBoxLayout * tile = new QHBoxLayout(m_tile_box);
    tile->setContentsMargins(4, 2, 4, 2);
    m_tile = new QLabel(m_tile_box);
    m_tile->setObjectName("gsmonitor:tile");
    m_tile->setWordWrap(true);
    m_tile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tile->addWidget(m_tile, 1);
    m_clear = new QPushButton(tr("Clear"), m_tile_box);
    m_clear->setObjectName("gsmonitor:clear");
    m_clear->setToolTip(tr("The tab's counters back to zero (the command log stays)"));
    connect(m_clear, &QPushButton::clicked, this, [this]() { clear_counters(); });
    tile->addWidget(m_clear, 0, Qt::AlignTop);
    layout->addWidget(m_tile_box);

    //The command log off: said, with Turn on
    m_off = new QWidget(this);
    m_off->setObjectName("gsmonitor:off");
    QHBoxLayout * off = new QHBoxLayout(m_off);
    off->setContentsMargins(0, 0, 0, 0);
    m_off_text = new QLabel(m_off);
    m_off_text->setWordWrap(true);
    off->addWidget(m_off_text, 1);
    m_turn_on = new QPushButton(tr("Turn on"), m_off);
    m_turn_on->setObjectName("gsmonitor:turn-on");
    connect(m_turn_on, &QPushButton::clicked, this, [this]() { turn_on(); });
    off->addWidget(m_turn_on, 0, Qt::AlignTop);
    m_off->hide();
    layout->addWidget(m_off);
    m_answer = new QLabel(this);
    m_answer->setObjectName("gsmonitor:answer");
    m_answer->hide();
    layout->addWidget(m_answer);

    //3. The log: its line (mode, counts, following) with Follow, then the records
    QHBoxLayout * line = new QHBoxLayout();
    line->setContentsMargins(0, 0, 0, 0);
    m_log_line = new QLabel(this);
    m_log_line->setObjectName("gsmonitor:logline");
    line->addWidget(m_log_line, 1);
    m_follow_button = new QToolButton(this);
    m_follow_button->setObjectName("gsmonitor:follow");
    m_follow_button->setText(tr("Follow"));
    m_follow_button->setToolTip(tr("Follow the newest records again"));
    m_follow_button->setEnabled(false);
    connect(m_follow_button, &QToolButton::clicked, this, [this]() { follow(); });
    line->addWidget(m_follow_button);
    layout->addLayout(line);

    m_table = new QTableWidget(this);
    m_table->setObjectName("gsmonitor:table");
    m_table->setFont(dbg::font());
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(dbg::row_height(m_table->fontMetrics()));
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels(QStringList() << tr("Frame") << tr("T") << tr("Kind") << tr("Command") << tr("Name")
                                                     << tr("Text"));
    m_table->horizontalHeader()->setStretchLastSection(true);
    if (QAbstractButton * corner = m_table->findChild<QAbstractButton *>()) corner->setProperty("dbg_mono", true);
    //The row actions (A5.4): a context menu on a row; Ctrl+C copies the selected rows
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &at) {
        const int row = m_table->rowAt(at.y());
        if (row < 0) return;
        QMenu * menu = row_menu(row, this);
        menu->exec(m_table->viewport()->mapToGlobal(at));
        menu->deleteLater();
    });
    QAction * copy = new QAction(tr("Copy"), m_table);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    connect(copy, &QAction::triggered, this, [this]() {
        if (m_table->currentRow() >= 0) QApplication::clipboard()->setText(copy_text(m_table->currentRow()));
    });
    m_table->addAction(copy);
    m_action = new QLabel(this);
    m_action->setObjectName("gsmonitor:action");
    m_action->setWordWrap(true);
    m_action->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_action->hide();
    layout->addWidget(m_action);
    layout->addWidget(m_table, 1);
    //Scrolling up stops following the tail; back at the bottom it follows again
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_scrolling) return;
        m_following = value >= m_table->verticalScrollBar()->maximum() - 1;
        m_follow_button->setEnabled(!m_following);
    });
    show_tile();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &GsMonitorView::on_timer);
}

void GsMonitorView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer->start((int)e->live_card_ms());
}

void GsMonitorView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

//While the machine runs (never paused for it: a read where it stands still in process, plain GETs remote); its stops
//are the window's (refreshed once there). Not while the window is minimized: following is for the eyes
void GsMonitorView::on_timer()
{
    if (!e->loaded || !e->debug_online() || window()->isMinimized()) return;
    if (e->debug_paused() && !e->debug_stepping()) return;
    refresh();
}

void GsMonitorView::set_card(const std::string &card, const QString &caption)
{
    m_caption->setText(caption);
    m_caption->setVisible(!caption.isEmpty());
    if (card == m_card) return;
    m_card = card;
    restart();
    m_anomalies = 0;
    m_last_anomaly.clear();
    m_last_anomaly_frame = -1;
    m_unknown_base = 0;
    m_unknown_total = 0;
    m_unknown_seen = 0;
    m_unknown_present = false;
    show_tile();
}

void GsMonitorView::set_machine_actions(QAction * pause, QAction * run)
{
    m_pause = pause;
    m_run = run;
    for (QAction * a : {pause, run})
        if (a != nullptr) connect(a, &QAction::changed, this, [this]() { update_machine_button(); });
    update_machine_button();
}

void GsMonitorView::update_machine_button()
{
    const bool running = m_pause != nullptr && m_pause->isEnabled();
    m_machine->setText(running ? tr("Pause") : tr("Continue"));
    m_machine->setToolTip(running ? tr("Pause the machine (F12)") : tr("Continue the machine, stopping at the breakpoints (F9)"));
    m_machine->setEnabled(running || (m_run != nullptr && m_run->isEnabled()));
}

void GsMonitorView::restart()
{
    m_table->setRowCount(0);
    m_have_cursor = false;
    m_since = 0;
    m_unseen = 0;
}

void GsMonitorView::clear_counters()
{
    m_anomalies = 0;
    m_last_anomaly.clear();
    m_last_anomaly_frame = -1;
    m_unseen = 0;
    m_unknown_base = m_unknown_total;
    m_unknown_seen = 0;
    show_tile();
}

void GsMonitorView::follow()
{
    m_following = true;
    m_follow_button->setEnabled(false);
    m_scrolling = true;
    m_table->scrollToBottom();
    m_scrolling = false;
}

void GsMonitorView::append(const Emulator::GsMonitorRow &r)
{
    const dbg::Colors c = dbg::colors(palette());
    const bool anomaly = r.kind == "anomaly";
    const int at = m_table->rowCount();
    m_table->insertRow(at);
    const QString cells[ColCount] = {QString::number((qlonglong)r.frame),
                                     QString::number((qlonglong)r.t),
                                     QString::fromStdString(anomaly && !r.anomaly.empty() ? r.anomaly : r.kind),
                                     r.command >= 0 ? hex2(r.command) : QString(),
                                     QString::fromStdString(r.name),
                                     QString::fromStdString(r.text)};
    QColor tint = c.breakpoint;
    tint.setAlpha(50);
    for (int k = 0; k < ColCount; k++) {
        QTableWidgetItem * item = new QTableWidgetItem(cells[k]);
        if (k <= ColT) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (anomaly) {
            item->setForeground(QBrush(c.breakpoint));
            item->setBackground(QBrush(tint));
        }
        m_table->setItem(at, k, item);
    }
    //What the row actions read: the moment, the command, a block's bytes and direction (a download, a sector read are
    //the card's bytes to the host, not an upload)
    QTableWidgetItem * first = m_table->item(at, ColFrame);
    const bool block = r.kind == "block";
    first->setData(RoleFrame, (qlonglong)r.frame);
    first->setData(RoleT, (qlonglong)r.t);
    first->setData(RoleCommand, r.command);
    first->setData(RoleBytes, (qlonglong)(block ? r.bytes : -1));
    first->setData(RoleUpload, block && r.direction != "read" && r.text.find("download") == std::string::npos);
    first->setData(RoleHostData, r.host_data);
    if (anomaly) {
        m_anomalies++;
        m_last_anomaly = r.text;
        m_last_anomaly_frame = r.frame;
    }
}

void GsMonitorView::show_strip(const Emulator::GsMonitor &m)
{
    QString first = QString::fromStdString(m.protocol_text);
    if (first.isEmpty()) first = m.protocol_on ? tr("idle") : tr("the command log is off: no protocol state");
    QStringList second;
    if (m.status >= 0) {
        QStringList bits;
        if (m.command_pending) bits << tr("cmd");
        if (m.data_pending) bits << tr("data");
        second << tr("status %1 (%2)").arg(hex2(m.status), bits.isEmpty() ? tr("clear") : bits.join(", "));
    }
    if (m.command_from_host >= 0)
        second << tr("mailbox: command %1, data %2, to host %3")
                      .arg(hex2(m.command_from_host), hex2(m.data_from_host), hex2(m.data_to_host));
    if (m.has_last_send) {
        QString send = tr("last send %1").arg(m.last_send_command >= 0 ? hex2(m.last_send_command) : QString("?"));
        if (!m.last_send_replies.empty()) {
            QStringList replies;
            for (int r : m.last_send_replies) replies << hex2(r);
            send += " -> " + replies.join(" ");
        }
        if (!m.last_send_state.empty()) send += " (" + QString::fromStdString(m.last_send_state) + ")";
        second << send;
    }
    m_strip->setText(first + (second.isEmpty() ? QString() : "\n" + second.join(kDot)));
}

void GsMonitorView::show_tile()
{
    const bool errors = m_anomalies > 0 || m_unknown_seen > 0;
    QString text;
    if (!errors) {
        text = m_unknown_present ? tr("No anomalies and no unknown commands since Clear") : tr("No anomalies since Clear");
    } else {
        text = plural(m_anomalies, "anomaly", "anomalies");
        if (m_unknown_present) text += kDot + plural(m_unknown_seen, "unknown command", "unknown commands");
        text += tr(" since Clear");
        if (!m_last_anomaly.empty())
            text += "\n" + tr("last: %1 (frame %2)").arg(QString::fromStdString(m_last_anomaly)).arg((qlonglong)m_last_anomaly_frame);
    }
    if (m_unseen > 0)
        text += "\n" + tr("%1 missed: gone from the history before the tab read them").arg(plural(m_unseen, "record", "records"));
    m_tile->setText(text);
    QPalette p = palette();
    if (errors) {
        QColor tint = dbg::colors(palette()).breakpoint;
        tint.setAlpha(60);
        p.setColor(QPalette::Window, tint);
    }
    m_tile_box->setPalette(p);
    m_tile_box->setProperty("errors", errors);
}

void GsMonitorView::turn_on()
{
    if (AddressRouter::read_only_at(this)) {
        m_answer->setText(tr("The machine is not connected: nothing changed"));
        m_answer->show();
        return;
    }
    const Emulator::BoardAnswer a = e->gs_command_log(m_card, true);
    m_answer->setText(QString::fromStdString(a.ok ? a.message : "refused: " + a.message));
    m_answer->show();
    refresh();
}

void GsMonitorView::refresh()
{
    update_machine_button();
    if (m_card.empty() || !e->loaded) return;
    //The records after the newest shown, by the history's cursor; the first read takes the log's counts, the tab then
    //fills with its newest kReadRows
    Emulator::GsMonitor m = m_have_cursor ? e->read_gs_monitor(m_card, kReadAfter, m_since)
                                          : e->read_gs_monitor(m_card, 1, std::nullopt);
    const bool filling = !m_have_cursor && m.ok && !m.log_device.empty() && m.log_mode != "off";
    if (filling) {
        //the newest record's number: a ring numbers its newest `written`, a capture keeps its first `kept`
        const uint64_t newest = m.log_mode == "capture" ? m.kept : m.written;
        m_since = newest > kReadRows ? newest - kReadRows : 0;
        m_have_cursor = true;
        m = e->read_gs_monitor(m_card, kReadRows, m_since);
    }
    if (!m.ok) {
        m_strip->setText(QString::fromStdString(m.message));
        return;
    }
    show_strip(m);
    m_ttd_units = std::max(1u, m.ttd_units);

    const bool off = m.log_device.empty() || m.log_mode == "off";
    if (m.log_device.empty()) {
        m_off_text->setText(tr("This card keeps no command log"));
        m_turn_on->hide();
    } else if (off) {
        m_off_text->setText(tr("The command log is off: the host's commands are not recorded. Turn on records them "
                               "(history %1, mode ring)").arg(QString::fromStdString(m.log_device)));
        m_turn_on->show();
    }
    m_off->setVisible(off);

    //The records after the cursor, oldest first: a burst over kReadAfter goes on at the next read
    if (m.cursor) {
        if (m.restarted) {
            m_table->setRowCount(0);  //the history started afresh (cleared, configured again)
            m_unseen = 0;
        }
        if (!filling) m_unseen += m.missed;  //the first fill starts where it may
        m_since = m.next_since;
    }
    if (m.cursor && !m.rows.empty()) {
        m_table->setUpdatesEnabled(false);
        for (const Emulator::GsMonitorRow &row : m.rows) append(row);
        const int over = m_table->rowCount() - kMaxRows;
        for (int i = 0; i < over; i++) m_table->removeRow(0);
        m_table->resizeColumnsToContents();
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->setUpdatesEnabled(true);
    }

    m_unknown_present = m.unknown_present;
    if (m.unknown_total < m_unknown_base) m_unknown_base = 0;  //the log cleared: its counts start again
    m_unknown_total = m.unknown_total;
    m_unknown_seen = m_unknown_total - m_unknown_base;
    show_tile();

    QString line;
    if (!m.log_device.empty())
        line = QString::fromStdString(m.log_mode.empty() ? std::string("?") : m.log_mode) + kDot +
               tr("%1 kept, %2 written").arg(grouped(m.kept), grouped(m.written)) + kDot +
               tr("%1 shown").arg(grouped((uint64_t)m_table->rowCount()));
    line += (line.isEmpty() ? QString() : kDot) + (m_following ? tr("following") : tr("scrolled up: Follow resumes"));
    m_log_line->setText(line);
    if (m_following) follow();
}

QMenu * GsMonitorView::row_menu(int row, QWidget * parent)
{
    QMenu * menu = new QMenu(parent);
    menu->setToolTipsVisible(true);
    const QTableWidgetItem * first = row >= 0 && row < m_table->rowCount() ? m_table->item(row, ColFrame) : nullptr;
    if (first == nullptr) return menu;
    const QString kind = m_table->item(row, ColKind)->text();
    const int command = first->data(RoleCommand).toInt();
    const qlonglong frame = first->data(RoleFrame).toLongLong(), t = first->data(RoleT).toLongLong();
    const qlonglong bytes = first->data(RoleBytes).toLongLong();
    const bool upload = first->data(RoleUpload).toBool() && bytes > 0;
    //A command the row belongs to: the command itself, its parameters and replies, the card taking it, its block
    const bool is_command = command >= 0 && !first->data(RoleHostData).toBool() &&
                            (kind == "command" || kind == "param" || kind == "reply" || kind == "card_took" || kind == "block");
    const QString code = is_command ? hex2(command) : QString();
    const QString not_command = tr("This row is not a command's (a command, its parameters, replies or block)");
    const bool live = !AddressRouter::read_only_at(this) && e->loaded;

    //The card's CPU (none on the lightweight player) and the recorded history, read now
    CPU * card = cpu_named(e, m_card);
    std::vector<std::string> kinds = card ? card->event_kinds() : std::vector<std::string>();
    const bool card_side = card != nullptr && std::find(kinds.begin(), kinds.end(), "card_command") != kinds.end();
    bool history = false;
    QString history_why = tr("No time-travel recording: record one (TTD) to go back to a row's moment");
    if (e->ttd_supported() && e->debug_online()) {
        const Emulator::TtdAnswer a = e->ttd_call("position", {});
        const QJsonObject p = QJsonDocument::fromJson(QByteArray::fromStdString(a.json)).object();
        if (a.ok && p.value("history").toBool()) {
            const qlonglong first_frame = p.value("first_frame").toInteger();
            const QJsonObject end = p.value("session_end").toObject();
            const qlonglong end_frame = end.value("frame").toInteger(), end_t = end.value("tinframe").toInteger();
            history = frame >= first_frame && (frame < end_frame || (frame == end_frame && t * m_ttd_units <= end_t));
            history_why = tr("The recording holds frames %1 to %2: this row's moment is not in it").arg(first_frame).arg(end_frame);
        }
    }

    auto add = [&](RowAction action, const QString &text, const char * name, bool enabled, const QString &tip) {
        QAction * a = menu->addAction(text);
        a->setObjectName(name);
        a->setEnabled(enabled);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, action, row, bytes]() {
            unsigned int byte = 0;
            if (action == RowAction::BreakUploadByte) {
                const int max = (int)std::clamp<qlonglong>(bytes, 1, 0x7FFFFFFF);
                int n = 1;
                bool ok = false;
                if (ask_byte)
                    ok = ask_byte(max, n);
                else
                    n = QInputDialog::getInt(this, tr("Break on a data byte of the upload"),
                                             tr("The data byte to stop on (1 to %1), counted from now:").arg(max), 1, 1, max, 1, &ok);
                if (!ok) return;
                byte = (unsigned int)n;
            }
            run_row_action(action, row, byte);
        });
        return a;
    };
    const QString offline = tr("The machine is not connected");
    add(RowAction::BreakHostSends, is_command ? tr("Break when the game sends %1").arg(code) : tr("Break when the game sends this command"),
        "gsLogBreakHost", is_command && live,
        !is_command ? not_command
        : !live     ? offline
                    : tr("A protocol breakpoint on the main CPU: the host writes command %1 (OUT #BB), host_command = %1").arg(code));
    add(RowAction::BreakCardTakes,
        is_command ? tr("Break when the firmware takes %1").arg(code) : tr("Break when the firmware takes this command"),
        "gsLogBreakCard", is_command && live && card_side,
        !is_command  ? not_command
        : !live      ? offline
        : !card_side ? tr("This card runs no firmware CPU (the lightweight player): there is no card side to break on")
                     : tr("A protocol breakpoint on the card's CPU (%1): the firmware reads command %2 (port #01), card_command = %2")
                           .arg(QString::fromStdString(m_card), code));
    add(RowAction::BreakUploadByte, tr("Break on data byte N of this upload..."), "gsLogBreakByte", upload && live,
        !upload ? tr("Only on an upload's row (a block the host wrote)")
        : !live ? offline
                : tr("A protocol breakpoint on the main CPU: host_data, stopping at the Nth data byte (OUT #B3) the host "
                     "writes from now on (hit policy N). Set it before the upload starts: go to the upload's command first "
                     "when the recording holds it, or before the game uploads again"));
    add(RowAction::GoTo, tr("Go to this moment"), "gsLogGoTo", history && live,
        !live ? offline : history ? tr("Time travel to frame %1, T %2 (the recording's seek)").arg(frame).arg(t) : history_why);
    QAction * copy = add(RowAction::Copy, tr("Copy"), "gsLogCopy", true, tr("The selected rows' text, tab-separated"));
    copy->setShortcut(QKeySequence::Copy);
    return menu;
}

QString GsMonitorView::copy_text(int row) const
{
    std::vector<int> rows;
    const QModelIndexList selected = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    bool in_selection = false;
    for (const QModelIndex &i : selected) in_selection = in_selection || i.row() == row;
    if (in_selection)
        for (const QModelIndex &i : selected) rows.push_back(i.row());
    else if (row >= 0 && row < m_table->rowCount())
        rows.push_back(row);
    std::sort(rows.begin(), rows.end());
    QStringList lines;
    for (int r : rows) {
        QStringList cells;
        for (int k = 0; k < ColCount; k++) cells << (m_table->item(r, k) ? m_table->item(r, k)->text() : QString());
        lines << cells.join('\t');
    }
    return lines.join('\n');
}

bool GsMonitorView::run_row_action(RowAction action, int row, unsigned int byte, QString * message)
{
    const QTableWidgetItem * first = row >= 0 && row < m_table->rowCount() ? m_table->item(row, ColFrame) : nullptr;
    QString said;
    bool ok = false;
    auto done = [&]() {
        m_action->setText(said);
        m_action->setVisible(!said.isEmpty());
        if (message) *message = said;
        return ok;
    };
    if (first == nullptr) return done();
    const int command = first->data(RoleCommand).toInt();
    const qlonglong frame = first->data(RoleFrame).toLongLong(), t = first->data(RoleT).toLongLong();
    const qlonglong bytes = first->data(RoleBytes).toLongLong();
    if (action == RowAction::Copy) {
        const QString text = copy_text(row);
        QApplication::clipboard()->setText(text);
        said = tr("Copied %n rows", nullptr, (int)text.count('\n') + 1);
        ok = !text.isEmpty();
        return done();
    }
    if (AddressRouter::read_only_at(this) || !e->loaded) {
        said = tr("The machine is not connected: nothing changed");
        return done();
    }
    auto event = [&](CPU * c, const char * cpu_name, const std::string &kind, unsigned int value, unsigned int mask,
                     const std::string &hits, const QString &words) {
        if (c == nullptr) {
            said = tr("Refused: no %1 CPU").arg(cpu_name);
            return false;
        }
        unsigned int id = 0;
        bool existed = false;
        std::string error;
        if (!event_breakpoint(e, c, kind, value, mask, hits, id, existed, error)) {
            said = tr("Refused: %1").arg(QString::fromStdString(error));
            return false;
        }
        said = (existed ? tr("Already there: breakpoint #%1 on %2: %3") : tr("Breakpoint #%1 on %2: %3")).arg(id).arg(cpu_name, words);
        emit breakpoints_changed();
        return true;
    };
    switch (action) {
    case RowAction::BreakHostSends:
        if (command >= 0)
            ok = event(cpu_named(e, "cpu"), "main", "host_command", (unsigned int)command, 0xFF, std::string(),
                       tr("the host sends command %1").arg(hex2(command)));
        break;
    case RowAction::BreakCardTakes:
        if (command >= 0)
            ok = event(cpu_named(e, m_card), m_card.c_str(), "card_command", (unsigned int)command, 0xFF, std::string(),
                       tr("the card takes command %1").arg(hex2(command)));
        break;
    case RowAction::BreakUploadByte:
        if (!first->data(RoleUpload).toBool() || byte == 0 || (bytes > 0 && byte > (qulonglong)bytes)) {
            said = tr("Not an upload's data byte");
            break;
        }
        ok = event(cpu_named(e, "cpu"), "main", "host_data", 0, 0, std::to_string(byte),
                   tr("the host sends a data byte, hit %1 from now").arg(byte));
        break;
    case RowAction::GoTo: {
        if (!e->ttd_supported()) {
            said = tr("No time-travel recording");
            break;
        }
        //The seek's answer: at once in process, later from a remote machine (the line says it is under way till then)
        QPointer<GsMonitorView> self(this);
        auto returned = std::make_shared<bool>(false);
        auto result = std::make_shared<std::pair<bool, QString>>(false, tr("Seeking frame %1, T %2...").arg(frame).arg(t));
        e->ttd_call_async("seek", {{"frame", std::to_string(frame)}, {"tinframe", std::to_string(t * m_ttd_units)}},
                          [self, returned, result, frame, t](const Emulator::TtdAnswer &a) {
                              const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(a.json)).object();
                              bool reached = false;
                              QString line;
                              if (!a.ok)
                                  line = o.value("message").toString(tr("The emulator did not answer."));
                              else if (o.contains("reached") && !o.value("reached").toBool())
                                  line = o.value("message").toString(tr("The seek did not reach the row's moment"));
                              else {
                                  line = tr("At frame %1, T %2").arg(frame).arg(t);
                                  reached = true;
                              }
                              *result = {reached, line};
                              if (!*returned || self.isNull()) return;
                              self->m_action->setText(line);
                              self->m_action->setVisible(true);
                              if (reached) emit self->moved();
                          });
        *returned = true;
        said = result->second;
        ok = result->first;
        if (ok) emit moved();
        break;
    }
    case RowAction::Copy:
        break;
    }
    return done();
}
