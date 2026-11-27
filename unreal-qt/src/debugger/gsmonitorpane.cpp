#include "gsmonitorpane.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpointstopline.h"
#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/history/historycontrol.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/state/devicestate.h"

namespace
{
// An anomaly's row and the tile with a count: a soft red over the palette (the report panes' marks are blue)
const QColor kAnomaly(220, 60, 60, 70);
const char* const kTileTinted = "QWidget#gsMonitorTile { background-color: rgba(220, 60, 60, 60); border: 1px solid rgb(190, 60, 60); "
                                "border-radius: 3px; }";
const char* const kTilePlain = "QWidget#gsMonitorTile { border: 1px solid palette(mid); border-radius: 3px; }";

const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key, int64_t none = 0)
{
    const StateNode* v = Member(node, key);
    if (v && v->kind == StateNode::Kind::Int)
        return v->i;
    if (v && v->kind == StateNode::Kind::Bool)
        return v->b ? 1 : 0;
    return none;
}

bool Flag(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::Bool && v->b;
}

QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

QString Hex2(int64_t value)
{
    return QLatin1Char('#') + QString::number(static_cast<qulonglong>(value & 0xFF), 16).toUpper().rightJustified(2, QLatin1Char('0'));
}

/// The slot of the card carrying a General Sound (the ZX-MultiSound); empty when none
std::string MultiSoundSlot(EmulatorContext* context)
{
    if (!context || !context->pSlotManager)
        return std::string();
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
        if (card && card->DebugGeneralSound())
            return card->SlotId();
    return std::string();
}

bool IsAnomaly(const StateNode& record)
{
    return Str(record, "kind") == QLatin1String("anomaly");
}

// A row's record, kept on its first cell
enum RowRole
{
    RoleFrame = Qt::UserRole,
    RoleT,
    RoleCommand,
    RoleBytes,
    RoleUpload,
    RoleHostData,
};

/// The card CPU of the card whose log the pane shows
DebugCpuId CardCpuOf(CardPanes::Target target)
{
    return target == CardPanes::Target::MultiSound ? DebugCpuId::MultiSoundGS : DebugCpuId::GS;
}

/// The card's CPU on `context` with event breakpoints, else why not
bool CardCpuThere(EmulatorContext* context, DebugCpuId cpu, QString* why)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    GSCardTarget* target = debug ? debug->GetCardTarget(cpu) : nullptr;
    if (target && target->Available() && target->GetBreakpointManager())
        return true;
    if (why)
        *why = QObject::tr("This card runs no firmware CPU (the lightweight player): there is no card side to break on");
    return false;
}

/// An event breakpoint on `cpu`: the user's equal one when there is one (enabled again, a hit policy's count reset to
/// count from now), else a new one through the manager's one entry. BRK_INVALID with the reason in `error`
uint16_t EventBreakpoint(EmulatorContext* context, DebugCpuId cpu, GSCardEvent kind, uint8_t value, uint8_t mask,
                         BreakpointHitModeEnum hitMode, uint32_t hitTarget, bool& existed, std::string& error)
{
    existed = false;
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    BreakpointManager* manager = debug ? debug->GetBreakpointsManager(cpu) : nullptr;
    if (!manager)
    {
        error = "this CPU has no breakpoints now";
        return BRK_INVALID;
    }
    for (const auto& [id, d] : manager->GetAllBreakpoints())
    {
        if (d->type != BRK_EVENT || !BreakpointManager::IsUserBreakpoint(*d) || d->z80address != static_cast<uint16_t>(kind) ||
            d->nrMask != mask || (d->nrValue & mask) != (value & mask) || d->hitMode != hitMode ||
            (hitMode != BRK_HIT_ALWAYS && d->hitTarget != hitTarget) || d->tapSink != BRK_NO_TAP || d->condition)
            continue;
        existed = true;
        if (!d->active)
            manager->ActivateBreakpoint(id);
        if (hitMode != BRK_HIT_ALWAYS)
            manager->ResetHitCount(id);
        return id;
    }
    BreakpointSpec spec;
    spec.type = BRK_EVENT;
    spec.access = 0;
    spec.address = static_cast<uint16_t>(kind);
    spec.nrValue = value;
    spec.nrMask = mask;
    spec.hitMode = hitMode;
    spec.hitTarget = hitTarget;
    spec.note = "GS monitor";
    return manager->AddBreakpoint(spec, error);
}
}  // namespace

GsMonitorPane::GsMonitorPane(std::shared_ptr<DebugCpuId> follow, QWidget* parent) : QWidget(parent), _follow(std::move(follow))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setVisible(false);
    layout->addWidget(_status);

    _body = new QWidget(this);
    auto* body = new QVBoxLayout(_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    layout->addWidget(_body, 1);

    // 1. The protocol strip
    _strip = new QLabel(_body);
    _strip->setObjectName("gsMonitorStrip");
    _strip->setWordWrap(true);
    _strip->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _strip->setStyleSheet("QLabel { border-left: 4px solid #E0A020; padding: 2px 6px; }");
    auto* stripRow = new QHBoxLayout();
    stripRow->setContentsMargins(0, 0, 0, 0);
    stripRow->addWidget(_strip, 1);
    _sendButton = new QToolButton(_body);
    _sendButton->setText(tr("Send..."));
    _sendButton->setToolTip(tr("The GS send tab: send the card a command as the host would (Ctrl+Shift+S)"));
    connect(_sendButton, &QToolButton::clicked, this, &GsMonitorPane::sendConsoleRequested);
    stripRow->addWidget(_sendButton, 0, Qt::AlignTop);
    body->addLayout(stripRow);

    // The command log off: say so, offer Turn on
    _offBar = new QWidget(_body);
    auto* off = new QHBoxLayout(_offBar);
    off->setContentsMargins(0, 0, 0, 0);
    _offText = new QLabel(_offBar);
    _offText->setWordWrap(true);
    off->addWidget(_offText, 1);
    _turnOn = new QPushButton(tr("Turn on"), _offBar);
    _turnOn->setToolTip(tr("Record the card's command log: history <slot>.commands mode ring (costs the emulation a little "
                           "on each host access to the card)"));
    connect(_turnOn, &QPushButton::clicked, this, &GsMonitorPane::turnOnRequested);
    off->addWidget(_turnOn);
    _offBar->setVisible(false);
    body->addWidget(_offBar);

    // 2. The error tile
    _tile = new QWidget(_body);
    _tile->setObjectName("gsMonitorTile");
    _tile->setAttribute(Qt::WA_StyledBackground, true);
    _tile->setStyleSheet(kTilePlain);
    auto* tile = new QHBoxLayout(_tile);
    tile->setContentsMargins(6, 2, 2, 2);
    _tileText = new QLabel(_tile);
    _tileText->setWordWrap(true);
    _tileText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tile->addWidget(_tileText, 1);
    _clear = new QToolButton(_tile);
    _clear->setText(tr("Clear"));
    _clear->setToolTip(tr("Count the anomalies and unknown commands from now on (the log and the history stay)"));
    connect(_clear, &QToolButton::clicked, this, &GsMonitorPane::clearCounters);
    tile->addWidget(_clear);
    body->addWidget(_tile);

    // 3. The log
    _table = new QTableWidget(0, 6, _body);
    _table->setHorizontalHeaderLabels({tr("Frame"), tr("T"), tr("Kind"), tr("Cmd"), tr("Name"), tr("Text")});
    _table->verticalHeader()->setVisible(false);
    _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setWordWrap(false);
    _table->setColumnWidth(0, 56);
    _table->setColumnWidth(1, 56);
    _table->setColumnWidth(2, 70);
    _table->setColumnWidth(3, 36);
    _table->setColumnWidth(4, 110);
    connect(_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (_scrolling)
            return;
        // The user scrolled: up stops following the tail, back to the end resumes it
        const bool atEnd = value >= _table->verticalScrollBar()->maximum();
        if (atEnd != _following)
            setFollowing(atEnd);
    });
    // The row actions (A5.4): a context menu on a row; Ctrl+C copies the selected rows
    _table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const int row = _table->rowAt(at.y());
        if (row < 0)
            return;
        QMenu* menu = rowMenu(row, this);
        menu->exec(_table->viewport()->mapToGlobal(at));
        menu->deleteLater();
    });
    auto* copy = new QAction(tr("Copy"), _table);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    connect(copy, &QAction::triggered, this, [this]() {
        if (_table->currentRow() >= 0)
            QApplication::clipboard()->setText(copyText(_table->currentRow()));
    });
    _table->addAction(copy);
    _action = new QLabel(_body);
    _action->setObjectName("gsMonitorAction");
    _action->setWordWrap(true);
    _action->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _action->setVisible(false);
    body->addWidget(_action);
    body->addWidget(_table, 1);

    // 4. Follow, Pause / Continue
    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    _followButton = new QToolButton(_body);
    _followButton->setText(tr("Follow"));
    _followButton->setCheckable(true);
    _followButton->setChecked(true);
    _followButton->setToolTip(tr("Keep the newest record in view (scrolling up stops it)"));
    connect(_followButton, &QToolButton::toggled, this, [this](bool on) { setFollowing(on); });
    bottom->addWidget(_followButton);
    _rows = new QLabel(_body);
    bottom->addWidget(_rows, 1);
    _run = new QPushButton(tr("Pause"), _body);
    _run->setToolTip(tr("Pause or continue the machine (the debugger's Pause / Continue)"));
    connect(_run, &QPushButton::clicked, this, [this]() {
        if (_live)
            emit pauseRequested();
        else
            emit continueRequested();
    });
    bottom->addWidget(_run);
    body->addLayout(bottom);

    showTile();
}

std::string GsMonitorPane::HistoryDevice(EmulatorContext* context, DebugCpuId front)
{
    switch (CardPanes::Front(context, front))
    {
        case CardPanes::Target::Slot: return GSHistoryDevice(context) + ".commands";
        case CardPanes::Target::MultiSound:
        {
            const std::string slot = MultiSoundSlot(context);
            return slot.empty() ? std::string() : slot + ".gs.commands";
        }
        case CardPanes::Target::None: break;
    }
    return std::string();
}

void GsMonitorPane::refresh(EmulatorContext* context, bool live)
{
    _live = live;
    _run->setText(live ? tr("Pause") : tr("Continue"));
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const CardPanes::Target target = CardPanes::Front(context, front);
    if (target == CardPanes::Target::None)
    {
        showUnavailable(tr("No General Sound card fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs | multisound)"));
        return;
    }
    _status->clear();
    _status->setVisible(false);
    _body->setVisible(true);

    // The strip: the `gs` report of the card shown (the MultiSound's: its `gs` member)
    StateNode gs;
    if (target == CardPanes::Target::MultiSound)
    {
        const StateNode ms = DeviceState::MultiSound(context);
        const StateNode* part = ms.find("gs");
        gs = part && part->isObject() ? *part : ms;
    }
    else
        gs = DeviceState::Gs(context);
    showStrip(gs, Member(gs, "protocol"));
    readActionState(context, front);

    // The log: the records after the newest one shown, by the history's cursor (its records' numbers), page by page
    // until caught up: none is lost between two refreshes while the history keeps it
    const std::string device = HistoryDevice(context, front);
    if (device != _device)
    {
        _device = device;  // another card: its own log
        _table->setRowCount(0);
        _hasCursor = false;
        _missed = 0;
    }
    HistoryRequest read;
    read.verb = "read";
    read.device = device;
    read.last = kReadRows;
    if (_hasCursor)
        read.since = _since;
    else
        read.last = 1;  // the first read: the history's state and counts, then the view fills with its newest kReadRows
    HistoryReply reply = HistoryControl::Execute(context, read);
    if (reply.httpStatus != 200)
    {
        _off = true;
        _offText->setText(tr("This card keeps no command log (%1)").arg(QString::fromStdString(device)));
        _turnOn->setVisible(false);
        _offBar->setVisible(true);
        _tile->setVisible(false);
        _table->setVisible(false);
        return;
    }
    _off = Str(reply.body, "mode") == QLatin1String("off");
    _offBar->setVisible(_off);
    _turnOn->setVisible(_off);
    _tile->setVisible(!_off);
    _table->setVisible(!_off);
    if (_off)
    {
        _offText->setText(tr("The command log is off: nothing is decoded (history %1 mode off)").arg(QString::fromStdString(device)));
        _rows->clear();
        return;
    }
    const bool filling = !_hasCursor;
    if (filling)
    {
        // The newest record's number: a ring numbers its newest `written`, a capture keeps its first `kept`
        const int64_t newest = Str(reply.body, "mode") == QLatin1String("capture") ? Int(reply.body, "kept")
                                                                                     : Int(reply.body, "written");
        _since = newest > static_cast<int64_t>(kReadRows) ? static_cast<uint64_t>(newest) - kReadRows : 0;
        _hasCursor = true;
        read.last = kReadRows;
        read.since = _since;
        reply = HistoryControl::Execute(context, read);
    }
    appendPages(context, read, reply, filling);

    // The commands the firmware's profile does not name (gs-firmware unknown_commands), counted by the log
    GSFirmwareObjectsQuery query;
    query.object = "unknown_commands";
    query.cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    const GSFirmwareObjectsReply objects = GSFirmwareObjectsRead(context, query);
    _unknown.clear();
    if (objects.status == 200)
        if (const StateNode* unknown = Member(objects.body, "unknown_commands"); unknown && unknown->isArray())
            for (const StateNode& entry : unknown->items)
                _unknown[static_cast<int>(Int(entry, "command"))] = Int(entry, "count");
    showTile();
}

void GsMonitorPane::showUnavailable(const QString& why)
{
    _status->setText(why);
    _status->setVisible(true);
    _body->setVisible(false);
}

void GsMonitorPane::showStrip(const StateNode& gs, const StateNode* protocol)
{
    if (!Flag(gs, "available"))
    {
        const QString why = Str(gs, "description");
        _strip->setText(why.isEmpty() ? tr("The card's report is not available") : why);
        return;
    }
    QString first;
    if (protocol && Flag(*protocol, "on"))
        first = Str(*protocol, "text");
    else
        first = tr("protocol: the command log is off");
    const int64_t status = Int(gs, "status");
    QStringList pending;
    if (Flag(gs, "command_pending"))
        pending << tr("cmd");
    if (Flag(gs, "data_pending"))
        pending << tr("data");
    QString second = tr("status %1").arg(Hex2(status)) +
                     (pending.isEmpty() ? tr(" (idle)") : QStringLiteral(" (") + pending.join(QStringLiteral(", ")) + QLatin1Char(')'));
    second += tr(" · mailbox cmd %1, data in %2, out %3")
                  .arg(Hex2(Int(gs, "command_from_host")), Hex2(Int(gs, "data_from_host")), Hex2(Int(gs, "data_to_host")));
    if (const StateNode* send = Member(gs, "last_send"); send && send->isObject())
        second += tr(" · last send ") + Str(*send, "text");
    _strip->setText(first + QLatin1Char('\n') + second);
}

void GsMonitorPane::appendPages(EmulatorContext* context, HistoryRequest& read, HistoryReply reply, bool filling)
{
    _scrolling = true;
    _table->setUpdatesEnabled(false);
    size_t appended = 0;
    // A burst beyond kPagesPerRefresh pages waits for the next refresh: the cursor keeps its place
    for (int page = 0; reply.httpStatus == 200; page++)
    {
        const StateNode& body = reply.body;
        if (Flag(body, "restarted"))
        {
            _table->setRowCount(0);  // the history started afresh (cleared, configured again)
            _missed = 0;
        }
        if (!filling || page > 0)
            _missed += Int(body, "missed");  // evicted before they were read (the first fill starts where it may)
        _since = static_cast<uint64_t>(Int(body, "next_since", static_cast<int64_t>(_since)));
        const StateNode* records = Member(body, "records");
        const size_t count = records && records->isArray() ? records->items.size() : 0;
        for (size_t i = 0; i < count; i++)
            appendRow(records->items[i]);
        appended += count;
        if (count < kReadRows || page + 1 >= kPagesPerRefresh)
            break;
        read.since = _since;
        reply = HistoryControl::Execute(context, read);
    }
    const int extra = _table->rowCount() - kMaxRows;
    for (int i = 0; i < extra; i++)
        _table->removeRow(0);
    _table->setUpdatesEnabled(true);
    _scrolling = false;
    QString rows = tr("%1 rows").arg(_table->rowCount());
    if (_missed > 0)
        rows += tr(" · %1 missed (gone from the history before they were read)").arg(_missed);
    _rows->setText(rows);
    if (_following && appended > 0)
        scrollToTail();
}

void GsMonitorPane::appendRow(const StateNode& record)
{
    const int row = _table->rowCount();
    _table->insertRow(row);
    const bool anomaly = IsAnomaly(record);
    const QString kind = Str(record, "kind");
    const int64_t code = Int(record, "command", -1);
    const QString command = code >= 0 ? Hex2(code) : QString();
    const QString cells[] = {QString::number(Int(record, "frame")), QString::number(Int(record, "t")), kind, command,
                             Str(record, "name"), Str(record, "text")};
    for (int column = 0; column < 6; column++)
    {
        auto* item = new QTableWidgetItem(cells[column]);
        if (anomaly)
            item->setBackground(kAnomaly);
        _table->setItem(row, column, item);
    }
    // What the row actions read: the moment, the command, a block's bytes and direction (a download, a sector read
    // are the card's bytes to the host, not an upload)
    QTableWidgetItem* first = _table->item(row, 0);
    first->setData(RoleFrame, static_cast<qlonglong>(Int(record, "frame")));
    first->setData(RoleT, static_cast<qlonglong>(Int(record, "t")));
    first->setData(RoleCommand, static_cast<int>(code));
    const bool block = kind == QLatin1String("block");
    first->setData(RoleBytes, static_cast<qlonglong>(block ? Int(record, "bytes", -1) : -1));
    first->setData(RoleUpload, block && Str(record, "direction") != QLatin1String("read") &&
                                   !Str(record, "text").contains(QLatin1String("download")));
    first->setData(RoleHostData, Flag(record, "host_data"));
    if (anomaly)
    {
        _anomalies++;
        _lastAnomaly = tr("%1 (frame %2)").arg(Str(record, "text")).arg(Int(record, "frame"));
    }
}

int64_t GsMonitorPane::unknownCommands() const
{
    int64_t total = 0;
    for (const auto& [code, count] : _unknown)
    {
        const auto base = _unknownAtClear.find(code);
        total += std::max<int64_t>(0, count - (base == _unknownAtClear.end() ? 0 : base->second));
    }
    return total;
}

void GsMonitorPane::showTile()
{
    const int64_t unknown = unknownCommands();
    QString text = tr("%n anomalies", nullptr, _anomalies) + QStringLiteral(" · ") + tr("%n unknown commands", nullptr, static_cast<int>(unknown));
    if (unknown)
    {
        QStringList codes;
        for (const auto& [code, count] : _unknown)
        {
            const auto base = _unknownAtClear.find(code);
            const int64_t since = count - (base == _unknownAtClear.end() ? 0 : base->second);
            if (since > 0)
                codes << QStringLiteral("%1 ×%2").arg(Hex2(code)).arg(since);
        }
        text += QStringLiteral(" (") + codes.join(QStringLiteral(", ")) + QLatin1Char(')');
    }
    if (_anomalies)
        text += QLatin1Char('\n') + tr("last: ") + _lastAnomaly;
    _tileText->setText(text);
    const bool tinted = _anomalies > 0 || unknown > 0;
    if (tinted != _tinted || _tile->styleSheet().isEmpty())
        _tile->setStyleSheet(tinted ? kTileTinted : kTilePlain);
    _tinted = tinted;
}

void GsMonitorPane::clearCounters()
{
    _anomalies = 0;
    _lastAnomaly.clear();
    _unknownAtClear = _unknown;
    showTile();
}

void GsMonitorPane::setFollowing(bool on)
{
    _following = on;
    if (_followButton->isChecked() != on)
    {
        const QSignalBlocker block(_followButton);
        _followButton->setChecked(on);
    }
    if (on)
        scrollToTail();
}

void GsMonitorPane::scrollToTail()
{
    _scrolling = true;
    _table->scrollToBottom();
    QScrollBar* bar = _table->verticalScrollBar();
    bar->setValue(bar->maximum());
    _scrolling = false;
}

bool GsMonitorPane::turnOn(EmulatorContext* context, QString* error)
{
    const std::string device = HistoryDevice(context, _follow ? *_follow : DebugCpuId::Main);
    if (device.empty())
    {
        if (error)
            *error = tr("No General Sound card fitted");
        return false;
    }
    HistoryRequest ring;
    ring.verb = "config";
    ring.device = device;
    ring.mode = "ring";
    const HistoryReply reply = HistoryControl::Execute(context, ring);
    if (reply.httpStatus != 200)
    {
        if (error)
            *error = Str(reply.body, "error");
        return false;
    }
    return true;
}

QString GsMonitorPane::rowsText() const
{
    return _rows->text();
}

QString GsMonitorPane::status() const
{
    return _status->text();
}

QString GsMonitorPane::stripText() const
{
    return _strip->text();
}

QString GsMonitorPane::tileText() const
{
    return _tileText->text();
}

/// region <The log row's actions (A5.4)>

void GsMonitorPane::readActionState(EmulatorContext* context, DebugCpuId front)
{
    const CardPanes::Target target = CardPanes::Front(context, front);
    _cardCpu = CardCpuOf(target);
    _cardWhy.clear();
    _cardCpuThere = target != CardPanes::Target::None && CardCpuThere(context, _cardCpu, &_cardWhy);

    // The recorded history: its position read (published, never parks the machine)
    _history = false;
    _historyWhy = tr("No time-travel recording: record one (TTD) to go back to a row's moment");
    if (!ttd::HasTimeTravelSession(context))
        return;
    const ttd::TTDReply position = ttd::TTDControl(context).Execute({"position", {}});
    if (!position.Ok() || !Flag(position.body, "history"))
        return;
    _history = true;
    _ttdUnits = std::max<uint32_t>(1, context->emulatorState.TtdTInFrame(1));
    _historyFirst = static_cast<uint64_t>(Int(position.body, "first_frame"));
    if (const StateNode* end = Member(position.body, "session_end"))
    {
        _historyEndFrame = static_cast<uint64_t>(Int(*end, "frame"));
        _historyEndT = static_cast<uint64_t>(Int(*end, "tinframe"));
    }
    _historyWhy = tr("The recording holds frames %1 to %2: this row's moment is not in it").arg(_historyFirst).arg(_historyEndFrame);
}

bool GsMonitorPane::recorded(const LogRow& row) const
{
    if (!_history || row.frame < 0)
        return false;
    const uint64_t frame = static_cast<uint64_t>(row.frame);
    return frame >= _historyFirst &&
           (frame < _historyEndFrame || (frame == _historyEndFrame && static_cast<uint64_t>(row.t) * _ttdUnits <= _historyEndT));
}

GsMonitorPane::LogRow GsMonitorPane::logRow(int row) const
{
    LogRow out;
    const QTableWidgetItem* first = row >= 0 && row < _table->rowCount() ? _table->item(row, 0) : nullptr;
    if (!first)
        return out;
    out.kind = _table->item(row, 2)->text();
    out.frame = first->data(RoleFrame).toLongLong();
    out.t = first->data(RoleT).toLongLong();
    out.command = first->data(RoleCommand).toInt();
    out.bytes = first->data(RoleBytes).toLongLong();
    out.upload = first->data(RoleUpload).toBool();
    out.hostData = first->data(RoleHostData).toBool();
    return out;
}

QMenu* GsMonitorPane::rowMenu(int row, QWidget* parent)
{
    auto* menu = new QMenu(parent);
    const LogRow r = logRow(row);
    // A command the row belongs to: the command itself, its parameters and replies, the card taking it, its block
    const bool command = r.command >= 0 && !r.hostData &&
                         (r.kind == QLatin1String("command") || r.kind == QLatin1String("param") ||
                          r.kind == QLatin1String("reply") || r.kind == QLatin1String("card_took") ||
                          r.kind == QLatin1String("block"));
    const QString code = command ? Hex2(r.command) : QString();
    const QString notCommand = tr("This row is not a command's (a command, its parameters, replies or block)");
    const auto add = [&](RowAction action, const QString& text, const char* name, bool enabled, const QString& tip) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        a->setEnabled(enabled);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, action, row, r]() {
            if (action == RowAction::Copy)
            {
                runRowAction(nullptr, RowAction::Copy, row);
                return;
            }
            uint32_t byte = 0;
            if (action == RowAction::BreakUploadByte)
            {
                const int max = static_cast<int>(std::clamp<int64_t>(r.bytes, 1, 0x7FFFFFFF));
                int n = 1;
                bool ok = false;
                if (askByte)
                    ok = askByte(max, n);
                else
                    n = QInputDialog::getInt(this, tr("Break on a data byte of the upload"),
                                             tr("The data byte to stop on (1 to %1), counted from now:").arg(max), 1, 1, max, 1, &ok);
                if (!ok)
                    return;
                byte = static_cast<uint32_t>(n);
            }
            emit rowActionRequested(static_cast<int>(action), row, byte);
        });
        return a;
    };
    menu->setToolTipsVisible(true);
    add(RowAction::BreakHostSends, command ? tr("Break when the game sends %1").arg(code) : tr("Break when the game sends this command"),
        "gsLogBreakHost", command,
        command ? tr("A protocol breakpoint on the main CPU: the host writes command %1 (OUT #BB), host_command = %1").arg(code)
                : notCommand);
    add(RowAction::BreakCardTakes,
        command ? tr("Break when the firmware takes %1").arg(code) : tr("Break when the firmware takes this command"),
        "gsLogBreakCard", command && _cardCpuThere,
        !command          ? notCommand
        : !_cardCpuThere ? _cardWhy
                          : tr("A protocol breakpoint on the card's CPU (%1): the firmware reads command %2 (port #01), "
                               "card_command = %2")
                                .arg(QString::fromLatin1(DebugCpuName(_cardCpu)), code));
    add(RowAction::BreakUploadByte, tr("Break on data byte N of this upload..."), "gsLogBreakByte", r.upload && r.bytes > 0,
        r.upload && r.bytes > 0
            ? tr("A protocol breakpoint on the main CPU: host_data, stopping at the Nth data byte (OUT #B3) the host writes "
                 "from now on (hit policy N). Set it before the upload starts: go to the upload's command first when the "
                 "recording holds it, or before the game uploads again")
            : tr("Only on an upload's row (a block the host wrote)"));
    add(RowAction::GoTo, tr("Go to this moment"), "gsLogGoTo", recorded(r),
        recorded(r) ? tr("Time travel to frame %1, T %2 (the recording's seek)").arg(r.frame).arg(r.t) : _historyWhy);
    QAction* copy = add(RowAction::Copy, tr("Copy"), "gsLogCopy", true, tr("The selected rows' text, tab-separated"));
    copy->setShortcut(QKeySequence::Copy);
    return menu;
}

QString GsMonitorPane::copyText(int row) const
{
    std::vector<int> rows;
    const QModelIndexList selected = _table->selectionModel() ? _table->selectionModel()->selectedRows() : QModelIndexList();
    bool inSelection = false;
    for (const QModelIndex& index : selected)
        inSelection = inSelection || index.row() == row;
    if (inSelection)
        for (const QModelIndex& index : selected)
            rows.push_back(index.row());
    else if (row >= 0 && row < _table->rowCount())
        rows.push_back(row);
    std::sort(rows.begin(), rows.end());
    QStringList lines;
    for (const int r : rows)
    {
        QStringList cells;
        for (int column = 0; column < _table->columnCount(); column++)
            cells << (_table->item(r, column) ? _table->item(r, column)->text() : QString());
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

bool GsMonitorPane::runRowAction(EmulatorContext* context, RowAction action, int row, uint32_t byte, QString* message)
{
    const LogRow r = logRow(row);
    QString said;
    bool ok = false;
    const auto event = [&](DebugCpuId cpu, GSCardEvent kind, uint8_t value, uint8_t mask, BreakpointHitModeEnum mode,
                           uint32_t target) {
        bool existed = false;
        std::string error;
        const uint16_t id = EventBreakpoint(context, cpu, kind, value, mask, mode, target, existed, error);
        if (id == BRK_INVALID)
        {
            said = tr("Refused: %1").arg(QString::fromStdString(error));
            return false;
        }
        QString words = EventBreakpointWords(kind, value, mask);
        if (mode == BRK_HIT_EQUAL)
            words += tr(", hit %1 from now").arg(target);
        said = (existed ? tr("Already there: breakpoint #%1 on %2: %3") : tr("Breakpoint #%1 on %2: %3"))
                   .arg(id)
                   .arg(QString::fromLatin1(DebugCpuName(cpu)), words);
        return true;
    };
    switch (action)
    {
        case RowAction::BreakHostSends:
            ok = r.command >= 0 && event(DebugCpuId::Main, GSCardEvent::HostCommandWrite, static_cast<uint8_t>(r.command), 0xFF,
                                         BRK_HIT_ALWAYS, 0);
            break;
        case RowAction::BreakCardTakes:
        {
            if (r.command < 0)
                break;
            const CardPanes::Target target = CardPanes::Front(context, _follow ? *_follow : DebugCpuId::Main);
            const DebugCpuId cpu = CardCpuOf(target);
            if (target == CardPanes::Target::None || !CardCpuThere(context, cpu, &said))
                break;
            ok = event(cpu, GSCardEvent::CardCommandRead, static_cast<uint8_t>(r.command), 0xFF, BRK_HIT_ALWAYS, 0);
            break;
        }
        case RowAction::BreakUploadByte:
            if (!r.upload || byte == 0 || (r.bytes > 0 && byte > static_cast<uint64_t>(r.bytes)))
            {
                said = tr("Not an upload's data byte");
                break;
            }
            ok = event(DebugCpuId::Main, GSCardEvent::HostDataWrite, 0, 0, BRK_HIT_EQUAL, byte);
            break;
        case RowAction::GoTo:
        {
            if (!context || !ttd::HasTimeTravelSession(context))
            {
                said = tr("No time-travel recording");
                break;
            }
            // The row's T is the main CPU's; the history counts in its own units (a turbo model's top clock)
            const uint64_t t = static_cast<uint64_t>(context->emulatorState.TtdTInFrame(static_cast<uint32_t>(r.t)));
            const ttd::TTDReply reply = ttd::TTDControl(context).Execute(
                {"seek", {{"frame", std::to_string(r.frame)}, {"tinframe", std::to_string(t)}}});
            const StateNode* reached = reply.body.find("reached");
            if (!reply.Ok())
                said = QString::fromStdString(reply.message);
            else if (reached && reached->kind == StateNode::Kind::Bool && !reached->b)
                said = Str(reply.body, "message").isEmpty() ? tr("The seek did not reach the row's moment") : Str(reply.body, "message");
            else
            {
                said = tr("At frame %1, T %2").arg(r.frame).arg(r.t);
                ok = true;
            }
            break;
        }
        case RowAction::Copy:
        {
            const QString text = copyText(row);
            QApplication::clipboard()->setText(text);
            said = tr("Copied %n rows", nullptr, static_cast<int>(text.count(QLatin1Char('\n')) + 1));
            ok = !text.isEmpty();
            break;
        }
    }
    showAction(said);
    if (message)
        *message = said;
    return ok;
}

void GsMonitorPane::showAction(const QString& text)
{
    _action->setText(text);
    _action->setVisible(!text.isEmpty());
}

QString GsMonitorPane::actionText() const
{
    return _action->text();
}

/// endregion

#include "moc_gsmonitorpane.cpp"
