#include "zxdmatraceview.h"

#include <QAbstractTableModel>
#include <QBoxLayout>
#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QTableView>
#include <QToolButton>

#include <deque>

#include "debugger/tracecontrol/tracecontrol.h"
#include "emulator/emulator.h"
#include "emulator/sound/chips/gs/gszxdmatrace.h"

namespace
{
const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::Int ? v->i : 0;
}

bool Bool(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::Bool && v->b;
}

QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

QString Grouped(int64_t value)
{
    QString digits = QString::number(value < 0 ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(i, QLatin1Char(','));
    return value < 0 ? QLatin1Char('-') + digits : digits;
}

QString Hex(int64_t value, int digits)
{
    return QLatin1Char('#') + QString::number(static_cast<qulonglong>(value), 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}
}  // namespace

/// The table's rows: the report's rows as they came, oldest first, at most kMaxRows
class ZxDmaTraceModel : public QAbstractTableModel
{
public:
    struct Row
    {
        int64_t frame = 0;
        int64_t hostT = 0;
        int64_t cardTime = 0;
        bool write = false;
        uint32_t address = 0;
        int64_t page = 0;
        uint8_t value = 0;
        int64_t wait = 0;
        uint16_t pc = 0;
        bool dropped = false;
    };

    explicit ZxDmaTraceModel(QObject* parent) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(_rows.size());
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ZxDmaTraceView::ColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return QVariant();
        switch (section)
        {
            case ZxDmaTraceView::Frame: return ZxDmaTraceView::tr("Frame");
            case ZxDmaTraceView::HostT: return ZxDmaTraceView::tr("Host T");
            case ZxDmaTraceView::CardCycle: return ZxDmaTraceView::tr("Card cycle");
            case ZxDmaTraceView::Direction: return ZxDmaTraceView::tr("Dir");
            case ZxDmaTraceView::CardAddress: return ZxDmaTraceView::tr("Card address");
            case ZxDmaTraceView::Value: return ZxDmaTraceView::tr("Value");
            case ZxDmaTraceView::Wait: return ZxDmaTraceView::tr("Wait");
            case ZxDmaTraceView::HostPc: return ZxDmaTraceView::tr("Host PC");
            default: return QVariant();
        }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_rows.size()))
            return QVariant();
        const Row& row = _rows[static_cast<size_t>(index.row())];
        if (role == Qt::BackgroundRole)
            return row.dropped ? QVariant(QColor(255, 190, 150)) : QVariant();
        if (role == Qt::ForegroundRole)
            return row.dropped ? QVariant(QColor(Qt::black)) : QVariant();
        if (role == Qt::ToolTipRole)
            return row.dropped ? ZxDmaTraceView::tr("Dropped: the module discarded this byte (an access of the other "
                                                    "direction came while it was pending), no wait")
                               : QVariant();
        if (role == Qt::TextAlignmentRole)
            return index.column() == ZxDmaTraceView::Direction ? QVariant()
                                                                : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        if (role != Qt::DisplayRole)
            return QVariant();
        switch (index.column())
        {
            case ZxDmaTraceView::Frame: return QString::number(row.frame);
            case ZxDmaTraceView::HostT: return Grouped(row.hostT);
            case ZxDmaTraceView::CardCycle: return Grouped(row.cardTime);
            case ZxDmaTraceView::Direction:
                return (row.write ? ZxDmaTraceView::tr("write") : ZxDmaTraceView::tr("read")) +
                       (row.dropped ? ZxDmaTraceView::tr(" (dropped)") : QString());
            case ZxDmaTraceView::CardAddress:
                return Hex(row.address, 6) + ZxDmaTraceView::tr(" · page %1").arg(Hex(row.page, 2));
            case ZxDmaTraceView::Value: return Hex(row.value, 2);
            case ZxDmaTraceView::Wait: return QString::number(row.wait);
            case ZxDmaTraceView::HostPc: return Hex(row.pc, 4);
            default: return QVariant();
        }
    }

    const Row& at(int row) const { return _rows[static_cast<size_t>(row)]; }

    void append(std::vector<Row>&& rows)
    {
        if (rows.empty())
            return;
        // More new rows than the table keeps: only the newest
        if (rows.size() > static_cast<size_t>(ZxDmaTraceView::kMaxRows))
            rows.erase(rows.begin(), rows.end() - ZxDmaTraceView::kMaxRows);
        const size_t overflow = _rows.size() + rows.size() > static_cast<size_t>(ZxDmaTraceView::kMaxRows)
                                    ? _rows.size() + rows.size() - static_cast<size_t>(ZxDmaTraceView::kMaxRows)
                                    : 0;
        if (overflow > 0)
        {
            beginRemoveRows(QModelIndex(), 0, static_cast<int>(overflow) - 1);
            _rows.erase(_rows.begin(), _rows.begin() + static_cast<std::ptrdiff_t>(overflow));
            endRemoveRows();
        }
        const int first = static_cast<int>(_rows.size());
        beginInsertRows(QModelIndex(), first, first + static_cast<int>(rows.size()) - 1);
        for (Row& row : rows)
            _rows.push_back(row);
        endInsertRows();
    }

    void clear()
    {
        if (_rows.empty())
            return;
        beginResetModel();
        _rows.clear();
        endResetModel();
    }

private:
    std::deque<Row> _rows;
};

ZxDmaTraceView::ZxDmaTraceView(QWidget* parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QHBoxLayout* toggleRow = new QHBoxLayout();
    _toggle = new QToolButton(this);
    _toggle->setObjectName("zxDmaTraceToggle");
    _toggle->setCheckable(true);
    _toggle->setText(tr("Trace ▸"));
    _toggle->setToolTip(tr("The ZX-DMA trace: every byte the Spectrum read from or wrote to card RAM through the "
                           "window, recorded while the GS port trace captures"));
    toggleRow->addStretch(1);
    toggleRow->addWidget(_toggle);
    layout->addLayout(toggleRow);

    _body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(2);
    QHBoxLayout* headerRow = new QHBoxLayout();
    _header = new QLabel(_body);
    _header->setWordWrap(true);
    _header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerRow->addWidget(_header, 1);
    auto button = [this, headerRow](const QString& text, const QString& op, const QString& tip) {
        QToolButton* b = new QToolButton(_body);
        b->setText(text);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [this, op]() { emit captureRequested(op); });
        headerRow->addWidget(b);
        return b;
    };
    _start = button(tr("Start"), QStringLiteral("start"), tr("Start the GS port trace (a new session: the ring empties)"));
    _stop = button(tr("Stop"), QStringLiteral("stop"), tr("Stop the GS port trace (the bytes kept stay)"));
    _clear = button(tr("Clear"), QStringLiteral("clear"), tr("Empty the GS port trace's ring and this table"));
    bodyLayout->addLayout(headerRow);
    _note = new QLabel(_body);
    _note->setWordWrap(true);
    _note->hide();
    bodyLayout->addWidget(_note);

    _model = new ZxDmaTraceModel(this);
    _table = new QTableView(_body);
    _table->setObjectName("zxDmaTraceTable");
    _table->setModel(_model);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setWordWrap(false);
    _table->verticalHeader()->hide();
    _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setMinimumHeight(_table->fontMetrics().height() * 16);
    _table->setToolTip(tr("Double-click: the host PC in the main CPU's disassembly; on the card address: the card's "
                          "memory view there (when one of its windows maps the page)"));
    for (int c = 0; c < ColumnCount; c++)
        _table->setColumnWidth(c, _table->fontMetrics().horizontalAdvance(c == CardAddress ? QStringLiteral("#000000 · page #00  ")
                                                                                            : QStringLiteral("00,000,000  ")));
    bodyLayout->addWidget(_table, 1);
    layout->addWidget(_body, 1);
    _body->hide();

    connect(_toggle, &QToolButton::toggled, this, [this](bool on) {
        _toggle->setText(on ? tr("Trace ▾") : tr("Trace ▸"));
        _body->setVisible(on);
        emit expandedChanged(on);
    });
    connect(_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        const ZxDmaTraceModel::Row& row = _model->at(index.row());
        if (index.column() == CardAddress)
            emit cardAddressPicked(row.address);
        else
            emit hostPcPicked(row.pc);
    });
}

bool ZxDmaTraceView::expanded() const
{
    return _toggle->isChecked();
}

void ZxDmaTraceView::refresh(EmulatorContext* context, bool live)
{
    (void)live;  // the rows are the same either way; a paused machine adds none
    if (!expanded() || !context)
        return;
    GSZxDmaTraceQuery query;
    query.last = static_cast<size_t>(kMaxRows);
    query.since = _since;
    const StateNode report = DeviceState::GsZxDmaTrace(context, query);
    if (!Bool(report, "available"))
    {
        reset();
        _header->setText(Str(report, "description"));
        _start->setEnabled(false);
        _stop->setEnabled(false);
        _clear->setEnabled(false);
        return;
    }
    if (Bool(report, "restarted"))
        _model->clear();  // a new session (the ring emptied): its rows from the start
    _since = static_cast<uint64_t>(Int(report, "last_seq"));

    std::vector<ZxDmaTraceModel::Row> rows;
    if (const StateNode* items = Member(report, "rows"); items && items->isArray())
    {
        rows.reserve(items->items.size());
        for (const StateNode& item : items->items)
        {
            ZxDmaTraceModel::Row row;
            row.frame = Int(item, "frame");
            row.hostT = Int(item, "host_t");
            row.cardTime = Int(item, "card_time");
            row.write = Str(item, "direction") == QLatin1String("write");
            row.address = static_cast<uint32_t>(Int(item, "card_address"));
            row.page = Int(item, "page");
            row.value = static_cast<uint8_t>(Int(item, "value"));
            row.wait = Int(item, "wait");
            row.pc = static_cast<uint16_t>(Int(item, "host_pc"));
            row.dropped = Bool(item, "dropped");
            rows.push_back(row);
        }
    }
    // Follow the tail unless the user scrolled up to read
    QScrollBar* bar = _table->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum();
    if (!rows.empty())
    {
        _model->append(std::move(rows));
        if (atEnd)
            _table->scrollToBottom();
    }
    ShowHeader(report);
}

void ZxDmaTraceView::ShowHeader(const StateNode& report)
{
    const bool capturing = Bool(report, "capturing");
    const bool armed = Bool(report, "armed");
    const int64_t kept = Int(report, "total_bytes_kept");
    const int64_t evicted = Int(report, "evicted");
    QString line = capturing ? tr("Capturing") : armed ? tr("Paused") : tr("Not capturing");
    line += tr(" · %1 byte(s) kept · %2 shown").arg(Grouped(kept), Grouped(_model->rowCount()));
    if (evicted > 0)
        line += tr(" · %1 oldest byte(s) evicted: the ring is full (Clear it, or read sooner)").arg(Grouped(evicted));
    if (!capturing && !armed)
    {
        const QString how = Str(report, "how_to_start");
        line += tr(". Start records the bytes (the GS port trace)") + (how.isEmpty() ? QString() : tr("; or ") + how);
    }
    _header->setText(line);
    _start->setEnabled(!capturing);
    _stop->setEnabled(capturing || armed);
    _clear->setEnabled(true);
}

QString ZxDmaTraceView::capture(Emulator* emulator, const QString& op)
{
    if (!emulator)
        return QString();
    const tracecontrol::Reply reply = tracecontrol::Execute(*emulator, "gsporttrace", op.toStdString());
    // A start begins a new session and a clear empties the ring: both number the events from 1 again
    if (reply.Ok() && (op == QLatin1String("clear") || op == QLatin1String("start")))
        reset();
    setNote(QString::fromStdString(reply.message));
    return note();
}

void ZxDmaTraceView::reset()
{
    _model->clear();
    _since = 0;
    setNote(QString());
}

void ZxDmaTraceView::setNote(const QString& text)
{
    _note->setText(text);
    _note->setVisible(!text.isEmpty());
}

QString ZxDmaTraceView::header() const
{
    return _header->text();
}

QString ZxDmaTraceView::note() const
{
    return _note->text();
}

QString ZxDmaTraceView::cell(int row, Column column) const
{
    return _model->data(_model->index(row, column), Qt::DisplayRole).toString();
}

int ZxDmaTraceView::rowCount() const
{
    return _model->rowCount();
}

#include "moc_zxdmatraceview.cpp"
