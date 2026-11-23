#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "debugger/targets/debugcpuid.h"
#include "emulator/emulator.h"

class BoardPane;
class EmulatorContext;
class GsLwPane;
class QWidget;
class ReportPane;
class ZxDmaTraceView;

/// @file cardpanes.h
/// @brief The General Sound card's panes of the debugger (gs-debugger TODO P5.1, gui-card-debugger.md A5.2): Log, Protocol,
/// Firmware, Hardware, Stats. Each is a ReportPane over a report of the device catalog every automation plane serves (`gs-commands`,
/// the `gs` report's protocol, `gs-firmware`, `gs`, `gs-stats`): the debugger shows what an agent reads, nothing of its own.
/// On a NeoGS (P5.5, gui-card-debugger.md B3.1-B3.3) the NeoGS board replaces the Hardware pane and two panes join: DMA and
/// SD · MP3 · Flash, each a BoardPane over the boards the planes read (`neogs`, `neogs_dma`, `neogs_sd`, `neogs_mp3`,
/// `neogs_flash`) with the counters of the statistics block (`gs_stats`). Under the DMA board its ZX-DMA trace (Trace ▸,
/// B3.2): a ZxDmaTraceView over the `gs-zxdma` report.
/// On the lightweight player (no CPU, gui-card-debugger.md A10, requirements W1-W5) two read-only panes join: GS interpreter
/// and GS player, each a GsLwPane over the `gs` report's `interpreter` and `player`.
/// The ZX-MultiSound's General Sound (CPU `ms`) has the same panes over its reports (`ms-gs-commands`, the `multisound`
/// report's `gs`, `ms-gs-firmware`, `ms-gs-stats`). Which card the panes show (Front()): the MultiSound's while its CPU tab is
/// in front, the GS slot's card otherwise; the MultiSound's when the GS slot is empty.
namespace CardPanes
{
/// Which card a pane is shown with
enum class Card
{
    Any,      ///< every GS-slot card (classic, lightweight, NeoGS)
    NotNeoGS, ///< the classic card and the lightweight player (the NeoGS board replaces it)
    NeoGS,    ///< a NeoGS only
    Lightweight,  ///< the lightweight player only (its interpreter and player, A10)
};

/// The card the panes show
enum class Target
{
    None,        ///< no General Sound anywhere
    Slot,        ///< the card in the GS slot (classic, lightweight, NeoGS): CPU `gs`
    MultiSound,  ///< the ZX-MultiSound's General Sound: CPU `ms`
};

/// The debugger CPU tab in front, shared by the panes' builders: the window sets it when its CPU tab changes
using Follow = std::shared_ptr<DebugCpuId>;

struct Pane
{
    QString title;
    QString tooltip;
    ReportPane* pane = nullptr;  ///< a report pane, or
    BoardPane* board = nullptr;  ///< a board pane, or (inspector)
    Card card = Card::Any;
    GsLwPane* inspector = nullptr;  ///< a lightweight player's pane
    ZxDmaTraceView* trace = nullptr;  ///< the board pane's ZX-DMA trace (the NeoGS DMA pane's)
    QWidget* widget() const;
};

/// The panes, children of `parent`, in tab order: GS log, GS protocol, GS firmware, GS hardware, NeoGS hardware,
/// NeoGS DMA, NeoGS SD · MP3 · Flash, GS stats, GS interpreter, GS player. `follow`: the CPU tab in front the card-neutral
/// panes follow (null: the main CPU's, i.e. the GS slot's card, else the MultiSound's)
std::vector<Pane> Create(QWidget* parent, Follow follow = {});

/// The card's line under the debugger's toolbar (gs-debugger A6 S1-S3): the `gs` report's `strip` ("GS Z80 12 MHz · PC
/// #0C32 · cycle 119,985 · MPAG #03 · Symbols: GS v1.05a"); after a stop of the card's (a breakpoint, a step) the CPU
/// that paused and where the main CPU stands in its instruction ("main #8003: T 6 of 11, port write; 2 T past the
/// card"). Of the card Front() picks for `front`. Empty without a card
QString StripText(EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front = DebugCpuId::Main);

/// Whether the machine has a General Sound: a card in its GS slot (classic, lightweight or NeoGS) or a ZX-MultiSound
/// carrying one. The panes are shown
bool Fitted(EmulatorContext* context);
/// Whether the card in the slot is a NeoGS: its panes are shown, the classic Hardware pane is not
bool NeoGSFitted(EmulatorContext* context);
/// Whether the card in the slot is the lightweight player (no CPU): the interpreter and player panes are shown
bool LightweightFitted(EmulatorContext* context);
/// Whether a ZX-MultiSound carrying a General Sound (CPU `ms`) is fitted
bool MultiSoundFitted(EmulatorContext* context);
/// The card the panes show with CPU tab `front` in front: the MultiSound's with `ms` in front, or with no card in the GS
/// slot; the GS slot's card otherwise
Target Front(EmulatorContext* context, DebugCpuId front = DebugCpuId::Main);
/// Where the GS-slot NeoGS's CPU sees card RAM address `cardAddress` (21 bits, wrapped to the card's RAM as the ZX-DMA
/// module does) now: the CPU address in the first of its four windows that maps that RAM page (a flash window maps
/// none). Nothing when no window maps it, or without a NeoGS
std::optional<uint16_t> CardCpuAddress(EmulatorContext* context, uint32_t cardAddress);
/// Whether pane `card` is shown on the machine with CPU tab `front` in front (the MultiSound's GS: the classic card's)
bool Shown(Card card, EmulatorContext* context, DebugCpuId front = DebugCpuId::Main);
}  // namespace CardPanes
