#include "pchistorypane.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include <exception>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/pchistory/pchistory.h"
#include "emulator/emulator.h"
#include "emulator/memory/memory.h"

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
    _status = new QLabel(this);
    _status->setWordWrap(true);
    _list = new QTreeWidget(this);
    _list->setColumnCount(3);
    _list->setHeaderLabels({"Addr", "Page", "Instruction"});
    _list->setRootIsDecorated(false);
    _list->setUniformRowHeights(true);
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
    if (_status->text() != text)  // the same text again: no relayout of the window
        _status->setText(text);
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
    const PcHistory::Result result = PcHistory::Report(_emulator.get(), PcHistory::kCapacity, 0);
    if (result.busy)
    {
        retryLater();  // another client holds the machine: the list stays
        return;
    }
    _retry->stop();
    if (!result.error.empty())
    {
        setStatus(QString::fromStdString(result.error));
        return;
    }
    const StateNode& report = result.report;
    const StateNode* entries = report.find("entries");
    const size_t count = entries ? entries->items.size() : 0;
    const unsigned long long total = static_cast<unsigned long long>(report.find("total")->i);
    setStatus(count == 0 ? QString("Recording: step or run")
              : total > count ? QString("The newest %1 of %2 instructions").arg(count).arg(total)
                              : QString("%1 instructions").arg(count));

    std::vector<Key> keys;
    keys.reserve(count);
    if (entries)
        for (const StateNode& entry : entries->items)
        {
            const std::string& kind = entry.find("kind")->s;
            keys.push_back({static_cast<uint16_t>(entry.find("address")->i), kind.empty() ? '\0' : kind[0],
                            static_cast<uint8_t>(entry.find("page")->i)});
        }

    // The rows already shown move down: only the entries since the last read are new. When the history does not
    // continue the rows shown (restarted, another machine), all rows are made again
    size_t fresh = keys.size();
    if (!_keys.empty() && total >= _total && total - _total <= keys.size())
    {
        const size_t added = static_cast<size_t>(total - _total);
        const size_t overlap = std::min(keys.size() - added, _keys.size());
        if (std::equal(keys.begin() + static_cast<std::ptrdiff_t>(added),
                       keys.begin() + static_cast<std::ptrdiff_t>(added + overlap), _keys.begin()))
            fresh = added;
    }

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
