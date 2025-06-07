#include "debuggerwindow.h"

#include "ui_debuggerwindow.h"

#include <Qt>
#include <QBoxLayout>
#include <QColor>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QMessageBox>

#include "debugger/debugmanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpointdialog.h"
#include "debugger/breakpointeditor.h"
#include "debugger/breakpointgroupdialog.h"
#include "debugger/labeleditor.h"

#include "emulator/emulator.h"

/// region <Constructor / destructors>

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget *parent) : QWidget(parent), ui(new Ui::DebuggerWindow)
{
    _emulator = emulator;
    _curPageOffset = 0;  // Initialize to 0 to prevent uninitialized memory access

    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    // Create floating toolbar
    //toolBar = new QToolBar("Debugger toolbar", this);
    //toolBar->setMovable(true);
    //toolBar->setFloatable(true);
    //toolBar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    //toolBar->setWindowFlags(Qt::Tool);
    toolBar = new QToolBar("Debugger toolbar");

    // Set toolbar size
    QSize toolbarSize = QSize(360, 32);
    toolBar->resize(toolbarSize);

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Populate actions
    continueAction = toolBar->addAction("Continue");
    pauseAction = toolBar->addAction("Pause");
    stepInAction = new QAction("Step In", this);
    stepInAction->setIcon(QIcon::fromTheme("debug-step-into"));
    stepInAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(stepInAction, &QAction::triggered, this, &DebuggerWindow::stepIn);
    toolBar->addAction(stepInAction);

    stepOverAction = new QAction("Step Over", this);
    stepOverAction->setIcon(QIcon::fromTheme("debug-step-over"));
    stepOverAction->setShortcut(QKeySequence(Qt::Key_F10));
    connect(stepOverAction, &QAction::triggered, this, &DebuggerWindow::stepOver);
    toolBar->addAction(stepOverAction);

    stepOutAction = new QAction("Step Out", this);
    stepOutAction->setIcon(QIcon::fromTheme("debug-step-out"));
    stepOutAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(stepOutAction, &QAction::triggered, this, &DebuggerWindow::stepOut);
    toolBar->addAction(stepOutAction);

    frameStepAction = toolBar->addAction("Frame step");
    waitInterruptAction = toolBar->addAction("Wait INT");
    resetAction = toolBar->addAction("Reset");
    toolBar->addWidget(spacer);
    labelsAction = new QAction("Labels", this);
    toolBar->addAction(labelsAction);
    breakpointsAction = toolBar->addAction("Breakpoints");

    connect(continueAction, &QAction::triggered, this, &DebuggerWindow::continueExecution);
    connect(pauseAction, &QAction::triggered, this, &DebuggerWindow::pauseExecution);
    connect(frameStepAction, &QAction::triggered, this, &DebuggerWindow::frameStep);
    connect(waitInterruptAction, &QAction::triggered, this, &DebuggerWindow::waitInterrupt);
    connect(resetAction, &QAction::triggered, this, &DebuggerWindow::resetEmulator);
    connect(labelsAction, &QAction::triggered, this, &DebuggerWindow::showLabelManager);
    connect(breakpointsAction, &QAction::triggered, this, &DebuggerWindow::showBreakpointManager);

    // Subscribe to events leading to MemoryView changes
    connect(ui->registersWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this, SLOT(changeMemoryViewZ80Address(uint16_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewBank(uint8_t)), this, SLOT(changeMemoryViewBank(uint8_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)), this, SLOT(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)));
    connect(ui->stackWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this, SLOT(changeMemoryViewZ80Address(uint16_t)));

    // Connect register and stack jump to disassembly signals
    connect(ui->registersWidget, SIGNAL(jumpToAddressInDisassembly(uint16_t)),
            ui->disassemblerWidget, SLOT(goToAddress(uint16_t)));
    connect(ui->stackWidget, SIGNAL(jumpToAddressInDisassembly(uint16_t)),
            ui->disassemblerWidget, SLOT(goToAddress(uint16_t)));

    // Inject toolbar on top of other widget lines
    ui->verticalLayout_2->insertWidget(0, toolBar);

    // Set hex memory viewer to readonly mode
    ui->hexView->setReadOnly(true);

    // Set up hex viewer
    QHexOptions options = ui->hexView->options();
    options.linelength = 8;     // Display 8 hex bytes per line
    options.addresswidth = 4;   // Address is 4 hex digits [0000-FFFF]
    ui->hexView->setOptions(options);


    /// region <Subscribe to events>
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    // Subscribe to emulator state changes
    ObserverCallbackMethod stateCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleEmulatorStateChanged);
    messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    // Subscribe to breakpoint trigger messages
    ObserverCallbackMethod breakpointCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleMessageBreakpointTriggered);
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, observerInstance, breakpointCallback);

    // Subscribe to CPU step messages
    ObserverCallbackMethod cpuStepCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleCPUStepMessage);
    messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, observerInstance, cpuStepCallback);

    /// endregion </Subscribe to events>
}

DebuggerWindow::~DebuggerWindow()
{
    qDebug() << "DebuggerWindow::~DebuggerWindow()";

    // Unsubscribe from all message topics
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    // Unsubscribe from emulator state changes
    ObserverCallbackMethod stateCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleEmulatorStateChanged);
    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    // Unsubscribe from breakpoint trigger messages
    ObserverCallbackMethod breakpointCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleMessageBreakpointTriggered);
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, observerInstance, breakpointCallback);

    // Unsubscribe from CPU step messages
    ObserverCallbackMethod cpuStepCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleCPUStepMessage);
    messageCenter.RemoveObserver(NC_EXECUTION_CPU_STEP, observerInstance, cpuStepCallback);

    delete ui;
}

/// endregion </Constructor / destructors>

void DebuggerWindow::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    if (_emulator)
    {
        // Load debugger state from disk
        loadState();

        // Initially disable all actions, including breakpoints and labels
        // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
        updateToolbarActions(false, false, false, false, false, false);

        // Update the full state which will set the correct button states
        updateState();

        // Set the emulator for the disassembler widget
        if (ui && ui->disassemblerWidget)
        {
            ui->disassemblerWidget->setEmulator(emulator);
        }
    }
    else
    {
        // No emulator available, disable all actions
        // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
        updateToolbarActions(false, false, false, false, false, false);
    }
}

Emulator* DebuggerWindow::getEmulator()
{
    return _emulator;
}

void DebuggerWindow::reset()
{
    ui->registersWidget->reset();
    ui->hexView->reset();

    QHexOptions options = ui->hexView->options();
    options.linelength = 8;
    options.addresswidth = 4;
    options.flags = QHexFlags::HSeparator | QHexFlags::VSeparator;
    ui->hexView->setOptions(options);

    ui->memorypagesWidget->reset();
    ui->stackWidget->reset();

    updateState();
}

/// region <Helper methods>

void DebuggerWindow::clearInterruptBreakpoints()
{
    if (!_emulator)
    {
        return;
    }

    BreakpointManager* breakpointManager = _emulator->GetDebugManager()->GetBreakpointsManager();
    if (!breakpointManager)
    {
        return;
    }

    // Remove all breakpoints in our IM1 and IM2 groups
    breakpointManager->RemoveBreakpointGroup(IM1_BREAKPOINT_GROUP);
    breakpointManager->RemoveBreakpointGroup(IM2_BREAKPOINT_GROUP);
    qDebug() << "Cleared all interrupt breakpoints";

    _waitingForInterrupt = false;
}

void DebuggerWindow::updateState()
{
    qDebug() << "DebuggerWindow::updateState() called - emulator state:" << (_emulator ? getEmulatorStateName(_emulator->GetState()) : "No emulator");
    if (_emulator)
    {
        Z80State* state = _emulator->GetZ80State();

        // Refresh registers widget
        ui->registersWidget->setZ80State(state);
        ui->registersWidget->refresh();

        // Update disassembler widget
        ui->disassemblerWidget->setDisassemblerAddress(state->Z80Registers::pc);
        ui->disassemblerWidget->refresh();

        // Update memory banks widget
        ui->memorypagesWidget->refresh();

        // Update stack widget
        ui->stackWidget->refresh();

        // Update hex viewer widget
        if (true)
        {
            // Getting address of current ROM page
            Memory* memory = _emulator->GetMemory();
            uint16_t pc = state->Z80Registers::pc;
            uint8_t bank = memory->GetZ80BankFromAddress(pc);
            uint16_t addressInBank = pc & 0b0011'1111'1111'1111;
            size_t pageOffset = memory->GetPhysicalOffsetForZ80Bank(bank);
            uint8_t* pagePhysicalAddress = memory->GetPhysicalAddressForZ80Page(bank);

            QHexDocument* document;
            if (pageOffset != _curPageOffset)
            {
                _curPageOffset = pageOffset;

                QByteArray data((const char*)pagePhysicalAddress, 0x4000);
                document = QHexDocument::fromMemory<QMemoryBuffer>(data);
                ui->hexView->setDocument(document);

                // Display 8 hex bytes per line
                QHexOptions options = ui->hexView->options();
                options.linelength = 8;
                ui->hexView->setOptions(options);
            }
            else
            {
                document = ui->hexView->getDocument();
            }

            ui->hexView->gotoOffset(addressInBank);
            QHexMetadata* hexmetadata = ui->hexView->getMetadata();
            hexmetadata->clear();
            hexmetadata->setMetadata(addressInBank, addressInBank + 1, Qt::black, Qt::blue, "JR Z,xx");
            hexmetadata->setMetadata(addressInBank + 1, addressInBank + 2, Qt::black, Qt::green, "");
        }

        ui->hexView->update();
    }
    else
    {
        // No emulator available, disable all actions
        // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
        updateToolbarActions(false, false, false, false, false, false);

        // Update disassembler widget to show detached state
        ui->disassemblerWidget->refresh();
    }
 }

 ///
 /// \brief DebuggerWindow::loadState
 /// Loads up debugger state (including breakpoints)
 void DebuggerWindow::loadState()
 {
    DebugManager& dbgManager = *_emulator->GetDebugManager();
    BreakpointManager& brkManager = *_emulator->GetBreakpointManager();

    /// <Test>
    _emulator->DebugOn();
    //brkManager.AddExecutionBreakpoint(0x272E);  // ROM128K::$272E - MENU_MOVE_UP
    //brkManager.AddExecutionBreakpoint(0x2731);  // ROM128K::$2731 - MENU_MOVE_DOWN

    //brkManager.AddExecutionBreakpoint(0x37A7);  // ROM128K::$37A7 - MENU_MOVE_UP
    //brkManager.AddExecutionBreakpoint(0x37B6);  // ROM128K::$37B6 - MENU_MOVE_DOWN

    //brkManager.AddExecutionBreakpoint(0x38A2);    // ROM48K:$38A2
    /// </Test>
 }

///
/// \brief DebuggerWindow::saveState
/// Persists debugger state (including breakpoints)
void DebuggerWindow::saveState()
{
    // TODO: Implement state persistence
}

///
/// \brief DebuggerWindow::updateToolbarActions
/// Updates the state of all toolbar actions based on emulator state
/// \param canContinue - Enable/disable Continue action
/// \param canPause - Enable/disable Pause action
/// \param canStep - Enable/disable Step actions (Step In, Step Out, etc.)
/// \param canReset - Enable/disable Reset action
/// \param canManageBreakpoints - Enable/disable Breakpoints action
void DebuggerWindow::updateToolbarActions(bool canContinue, bool canPause, bool canStep, bool canReset, bool canManageBreakpoints, bool canManageLabels)
{
    // Update main execution control actions
    continueAction->setEnabled(canContinue);
    pauseAction->setEnabled(canPause);
    resetAction->setEnabled(canReset);

    // Update stepping actions
    stepInAction->setEnabled(canStep);
    stepOverAction->setEnabled(canStep);
    stepOutAction->setEnabled(canStep);
    frameStepAction->setEnabled(canStep);
    waitInterruptAction->setEnabled(canStep);

    // Update breakpoint management
    breakpointsAction->setEnabled(canManageBreakpoints);
    labelsAction->setEnabled(canManageLabels);
 }

 void DebuggerWindow::restoreDeactivatedBreakpoints()
{
    if (_deactivatedBreakpoints.empty() || !_emulator)
        return;

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;

    qDebug() << "Step Over: Restoring" << _deactivatedBreakpoints.size() << "temporarily deactivated breakpoints";

    for (uint16_t id : _deactivatedBreakpoints)
    {
        bpManager->ActivateBreakpoint(id);
        qDebug() << "Step Over: Restored breakpoint with ID:" << id;
    }

    _deactivatedBreakpoints.clear();
    _inStepOverOperation = false;
}

/// endregion </Helper methods>

/// region <QT Helper methods>

/// Dispatch callback execution in QT main thread (GUI rendering)
/// @param callback
void DebuggerWindow::dispatchToMainThread(std::function<void()> callback)
{
    QThread *mainThread = qApp->thread();
    QThread *currentThread = QThread::currentThread();

    if (currentThread == mainThread)
    {
        callback();
    }
    else
    {
        QTimer *timer = new QTimer();
        timer->moveToThread(qApp->thread());
        timer->setSingleShot(true);

        // This lambda will be called from main thread
        QObject::connect(timer, &QTimer::timeout, [=]()
        {
            // Execution will be done in main thread
            try
            {
                callback();
            }
            catch (...)
            {
                // Just to prevent main thread from crashing
            }

            timer->deleteLater();
        });

        // Schedule lambda execution during very next context switching
        QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
    }
}
/// endregion <QT Helper methods>

/// region <Event handlers / Slots>

void DebuggerWindow::handleEmulatorStateChanged(int id, Message* message)
{
    if (message == nullptr || message->obj == nullptr)
        return;

    SimpleNumberPayload *payload = static_cast<SimpleNumberPayload *>(message->obj);
    _emulatorState = static_cast<EmulatorStateEnum>(payload->_payloadNumber);

    qDebug() << "DebuggerWindow::handleEmulatorStateChanged(" << getEmulatorStateName(_emulatorState) << ")";

    dispatchToMainThread([this]()
    {
        switch (_emulatorState)
        {
            case StateUnknown:
            case StateStopped:
                // When emulator is stopped:
                // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
                updateToolbarActions(false, false, false, false, false, false);

                // Emulator already stopped working.
                // Time to disable all rendering activities and set controls to initial inactive state
                _emulator = nullptr;
                reset();
                break;

            case StateInitialized:
            default:
                // When emulator is initialized:
                // (Continue: OFF, Pause: ON, Step: OFF, Reset: OFF, Breakpoints: ON, Labels: ON)
                updateToolbarActions(false, true, false, false, true, true);
                break;

            case StateRun:
            case StateResumed:
                // When emulator is running:
                // (Continue: OFF, Pause: ON, Step: OFF, Reset: ON, Breakpoints: ON)
                updateToolbarActions(false, true, false, true, true, true);
                break;

            case StatePaused:
                // When emulator is paused:
                // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON, Labels: ON)
                updateToolbarActions(true, false, true, true, true, true);
                
                // Scroll to current PC when pausing
                if (_emulator && _emulator->GetZ80State()) {
                    uint16_t pc = _emulator->GetZ80State()->pc;
                    ui->disassemblerWidget->goToAddress(pc);
                }
                break;
        }

        updateState();
    });
}

void DebuggerWindow::handleMessageBreakpointTriggered(int id, Message* message)
{
    Q_UNUSED(id);

    if (!_emulator || !message || !message->obj)
    {
        return;
    }

    SimpleNumberPayload* payload = static_cast<SimpleNumberPayload*>(message->obj);
    if (!payload)
    {
        return;
    }

    uint16_t breakpointID = static_cast<uint16_t>(payload->_payloadNumber);
    BreakpointManager* bpManager = _emulator->GetDebugManager()->GetBreakpointsManager();
    if (!bpManager)
    {
        return;
    }

    // Get all breakpoints and find the one with matching ID
    const auto& allBreakpoints = bpManager->GetAllBreakpoints();
    auto it = allBreakpoints.find(breakpointID);
    if (it == allBreakpoints.end())
    {
        return;
    }
    BreakpointDescriptor* breakpoint = it->second;

    // Handle interrupt breakpoints if we're waiting for an interrupt
    if (_waitingForInterrupt)
    {
        // For IM2, we might hit a vector table read breakpoint first
        if (breakpoint->type == BRK_MEMORY && (breakpoint->memoryType & BRK_MEM_READ))
        {
            EmulatorContext* context = _emulator->GetContext();
            Memory* memory = context->pMemory;
            Z80* z80 = context->pCore->GetZ80();

            // This is a vector table read breakpoint for IM2
            // We need to set an execution breakpoint at the handler address
            if (z80 && z80->im == 2)
            {
                // Read the vector from memory
                uint8_t lsb = memory->DirectReadFromZ80Memory(breakpoint->z80address);
                uint8_t msb = memory->DirectReadFromZ80Memory(breakpoint->z80address + 1);
                uint16_t handlerAddress = (msb << 8) | lsb;

                // Add execution breakpoint at the handler address
                bpManager->AddExecutionBreakpoint(handlerAddress);

                // Continue execution until we hit the handler
                continueExecution();
                return;
            }
        }
        // This is the actual interrupt handler breakpoint
        else if (breakpoint->type == BRK_MEMORY && (breakpoint->memoryType & BRK_MEM_EXECUTE))
        {
            // We've hit our interrupt handler breakpoint
            _waitingForInterrupt = false;

            // Clear all interrupt breakpoints (both IM1 and IM2)
            clearInterruptBreakpoints();

            // Pause the emulator
            _breakpointTriggered = true;
            _emulator->Pause();

            // Update the UI
            updateState();
            return;
        }
    }

    // Handle step over/out breakpoints
    if (breakpoint->note == STEP_OVER_NOTE)
    {
        _inStepOverOperation = false;
        bpManager->RemoveBreakpointByID(breakpointID);
        _stepOverBreakpointID = BRK_INVALID;
        restoreDeactivatedBreakpoints();
    }
    else if (breakpoint->note == STEP_OUT_NOTE)
    {
        _inStepOutOperation = false;
        bpManager->RemoveBreakpointByID(breakpointID);
        _stepOutBreakpointID = BRK_INVALID;
    }

    // Update the UI in the main thread
    dispatchToMainThread([this]()
    {
        // When a breakpoint is hit:
        // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON, Labels: ON)
        updateToolbarActions(true, false, true, true, true, true);
        updateState();
    });
}

void DebuggerWindow::handleCPUStepMessage(int id, Message* message)
{
    dispatchToMainThread([this]()
    {
        updateState();
        
        // After stepping, ensure we're showing the current PC
        if (_emulator && _emulator->GetZ80State()) {
            uint16_t pc = _emulator->GetZ80State()->pc;
            ui->disassemblerWidget->goToAddress(pc);
        }
    });
}

void DebuggerWindow::continueExecution()
{
    qDebug() << "DebuggerWindow::continueExecution()";

    _breakpointTriggered = false;

    if (_emulator && !_emulator->IsRunning())
    {
        _emulator->Resume();

        // When emulator is running:
        // (Continue: OFF, Pause: ON, Step: OFF, Reset: ON, Breakpoints: ON)
        updateToolbarActions(false, true, false, true, true, true);

        // Force immediate update of the disassembler widget state
        ui->disassemblerWidget->refresh();

        updateState();
    }
}

void DebuggerWindow::pauseExecution()
{
    qDebug() << "DebuggerWindow::pauseExecution()";

    if (_emulator && _emulator->IsRunning())
    {
        _emulator->Pause();
        _emulator->DebugOn();

        // When emulator is paused:
        // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON)
        updateToolbarActions(true, false, true, true, true, true);

        updateState();
    }
}

void DebuggerWindow::stepIn()
{
    qDebug() << "DebuggerWindow::stepIn()";

    _breakpointTriggered = false;

    if (_emulator)
    {
        // Execute single Z80 command (Step execution does not trigger any breakpoints)
        bool skipBreakpoints = true;
        _emulator->RunSingleCPUCycle(skipBreakpoints);

        updateState();
    }
}

uint16_t DebuggerWindow::getNextInstructionAddress(uint16_t address)
{
    if (!_emulator)
        return (address + 1) & 0xFFFF;

    Memory* memory = _emulator->GetMemory();
    Z80Disassembler* disassembler = _emulator->GetContext()->pDebugManager->GetDisassembler().get();

    // Read instruction bytes
    uint8_t buffer[4]; // Max instruction length for Z80 is 4 bytes
    for (int i = 0; i < sizeof(buffer); i++)
        buffer[i] = memory->DirectReadFromZ80Memory(address + i);

    // Disassemble the current instruction to get its length
    DecodedInstruction decoded;
    uint8_t instructionLength = 0;
    disassembler->disassembleSingleCommand(buffer, sizeof(buffer), &instructionLength, &decoded);

    // Calculate the next address by adding the instruction length
    return (address + decoded.fullCommandLen) & 0xFFFF;
}

bool DebuggerWindow::shouldStepOver(uint16_t address)
{
    if (!_emulator)
        return false;

    // Get memory and Z80 state
    Memory* memory = _emulator->GetMemory();

    // Read instruction bytes
    uint8_t buffer[4]; // Max instruction length for Z80 is 4 bytes
    for (int i = 0; i < sizeof(buffer); i++)
        buffer[i] = memory->DirectReadFromZ80Memory(address + i);

    // Use the disassembler's helper method to determine if we should step over
    Z80Disassembler* disassembler = _emulator->GetContext()->pDebugManager->GetDisassembler().get();
    return disassembler->shouldStepOver(buffer, sizeof(buffer));
}

void DebuggerWindow::stepOver()
{
    qDebug() << "DebuggerWindow::stepOver()";

    _breakpointTriggered = false;

    if (!_emulator)
        return;

    // Get the current instruction address
    Z80State* z80 = _emulator->GetZ80State();
    uint16_t pc = z80->pc;

    // Determine if this is an instruction we should step over
    if (shouldStepOver(pc))
    {
        // Get the disassembler
        Z80Disassembler* disassembler = _emulator->GetContext()->pDebugManager->GetDisassembler().get();

        // Get the address of the next instruction
        uint16_t nextInstructionAddress = disassembler->getNextInstructionAddress(pc, _emulator->GetMemory());

        // Get the exclusion ranges for step-over
        std::vector<std::pair<uint16_t, uint16_t>> exclusionRanges =
            disassembler->getStepOverExclusionRanges(pc, _emulator->GetMemory(), 5); // Max depth of 5 for nested calls

        // Set a temporary breakpoint at the next instruction and continue execution until we reach it
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();

        // Store breakpoints that we'll need to restore later
        _deactivatedBreakpoints.clear();

        // Find all execution breakpoints within the exclusion ranges
        const BreakpointMapByID& allBreakpoints = bpManager->GetAllBreakpoints();
        for (const auto& [bpId, bp] : allBreakpoints)
        {
            // Only consider active execution breakpoints
            if (bp->active && (bp->type == BRK_MEMORY) && (bp->memoryType & BRK_MEM_EXECUTE))
            {
                // Check if the breakpoint is within any exclusion range
                for (const auto& range : exclusionRanges)
                {
                    if (bp->z80address >= range.first && bp->z80address <= range.second)
                    {
                        // Deactivate the breakpoint temporarily
                        bpManager->DeactivateBreakpoint(bpId);
                        _deactivatedBreakpoints.push_back(bpId);
                        qDebug() << "Step Over: Temporarily deactivated breakpoint at address:"
                                 << QString("0x%1").arg(bp->z80address, 4, 16, QLatin1Char('0')).toUpper();
                        break;
                    }
                }
            }
        }

        // Create a breakpoint descriptor with the note field already set
        BreakpointDescriptor* bpDesc = new BreakpointDescriptor();
        bpDesc->type = BreakpointTypeEnum::BRK_MEMORY;
        bpDesc->memoryType = BRK_MEM_EXECUTE;
        bpDesc->z80address = nextInstructionAddress;
        bpDesc->note = STEP_OVER_NOTE;

        // Add the breakpoint and store its ID
        _stepOverBreakpointID = bpManager->AddBreakpoint(bpDesc);

        // Set its group if successfully added
        if (_stepOverBreakpointID != BRK_INVALID)
        {
            bpManager->SetBreakpointGroup(_stepOverBreakpointID, TEMP_BREAKPOINT_GROUP);
        }

        if (_stepOverBreakpointID != BRK_INVALID)
        {
            // Set flag to indicate we're in a step-over operation
            _inStepOverOperation = true;

            // Continue execution until the breakpoint is hit
            continueExecution();
        }
        else
        {
            qDebug() << "Step Over: Failed to set breakpoint at address:"
                     << QString("0x%1").arg(nextInstructionAddress, 4, 16, QLatin1Char('0')).toUpper();

            // Restore any deactivated breakpoints
            restoreDeactivatedBreakpoints();

            // If we couldn't set the breakpoint, just do a normal step
            stepIn();
        }
    }
    else
    {
        // If it's not a special instruction, just do a normal step
        stepIn();
    }
}

void DebuggerWindow::stepOut()
{
    qDebug() << "DebuggerWindow::stepOut()";

    _breakpointTriggered = false;

    if (_emulator)
    {
        // 1. Read the return address from the stack
        Memory* memory = _emulator->GetMemory();
        Z80State* z80 = _emulator->GetZ80State();
        uint16_t sp = static_cast<uint16_t>(z80->Z80Registers::sp);

        // Read the return address from the stack (first word on the stack)
        uint8_t loByte = memory->DirectReadFromZ80Memory(sp);
        uint8_t hiByte = memory->DirectReadFromZ80Memory(sp + 1);
        uint16_t returnAddress = (hiByte << 8) | loByte;

        qDebug() << "Step Out: Return address found at" << QString("0x%1").arg(returnAddress, 4, 16, QLatin1Char('0')).toUpper();

        // 2. Set a temporary breakpoint at the return address
        BreakpointManager* breakpointManager = _emulator->GetBreakpointManager();

        // Create a breakpoint descriptor with the note field already set
        BreakpointDescriptor* bpDesc = new BreakpointDescriptor();
        bpDesc->type = BreakpointTypeEnum::BRK_MEMORY;
        bpDesc->memoryType = BRK_MEM_EXECUTE;
        bpDesc->z80address = returnAddress;
        bpDesc->note = STEP_OUT_NOTE;

        // Add the breakpoint
        _stepOutBreakpointID = breakpointManager->AddBreakpoint(bpDesc);

        // Set its group if successfully added
        if (_stepOutBreakpointID != BRK_INVALID)
        {
            breakpointManager->SetBreakpointGroup(_stepOutBreakpointID, TEMP_BREAKPOINT_GROUP);
        }

        if (_stepOutBreakpointID != BRK_INVALID)
        {
            // Set flag to indicate we're in a step-out operation
            _inStepOutOperation = true;

            qDebug() << "Step Out: Successfully set breakpoint ID:" << _stepOutBreakpointID << "at address:"
                     << QString("0x%1").arg(returnAddress, 4, 16, QLatin1Char('0')).toUpper();

            // 3. Continue execution until the breakpoint is hit
            continueExecution();
        }
        else
        {
            qDebug() << "Step Out: Failed to set breakpoint at return address:"
                     << QString("0x%1").arg(returnAddress, 4, 16, QLatin1Char('0')).toUpper();

            // If we couldn't set the breakpoint, just do a normal step
            qDebug() << "Step Out: Failed to set breakpoint, falling back to step-in";
            stepIn();
        }
    }

    updateState();
}

void DebuggerWindow::frameStep()
{
    qDebug() << "DebuggerWindow::frameStep()";

    _breakpointTriggered = false;

    updateState();
}

void DebuggerWindow::waitInterrupt()
{
    qDebug() << "DebuggerWindow::waitInterrupt()";

    if (!_emulator)
    {
        qWarning() << "No emulator instance";
        return;
    }

    // Clear any existing interrupt breakpoints
    clearInterruptBreakpoints();

    EmulatorContext* context = _emulator->GetContext();
    Z80* cpu = context->pCore->GetZ80();
    if (!cpu)
    {
        qWarning() << "No CPU instance";
        return;
    }

    DebugManager* debugManager = _emulator->GetDebugManager();
    BreakpointManager* bpManager = debugManager->GetBreakpointsManager();
    Memory* memory = _emulator->GetMemory();

    // Check current interrupt mode
    uint8_t im = cpu->im;

    if (im == 1 || im == 0)  // IM1 or IM0 (both use 0x0038 handler)
    {
        // For IM1/IM0, set breakpoint at 0x0038
        uint16_t breakpointId = bpManager->AddExecutionBreakpoint(0x0038);
        bpManager->SetBreakpointGroup(breakpointId, IM1_BREAKPOINT_GROUP);
        qDebug() << "Set IM1/IM0 interrupt breakpoint at 0x0038";
    }
    else if (im == 2)  // IM2
    {
        // For IM2, the interrupt handler address is read from the vector table at (I << 8) | (data_bus)
        // The data bus can have any value (0-255), so we need to handle all possible vectors
        uint8_t i_reg = cpu->i;
        uint16_t interruptVectorTableBase = (uint16_t)(i_reg) << 8;
        uint8_t lsb = memory->DirectReadFromZ80Memory(interruptVectorTableBase);
        uint8_t msb = memory->DirectReadFromZ80Memory(interruptVectorTableBase + 1);
        uint16_t interruptHandler = (msb << 8) | lsb;

        uint16_t breakpointId = bpManager->AddExecutionBreakpoint(interruptHandler);
        bpManager->SetBreakpointGroup(breakpointId, IM2_BREAKPOINT_GROUP);
        qDebug() << "Set IM2 interrupt handler breakpoint at" << QString("0x%1").arg(interruptHandler, 4, 16, QLatin1Char('0')).toUpper();
    }

    _waitingForInterrupt = true;
    updateState();

    // Continue execution automatically if paused
    if (_emulator && _emulator->IsPaused())
    {
        continueExecution();
    }
}

void DebuggerWindow::resetEmulator()
{
    qDebug() << "DebuggerWindow::resetEmulator()";

    if (_emulator)
    {
        _emulator->Reset();
    }

    // Update debugger state after the reset
    updateState();
}

void DebuggerWindow::showBreakpointManager()
{
    qDebug() << "DebuggerWindow::showBreakpointManager()";

    if (!_emulator)
    {
        QMessageBox::warning(this, "Warning", "No emulator selected");
        return;
    }

    BreakpointDialog dialog(_emulator, this);
    dialog.exec();

    // Update debugger state after dialog closes
    updateState();
}

void DebuggerWindow::changeMemoryViewZ80Address(uint16_t addr)
{
    qDebug("DebuggerWindow::changeMemoryViewZ80Address");

    Memory* memory = _emulator->GetMemory();
    uint8_t bank = memory->GetZ80BankFromAddress(addr);
    _curPageOffset = memory->GetPhysicalOffsetForZ80Bank(bank);
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;
    uint8_t* pageAddress = memory->GetPhysicalAddressForZ80Page(bank);
    size_t size = 0x4000;
    uint16_t offset = bank * 0x4000;

    changeMemoryViewAddress(pageAddress, size, offset, addressInBank);
}

/// Event to change Memory View to one of 4 Z80 memory pages
/// \param page
void DebuggerWindow::changeMemoryViewBank(uint8_t bank)
{
    qDebug("DebuggerWindow::changeMemoryViewBank");

    // Only 4 pages are available (4 x 16Kb pages in Z80 address space)
    bank &= 0b0000'0011;

    // Getting address of specified memory page
    Memory* memory = _emulator->GetMemory();
    _curPageOffset = memory->GetPhysicalOffsetForZ80Bank(bank);
    uint8_t* pageAddress = memory->MapZ80AddressToPhysicalAddress(bank * 0x4000);
    size_t size = 0x4000;
    uint16_t offset = bank * 0x4000;

    changeMemoryViewAddress(pageAddress, size, offset);
}

/// Event to change Memory View
/// \param address Physical address - start of memory view
/// \param size Size of memory view in bytes
/// \param offset Base offset for memory view display
void DebuggerWindow::changeMemoryViewAddress(uint8_t* address, size_t size, uint16_t offset, uint16_t currentAddress)
{
    if (!address || !size)
    {
        qDebug("DebuggerWindow::changeMemoryViewAddress - invalid parameters");
        throw std::logic_error("DebuggerWindow::changeMemoryViewAddress - invalid parameters");
    }

    qDebug("DebuggerWindow::changeMemoryViewAddress - address: %p, size: 0x%04X, offset: 0x%02X, currentAddress: 0x%02X", address, (uint16_t)size, offset, currentAddress);;

    QByteArray data((const char*)address, size);
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(data);
    ui->hexView->setBaseAddress(offset);       // Set base offset for the whole hex view
    ui->hexView->setDocument(document);

    // Note: change offset position only after assigning document to HexView
    // otherwise widget is unaware of the document and where to jump so just skipping the request
    ui->hexView->gotoOffset(currentAddress);       // Position cursor on the byte with offset
    ui->hexView->update();
}

void DebuggerWindow::showLabelManager()
{
    qDebug() << "DebuggerWindow::showLabelManager()";

    if (!_emulator)
    {
        QMessageBox::warning(this, "Warning", "No emulator selected");
        return;
    }

    LabelEditor labelEditor(_emulator->GetDebugManager()->GetLabelManager(), this);
    labelEditor.exec();

    // Update debugger state after dialog closes (if needed)
    updateState(); // Refresh in case labels changed that affect disassembly, etc.
}

/// endregion </Event handlers / Slots>
