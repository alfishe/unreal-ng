#include "pchistorypane.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHash>
#include <QHeaderView>
#include <QPainter>
#include <QStaticText>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QAbstractTableModel>
#include <QItemSelectionModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

#include <exception>
#include <optional>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/pchistory/pchistory.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The pane's status line ("1018 instructions"): wrapped text like a word-wrapping QLabel, but a new text relays out
/// the window only when it needs another number of lines. A QLabel invalidates its layout on every text change, and
/// the layouts above it (the side tabs, the splitter, the window) are done again and repaint on every stop
class PcHistoryStatusLine : public QWidget
{
public:
    explicit PcHistoryStatusLine(QWidget* parent) : QWidget(parent)
    {
        QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    const QString& text() const { return _text; }
    void setText(const QString& text)
    {
        if (text == _text)
            return;
        const int before = heightForWidth(width());
        _text = text;
        if (width() <= 0 || heightForWidth(width()) != before)
            updateGeometry();
        update();
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        if (_text.isEmpty())
            return 0;
        const QFontMetrics metrics(font());
        return metrics.boundingRect(QRect(0, 0, std::max(1, width), 1 << 20), kFlags, _text).height();
    }
    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        return {metrics.horizontalAdvance(_text), _text.isEmpty() ? 0 : metrics.height()};
    }
    QSize minimumSizeHint() const override { return {0, sizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        style()->drawItemText(&painter, rect(), kFlags, palette(), isEnabled(), _text, foregroundRole());
    }
    void changeEvent(QEvent* event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            updateGeometry();
    }

private:
    static constexpr int kFlags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap;
    QString _text;
};

/// Paints a cell as QStyledItemDelegate does, but a plain cell's text (no selection, focus, check, icon or own
/// background: what the style would draw is the text alone) as one line by QPainter::drawText: the style's per-cell
/// layout and QTextLayout were most of the list's repaint, and a stop repaints every row (the rows move down)
class PcHistoryRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const bool plain = !(opt.state & (QStyle::State_Selected | QStyle::State_HasFocus)) &&
                           opt.backgroundBrush.style() == Qt::NoBrush &&
                           !(opt.features & (QStyleOptionViewItem::HasCheckIndicator |
                                             QStyleOptionViewItem::HasDecoration)) &&
                           !(opt.features & QStyleOptionViewItem::WrapText);
        if (!plain)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        if (opt.text.isEmpty())
            return;
        const QWidget* widget = opt.widget;
        const QStyle* style = widget ? widget->style() : QApplication::style();
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
        const QRect textRect = opt.rect.adjusted(margin, 0, -margin, 0);
        const QPalette::ColorGroup group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
                                           : !(opt.state & QStyle::State_Active) ? QPalette::Inactive
                                                                                 : QPalette::Normal;
        const QPen pen = painter->pen();
        const QFont font = painter->font();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(group, QPalette::Text));
        if (opt.displayAlignment == (Qt::AlignLeft | Qt::AlignVCenter))
        {
            // The rows move down a row per instruction: a text is laid out once, not on every repaint
            const QStaticText& text = laidOut(opt, textRect.width());
            const qreal y = textRect.top() + (textRect.height() - text.size().height()) / 2;
            painter->drawStaticText(QPointF(textRect.left(), y), text);
        }
        else
            painter->drawText(textRect, static_cast<int>(opt.displayAlignment) | Qt::TextSingleLine,
                              opt.fontMetrics.elidedText(opt.text, opt.textElideMode, textRect.width()));
        painter->setFont(font);
        painter->setPen(pen);
    }

private:
    const QStaticText& laidOut(const QStyleOptionViewItem& opt, int width) const
    {
        const QString fontKey = opt.font.key();
        if (fontKey != _fontKey || _texts.size() > kTexts)
        {
            _texts.clear();
            _fontKey = fontKey;
        }
        const QString key = opt.text + QChar(0x1F) + QString::number(width) + QChar(0x1F) +
                            QString::number(static_cast<int>(opt.textElideMode));
        auto it = _texts.find(key);
        if (it == _texts.end())
        {
            QStaticText text(opt.fontMetrics.elidedText(opt.text, opt.textElideMode, width));
            text.setTextFormat(Qt::PlainText);
            text.prepare(QTransform(), opt.font);
            it = _texts.insert(key, text);
        }
        return *it;
    }

    static constexpr qsizetype kTexts = 8192;  ///< laid-out texts kept (a few rows' worth per instruction)
    mutable QHash<QString, QStaticText> _texts;
    mutable QString _fontKey;
};

/// The rows of the list, newest first, over a ring of PcHistory::kCapacity rows: no item objects; a stop adds its rows
/// at the top (rowsInserted while the ring fills; once full, the rows are the ring's turned by the new ones and change
/// as data, so the view lays out nothing: QTreeView's layout of 1024 rows was most of a stop's cost)
class PcHistoryModel : public QAbstractTableModel
{
public:
    static constexpr int kSequenceRole = Qt::UserRole + 1;  ///< the entry's number in the history (its stripe)

    struct Row
    {
        uint16_t address = 0;
        unsigned long long sequence = 0;
        QString addressText;
        QString pageText;
        QString instruction;
    };

    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(_count);
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : 3; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return QAbstractTableModel::headerData(section, orientation, role);
        static const char* const kNames[] = {"Addr", "Page", "Instruction"};
        return section >= 0 && section < 3 ? QString::fromLatin1(kNames[section]) : QVariant();
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= static_cast<int>(_count))
            return {};
        const Row& r = row(index.row());
        switch (role)
        {
            case Qt::DisplayRole:
                return index.column() == 0 ? r.addressText : index.column() == 1 ? r.pageText : r.instruction;
            case Qt::UserRole:
                return index.column() == 0 ? QVariant(r.address) : QVariant();
            case kSequenceRole:
                return QVariant::fromValue(r.sequence);
            default:
                return {};
        }
    }

    const Row& row(int at) const { return _ring[(_newest + kCapacity - static_cast<size_t>(at)) % kCapacity]; }
    bool full() const { return _count == kCapacity; }

    /// All rows anew (`rows` newest first)
    void reset(std::vector<Row> rows)
    {
        beginResetModel();
        _count = std::min(rows.size(), kCapacity);
        for (size_t i = 0; i < _count; ++i)
            _ring[(kCapacity - 1 - i) % kCapacity] = std::move(rows[i]);
        _newest = kCapacity - 1;
        endResetModel();
    }
    /// `rows` (newest first) above the rows there; at most `keep` rows in all, the oldest leave
    void push(std::vector<Row> rows, size_t keep)
    {
        keep = std::min(keep, kCapacity);
        const size_t fresh = std::min(rows.size(), keep);
        if (fresh == 0)
            return;
        if (full() && keep == kCapacity)
        {
            // The ring turns: every row on screen shows the entry `fresh` rows above it now
            for (size_t i = fresh; i-- > 0;)
            {
                _newest = (_newest + 1) % kCapacity;
                _ring[_newest] = std::move(rows[i]);
            }
            emit dataChanged(index(0, 0), index(static_cast<int>(_count) - 1, 2));
            return;
        }
        beginInsertRows(QModelIndex(), 0, static_cast<int>(fresh) - 1);
        // The ring holds kCapacity rows: past the oldest kept, so they are first moved out of the way
        if (_count + fresh > kCapacity)
        {
            const size_t drop = _count + fresh - kCapacity;
            endInsertRows();
            beginRemoveRows(QModelIndex(), static_cast<int>(_count - drop), static_cast<int>(_count) - 1);
            _count -= drop;
            endRemoveRows();
            beginInsertRows(QModelIndex(), 0, static_cast<int>(fresh) - 1);
        }
        for (size_t i = fresh; i-- > 0;)
        {
            _newest = (_newest + 1) % kCapacity;
            _ring[_newest] = std::move(rows[i]);
        }
        _count += fresh;
        endInsertRows();
        if (_count > keep)
        {
            beginRemoveRows(QModelIndex(), static_cast<int>(keep), static_cast<int>(_count) - 1);
            _count = keep;
            endRemoveRows();
        }
    }

private:
    static constexpr size_t kCapacity = PcHistory::kCapacity;
    std::array<Row, kCapacity> _ring{};
    size_t _newest = kCapacity - 1;  ///< the ring's slot of row 0
    size_t _count = 0;
};

/// The list: alternate rows striped by the entry (its place in the history), not by the row, so the rows shown keep
/// their pixels when a stop puts new ones above them; a stop then moves the rows' pixels down (QWidget::scroll) and
/// paints only the new rows, where Qt's item views repaint every row on screen for a row inserted at the top
class PcHistoryList : public QTreeView
{
public:
    using QTreeView::QTreeView;

    /// New rows at the top: `change` changes the model, then the rows shown move down `rows` rows as pixels and only
    /// the new rows are painted (a full repaint when the view is not shown or the move covers it). `relayout`: the
    /// change inserted or removed rows (laid out now, not from the view's timer, which would repaint everything)
    template <typename Change>
    void insertAtTop(int rows, bool relayout, Change&& change)
    {
        QWidget* port = viewport();
        QWidget* head = header()->viewport();
        const int rowHeight = model() && model()->rowCount() > 0 ? sizeHintForRow(0) : 0;
        const bool shift = rows > 0 && isVisible() && rowHeight > 0 && rows * rowHeight < port->height() &&
                           port->updatesEnabled() && head->updatesEnabled();
        if (shift)
        {
            // Not setUpdatesEnabled: enabling again repaints the whole widget, the cost this avoids
            port->setAttribute(Qt::WA_UpdatesDisabled, true);
            head->setAttribute(Qt::WA_UpdatesDisabled, true);  // the header shows the same labels
        }
        change();
        if (relayout)
            doItemsLayout();
        if (shift)
        {
            port->setAttribute(Qt::WA_UpdatesDisabled, false);
            head->setAttribute(Qt::WA_UpdatesDisabled, false);
            port->scroll(0, rows * rowHeight);
        }
        shifts += shift ? 1 : 0;
    }
    int shifts = 0;  ///< stops whose rows moved as pixels (tests)

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        // The stripe of the row's entry, as QTreeView draws alternate rows (PE_PanelItemViewRow), then the row
        if (index.siblingAtColumn(0).data(PcHistoryModel::kSequenceRole).toULongLong() & 1)
        {
            QStyleOptionViewItem stripe = option;
            stripe.features |= QStyleOptionViewItem::Alternate;
            stripe.rect = QRect(0, option.rect.top(), viewport()->width(), option.rect.height());
            style()->drawPrimitive(QStyle::PE_PanelItemViewRow, &stripe, painter, this);
        }
        QTreeView::drawRow(painter, option, index);
    }
};

namespace
{
/// The instruction at `address` as it was: read from the page its window showed then, not from today's mapping
QString InstructionAt(Emulator* emulator, uint16_t address, uint8_t kind, uint8_t page)
{
    Memory* memory = emulator->GetMemory();
    DebugManager* debugManager = emulator->GetDebugManager();
    if (!memory || !debugManager || !debugManager->GetDisassembler())
        return {};
    const uint8_t* base = kind == BANK_ROM ? memory->ROMPageHostAddress(page)
                          : kind == BANK_RAM ? memory->RAMPageAddress(page)
                                             : nullptr;
    std::vector<uint8_t> bytes(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        const uint16_t at = static_cast<uint16_t>(address + i);
        const uint16_t inPage = static_cast<uint16_t>((address & 0x3FFF) + i);
        // Past the page's end (or a cache page) the bytes come from the CPU's view now
        bytes[i] = base && inPage < 0x4000 ? base[inPage] : memory->DirectReadFromZ80Memory(at);
    }
    try
    {
        return QString::fromStdString(debugManager->GetDisassembler()->disassembleSingleCommand(bytes, address));
    }
    catch (const std::exception&)
    {
        return QStringLiteral("?");
    }
}
/// The page column's kind, as PcHistory::ReportNow names it
QString KindName(uint8_t kind)
{
    return kind == BANK_ROM     ? QStringLiteral("ROM")
           : kind == BANK_RAM   ? QStringLiteral("RAM")
           : kind == BANK_CACHE ? QStringLiteral("CACHE")
                                : QStringLiteral("NONE");
}
}  // namespace

PcHistoryPane::PcHistoryPane(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent)
{
    _status = new PcHistoryStatusLine(this);
    _model = new PcHistoryModel(this);
    _list = new PcHistoryList(this);
    _list->setModel(_model);
    _list->setSelectionBehavior(QAbstractItemView::SelectRows);
    _list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _list->setRootIsDecorated(false);
    _list->setUniformRowHeights(true);
    _list->setItemDelegate(new PcHistoryRowDelegate(_list));
    _list->setAlternatingRowColors(false);  // striped by the entry: PcHistoryList::drawRow
    _list->header()->setStretchLastSection(true);
    // Fixed widths: ResizeToContents measured (shaped) all 1024 rows on every stop and layout pass
    _list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    _list->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    sizeColumns();
    _retry = new QTimer(this);
    _retry->setSingleShot(true);
    _retry->setInterval(kRetryMs);
    connect(_retry, &QTimer::timeout, this, &PcHistoryPane::read);
    _list->setToolTip("Newest first. Click a row: the disassembly shows that address");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_status);
    layout->addWidget(_list, 1);

    connect(_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!_filling && index.isValid())
            emit addressPicked(static_cast<uint16_t>(index.siblingAtColumn(0).data(Qt::UserRole).toUInt()));
    });

    setEmulator(std::move(emulator));
}

void PcHistoryPane::setEmulator(std::shared_ptr<Emulator> emulator)
{
    if (emulator == _emulator)
        return;
    if (_recording && _emulator)
        PcHistory::SetArmed(_emulator.get(), false);  // the machine left behind pays nothing
    _emulator = std::move(emulator);
    clearRows();
    setStatus(_recording ? QString("Recording: step or run") : QString());
    if (_recording && _emulator)
        PcHistory::SetArmed(_emulator.get(), true);
}

void PcHistoryPane::setRecording(bool on)
{
    _recording = on;
    clearRows();
    if (!_emulator)
        return;
    const std::string error = PcHistory::SetArmed(_emulator.get(), on);
    setStatus(!error.empty() ? QString::fromStdString(error)
              : on           ? QString("Recording: step or run")
                             : QString("Stopped"));
    if (on)
        refresh();
}

void PcHistoryPane::refresh()
{
    _retriesLeft = kRetries;
    read();
}

void PcHistoryPane::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        sizeColumns();
}

void PcHistoryPane::sizeColumns()
{
    const QFontMetrics metrics(_list->font());
    const int pad = metrics.horizontalAdvance(QStringLiteral("MM"));
    const QFontMetrics headerMetrics(_list->header()->font());
    _list->setColumnWidth(0, std::max(metrics.horizontalAdvance(QStringLiteral("FFFF")),
                                      headerMetrics.horizontalAdvance(QStringLiteral("Addr"))) +
                                 pad);
    _list->setColumnWidth(1, std::max(metrics.horizontalAdvance(QStringLiteral("ROM255")),
                                      headerMetrics.horizontalAdvance(QStringLiteral("Page"))) +
                                 pad);
}

void PcHistoryPane::setStatus(const QString& text)
{
    _status->setText(text);  // relays out the window only when it needs another number of lines
}

void PcHistoryPane::clearRows()
{
    _model->reset({});
    _keys.clear();
    _total = 0;
}

void PcHistoryPane::retryLater()
{
    _busyReads++;
    if (_retriesLeft-- > 0)
        _retry->start();
}

void PcHistoryPane::read()
{
    if (!_recording || !_emulator)
        return;
    if (_emulator->IsRunning() && !_emulator->IsPaused())
    {
        setStatus("Running: the list follows at the next stop");
        return;
    }
    // Stopping (not parked yet) or stepped by automation: a read now would wait; the list stays, read again soon
    if ((_emulator->IsDirectStepping() || (_emulator->IsPaused() && !_emulator->IsEmulationParked())))
    {
        retryLater();
        return;
    }
    EmulatorContext* context = _emulator->GetContext();  // lifetime-ok: the window's lease at a stop
    PcHistory* history = context && context->pDebugManager ? context->pDebugManager->GetPcHistory() : nullptr;
    if (!history || !context->pMemory)
    {
        _retry->stop();
        setStatus(QStringLiteral("emulator not available"));
        return;
    }
    // A stop reads only the entries since the rows shown (the history's total tells how many) and kOverlap of the
    // rows shown, which prove the new ones continue them; the whole ring (kCapacity entries) only for the first read
    // or when they do not (re-armed, cleared). One try at a coherent moment: busy, the list stays, read again later
    std::vector<PcHistory::Entry> entries;
    unsigned long long total = 0;
    const auto readNewest = [&](bool all) {
        return _emulator->RunAtCoherentMoment(
            [&]() {
                if (!history->IsArmed())
                    history->Arm(true);  // the first read starts it (as PcHistory::Report)
                total = history->Total();
                size_t depth = PcHistory::kCapacity;
                if (!all && total >= _total && total - _total + kOverlap <= PcHistory::kCapacity)
                    depth = static_cast<size_t>(total - _total) + kOverlap;
                entries = history->Newest(depth);
            },
            0);
    };
    const auto keysOf = [&]() {
        std::vector<Key> keys;
        keys.reserve(entries.size());
        for (const PcHistory::Entry& entry : entries)
            keys.push_back({entry.address, static_cast<char>(entry.kind), entry.page});
        return keys;
    };
    // The newest entries in `keys` continue the rows shown: how many are new, or nothing when they do not
    const auto added = [this](const std::vector<Key>& keys, unsigned long long total) -> std::optional<size_t> {
        if (_keys.empty() || total < _total || total - _total > keys.size())
            return std::nullopt;
        const size_t fresh = static_cast<size_t>(total - _total);
        const size_t overlap = std::min(keys.size() - fresh, _keys.size());
        if (!std::equal(keys.begin() + static_cast<std::ptrdiff_t>(fresh),
                        keys.begin() + static_cast<std::ptrdiff_t>(fresh + overlap), _keys.begin()))
            return std::nullopt;
        return fresh;
    };

    const bool whole = _keys.empty();
    if (readNewest(whole) == Emulator::CoherentMoment::Busy)
    {
        retryLater();  // another client holds the machine: the list stays
        return;
    }
    std::vector<Key> keys = keysOf();
    size_t fresh = keys.size();
    if (!whole)
    {
        const std::optional<size_t> probed = added(keys, total);
        if (probed && (*probed < keys.size() || _total == total))
        {
            fresh = *probed;
            keys.resize(fresh);
            const size_t kept = std::min(_keys.size(), PcHistory::kCapacity - fresh);
            keys.insert(keys.end(), _keys.begin(), _keys.begin() + static_cast<std::ptrdiff_t>(kept));
        }
        else
        {
            if (readNewest(true) == Emulator::CoherentMoment::Busy)
            {
                retryLater();
                return;
            }
            keys = keysOf();
            fresh = added(keys, total).value_or(keys.size());
        }
    }
    _retry->stop();
    const size_t count = keys.size();
    setStatus(count == 0 ? QString("Recording: step or run")
              : total > count ? QString("The newest %1 of %2 instructions").arg(count).arg(total)
                              : QString("%1 instructions").arg(count));
    if (fresh == 0 && count == static_cast<size_t>(_model->rowCount()))
    {
        _keys = std::move(keys);
        _total = total;
        return;  // nothing ran since the rows shown
    }

    const bool refill = fresh == count;
    std::vector<PcHistoryModel::Row> rows;
    rows.reserve(fresh);
    for (size_t i = 0; i < fresh; ++i)
    {
        const PcHistory::Entry& entry = entries[i];
        PcHistoryModel::Row row;
        row.address = entry.address;
        row.sequence = total - 1 - i;  // its stripe
        row.addressText = QString("%1").arg(entry.address, 4, 16, QChar('0')).toUpper();
        row.pageText = KindName(entry.kind) + QString::number(entry.page);
        row.instruction = InstructionAt(_emulator.get(), entry.address, entry.kind, entry.page);
        rows.push_back(std::move(row));
    }
    _rowsMade += static_cast<int>(fresh);
    _filling = true;  // a refilled or moved list is not a pick
    if (refill)
        _model->reset(std::move(rows));
    else
    {
        const bool turns = _model->full() && count == PcHistory::kCapacity;
        // The picked row follows its entry down (the ring turning moves data, not rows); out of the ring: none
        QItemSelectionModel* selection = _list->selectionModel();
        const QModelIndex current = _list->currentIndex();
        const bool selected = current.isValid() && selection->isSelected(current);
        _list->insertAtTop(static_cast<int>(fresh), !turns, [&]() {
            _model->push(std::move(rows), count);
            if (turns && current.isValid())
            {
                const int moved = current.row() + static_cast<int>(fresh);
                selection->clear();
                if (moved < _model->rowCount())
                    selection->setCurrentIndex(_model->index(moved, current.column()),
                                               selected ? QItemSelectionModel::Select | QItemSelectionModel::Rows
                                                        : QItemSelectionModel::NoUpdate);
            }
        });
    }
    _filling = false;
    _keys = std::move(keys);
    _total = total;
}
