#include "debuggerwindow.h"

#include "ui_debuggerwindow.h"

#include <Qt>
#include <QBoxLayout>
#include <QColor>

#include "debugger/debugmanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulator.h"

/// region <Constructor / destructors>

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget *parent) : QWidget(parent), ui(new Ui::DebuggerWindow)
{
    _emulator = emulator;

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

    // Populate actions
    continueAction = toolBar->addAction("Continue");
    pauseAction = toolBar->addAction("Pause");
    cpuStepAction = toolBar->addAction("CPU step");
    frameStepAction = toolBar->addAction("Frame step");
    waitInterruptAction = toolBar->addAction("Wait INT");
    resetAction = toolBar->addAction("Reset");

    connect(continueAction, &QAction::triggered, this, &DebuggerWindow::continueExecution);
    connect(pauseAction, &QAction::triggered, this, &DebuggerWindow::pauseExecution);
    connect(cpuStepAction, &QAction::triggered, this, &DebuggerWindow::cpuStep);
    connect(frameStepAction, &QAction::triggered, this, &DebuggerWindow::frameStep);
    connect(waitInterruptAction, &QAction::triggered, this, &DebuggerWindow::waitInterrupt);
    connect(resetAction, &QAction::triggered, this, &DebuggerWindow::resetEmulator);

    // Subscribe to events leading to MemoryView changes
    connect(ui->registersWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this, SLOT(changeMemoryViewZ80Address(uint16_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewBank(uint8_t)), this, SLOT(changeMemoryViewBank(uint8_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)), this, SLOT(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)));
    connect(ui->stackWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this, SLOT(changeMemoryViewZ80Address(uint16_t)));

    // Inject toolbar on top of other widget lines
    ui->verticalLayout_2->insertWidget(0, toolBar);

    // Set hex memory viewer to readonly mode
    ui->hexView->setReadOnly(true);


    /// region <Subscribe to events>
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);

    // Subscribe to emulator state changes
    ObserverCallbackMethod stateCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleEmulatorStateChanged);
    messageCenter.AddObserver(NC_LOGGER_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    // Subscribe to breakpoint trigger messages
    ObserverCallbackMethod breakpointCallback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleMessageBreakpointTriggered);
    messageCenter.AddObserver(NC_LOGGER_BREAKPOINT, observerInstance, breakpointCallback);

    /// endregion </Subscribe to events>
}

DebuggerWindow::~DebuggerWindow()
{
    qDebug() << "DebuggerWindow::~DebuggerWindow()";

    // Unsubscribe from breakpoint trigger messages
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&DebuggerWindow::handleMessageBreakpointTriggered);
    messageCenter.RemoveObserver(NC_LOGGER_BREAKPOINT, observerInstance, callback);
}

/// endregion </Constructor / destructors>

void DebuggerWindow::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    // Load debugger state from disk
    loadState();

    pauseAction->setEnabled(true);

    updateState();
}

Emulator* DebuggerWindow::getEmulator()
{
    return _emulator;
}

void DebuggerWindow::reset()
{
    ui->registersWidget->reset();
    ui->hexView->reset();

    ui->memorypagesWidget->reset();
    ui->stackWidget->reset();

    updateState();
}

/// region <Helper methods>

 void DebuggerWindow::updateState()
 {
    if (_emulator)
    {
        Z80State* state = _emulator->GetZ80State();

        // Refresh registers widget
        ui->registersWidget->setZ80State(state);
        ui->registersWidget->refresh();

        // Update disassembler widget
        ui->disassemblerWidget->setDisassemblerAddress(static_cast<uint16_t>(state->Z80Registers::pc));

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
            uint8_t* pagePhysicalAddress = memory->GetPhysicalAddressForZ80Bank(bank);

            QHexDocument* document;
            if (pageOffset != _curPageOffset)
            {
                _curPageOffset = pageOffset;

                QByteArray data((const char*)pagePhysicalAddress, 0x4000);
                document = QHexDocument::fromMemory<QMemoryBuffer>(data);
                document->setHexLineWidth(8);   // Display 8 hex bytes per line
                ui->hexView->setDocument(document);
            }
            else
            {
                document = ui->hexView->document();
            }

            document->gotoOffset(addressInBank);
            document->cursor()->selectOffset(addressInBank, 1);
            QHexMetadata* hexmetadata = document->metadata();
            hexmetadata->clear();
            hexmetadata->metadata(addressInBank, addressInBank + 1, Qt::black, Qt::blue, "JR Z,xx");
            hexmetadata->metadata(addressInBank + 1, addressInBank + 2, Qt::black, Qt::green, "");
        }

        ui->hexView->update();
    }
    else
    {
        // Disable all toolbar actions when no active emulator available
        continueAction->setEnabled(false);
        pauseAction->setEnabled(false);
        cpuStepAction->setEnabled(false);
        frameStepAction->setEnabled(false);
        waitInterruptAction->setEnabled(false);
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

    qDebug() << "DebuggerWindow::handleEmulatorStateChanged()";

    SimpleNumberPayload *payload = static_cast<SimpleNumberPayload *>(message->obj);
    _emulatorState = static_cast<EmulatorStateEnum>(payload->_payloadNumber);

    dispatchToMainThread([this]()
    {
        switch (_emulatorState)
        {
            case StateUnknown:
            case StateStopped:
                continueAction->setEnabled(false);
                pauseAction->setEnabled(false);
                cpuStepAction->setEnabled(false);
                frameStepAction->setEnabled(false);
                waitInterruptAction->setEnabled(false);
                resetAction->setEnabled(false);

                // Emulator already stopped working.
                // Time to disable all rendering activities and set controls to initial inactive state
                _emulator = nullptr;
                reset();
                break;
            case StateInitialized:
            default:
                continueAction->setEnabled(false);
                pauseAction->setEnabled(true);
                cpuStepAction->setEnabled(false);
                frameStepAction->setEnabled(false);
                waitInterruptAction->setEnabled(false);
                resetAction->setEnabled(false);
                break;
            case StateRun:
            case StateResumed:
                continueAction->setEnabled(false);
                pauseAction->setEnabled(true);
                cpuStepAction->setEnabled(false);
                frameStepAction->setEnabled(false);
                waitInterruptAction->setEnabled(false);
                resetAction->setEnabled(true);
                break;
            case StatePaused:
                continueAction->setEnabled(true);
                pauseAction->setEnabled(false);
                cpuStepAction->setEnabled(true);
                frameStepAction->setEnabled(true);
                waitInterruptAction->setEnabled(true);
                resetAction->setEnabled(true);
                break;
        }

        updateState();
    });
}

void DebuggerWindow::handleMessageBreakpointTriggered(int id, Message* message)
{
    if (message == nullptr || message->obj == nullptr)
        return;

    qDebug() << "DebuggerWindow::handleMessageBreakpointTriggered()";

    _breakpointTriggered = true;

    SimpleNumberPayload *payload = static_cast<SimpleNumberPayload *>(message->obj);
    uint16_t breakpointID = static_cast<uint16_t>(payload->_payloadNumber);

    dispatchToMainThread([this]()
    {
        continueAction->setEnabled(true);
        pauseAction->setEnabled(false);
        cpuStepAction->setEnabled(true);
        frameStepAction->setEnabled(true);
        waitInterruptAction->setEnabled(true);

        updateState();
    });
}

void DebuggerWindow::continueExecution()
{
    qDebug() << "DebuggerWindow::continueExecution()";

    _breakpointTriggered = false;

    if (_emulator && _emulator->IsPaused())
    {
        continueAction->setEnabled(false);
        pauseAction->setEnabled(true);
        cpuStepAction->setEnabled(false);
        frameStepAction->setEnabled(false);
        waitInterruptAction->setEnabled(false);

        _emulator->Resume();
    }
}

void DebuggerWindow::pauseExecution()
{
    qDebug() << "DebuggerWindow::pauseExecution()";

    if (_emulator && _emulator->IsRunning())
    {
        _emulator->Pause();
        _emulator->DebugOn();

        continueAction->setEnabled(true);
        pauseAction->setEnabled(false);
        cpuStepAction->setEnabled(true);
        frameStepAction->setEnabled(true);
        waitInterruptAction->setEnabled(true);

        updateState();
    }
}

void DebuggerWindow::cpuStep()
{
    qDebug() << "DebuggerWindow::cpuStep()";

    _breakpointTriggered = false;

    if (_emulator)
    {
        // Execute single Z80 command (Step execution does not trigger any breakpoints)
        bool skipBreakpoints = true;
        _emulator->RunSingleCPUCycle(skipBreakpoints);

        updateState();
    }
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

    _breakpointTriggered = false;

    updateState();
}

void DebuggerWindow::resetEmulator()
{
    qDebug() << "DebuggerWindow::resetEmulator()";

    if (_emulator)
    {
        _emulator->Reset();
    }
}

void DebuggerWindow::changeMemoryViewZ80Address(uint16_t addr)
{
    qDebug("DebuggerWindow::changeMemoryViewZ80Address");

    Memory* memory = _emulator->GetMemory();
    uint8_t bank = memory->GetZ80BankFromAddress(addr);
    _curPageOffset = memory->GetPhysicalOffsetForZ80Bank(bank);
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;
    uint8_t* pageAddress = memory->GetPhysicalAddressForZ80Bank(bank);
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

    qDebug("DebuggerWindow::changeMemoryViewAddress");

    QByteArray data((const char*)address, size);
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(data);
    document->setHexLineWidth(8);               // Display 8 hex bytes per line
    document->setBaseAddress(offset);           // Set base offset for the whole hex view
    ui->hexView->setDocument(document);

    // Note: change offset position only after assigning document to HexView
    // otherwise widget is unaware of the document and where to jump so just skipping the request
    document->gotoOffset(currentAddress);       // Position cursor on the byte with offset

    ui->hexView->update();
}

/// endregion </Event handlers / Slots>
