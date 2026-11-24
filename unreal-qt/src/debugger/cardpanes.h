#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "debugger/targets/debugcpuid.h"
#include "emulator/emulator.h"

class BoardPane;
class EmulatorContext;
class GsLwPane;
class GsMonitorPane;
class QLabel;
class QTimer;
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
    GsMonitorPane* monitor = nullptr;  ///< the protocol monitor (P5.6, A5.3), else one of the above
    QWidget* widget() const;
};

/// The SD · MP3 · Flash pane's button on the SD card (N8, BoardPane::uiActionRequested of board `neogs_sd`): the owner
/// opens the media panel on slot kSdSlot
inline constexpr const char* kOpenMediaPanel = "open_media_panel";
inline constexpr const char* kSdSlot = "sd.ngs";

/// The panes, children of `parent`, in tab order: GS log, GS protocol, GS monitor (the protocol monitor: the strip, the
/// error tile, the log following its tail, Pause / Continue; P5.6), GS firmware, GS hardware, NeoGS hardware,
/// NeoGS DMA, NeoGS SD · MP3 · Flash, GS stats, GS interpreter, GS player. `follow`: the CPU tab in front the card-neutral
/// panes follow (null: the main CPU's, i.e. the GS slot's card, else the MultiSound's)
std::vector<Pane> Create(QWidget* parent, Follow follow = {});

/// The card's line under the debugger's toolbar (gs-debugger A6 S1-S3): the `gs` report's `strip` ("GS Z80 12 MHz · PC
/// #0C32 · cycle 119,985 · MPAG #03 · Symbols: GS v1.05a"); after a stop of the card's (a breakpoint, a step) the CPU
/// that paused and where the main CPU stands in its instruction ("main #8003: T 6 of 11, port write; 2 T past the
/// card"). Of the card Front() picks for `front`. Empty without a card. `withMainPosition` false: the main CPU's position
/// is left out (the S3 strip on top of the main registers shows it: MainPositionStrip)
QString StripText(EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front = DebugCpuId::Main,
                  bool withMainPosition = true);

/// The inside-an-instruction strip on top of the main registers (gui-card-debugger.md A6 S3) after a card's stop that
/// knows where the main CPU stands: its instruction, the T of its total with the device /WAIT ("+ 3 T wait", ZX-DMA),
/// the bus cycle under way and the gap to the card ("in #8003 LD A,(HL) · T 6 of 7 + 3 T wait · memory read · 2 T past
/// the card"). Empty when the stop is not a card's or the position is not known
QString MainPositionStrip(EmulatorContext* context, const Emulator::DebugStop& stop);
/// The same for a position as it is (MainPosition::known not checked); `context` disassembles the instruction (null:
/// its address only)
QString MainPositionStrip(EmulatorContext* context, const MainPosition& main);

/// The ZX-DMA banner over the main CPU's memory view and disassembly (gui-card-debugger.md A6 S4, neogs-zxdma-design
/// §6.2): DeviceState::ZxDmaBanner of the GS-slot NeoGS (Divert, Watch), empty when the module is off or without a
/// NeoGS. A peek: no DMA byte consumed
QString ZxDmaBanner(EmulatorContext* context);
/// Whether the main CPU's reads at #0000-#3FFF come from the card now (Divert with the host's ROM paged): the main
/// disassembly marks the range ("bytes come from the card at run time")
bool ZxDmaDivertsReads(EmulatorContext* context);

/// The debugger window's lines about the card (A6): the card line under the stop banner (S1-S3), the S3 strip on top of
/// the main registers, the ZX-DMA banners over the main memory view and disassembly (S4). Any may be null
struct Lines
{
    QLabel* card = nullptr;
    QLabel* mainPosition = nullptr;
    QLabel* memoryBanner = nullptr;
    QLabel* disasmBanner = nullptr;
};
/// Fill and show / hide `lines` for CPU tab `front` in front: at a stop (`live` false) from `stop`; while the machine
/// runs (`live`) without a stop (no "paused by", no main position). The S3 strip shows while the main CPU's tab is in
/// front and the stop knows the main position; the card line then leaves it out. The banners: the main CPU's tab only.
/// True when the main disassembly marks #0000-#3FFF (ZxDmaDivertsReads, main in front)
bool FillLines(const Lines& lines, EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front, bool live);
/// FillLines while the machine runs
bool FillLiveLines(const Lines& lines, EmulatorContext* context, DebugCpuId front);

/// The card line while the machine runs (gs-debugger P5.2 left): every `intervalMs` while `window` is visible and the
/// machine `emulator` gives runs with a General Sound fitted, `read` is called with its context under a lease (the
/// reports' side-effect-free reads the card panes' live refresh uses: the emulation thread is never paused or parked for
/// it). Nothing while paused or stepped by automation: the stop's refresh fills the line
class LiveLine : public QObject
{
    Q_OBJECT
public:
    static constexpr int kIntervalMs = 250;  ///< 4 Hz
    LiveLine(QWidget* window, std::function<std::shared_ptr<Emulator>()> emulator, std::function<void(EmulatorContext*)> read,
             int intervalMs = kIntervalMs);
    /// The timer runs while the window is visible; the window calls it as it is shown or hidden
    void update();
    bool active() const;
    /// Reads done (for the tests)
    int reads() const { return _reads; }

private:
    void tick();
    QWidget* _window = nullptr;
    std::function<std::shared_ptr<Emulator>()> _emulator;
    std::function<void(EmulatorContext*)> _read;
    QTimer* _timer = nullptr;
    int _reads = 0;
};

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
