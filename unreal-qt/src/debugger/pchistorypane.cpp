#include "pchistorypane.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHash>
#include <QHeaderView>
#include <QPainter>
#include <QStaticText>
#include <QStyle>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include <exception>
#include <optional>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/pchistory/pchistory.h"
#include "emulator/emulator.h"
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

namespace
{
/// The instruction at `address` as it was: read from the page its window showed then, not from today's mapping
QString InstructionAt(Emulator* emulator, uint16_t address, const std::string& kind, uint8_t page)
{
    Memory* memory = emulator->GetMemory();
    DebugManager* debugManager = emulator->GetDebugManager();
    if (!memory || !debugManager || !debugManager->GetDisassembler())
        return {};
    const uint8_t* base = kind == "rom" ? memory->ROMPageHostAddress(page)
                          : kind == "ram" ? memory->RAMPageAddress(page)
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
}  // namespace

PcHistoryPane::PcHistoryPane(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent)
{
    _status = new PcHistoryStatusLine(this);
    _list = new QTreeWidget(this);
    _list->setColumnCount(3);
    _list->setHeaderLabels({"Addr", "Page", "Instruction"});
    _list->setRootIsDecorated(false);
    _list->setUniformRowHeights(true);
    _list->setItemDelegate(new PcHistoryRowDelegate(_list));
    _list->setAlternatingRowColors(true);
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

    connect(_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item)
            emit addressPicked(static_cast<uint16_t>(item->data(0, Qt::UserRole).toUInt()));
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
    _list->clear();
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
    // A stop adds a few entries: the newest kProbe of them show what is new and that the rest continues the rows
    // shown; the whole ring (kCapacity entries, made into a report each time) is read only when they do not
    PcHistory::Result result = PcHistory::Report(_emulator.get(), _keys.empty() ? PcHistory::kCapacity : kProbe, 0);
    if (result.busy)
    {
        retryLater();  // another client holds the machine: the list stays
        return;
    }
    if (!result.error.empty())
    {
        _retry->stop();
        setStatus(QString::fromStdString(result.error));
        return;
    }
    const auto keysOf = [](const StateNode* entries) {
        std::vector<Key> keys;
        keys.reserve(entries ? entries->items.size() : 0);
        if (entries)
            for (const StateNode& entry : entries->items)
            {
                const std::string& kind = entry.find("kind")->s;
                keys.push_back({static_cast<uint16_t>(entry.find("address")->i), kind.empty() ? '\0' : kind[0],
                                static_cast<uint8_t>(entry.find("page")->i)});
            }
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

    std::vector<Key> keys = keysOf(result.report.find("entries"));
    unsigned long long total = static_cast<unsigned long long>(result.report.find("total")->i);
    size_t fresh = keys.size();
    if (!_keys.empty())
    {
        // The probe holds every new entry and, after them, the rows shown (all of them, when fewer than it)
        const std::optional<size_t> probed = added(keys, total);
        if (probed && *probed < keys.size() && keys.size() - *probed <= _keys.size())
        {
            fresh = *probed;
            keys.resize(fresh);
            const size_t kept = std::min(_keys.size(), PcHistory::kCapacity - fresh);  // fresh < kProbe
            keys.insert(keys.end(), _keys.begin(), _keys.begin() + static_cast<std::ptrdiff_t>(kept));
        }
        else
        {
            result = PcHistory::Report(_emulator.get(), PcHistory::kCapacity, 0);
            if (result.busy)
            {
                retryLater();
                return;
            }
            if (!result.error.empty())
            {
                _retry->stop();
                setStatus(QString::fromStdString(result.error));
                return;
            }
            keys = keysOf(result.report.find("entries"));
            total = static_cast<unsigned long long>(result.report.find("total")->i);
            fresh = added(keys, total).value_or(keys.size());
        }
    }
    _retry->stop();
    const StateNode* entries = result.report.find("entries");
    const size_t count = keys.size();
    setStatus(count == 0 ? QString("Recording: step or run")
              : total > count ? QString("The newest %1 of %2 instructions").arg(count).arg(total)
                              : QString("%1 instructions").arg(count));

    QSignalBlocker block(_list);  // a refilled list is not a pick
    if (fresh == keys.size())
        _list->clear();
    QList<QTreeWidgetItem*> rows;
    rows.reserve(static_cast<int>(fresh));
    for (size_t i = 0; i < fresh; ++i)
    {
        const StateNode& entry = entries->items[i];
        const uint16_t address = keys[i].address;
        const std::string& kind = entry.find("kind")->s;
        const uint8_t page = keys[i].page;
        auto* row = new QTreeWidgetItem();
        row->setText(0, QString("%1").arg(address, 4, 16, QChar('0')).toUpper());
        row->setText(1, QString("%1%2").arg(QString::fromStdString(kind).toUpper()).arg(page));
        row->setText(2, InstructionAt(_emulator.get(), address, kind, page));
        row->setData(0, Qt::UserRole, address);
        rows.append(row);
    }
    _rowsMade += static_cast<int>(fresh);
    if (!rows.isEmpty())
        _list->insertTopLevelItems(0, rows);
    while (_list->topLevelItemCount() > static_cast<int>(keys.size()))  // the oldest left the ring
        delete _list->takeTopLevelItem(_list->topLevelItemCount() - 1);
    _keys = std::move(keys);
    _total = total;
}

#include "moc_pchistorypane.cpp"
