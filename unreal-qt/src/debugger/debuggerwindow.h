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

class Emulator;
class PaneFocus;
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
    /// Toggle an execution breakpoint at the disassembly cursor (the Debug menu, the dbg.breakpoint_toggle_cursor
    /// action); refused while an automation session holds the machine
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
    /// The memory view's first byte and cursor as CPU addresses (none: its page is not mapped now)
    std::optional<uint16_t> memoryTop() const { return memoryViewBase(); }
    std::optional<uint16_t> memoryCursor() const;
    /// Where the memory view goes when the machine stops: "pc" (the default), "sp", "bc", "de", "hl", "ix", "iy" or
    /// "off" (it stays)
    const QString& memoryFollow() const { return _memoryFollow; }
    bool setMemoryFollow(const QString& name);
    /// endregion </The GUI control plane>

    /// region <Helper methods>
protected:
    void updateState();
    /// The memory view at PC's page (a paused machine)
    void showMemoryAtPc();
    /// The memory view's colours: the memory markup's spans on the page shown (a colour per kind, the layer and
    /// title as the tooltip), PC on top while paused; the legend under the view names the kinds shown
    void paintMemoryMetadata();
    /// The CPU address of the memory view's first byte: the window mapping its page now (none: not mapped)
    std::optional<uint16_t> memoryViewBase() const;
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
    void showPcHistory(bool shown);
    /// Device memory / Disk sector on only when the machine has regions / drives
    void updateMachineActions();
    void showPortWatch();
    void showRtc();
    void showPortEvents();
    void showBreakpointManager();
    void showLabelManager();
    void showVisualizationWindow();

    // Pane focus for controllers: disassembly, registers, memory, stack
    void focusNextPane();
    void focusPrevPane();
    void paneKey(int key);  ///< Qt::Key_Up / Down / Left / Right / PageUp / PageDown / Return into the pane in focus

    // The disassembly's GUI actions (controllers/guiactions.cpp: dbg.follow_pc, dbg.scroll_mode_toggle, dbg.goto)
    void followPc();
    void toggleScrollMode();
    void goToDisassemblyAddress(int address);
    /// The memory view's cursor as a Z80 address: the debugger's "memory" cursor (ttd.find_last_write)
    void publishMemoryCursor();

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

    /// The memory view shows another page or its follow mode changed (its cursor: QHexView::positionChanged)
    void memoryViewChanged();

    /// @brief Emitted when the window is shown or hidden (menu toggle, close box,
    /// docking). MainWindow keeps the Debug menu in sync and switches the
    /// emulator's debug instrumentation on only while the window is visible.
    void visibilityChanged(bool visible);

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
    QAction* pcHistoryAction;
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
    class PcHistoryPane* _pcHistoryPane = nullptr;  // in the splitter, right of the disassembly
    QPointer<class PortEventsDialog> _portEventsDialog;  // not modal: open while the program runs

    // Speed control widget
    SpeedControlWidget* m_speedControl = nullptr;
    // The stop line under the speed control: the breakpoint the machine stopped at
    QLabel* _stopBanner = nullptr;
    uint16_t _stopBreakpointId = BRK_INVALID;
    uint64_t _breakpointObserver = 0;  // NC_EXECUTION_BREAKPOINT, filtered by the machine's id
    uint64_t _markupObserver = 0;      // NC_READOUTS_CHANGED of the markup, filtered by the machine's id
    QLabel* _markupLegend = nullptr;   // under the memory view: the kinds it colours
    PaneFocus* _paneFocus = nullptr;
    QString _memoryFollow = QStringLiteral("pc");  // the memory view's register on a stop (memoryFollow)
};

#endif  // DEBUGGERWINDOW_H
