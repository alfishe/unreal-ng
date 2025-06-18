#pragma once
#ifndef DEBUGGERWINDOW_H
#define DEBUGGERWINDOW_H

#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/breakpointgroupdialog.h>
#include <debugger/labeldialog.h>
#include <emulator/emulator.h>

#include <QAction>
#include <QDebug>
#include <QToolBar>
#include <QWidget>

#include "3rdparty/message-center/messagecenter.h"
#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/qhexview.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DebuggerWindow; }
QT_END_NAMESPACE

class Emulator;

class DebuggerWindow : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit DebuggerWindow(Emulator* emulator = nullptr, QWidget *parent = nullptr);
    virtual ~DebuggerWindow() override;

    void setEmulator(Emulator* emulator);
    Emulator* getEmulator();

    void reset();

    /// region <Helper methods>
protected:
    void updateState();
    void loadState();
    void saveState();
    void updateToolbarActions(bool canContinue, bool canPause, bool canStep, bool canReset, bool canManageBreakpoints, bool canManageLabels);
    void clearInterruptBreakpoints();
    /// endregion <Helper methods>

    /// region <QT Helper methods>
protected:
    void dispatchToMainThread(std::function<void()> callback);
    /// endregion <QT Helper methods>

    /// region <Event handlers / Slots>
private slots:
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
    void showVisualizationWindow();

    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewBank(uint8_t bank);
    void changeMemoryViewAddress(uint8_t* address, size_t size, uint16_t offset = 0, uint16_t currentAddress = 0);

    /// endregion </Event handlers / Slots>

signals:

protected:
    // Constants for breakpoint management
    static constexpr const char* TEMP_BREAKPOINT_GROUP = "TemporaryBreakpoints";  // Group for all temporary breakpoints
    static constexpr const char* STEP_OVER_NOTE = "StepOver";                     // Note for step over breakpoints
    static constexpr const char* STEP_OUT_NOTE = "StepOut";                       // Note for step out breakpoints
    static constexpr const char* IM1_BREAKPOINT_GROUP = "_im1_interrupt_handler";  // Group for IM1 interrupt handler breakpoints
    static constexpr const char* IM2_BREAKPOINT_GROUP = "_im2_interrupt_handler";  // Group for IM2 interrupt handler breakpoints
    
    // Helper method to determine if an instruction should be stepped over
    bool shouldStepOver(uint16_t address);
    
    // Helper method to get the address of the next instruction
    uint16_t getNextInstructionAddress(uint16_t address);
    
    // Helper method to restore deactivated breakpoints
    void restoreDeactivatedBreakpoints();
    
    // Fields
    Emulator* _emulator = nullptr;
    EmulatorStateEnum _emulatorState = EmulatorStateEnum::StateUnknown;
    bool _breakpointTriggered = false;
    size_t _curPageOffset;  // Currently displayed in hex view memory page offset
    uint16_t _stepOutBreakpointID = BRK_INVALID;  // Stores the ID of the temporary breakpoint used for step out
    uint16_t _stepOverBreakpointID = BRK_INVALID;  // Stores the ID of the temporary breakpoint used for step over
    
    // Step operation state tracking
    bool _inStepOutOperation = false;  // Flag indicating we're in a step-out operation
    bool _inStepOverOperation = false;  // Flag indicating we're in a step-over operation
    bool _waitingForInterrupt = false;  // Flag indicating we're waiting for an interrupt
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
    QAction* visualizationAction;
    
    // Visualization window
    class DebugVisualizationWindow* _visualizationWindow = nullptr;
};

#endif // DEBUGGERWINDOW_H
