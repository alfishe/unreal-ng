#include "pchistorypane.h"

#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

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
    _list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
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
    _list->clear();
    _status->setText(_recording ? QString("Recording: step or run") : QString());
    if (_recording && _emulator)
        PcHistory::SetArmed(_emulator.get(), true);
}

void PcHistoryPane::setRecording(bool on)
{
    _recording = on;
    _list->clear();
    if (!_emulator)
        return;
    const std::string error = PcHistory::SetArmed(_emulator.get(), on);
    _status->setText(!error.empty() ? QString::fromStdString(error)
                     : on           ? QString("Recording: step or run")
                                    : QString("Stopped"));
    if (on)
        refresh();
}

void PcHistoryPane::refresh()
{
    if (!_recording || !_emulator)
        return;
    if (_emulator->IsRunning() && !_emulator->IsPaused())
    {
        _status->setText("Running: the list follows at the next stop");
        return;
    }
    const PcHistory::Result result = PcHistory::Report(_emulator.get(), PcHistory::kCapacity);
    if (!result.error.empty())
    {
        _status->setText(QString::fromStdString(result.error));
        return;
    }
    const StateNode& report = result.report;
    const StateNode* entries = report.find("entries");
    const size_t count = entries ? entries->items.size() : 0;
    const qulonglong total = static_cast<qulonglong>(report.find("total")->i);
    _status->setText(count == 0 ? QString("Recording: step or run")
                     : total > count ? QString("The newest %1 of %2 instructions").arg(count).arg(total)
                                     : QString("%1 instructions").arg(count));

    QSignalBlocker block(_list);  // a refilled list is not a pick
    _list->clear();
    if (!entries)
        return;
    QList<QTreeWidgetItem*> rows;
    rows.reserve(static_cast<int>(count));
    for (const StateNode& entry : entries->items)
    {
        const uint16_t address = static_cast<uint16_t>(entry.find("address")->i);
        const std::string kind = entry.find("kind")->s;
        const uint8_t page = static_cast<uint8_t>(entry.find("page")->i);
        auto* row = new QTreeWidgetItem();
        row->setText(0, QString("%1").arg(address, 4, 16, QChar('0')).toUpper());
        row->setText(1, QString("%1%2").arg(QString::fromStdString(kind).toUpper()).arg(page));
        row->setText(2, InstructionAt(_emulator.get(), address, kind, page));
        row->setData(0, Qt::UserRole, address);
        rows.append(row);
    }
    _list->addTopLevelItems(rows);
}
