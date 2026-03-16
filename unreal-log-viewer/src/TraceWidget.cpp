#include "TraceWidget.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>

#include <cstring>

#include "emulator/monitoring/manifest.h"

using namespace monitoring;

static const char* cfTypeNames[] = {
    "JP", "JR", "CALL", "RST", "RET", "RETI", "DJNZ"
};

// Control-flow type colors
static const QColor COLOR_CF_JUMP(255, 200, 50);      // JP / JR — yellow
static const QColor COLOR_CF_CALL(80, 140, 255);      // CALL — blue
static const QColor COLOR_CF_RST(255, 160, 50);       // RST — orange
static const QColor COLOR_CF_RET(80, 200, 80);        // RET / RETI — green
static const QColor COLOR_CF_DJNZ(0, 200, 200);       // DJNZ — cyan
static const QColor COLOR_CF_DEFAULT(180, 180, 180);   // Unknown — gray

static QColor cfTypeColor(uint8_t type)
{
    switch (type)
    {
        case 0: return COLOR_CF_JUMP;     // JP
        case 1: return COLOR_CF_JUMP;     // JR
        case 2: return COLOR_CF_CALL;     // CALL
        case 3: return COLOR_CF_RST;      // RST
        case 4: return COLOR_CF_RET;      // RET
        case 5: return COLOR_CF_RET;      // RETI
        case 6: return COLOR_CF_DJNZ;     // DJNZ
        default: return COLOR_CF_DEFAULT;
    }
}

TraceWidget::TraceWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _tabs = new QTabWidget(this);

    // Call trace tab
    auto* callPage = new QWidget();
    auto* callLayout = new QVBoxLayout(callPage);
    callLayout->setContentsMargins(0, 0, 0, 0);

    _callInfo = new QLabel("No call trace data", callPage);
    callLayout->addWidget(_callInfo);

    _callTable = new QTableWidget(callPage);
    _callTable->setColumnCount(7);
    _callTable->setHorizontalHeaderLabels({"PC", "Target", "Type", "Opcode", "Flags", "SP", "Loops"});
    _callTable->setFont(QFont("Menlo", 10));
    _callTable->setAlternatingRowColors(true);
    _callTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _callTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _callTable->horizontalHeader()->setStretchLastSection(true);
    _callTable->verticalHeader()->setDefaultSectionSize(20);
    _callTable->verticalHeader()->hide();
    callLayout->addWidget(_callTable, 1);

    _tabs->addTab(callPage, "Call Trace");

    // Opcode trace tab
    auto* opPage = new QWidget();
    auto* opLayout = new QVBoxLayout(opPage);
    opLayout->setContentsMargins(0, 0, 0, 0);

    _opcodeInfo = new QLabel("No opcode trace data", opPage);
    opLayout->addWidget(_opcodeInfo);

    _opcodeTable = new QTableWidget(opPage);
    _opcodeTable->setColumnCount(7);
    _opcodeTable->setHorizontalHeaderLabels({"PC", "Prefix", "Opcode", "Flags", "A", "Frame", "T-state"});
    _opcodeTable->setFont(QFont("Menlo", 10));
    _opcodeTable->setAlternatingRowColors(true);
    _opcodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _opcodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _opcodeTable->horizontalHeader()->setStretchLastSection(true);
    _opcodeTable->verticalHeader()->setDefaultSectionSize(20);
    _opcodeTable->verticalHeader()->hide();
    opLayout->addWidget(_opcodeTable, 1);

    _tabs->addTab(opPage, "Opcode Trace");

    layout->addWidget(_tabs);

    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &TraceWidget::onRefresh);
}

void TraceWidget::attachToShm(void* base, size_t size)
{
    _shmBase = base;
    _shmSize = size;
    _refreshTimer->start(REFRESH_MS);
}

void TraceWidget::detachFromShm()
{
    _refreshTimer->stop();
    _shmBase = nullptr;
    _shmSize = 0;
    _fifoActive = false;
    _frameCounter = 0;
    _callTable->setRowCount(0);
    _opcodeTable->setRowCount(0);
    _callInfo->setText("No call trace data");
    _opcodeInfo->setText("No opcode trace data");
}

void TraceWidget::onFrameReady()
{
    _frameCounter++;
    if (_frameCounter % FRAME_DIVISOR != 0)
        return;
    onRefresh();
}

void TraceWidget::setFifoActive(bool active)
{
    _fifoActive = active;
    if (_shmBase)
        _refreshTimer->start(active ? FALLBACK_MS : REFRESH_MS);
}

void TraceWidget::onRefresh()
{
    if (!_shmBase)
        return;
    refreshCallTrace();
    refreshOpcodeTrace();
}

void TraceWidget::refreshCallTrace()
{
    auto* manifest = static_cast<ManifestHeader*>(_shmBase);

    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::CALL_TRACE)
            continue;

        uint64_t before = desc->epoch.load(std::memory_order_acquire);
        if (before == EPOCH_UPDATING)
            return;

        auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
        if (!data) return;

        CallTraceHeader hdr;
        std::memcpy(&hdr, data, sizeof(hdr));

        uint32_t count = qMin(hdr.entry_count, hdr.max_entries);
        if (count > 512) count = 512;

        const auto* entries = reinterpret_cast<const CallTraceEntryPOD*>(
            data + sizeof(CallTraceHeader));

        // Local copy for torn-read safety
        std::vector<CallTraceEntryPOD> local(count);
        if (count > 0)
            std::memcpy(local.data(), entries, count * sizeof(CallTraceEntryPOD));

        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t after = desc->epoch.load(std::memory_order_relaxed);
        if (before != after) return;

        _callInfo->setText(QString("Entries: %1/%2  Cold: %3  Hot: %4  %5")
            .arg(count).arg(hdr.max_entries)
            .arg(hdr.cold_size).arg(hdr.hot_size)
            .arg(hdr.session_active ? "Active" : "Inactive"));

        _callTable->setRowCount(static_cast<int>(count));
        for (uint32_t r = 0; r < count; r++)
        {
            const auto& e = local[r];
            const char* typeName = e.type < 7 ? cfTypeNames[e.type] : "?";
            QColor color = cfTypeColor(e.type);

            QString opcBytes;
            for (int b = 0; b < e.opcode_len && b < 4; b++)
                opcBytes += QString("%1 ").arg(e.opcode_bytes[b], 2, 16, QChar('0'));

            auto setCell = [&](int col, const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setForeground(color);
                _callTable->setItem(r, col, item);
            };

            setCell(0, QString("%1").arg(e.m1_pc, 4, 16, QChar('0')));
            setCell(1, QString("%1").arg(e.target_addr, 4, 16, QChar('0')));
            setCell(2, typeName);
            setCell(3, opcBytes.trimmed());
            setCell(4, QString("%1").arg(e.flags, 2, 16, QChar('0')));
            setCell(5, QString("%1").arg(e.sp, 4, 16, QChar('0')));
            setCell(6, e.loop_count > 1 ? QString("x%1").arg(e.loop_count) : "");
        }
        return;
    }
    _callInfo->setText("CALL_TRACE section not found");
}

void TraceWidget::refreshOpcodeTrace()
{
    auto* manifest = static_cast<ManifestHeader*>(_shmBase);

    for (uint16_t i = 0; i < manifest->section_count; i++)
    {
        auto* desc = getDescriptor(_shmBase, manifest, i);
        if (!desc || desc->type != SectionType::OPCODE_TRACE)
            continue;

        uint64_t before = desc->epoch.load(std::memory_order_acquire);
        if (before == EPOCH_UPDATING)
            return;

        auto* data = static_cast<const uint8_t*>(getSectionData(_shmBase, desc));
        if (!data) return;

        OpcodeTraceHeader hdr;
        std::memcpy(&hdr, data, sizeof(hdr));

        uint32_t count = qMin(hdr.entry_count, hdr.max_entries);
        if (count > 1024) count = 1024;

        const auto* entries = reinterpret_cast<const OpcodeTraceEntryPOD*>(
            data + sizeof(OpcodeTraceHeader));

        std::vector<OpcodeTraceEntryPOD> local(count);
        if (count > 0)
            std::memcpy(local.data(), entries, count * sizeof(OpcodeTraceEntryPOD));

        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t after = desc->epoch.load(std::memory_order_relaxed);
        if (before != after) return;

        _opcodeInfo->setText(QString("Entries: %1/%2  Total exec: %3  %4")
            .arg(count).arg(hdr.max_entries)
            .arg(hdr.total_executions)
            .arg(hdr.session_active ? "Active" : "Inactive"));

        _opcodeTable->setRowCount(static_cast<int>(count));
        for (uint32_t r = 0; r < count; r++)
        {
            const auto& e = local[r];

            QString prefixStr;
            if (e.prefix == 0) prefixStr = "-";
            else prefixStr = QString("%1").arg(e.prefix, 0, 16).toUpper();

            _opcodeTable->setItem(r, 0, new QTableWidgetItem(
                QString("%1").arg(e.pc, 4, 16, QChar('0'))));
            _opcodeTable->setItem(r, 1, new QTableWidgetItem(prefixStr));
            _opcodeTable->setItem(r, 2, new QTableWidgetItem(
                QString("%1").arg(e.opcode, 2, 16, QChar('0'))));
            _opcodeTable->setItem(r, 3, new QTableWidgetItem(
                QString("%1").arg(e.flags, 2, 16, QChar('0'))));
            _opcodeTable->setItem(r, 4, new QTableWidgetItem(
                QString("%1").arg(e.a, 2, 16, QChar('0'))));
            _opcodeTable->setItem(r, 5, new QTableWidgetItem(
                QString::number(e.frame)));
            _opcodeTable->setItem(r, 6, new QTableWidgetItem(
                QString::number(e.tState)));
        }
        return;
    }
    _opcodeInfo->setText("OPCODE_TRACE section not found");
}
