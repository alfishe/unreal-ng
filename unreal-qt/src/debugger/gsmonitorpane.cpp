#include "gsmonitorpane.h"

#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>

#include "debugger/cardpanes.h"
#include "debugger/history/historycontrol.h"
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
    body->addWidget(_strip);

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
