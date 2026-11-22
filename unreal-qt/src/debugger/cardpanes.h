#pragma once

#include <QString>

#include <vector>

#include "emulator/emulator.h"

class EmulatorContext;
class QWidget;
class ReportPane;

/// @file cardpanes.h
/// @brief The General Sound card's panes of the debugger (gs-debugger TODO P5.1, gui-card-debugger.md A5.2): Log, Protocol,
/// Firmware, Hardware, Stats. Each is a ReportPane over a report of the device catalog every automation plane serves (`gs-commands`,
/// the `gs` report's protocol, `gs-firmware`, `gs`, `gs-stats`): the debugger shows what an agent reads, nothing of its own.
namespace CardPanes
{
struct Pane
{
    QString title;
    QString tooltip;
    ReportPane* pane = nullptr;
};

/// The five panes, children of `parent`, in tab order
std::vector<Pane> Create(QWidget* parent);

/// The card's line under the debugger's toolbar (gs-debugger A6 S1-S3): the `gs` report's `strip` ("GS Z80 12 MHz · PC
/// #0C32 · cycle 119,985 · MPAG #03 · Symbols: GS v1.05a"); after a stop of the card's (a breakpoint, a step) the CPU
/// that paused and where the main CPU stands in its instruction ("main #8003: T 6 of 11, port write; 2 T past the
/// card"). Empty without a card
QString StripText(EmulatorContext* context, const Emulator::DebugStop& stop);

/// Whether the machine has a General Sound card (classic, lightweight or NeoGS) in its slot: the panes are shown
bool Fitted(EmulatorContext* context);
}  // namespace CardPanes
