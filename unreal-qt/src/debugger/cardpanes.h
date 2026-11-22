#pragma once

#include <QString>

#include <vector>

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

/// Whether the machine has a General Sound card (classic, lightweight or NeoGS) in its slot: the panes are shown
bool Fitted(EmulatorContext* context);
}  // namespace CardPanes
