#pragma once

#include <QString>

#include <vector>

class QWidget;
class ReportPane;

/// @file nextpanes.h
/// @brief The ZX Spectrum Next's panes of the debugger (Next TODO "Qt panels"): NextREG, MMU, Copper, Sprites, DMA, CTC / IM2. Each is a
/// ReportPane over the report every automation plane already serves (`next_regs`, `next_mmu`, `next_copper`, `next_sprites`,
/// `next_dma`, the `interrupts` and `ctc` of `next`): the debugger shows what an agent reads, nothing of its own.
namespace NextPanes
{
struct Pane
{
    QString title;
    QString tooltip;
    ReportPane* pane = nullptr;
};

/// The six panes, children of `parent`, in tab order
std::vector<Pane> Create(QWidget* parent);
}  // namespace NextPanes
