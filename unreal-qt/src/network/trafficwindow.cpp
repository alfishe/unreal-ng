/**
 * @file trafficwindow.cpp
 * @brief The Network traffic window: the tap through TrafficAccess, rows /
 *        decode / hex through the Qt-free TrafficPanelModel.
 */

#include "trafficwindow.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/traffic/trafficaccess.h"

namespace
{
    QString Q(const std::string& text) { return QString::fromStdString(text); }

    enum Column
    {
        ColIndex,
        ColTime,
        ColAdapter,
        ColDirection,
        ColLength,
        ColSummary,
        ColCount
    };

    const StateNode* Path(const StateNode& node, std::initializer_list<const char*> keys)
    {
        const StateNode* n = &node;
        for (const char* k : keys)
        {
            n = n ? n->find(k) : nullptr;
        }
        return n;
    }
    bool Flag(const StateNode* n) { return n && n->kind == StateNode::Kind::Bool && n->b; }
    uint64_t Num(const StateNode* n) { return n ? static_cast<uint64_t>(n->i) : 0; }
    std::string Text(const StateNode* n) { return n && n->kind == StateNode::Kind::String ? n->s : std::string(); }
}  // namespace

TrafficWindow::TrafficWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("Network traffic"));
    setWindowFlags(Qt::Tool);
    resize(900, 620);
    buildUi();

    _timer = new QTimer(this);
    _timer->setInterval(500);
    connect(_timer, &QTimer::timeout, this, &TrafficWindow::refresh);
}

void TrafficWindow::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout();
    _filter = new QLineEdit(this);
    _filter->setPlaceholderText(tr("Filter: words in adapter, direction, operation or summary (e.g. \"isa2 dns\")"));
    _filter->setClearButtonEnabled(true);
    _kind = new QComboBox(this);
    _kind->addItems({tr("Everything"), tr("Ethernet frames"), tr("Socket operations")});
    bar->addWidget(_filter, 1);
    bar->addWidget(_kind);
    layout->addLayout(bar);
    connect(_filter, &QLineEdit::textChanged, this, &TrafficWindow::rebuildTable);
    connect(_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, &TrafficWindow::rebuildTable);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto* split = new QSplitter(Qt::Vertical, this);
    _table = new QTableWidget(0, ColCount, split);
    _table->setHorizontalHeaderLabels({tr("#"), tr("Time"), tr("Adapter"), tr("Dir"), tr("Length"), tr("Summary")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->verticalHeader()->setVisible(false);
    _table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setFont(fixed);
    _table->setWordWrap(false);
    connect(_table, &QTableWidget::itemSelectionChanged, this, &TrafficWindow::onSelection);
    connect(_table, &QTableWidget::cellDoubleClicked, this, [this] { onSeek(); });

    auto* details = new QSplitter(Qt::Horizontal, split);
    _decode = new QTreeWidget(details);
    _decode->setHeaderHidden(true);
    _hex = new QPlainTextEdit(details);
    _hex->setReadOnly(true);
    _hex->setFont(fixed);
    _hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    auto* buttons = new QHBoxLayout();
    _seek = new QPushButton(tr("Seek here"), this);
    _seek->setToolTip(tr("Time travel to the moment of the selected record (needs a TTD recording that covers it)"));
    _clear = new QPushButton(tr("Clear"), this);
    _save = new QPushButton(tr("Save pcapng..."), this);
    _save->setToolTip(tr("Save the records the window shows as a pcapng file for Wireshark"));
    _record = new QPushButton(tr("Record to file..."), this);
    _record->setToolTip(tr("Write every record from now on into a pcapng file, unbounded, until stopped"));
    _stream = new QPushButton(tr("Start stream"), this);
    _stream->setToolTip(tr("Serve the traffic live as pcapng over TCP: wireshark -k -i TCP@127.0.0.1:<port>"));
    buttons->addWidget(_seek);
    buttons->addStretch(1);
    buttons->addWidget(_clear);
    buttons->addWidget(_save);
    buttons->addWidget(_record);
    buttons->addWidget(_stream);
    layout->addLayout(buttons);
    connect(_seek, &QPushButton::clicked, this, &TrafficWindow::onSeek);
    connect(_clear, &QPushButton::clicked, this, &TrafficWindow::onClear);
    connect(_save, &QPushButton::clicked, this, &TrafficWindow::onSave);
    connect(_record, &QPushButton::clicked, this, &TrafficWindow::onRecord);
    connect(_stream, &QPushButton::clicked, this, &TrafficWindow::onStream);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _status->setWordWrap(true);
    layout->addWidget(_status);
}

void TrafficWindow::setBinding(EmulatorBinding* binding)
{
    if (_binding)
        disconnect(_binding, nullptr, this, nullptr);
    _binding = binding;
    if (_binding)
    {
        connect(_binding, &EmulatorBinding::bound, this, [this] { reset(); refresh(); });
        connect(_binding, &EmulatorBinding::unbound, this, [this] { reset(); refresh(); });
    }
    reset();
    refresh();
}

void TrafficWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void TrafficWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

EmulatorContext* TrafficWindow::context() const
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    return emulator ? emulator->GetContext() : nullptr;
}

void TrafficWindow::reset()
{
    _rows.clear();
    _next = 0;
    _table->setRowCount(0);
    _decode->clear();
    _hex->clear();
}

void TrafficWindow::refresh()
{
    EmulatorContext* ctx = context();
    TrafficAccess::Query query;
    query.since = _next;
    query.last = 0;
    const StateNode report = TrafficAccess::Records(ctx, query);
    const bool available = Flag(report.find("available"));
    for (QWidget* w : std::initializer_list<QWidget*>{_clear, _save, _record, _stream, _filter, _kind})
        w->setEnabled(available);
    if (!available)
    {
        _status->setText(ctx ? Q(Text(report.find("error"))) : tr("No machine."));
        updateSeek();
        return;
    }

    std::vector<TrafficRow> fresh = TrafficRows(report);
    _next = std::max(_next, Num(Path(report, {"tap", "next_index"})));
    if (!fresh.empty())
    {
        QScrollBar* scroll = _table->verticalScrollBar();
        const bool atBottom = scroll->value() >= scroll->maximum() - 2;
        const size_t first = _rows.size();
        for (TrafficRow& r : fresh)
            _rows.push_back(std::move(r));
        if (_rows.size() > kMaxRows)
        {
            _rows.erase(_rows.begin(), _rows.begin() + static_cast<std::ptrdiff_t>(_rows.size() - kMaxRows));
            rebuildTable();
        }
        else
        {
            _table->setUpdatesEnabled(false);
            for (size_t i = first; i < _rows.size(); ++i)
                appendRow(i);
            _table->setUpdatesEnabled(true);
        }
        if (atBottom)
            _table->scrollToBottom();
    }

    _recording = Flag(Path(report, {"tap", "file", "recording"}));
    _streaming = Flag(Path(report, {"stream", "running"}));
    _record->setText(_recording ? tr("Stop recording") : tr("Record to file..."));
    _stream->setText(_streaming ? tr("Stop stream") : tr("Start stream"));

    QString status = tr("%1 records in the ring (%2 KiB of %3 KiB), %4 shown")
                         .arg(Num(Path(report, {"tap", "records"})))
                         .arg(Num(Path(report, {"tap", "ring_bytes"})) / 1024)
                         .arg(Num(Path(report, {"tap", "ring_budget"})) / 1024)
                         .arg(_table->rowCount());
    if (_recording)
        status += tr(" | recording %1 records to %2")
                      .arg(Num(Path(report, {"tap", "file", "records"})))
                      .arg(Q(Text(Path(report, {"tap", "file", "path"}))));
    if (_streaming)
        status += tr(" | stream: %1 (%2 clients)")
                      .arg(Q(Text(Path(report, {"stream", "wireshark"}))))
                      .arg(Num(Path(report, {"stream", "clients"})));
    _status->setText(status);
    updateSeek();
}

void TrafficWindow::appendRow(size_t rowIndex)
{
    const TrafficRow& r = _rows[rowIndex];
    if (!TrafficRowMatches(r, _filter->text().toStdString(), _kind->currentIndex()))
        return;
    // The time delta is to the row shown above
    uint64_t previousUs = 0;
    const int row = _table->rowCount();
    if (row > 0)
    {
        const size_t above = _table->item(row - 1, ColIndex)->data(Qt::UserRole).toULongLong();
        previousUs = _rows[above].timeUs;
    }
    _table->insertRow(row);
    auto* index = new QTableWidgetItem(QString::number(r.index));
    index->setData(Qt::UserRole, static_cast<qulonglong>(rowIndex));
    _table->setItem(row, ColIndex, index);
    _table->setItem(row, ColTime, new QTableWidgetItem(Q(TrafficTimeText(r, previousUs))));
    _table->setItem(row, ColAdapter, new QTableWidgetItem(Q(r.adapter)));
    _table->setItem(row, ColDirection, new QTableWidgetItem(r.out ? QStringLiteral("out") : QStringLiteral("in")));
    _table->setItem(row, ColLength, new QTableWidgetItem(QString::number(r.bytes.size())));
    _table->setItem(row, ColSummary, new QTableWidgetItem(Q(r.summary)));
}

void TrafficWindow::rebuildTable()
{
    _table->setUpdatesEnabled(false);
    _table->setRowCount(0);
    for (size_t i = 0; i < _rows.size(); ++i)
        appendRow(i);
    _table->setUpdatesEnabled(true);
    _table->scrollToBottom();
    onSelection();
}

const TrafficRow* TrafficWindow::selectedRow() const
{
    const QList<QTableWidgetItem*> items = _table->selectedItems();
    if (items.isEmpty())
        return nullptr;
    const QTableWidgetItem* index = _table->item(items.first()->row(), ColIndex);
    const size_t at = index ? index->data(Qt::UserRole).toULongLong() : _rows.size();
    return at < _rows.size() ? &_rows[at] : nullptr;
}

void TrafficWindow::onSelection()
{
    _decode->clear();
    _hex->clear();
    const TrafficRow* r = selectedRow();
    if (r)
    {
        std::function<void(QTreeWidgetItem*, const TrafficDecodeNode&)> add = [&](QTreeWidgetItem* item, const TrafficDecodeNode& n) {
            item->setText(0, Q(n.text));
            for (const TrafficDecodeNode& c : n.children)
                add(new QTreeWidgetItem(item), c);
        };
        for (const TrafficDecodeNode& n : TrafficDecode(*r))
            add(new QTreeWidgetItem(_decode), n);
        _decode->expandAll();
        _hex->setPlainText(Q(TrafficHexDump(r->bytes)));
    }
    updateSeek();
}

void TrafficWindow::updateSeek()
{
    EmulatorContext* ctx = context();
    const TrafficRow* r = selectedRow();
    QString why;
    bool can = false;
    if (!r)
        why = tr("Select a record to travel to its moment");
    else if (!ctx || !ctx->pTimeTravelManager)
        why = tr("No time travel on this machine");
    else
    {
        const ttd::TTDSessionInfo info = ctx->pTimeTravelManager->GetPublishedSessionInfo();
        if (info.checkpointCount == 0)
            why = tr("Start a TTD recording first: records before it cannot be reached");
        else if (r->frame < info.sessionStartFrame || r->frame > info.currentEndFrame)
            why = tr("Outside the TTD recording (frames %1 - %2)").arg(info.sessionStartFrame).arg(info.currentEndFrame);
        else
            can = true;
    }
    _seek->setEnabled(can);
    _seek->setToolTip(can ? tr("Time travel to frame %1, %2 units in (pauses the machine and ends a running TTD recording; "
                               "its history stays)")
                                .arg(r->frame)
                                .arg(r->tInFrame)
                          : why);
}

void TrafficWindow::onSeek()
{
    EmulatorContext* ctx = context();
    const TrafficRow* r = selectedRow();
    if (!r || !ctx || !ctx->pTimeTravelManager || !_seek->isEnabled())
        return;
    // Like the WebAPI's seek: the machine parks first, and a running recording ends (its history stays; a seek is
    // refused while recording)
    Emulator* emulator = _binding->emulator();
    if (emulator->IsRunning() && !emulator->IsPaused())
    {
        emulator->Pause();
        emulator->WaitForPauseConfirmation(1000);
    }
    ttd::TimeTravelManager* ttd = ctx->pTimeTravelManager;
    if (ttd->IsRecording())
        ttd->StopRecording();
    ttd::TimeTravelManager::TTDSeekResult result;
    const bool ok = ttd->SeekTo(ttd::TTDTimePoint{r->frame, static_cast<uint32_t>(r->tInFrame)}, &result);
    if (ok)
        _status->setText(tr("At record #%1: frame %2, %3 units in").arg(r->index).arg(r->frame).arg(r->tInFrame));
    else
        _status->setText(tr("The seek to frame %1 stopped at frame %2, %3 units in")
                             .arg(r->frame)
                             .arg(result.arrivedAt.frame)
                             .arg(result.arrivedAt.tInFrame));
    emit seeked();
    updateSeek();
}

void TrafficWindow::control(const std::string& action, const std::string& path, uint64_t number)
{
    std::string error;
    if (!TrafficAccess::Control(context(), action, path, number, error))
        _status->setText(Q(error));
    refresh();
}

void TrafficWindow::onClear()
{
    control("clear");
    reset();
}

void TrafficWindow::onSave()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save traffic"), QStringLiteral("traffic.pcapng"),
                                                      tr("pcapng (*.pcapng)"));
    if (file.isEmpty())
        return;
    // The ring's records the window's filter keeps (the kind; the word filter is the window's own)
    TrafficAccess::Query query;
    query.last = 0;
    query.kind = _kind->currentIndex() == 1 ? "frame" : _kind->currentIndex() == 2 ? "socket" : "";
    std::vector<uint8_t> bytes;
    std::string error;
    if (!TrafficAccess::Pcapng(context(), query, bytes, error))
    {
        _status->setText(Q(error));
        return;
    }
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
    {
        _status->setText(tr("Cannot write %1").arg(file));
        return;
    }
    _status->setText(tr("Saved %1 bytes to %2").arg(bytes.size()).arg(file));
}

void TrafficWindow::onRecord()
{
    if (_recording)
    {
        control("stop");
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Record traffic to"), QStringLiteral("traffic.pcapng"),
                                                      tr("pcapng (*.pcapng)"));
    if (!file.isEmpty())
        control("start", file.toStdString());
}

void TrafficWindow::onStream()
{
    control(_streaming ? "stream-stop" : "stream", {}, 0);
}
