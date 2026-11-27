#pragma once

#include <optional>

#include <memory>
#include <string>
#include <utility>
#include <vector>
#ifndef DEBUGGERWINDOW_H
#define DEBUGGERWINDOW_H

#include <debugger/breakpoints/breakpointmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorbinding.h>
#include "debugger/breakpointstopline.h"
#include "debugger/cardpanes.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/cpu/z80.h"

#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "3rdparty/message-center/messagecenter.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DebuggerWindow;
}
QT_END_NAMESPACE

class DebuggerCpuTabs;
class Emulator;
class PaneFocus;
class QGroupBox;
class QTabWidget;
class QTimer;
class ReportPane;
class SpeedControlWidget;

class DebuggerWindow : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit DebuggerWindow(std::shared_ptr<Emulator> emulator = nullptr, QWidget* parent = nullptr);

    /// The debugger window a pane sits in, however deep (a pane's parent is its frame); null outside one
    static DebuggerWindow* Of(QWidget* widget);

    /// The panes as control surface contexts (control surfaces architecture.md §6): disassembly `debug.disasm`,
    /// registers `debug.registers`, memory `debug.memory`, stack `debug.stack`
    std::vector<std::pair<QWidget*, std::string>> contextPanes() const;
    virtual ~DebuggerWindow() override;

    void setEmulator(std::shared_ptr<Emulator> emulator);  // Legacy - will be removed
    Emulator* getEmulator();

    // Set the EmulatorBinding this window receives state from
    void setBinding(EmulatorBinding* binding);

    void reset();

    /// @brief Prepare for application shutdown - blocks all refreshes
    void prepareForShutdown();

public slots:
    /// Toggle an execution breakpoint at the disassembly cursor (the Debug menu, the GUI plane's
    /// disassembly.breakpoint_toggle_cursor); refused while an automation session holds the machine
    void toggleBreakpointAtCursor();
    /// The breakpoint list and editor (the Debug menu's Show Breakpoints...)
    void openBreakpoints();
    /// Focus the registers / memory pane (the Debug menu's Show Registers / Show Memory)
    void focusRegistersPane();
    void focusMemoryPane();

public:
    /// region <The GUI control plane's debugger, disassembly and memory (guicontrol/debuggercontrol.cpp)>
    class DisassemblerWidget* disassembler() const;
    class QHexView* memoryView() const;
    /// The panes of the focus ring: "disassembly", "registers", "memory", "stack"
    static const std::vector<QString>& paneNames();
    /// The pane holding the focus, else the one focused last
    QString focusedPane() const;
    bool focusPane(const QString& name);
    void movePaneFocus(int delta);
    void sendPaneKey(int key);
    /// The memory view at a CPU address: its 16 KB page, the cursor on it; false without a machine
    bool showMemoryAt(uint16_t address);
    /// The memory view's first byte and cursor as CPU addresses (none: its page is not mapped now); a card's tab: the
    /// card CPU's 16 KB window shown
    std::optional<uint16_t> memoryTop() const { return memoryViewBase(); }
    std::optional<uint16_t> memoryCursor() const;
    /// Where the memory view goes when the machine stops: "pc" (the default), "sp", "bc", "de", "hl", "ix", "iy" or
    /// "off" (it stays)
    const QString& memoryFollow() const { return _memoryFollow; }
    bool setMemoryFollow(const QString& name);

    /// region <The processor tabs (debuggercpu.h): the main CPU and each sound card CPU fitted>
    /// The CPU in front: the panes, the disassembly's breakpoints, the steps and the time group act on it
    DebugCpuId currentCpu() const;
    /// The CPUs with a tab (the main CPU first); the bar shows with more than one
    std::vector<DebugCpuId> cpus() const;
    /// Put `cpu`'s tab in front; false when it has none (no such card fitted)
    bool selectCpu(DebugCpuId cpu);
    DebuggerCpuTabs* cpuTabs() const { return _cpuTabs; }
    class DebuggerTimeTravel* timeTravel() const { return _timeTravel; }
    /// The card run controls (Run to's card entries, Step other, the CPU shortcuts): gs-debugger TODO P5.4
    class DebuggerCardRun* cardRun() const { return _cardRun; }
    /// A byte as the CPU in front sees it (no side effects)
    uint8_t readCpuByte(uint16_t address);
    /// Step In / Over / Out (`kind` "in", "over", "out") or run to `address` ("to") on the card in front
    /// (RunControl::CardStep: the machine runs until the card ends it, then pauses); Step In in the recorded past is
    /// the time group's card step forward. False with the main CPU in front: the caller steps as before. `error`:
    /// why a card step was refused ("" when it ran)
    bool cardStep(const char* kind, uint16_t address = 0, QString* error = nullptr);
    /// endregion </The processor tabs>
    /// endregion </The GUI control plane>

    /// region <Helper methods>
protected:
    /// All panes read the machine now (a user action that needs them at once)
    void updateState();
    /// One updateState in the event loop's next turn for any number of stop notices (the frame refreshes queued
    /// while a step ran, the step's end, the pause): a burst refreshes the panes once, never once per notice
    void requestUpdateState();
    /// The memory view at PC's page (a paused machine)
    void showMemoryAtPc();
    /// The memory view's colours: the memory markup's spans on the page shown (a colour per kind, the layer and
    /// title as the tooltip), PC on top while paused; the legend under the view names the kinds shown
    void paintMemoryMetadata();
    /// The CPU address of the memory view's first byte: the window mapping its page now (none: not mapped)
    std::optional<uint16_t> memoryViewBase() const;
    /// The panes read the CPU in front (a tab changed, a card fitted or removed): the registers, stack, pages,
    /// disassembly, memory view and the time group
    void retargetPanes();
    /// The registers of the CPU in front for the registers pane (a card's: a copy read now); null without it
    Z80State* cpuState();
    /// The memory view's tooltip for the byte at `offset` in the page: the address, byte and word, the label at or
    /// before it with the distance, the markup block (kind, extent, place in it, source, title, a variable's value)
    /// and the comments on it
    QString memoryToolTip(qint64 offset);
    void loadState();
    void saveState();
    void updateToolbarActions(bool canContinue, bool canPause, bool canStep, bool canReset, bool canManageBreakpoints,
                              bool canManageLabels);
    void clearInterruptBreakpoints();
    /// endregion <Helper methods>

    /// region <QT Helper methods>
protected:
    void dispatchToMainThread(std::function<void()> callback);
    /// The children that keep the machine (speed control, live source, visualization) follow _emulator
    void propagateEmulator();
    /// endregion <QT Helper methods>

    /// region <Event handlers / Slots>
private slots:
    // Binding signal handlers
    void onBindingBound();
    void onBindingUnbound();
    void onBindingStateChanged(EmulatorStateEnum state);
    void onBindingReady();
    void onBindingNotReady();

    // Legacy MessageCenter handlers (to be removed)
    void handleEmulatorStateChanged(int id, Message* message);
    /// A breakpoint of this window's machine fired (GUI thread): the step traps finish, the stop line tells which
    void onBreakpointHit(const BreakpointHit& hit);
    /// The stop line goes when the machine moves on (continue, a step, a reset)
    void clearStopBanner();
    /// The stop line with a message of the debugger's own (a card run that did not get there): plain text
    void showStopNotice(const QString& text);
    void onStopBannerLink(const QString& link);
    void handleCPUStepMessage(int id, Message* message);
    void handleLabelChanged(int id, Message* message);

    // Debug toolbar actions
    void continueExecution();
    void pauseExecution();
    void stepIn();
    void stepOver();
    void stepOut();
    void frameStep();
    void waitInterrupt();

    // Atomic stepping actions
    void runTStates();
    void runToScanline();
    void runNScanlines();
    void runToPixel();
    void runToInterrupt();
    void resetEmulator();
    void portOut();
    void showDeviceMemory();
    void showDiskSector();
    void showDiskFiles();
    void showLiveSource();
    /// Device memory / Disk sector on only when the machine has regions / drives
    void updateMachineActions();
    void showPortWatch();
    void showRtc();
    void showPortEvents();
    void showBreakpointManager();
    void showLabelManager();
    void showVisualizationWindow();

    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewBank(uint8_t bank);
    void changeMemoryViewAddress(uint8_t* address, size_t size, uint16_t offset = 0, uint16_t currentAddress = 0);

    /// endregion </Event handlers / Slots>

signals:
    // Emitted to child widgets when state changes
    void stateChangedForChildren(EmulatorStateEnum state);
    void readyForChildren();
    void notReadyForChildren();

    /// @brief Emitted when screen framebuffer needs to be repainted
    /// (e.g., after speed control stepping). MainWindow connects this
    /// to DeviceScreen::refresh().
    void screenRefreshRequested();

    /// The time travel group's "Show in the TTD panel": MainWindow opens the panel with its Branches row
    void showInTtdPanelRequested();

    /// The NeoGS SD card pane's "Open media panel" (N8): MainWindow shows the media panel on `slot` (`sd.ngs`)
    void mediaPanelRequested(const QString& slot);

    /// The memory view shows another page or its follow mode changed (its cursor: QHexView::positionChanged)
    void memoryViewChanged();

    /// @brief Emitted when the window is shown or hidden (menu toggle, close box,
    /// docking). MainWindow keeps the Debug menu in sync and switches the
    /// emulator's debug instrumentation on only while the window is visible.
    void visibilityChanged(bool visible);

    /// Another CPU's tab is in front (a click, a stop of that CPU, debugger.select_cpu)
    void cpuChanged(DebugCpuId cpu);

public:
    /// region <For the tests>
    QTabWidget* sideTabs() const { return _sideTabs; }
    QLabel* cardStrip() const { return _cardStrip; }
    class TwinTimelineView* twinTimeline() const { return _twinTimeline; }
    QLabel* mainPositionStrip() const { return _mainPositionStrip; }
    QLabel* zxDmaMemoryBanner() const { return _zxDmaMemoryBanner; }
    QLabel* zxDmaDisasmBanner() const { return _zxDmaDisasmBanner; }
    const std::vector<class ReportPane*>& nextPanes() const { return _nextPanes; }
    const std::vector<CardPanes::Pane>& cardPanes() const { return _cardPanes; }
    /// endregion

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    // Constants for breakpoint management
    static constexpr const char* TEMP_BREAKPOINT_GROUP = "TemporaryBreakpoints";  // Group for all temporary breakpoints
    static constexpr const char* STEP_OVER_NOTE = "StepOver";                     // Note for step over breakpoints
    static constexpr const char* STEP_OUT_NOTE = "StepOut";                       // Note for step out breakpoints
    static constexpr const char* IM1_BREAKPOINT_GROUP =
        "_im1_interrupt_handler";  // Group for IM1 interrupt handler breakpoints
    static constexpr const char* IM2_BREAKPOINT_GROUP =
        "_im2_interrupt_handler";  // Group for IM2 interrupt handler breakpoints

    // Fields
    std::shared_ptr<Emulator> _emulator;         // Legacy - use m_binding->emulator() instead
    EmulatorBinding* m_binding = nullptr;  // Central state binding
    EmulatorStateEnum _emulatorState = EmulatorStateEnum::StateUnknown;
    bool _breakpointTriggered = false;
    size_t _curPageOffset;                        // Currently displayed in hex view memory page offset
    uint16_t _stepOutBreakpointID = BRK_INVALID;  // Stores the ID of the temporary breakpoint used for step out

    // Step operation state tracking
    bool _inStepOutOperation = false;               // Flag indicating we're in a step-out operation
    bool _waitingForInterrupt = false;              // Flag indicating we're waiting for an interrupt
    bool _updatePending = false;  ///< requestUpdateState posted its refresh, not run yet
    bool _isClosing = false;                         // Flag indicating window is being destroyed
    bool _speedControlActive = false;                // Speed widget is auto-stepping (set synchronously via signal)
    std::vector<uint16_t> _deactivatedBreakpoints;  // Stores IDs of temporarily deactivated breakpoints

    // UI fields
private:
    Ui::DebuggerWindow* ui;

    QToolBar* toolBar;
    QAction* continueAction;
    QAction* pauseAction;
    QAction* stepInAction;
    QAction* stepOverAction;
    QAction* stepOutAction;
    QAction* frameStepAction;
    QAction* waitInterruptAction;
    QAction* resetAction;
    QAction* portOutAction;
    QAction* deviceMemoryAction;
    QAction* diskSectorAction;
    QAction* diskFilesAction;
    QAction* liveSourceAction;
    QToolButton* machineButton = nullptr;  ///< Machine: Ports, CMOS clock, Port events
    QAction* breakpointsAction;
    QAction* labelsAction;
    QAction* visualizationAction;

    // Atomic stepping dropdown
    QMenu* atomicStepMenu = nullptr;
    QToolButton* atomicStepButton = nullptr;
    QAction* runTStatesAction = nullptr;
    QAction* runToScanlineAction = nullptr;
    QAction* runNScanlinesAction = nullptr;
    QAction* runToPixelAction = nullptr;
    QAction* runToInterruptAction = nullptr;
    
    // Visualization window
    class DebugVisualizationWindow* _visualizationWindow = nullptr;
    class LiveSourceWindow* _liveSourceWindow = nullptr;
    class PcHistoryPane* _pcHistoryPane = nullptr;
    // The splitter's third pane: tabs of every view the machine has (the PC history; a Next's panes; a GS card's)
    class QGroupBox* _sideGroup = nullptr;
    class QTabWidget* _sideTabs = nullptr;
    std::vector<class ReportPane*> _nextPanes;
    std::vector<CardPanes::Pane> _cardPanes;  // the General Sound card's (gs-debugger P5.1; NeoGS boards P5.5)
    class QTimer* _nextPanesTimer = nullptr;  // a live refresh of the report tab in front while the machine runs (5 Hz)
    bool _nextMachine = false;
    // The card panes' CPU tab in front (they follow it: the GS slot's card or the MultiSound's, CardPanes::Front), and
    // which of them were shown last (a change: their tabs shown or hidden, their marks forgotten)
    CardPanes::Follow _cardFollow = std::make_shared<DebugCpuId>(DebugCpuId::Main);
    std::vector<bool> _cardShown;
    CardPanes::Target _cardTarget = CardPanes::Target::None;
    /// Refresh the report or board tab in front (a Next's, a card's; when shown); `live`: the machine runs
    void refreshNextPanes(bool live);
    /// A board pane's action (NeoGS SD insert / eject, flash save) under the context's lease, then its refresh
    void runBoardAction(class BoardPane* pane, const QString& board, const QString& action, const QString& arg);
    /// The NeoGS DMA pane's ZX-DMA trace: its capture buttons under the lease, a row's host PC to the main CPU's
    /// disassembly, its card address to the card CPU's memory view (when a window maps the page)
    void connectZxDmaTrace(class ZxDmaTraceView* trace);
    /// The GS monitor tab: its Pause / Continue to the window's, its Turn on under the lease
    void connectGsMonitor(class GsMonitorPane* monitor);
    /// The GS send tab: its Send under the lease, then refreshed
    void connectGsSend(class GsSendPane* pane);
    /// Bring the GS send tab to the front (the monitor's Send..., Ctrl+Shift+S) when a card shows it
    void showGsSend();
    /// An analysis tab's Turn on (GS unknown, GS writers): its histories to ring under a lease, then refreshed
    void connectGsAnalysis(class GsAnalysisPane* pane);
    /// A card CPU analysis tab's buttons (Profiler, Events, Coverage, Heat map): its verb under a lease, then refreshed
    void connectGsCpuAnalysis(class GsCpuAnalysisPane* pane);
    /// The Next tabs on a Next only, the card tabs with a GS card; the PC history records while in front, the live
    /// refresh while a report tab is
    void updateSidePane();
    class DebuggerCardRun* _cardRun = nullptr;         // Run to's card entries, Step other, Ctrl+1 / Ctrl+2
    class DebuggerTimeTravel* _timeTravel = nullptr;   // the toolbar's time controls (debuggers.md §4)  // in the splitter, right of the disassembly
    QPointer<class PortEventsDialog> _portEventsDialog;  // not modal: open while the program runs

    // Speed control widget
    SpeedControlWidget* m_speedControl = nullptr;
    // The stop line under the speed control: the breakpoint the machine stopped at
    QLabel* _stopBanner = nullptr;
    // A sound card's line (gs-debugger A6 S1-S3): its CPU, PC, cycle, page, symbols; whose stop and where the main CPU
    // stands in its instruction. Shown at a stop while a card is fitted
    QLabel* _cardStrip = nullptr;
    void updateCardStrip();
    // The inside-an-instruction strip on top of the main registers (A6 S3): where the main CPU stands in its instruction
    // at a card's stop (the card line leaves it out while this shows it)
    QLabel* _mainPositionStrip = nullptr;
    // The ZX-DMA banner over the main CPU's memory view and disassembly (A6 S4) while the NeoGS module diverts or
    // watches the reads at #0000-#3FFF; in Divert with the ROM paged the disassembly's rows there are marked
    QLabel* _zxDmaMemoryBanner = nullptr;
    QLabel* _zxDmaDisasmBanner = nullptr;
    CardPanes::Lines cardLines() const { return {_cardStrip, _mainPositionStrip, _zxDmaMemoryBanner, _zxDmaDisasmBanner}; }
    // The twin timeline under the card line (A5.1): shown while a card CPU is fitted
    class TwinTimelineView* _twinTimeline = nullptr;
    void updateTwinTimeline(EmulatorContext* context);
    // The card line and the banner while the machine runs (4 Hz, while the window is shown)
    CardPanes::LiveLine* _liveLine = nullptr;
    uint16_t _stopBreakpointId = BRK_INVALID;
    DebugCpuId _stopBreakpointCpu = DebugCpuId::Main;  // whose breakpoint the stop line names (its manager)
    uint64_t _breakpointObserver = 0;  // NC_EXECUTION_BREAKPOINT, filtered by the machine's id
    uint64_t _markupObserver = 0;      // NC_READOUTS_CHANGED of the markup, filtered by the machine's id
    QLabel* _markupLegend = nullptr;   // under the memory view: the kinds it colours
    PaneFocus* _paneFocus = nullptr;
    QString _memoryFollow = QStringLiteral("pc");  // the memory view's register on a stop (memoryFollow)
    DebuggerCpuTabs* _cpuTabs = nullptr;            // the processor tabs above the panes
    Z80State _cardState{};                          // the card in front's registers, for the registers pane
    std::optional<uint16_t> _cardMemoryBase;        // the card CPU's 16 KB window the memory view shows
    QString _memoryTitle;                           // the panes' titles of the main CPU
    QString _disasmTitle;
    QString _registersTitle;
    QString _stackTitle;
    QString _pagesTitle;
};

#endif  // DEBUGGERWINDOW_H
