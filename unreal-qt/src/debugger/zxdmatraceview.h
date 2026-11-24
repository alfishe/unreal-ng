#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>

#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
class QLabel;
class QTableView;
class QToolButton;
class ZxDmaTraceModel;

/// @file zxdmatraceview.h
/// @brief The NeoGS ZX-DMA trace in the debugger's DMA pane (gui-card-debugger.md B3.2 "[Trace ▸]", neogs-zxdma-design.md
/// §6.4): a toggle under the board that expands a table of the bytes the Spectrum read from or wrote to card RAM through
/// the ZX-DMA window, one row a byte: frame · host T · card cycle · direction · card address (page) · value · wait · host
/// PC; dropped bytes tinted. The rows are the core's report `gs-zxdma` (DeviceState::GsZxDmaTrace), the one every plane
/// reads: nothing of its own. A header line says whether the GS port trace captures, how many bytes it keeps and whether
/// the ring evicted any; Start / Stop / Clear drive that trace through tracecontrol (`gsporttrace`), as the planes do.
///
/// It never reads by itself: its owner's refresh (BoardPane::refreshed, under the owner's lease) appends the rows after
/// the last sequence number it saw (the report's `since`), keeps at most kMaxRows and follows the tail unless the user
/// scrolled up. Collapsed, a refresh reads nothing. A paused machine adds no bytes: the table stays as it is.
class ZxDmaTraceView : public QWidget
{
    Q_OBJECT

public:
    /// The most rows the table keeps (the oldest go first)
    static constexpr int kMaxRows = 4000;
    /// The table's columns
    enum Column
    {
        Frame,
        HostT,
        CardCycle,
        Direction,
        CardAddress,
        Value,
        Wait,
        HostPc,
        ColumnCount
    };

    explicit ZxDmaTraceView(QWidget* parent = nullptr);

    /// Append the bytes recorded since the last refresh (expanded only); `live`: the machine runs (no difference)
    void refresh(EmulatorContext* context, bool live);
    /// Start, stop or clear the GS port trace (`op`: "start", "stop", "clear") on `emulator`, under the caller's lease;
    /// the answer goes to the note line. Start (a new session) and Clear also empty the table
    QString capture(Emulator* emulator, const QString& op);
    /// Forget the rows and the cursor (another machine or card)
    void reset();
    /// A line under the header (a navigation that had nowhere to go)
    void setNote(const QString& text);

    /// region <For the tests>
    QToolButton* toggle() const { return _toggle; }
    bool expanded() const;
    QTableView* table() const { return _table; }
    QString header() const;
    QString note() const;
    QToolButton* startButton() const { return _start; }
    QToolButton* stopButton() const { return _stop; }
    QToolButton* clearButton() const { return _clear; }
    /// The shown text of a cell
    QString cell(int row, Column column) const;
    int rowCount() const;
    /// endregion

signals:
    /// The table was expanded or collapsed: the owner refreshes to fill it at once
    void expandedChanged(bool expanded);
    /// A capture button: the owner calls capture under its lease
    void captureRequested(const QString& op);
    /// A row double-clicked: the host PC of the access (the main CPU's disassembly there)
    void hostPcPicked(uint16_t pc);
    /// A row's card address double-clicked (the card CPU's memory view there, when a window maps it)
    void cardAddressPicked(uint32_t address);

private:
    void ShowHeader(const StateNode& report);

    QToolButton* _toggle = nullptr;
    QWidget* _body = nullptr;
    QLabel* _header = nullptr;
    QLabel* _note = nullptr;
    QToolButton* _start = nullptr;
    QToolButton* _stop = nullptr;
    QToolButton* _clear = nullptr;
    QTableView* _table = nullptr;
    ZxDmaTraceModel* _model = nullptr;
    uint64_t _since = 0;  ///< the report's `last_seq` of the last refresh: the next one's `since`
};
