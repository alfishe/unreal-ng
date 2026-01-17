#pragma once
#ifndef DEBUGGERWINDOW_H
#define DEBUGGERWINDOW_H

#include <debugger/breakpointgroupdialog.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/labeldialog.h>
#include <emulator/emulator.h>
#include <emulator/emulatorbinding.h>

#include <QAction>
#include <QDebug>
#include <QToolBar>
#include <QWidget>

#include "3rdparty/message-center/messagecenter.h"
#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/qhexview.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DebuggerWindow;
}
QT_END_NAMESPACE

class Emulator;

class DebuggerWindow : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit DebuggerWindow(Emulator* emulator = nullptr, QWidget* parent = nullptr);
    virtual ~DebuggerWindow() override;

    void setEmulator(Emulator* emulator);  // Legacy - will be removed
    Emulator* getEmulator();

    // Set the EmulatorBinding this window receives state from
    void setBinding(EmulatorBinding* binding);

    void reset();

    /// region <Helper methods>
protected:
    void updateState();
    void loadState();
    void saveState();
    void updateToolbarActions(bool canContinue, bool canPause, bool canStep, bool canReset, bool canManageBreakpoints,
                              bool canManageLabels);
    void clearInterruptBreakpoints();
    /// endregion <Helper methods>

    /// region <QT Helper methods>
protected:
    void dispatchToMainThread(std::function<void()> callback);
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
    void handleMessageBreakpointTriggered(int id, Message* message);
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
    void resetEmulator();
    void showBreakpointManager();
    void showLabelManager();

    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewBank(uint8_t bank);
    void changeMemoryViewAddress(uint8_t* address, size_t size, uint16_t offset = 0, uint16_t currentAddress = 0);

    /// endregion </Event handlers / Slots>

signals:
    // Emitted to child widgets when state changes
    void stateChangedForChildren(EmulatorStateEnum state);
    void readyForChildren();
    void notReadyForChildren();

protected:
    // Constants for breakpoint management
    static constexpr const char* TEMP_BREAKPOINT_GROUP = "TemporaryBreakpoints";  // Group for all temporary breakpoints
    static constexpr const char* STEP_OVER_NOTE = "StepOver";                     // Note for step over breakpoints
    static constexpr const char* STEP_OUT_NOTE = "StepOut";                       // Note for step out breakpoints
    static constexpr const char* IM1_BREAKPOINT_GROUP =
        "_im1_interrupt_handler";  // Group for IM1 interrupt handler breakpoints
    static constexpr const char* IM2_BREAKPOINT_GROUP =
        "_im2_interrupt_handler";  // Group for IM2 interrupt handler breakpoints

    // Fields
    Emulator* _emulator = nullptr;         // Legacy - use m_binding->emulator() instead
    EmulatorBinding* m_binding = nullptr;  // Central state binding
    bool _isAttached = false;
    uint16_t _lastDisassembledPC = 0xFFFF;  // Track last disassembled PC to avoid redundant updates
    EmulatorStateEnum _emulatorState = EmulatorStateEnum::StateUnknown;
    bool _breakpointTriggered = false;
    size_t _curPageOffset;                        // Currently displayed in hex view memory page offset
    uint16_t _stepOutBreakpointID = BRK_INVALID;  // Stores the ID of the temporary breakpoint used for step out

    // Step operation state tracking
    bool _inStepOutOperation = false;               // Flag indicating we're in a step-out operation
    bool _waitingForInterrupt = false;              // Flag indicating we're waiting for an interrupt
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
    QAction* breakpointsAction;
    QAction* labelsAction;
};

#endif  // DEBUGGERWINDOW_H
