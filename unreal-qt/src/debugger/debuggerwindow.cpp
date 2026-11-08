#include "debuggerwindow.h"
#include "emulator/actions/steppingactions.h"
#include "emulator/remotecontrolguard.h"

#include <QBoxLayout>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <Qt>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <set>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "debugger/breakpointdialog.h"
#include "debugger/breakpointeditor.h"
#include "debugger/breakpointstopline.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/metadatasettings.h"
#include "debugger/devicememorydialog.h"
#include "debugger/disksectordialog.h"
#include "debugger/diskfilesdialog.h"
#include "debugger/livesourcewindow.h"
#include "debugger/markup/memorymarkup.h"
#include "debugger/panefocus.h"
#include "debugger/pchistorypane.h"
#include "debugger/porteventsdialog.h"
#include "debugger/portwatchdialog.h"
#include "debugger/rtcdialog.h"
#include "debugger/ports/portwrite.h"
#include "debugger/labeleditor.h"
#include "debugvisualizationwindow.h"
#include "emulator/emulator.h"
#include "emulator/readoutschanged.h"
#include "widgets/speedcontrolwidget.h"
#include "ui_debuggerwindow.h"

/// region <Constructor / destructors>

DebuggerWindow* DebuggerWindow::Of(QWidget* widget)
{
    for (QWidget* at = widget; at; at = at->parentWidget())
        if (auto* window = qobject_cast<DebuggerWindow*>(at))
            return window;
    return nullptr;
}

DebuggerWindow::DebuggerWindow(std::shared_ptr<Emulator> emulator, QWidget* parent) : QWidget(parent), ui(new Ui::DebuggerWindow)
{
    _emulator = emulator;
    _curPageOffset = 0;  // Initialize to 0 to prevent uninitialized memory access

    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    // Panels: one frame style for all of them (the speed control's), the panels' text in the window's monospace font
    // (a group box's style sheet font would pass its small title size down to the contents)
    // The PC history: a third splitter pane right of the disassembly, shown (and recording) by its toolbar toggle
    QGroupBox* historyGroup = new QGroupBox("PC history", this);
    historyGroup->setObjectName("historyGroup");
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setSpacing(0);
    _pcHistoryPane = new PcHistoryPane(_emulator, historyGroup);
    historyLayout->addWidget(_pcHistoryPane);
    ui->mainSplitter->addWidget(historyGroup);
    historyGroup->hide();

    const QFont panelFont = font();
    for (QWidget* contents : std::initializer_list<QWidget*>{ui->registersWidget, ui->stackWidget,
                                                              ui->memorypagesWidget, ui->hexView,
                                                              ui->disassemblerWidget, _pcHistoryPane})
        contents->setFont(panelFont);
    setStyleSheet(QStringLiteral(R"(
        QGroupBox#registersGroup, QGroupBox#stackGroup, QGroupBox#pagesGroup, QGroupBox#memoryGroup,
        QGroupBox#disasmGroup, QGroupBox#historyGroup {
            font-family: "Consolas", "Monaco", "Courier New", monospace;
            font-size: 11px;
            border: 1px solid palette(mid);
            border-radius: 4px;
            margin-top: 8px;
            padding: 8px 6px 4px 6px;
        }
        QGroupBox#registersGroup::title, QGroupBox#stackGroup::title, QGroupBox#pagesGroup::title,
        QGroupBox#memoryGroup::title, QGroupBox#disasmGroup::title, QGroupBox#historyGroup::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            padding: 0 4px;
            color: palette(text);
        }
    )"));
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setStretchFactor(2, 0);
    ui->mainSplitter->setSizes({400, 700, 260});

    // Create floating toolbar
    // toolBar = new QToolBar("Debugger toolbar", this);
    // toolBar->setMovable(true);
    // toolBar->setFloatable(true);
    // toolBar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    // toolBar->setWindowFlags(Qt::Tool);
    toolBar = new QToolBar("Debugger toolbar");

    // Set toolbar stylesheet for consistent disabled state appearance on Linux
    // Linux themes often don't properly gray out disabled buttons
#ifdef Q_OS_LINUX
    toolBar->setStyleSheet(R"(
        QToolBar {
            spacing: 2px;
            padding: 2px;
        }
        QToolButton {
            border: 1px solid #888888;
            border-radius: 3px;
            padding: 4px 8px;
            margin: 1px;
            background-color: #f0f0f0;
        }
        QToolButton:hover {
            background-color: #e0e0e0;
            border-color: #666666;
        }
        QToolButton:pressed {
            background-color: #d0d0d0;
        }
        QToolButton:disabled {
            color: #999999;
            background-color: #e8e8e8;
            border-color: #cccccc;
        }
    )");
#endif

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
    connect(stepInAction, &QAction::triggered, this, &DebuggerWindow::stepIn);
    toolBar->addAction(stepInAction);

    stepOverAction = new QAction("Step Over", this);
    stepOverAction->setIcon(QIcon::fromTheme("debug-step-over"));
    connect(stepOverAction, &QAction::triggered, this, &DebuggerWindow::stepOver);
    toolBar->addAction(stepOverAction);

    stepOutAction = new QAction("Step Out", this);
    stepOutAction->setIcon(QIcon::fromTheme("debug-step-out"));
    connect(stepOutAction, &QAction::triggered, this, &DebuggerWindow::stepOut);
    toolBar->addAction(stepOutAction);

    frameStepAction = new QAction("Frame step", this);
    connect(frameStepAction, &QAction::triggered, this, &DebuggerWindow::frameStep);
    toolBar->addAction(frameStepAction);

    waitInterruptAction = toolBar->addAction("Wait INT");

    // Atomic stepping dropdown button
    atomicStepMenu = new QMenu("Advanced Step", this);
    runTStatesAction = atomicStepMenu->addAction("Run T-States...");
    runToScanlineAction = atomicStepMenu->addAction("Run to Scanline...");
    runNScanlinesAction = atomicStepMenu->addAction("Run N Scanlines...");
    runToPixelAction = atomicStepMenu->addAction("Run to Screen Pixel");
    runToInterruptAction = atomicStepMenu->addAction("Run to Interrupt");

    atomicStepButton = new QToolButton(this);
    atomicStepButton->setText("Adv Step \u25BE");
    atomicStepButton->setMenu(atomicStepMenu);
    atomicStepButton->setPopupMode(QToolButton::InstantPopup);
    toolBar->addWidget(atomicStepButton);

    // Create toolbar actions
    resetAction = new QAction("Reset", this);
    toolBar->addAction(resetAction);
    portOutAction = new QAction("Port OUT...", this);
    portOutAction->setToolTip("Write a port like a CPU OUT (paging, TS-Conf registers); no breakpoints, a TTD tool edit");
    toolBar->addAction(portOutAction);
    deviceMemoryAction = new QAction("Device memory", this);
    deviceMemoryAction->setToolTip("Memory a device owns outside RAM / ROM (Sprinter vram, TS-Conf cram / sfile): view and edit");
    toolBar->addAction(deviceMemoryAction);
    diskSectorAction = new QAction("Disk sector", this);
    diskSectorAction->setToolTip("View and edit a floppy sector's data field (CRC recalculated, the image counts as modified)");
    toolBar->addAction(diskSectorAction);
    diskFilesAction = new QAction("Disk files", this);
    diskFilesAction->setToolTip("The files on a disk with their source formats: open an assembler source as text, export it, convert it");
    toolBar->addAction(diskFilesAction);
    liveSourceAction = new QAction("Live source", this);
    liveSourceAction->setToolTip("The source an assembler (ALASM, TASM 4.12) holds in RAM, followed while you type: its labels in the debugger, its errors on their lines");
    toolBar->addAction(liveSourceAction);
    pcHistoryAction = new QAction("PC history", this);
    pcHistoryAction->setToolTip("The PC history pane: the newest instructions the CPU started, with their window's page "
                                "(records while shown)");
    pcHistoryAction->setCheckable(true);
    toolBar->addAction(pcHistoryAction);
    QMenu* machineMenu = new QMenu(this);
    QAction* portWatchAction = machineMenu->addAction("Ports");
    portWatchAction->setToolTip("Every port the machine decodes, the latched ones with their values (changes lit)");
    QAction* rtcAction = machineMenu->addAction("CMOS clock");
    rtcAction->setToolTip("The clock chip: time, registers A-D and the alarm decoded, every cell edited in place");
    QAction* portEventsAction = machineMenu->addAction("Port events");
    portEventsAction->setToolTip("When did the program read a key, write an AY register, change the border... (time travel)");
    machineMenu->setToolTipsVisible(true);
    machineButton = new QToolButton(this);
    machineButton->setText("Machine \u25BE");
    machineButton->setMenu(machineMenu);
    machineButton->setPopupMode(QToolButton::InstantPopup);
    toolBar->addWidget(machineButton);
    connect(portWatchAction, &QAction::triggered, this, &DebuggerWindow::showPortWatch);
    connect(rtcAction, &QAction::triggered, this, &DebuggerWindow::showRtc);
    connect(portEventsAction, &QAction::triggered, this, &DebuggerWindow::showPortEvents);
    breakpointsAction = new QAction("Breakpoints", this);
    labelsAction = new QAction("Labels", this);
    visualizationAction = new QAction("Visualization", this);
    
    // Add expanding spacer to push management buttons to the right
    QWidget* toolbarSpacer = new QWidget();
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(toolbarSpacer);
    
    toolBar->addAction(labelsAction);
    breakpointsAction = toolBar->addAction("Breakpoints");
    toolBar->addAction(visualizationAction);

    connect(continueAction, &QAction::triggered, this, &DebuggerWindow::continueExecution);
    connect(pauseAction, &QAction::triggered, this, &DebuggerWindow::pauseExecution);
    connect(frameStepAction, &QAction::triggered, this, &DebuggerWindow::frameStep);
    connect(waitInterruptAction, &QAction::triggered, this, &DebuggerWindow::runToInterrupt);
    connect(runTStatesAction, &QAction::triggered, this, &DebuggerWindow::runTStates);
    connect(runToScanlineAction, &QAction::triggered, this, &DebuggerWindow::runToScanline);
    connect(runNScanlinesAction, &QAction::triggered, this, &DebuggerWindow::runNScanlines);
    connect(runToPixelAction, &QAction::triggered, this, &DebuggerWindow::runToPixel);
    connect(runToInterruptAction, &QAction::triggered, this, &DebuggerWindow::runToInterrupt);
    connect(resetAction, &QAction::triggered, this, &DebuggerWindow::resetEmulator);
    connect(portOutAction, &QAction::triggered, this, &DebuggerWindow::portOut);
    connect(deviceMemoryAction, &QAction::triggered, this, &DebuggerWindow::showDeviceMemory);
    connect(diskSectorAction, &QAction::triggered, this, &DebuggerWindow::showDiskSector);
    connect(diskFilesAction, &QAction::triggered, this, &DebuggerWindow::showDiskFiles);
    connect(liveSourceAction, &QAction::triggered, this, &DebuggerWindow::showLiveSource);
    connect(pcHistoryAction, &QAction::toggled, this, &DebuggerWindow::showPcHistory);
    connect(_pcHistoryPane, &PcHistoryPane::addressPicked, ui->disassemblerWidget, &DisassemblerWidget::goToAddress);
    connect(this, &DebuggerWindow::readyForChildren, _pcHistoryPane, &PcHistoryPane::refresh);
    connect(labelsAction, &QAction::triggered, this, &DebuggerWindow::showLabelManager);
    connect(breakpointsAction, &QAction::triggered, this, &DebuggerWindow::showBreakpointManager);
    connect(visualizationAction, &QAction::triggered, this, &DebuggerWindow::showVisualizationWindow);

    // Subscribe to events leading to MemoryView changes
    connect(ui->registersWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this,
            SLOT(changeMemoryViewZ80Address(uint16_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewBank(uint8_t)), this, SLOT(changeMemoryViewBank(uint8_t)));
    connect(ui->memorypagesWidget, SIGNAL(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)), this,
            SLOT(changeMemoryViewAddress(uint8_t*, size_t, uint16_t)));
    connect(ui->stackWidget, SIGNAL(changeMemoryViewZ80Address(uint16_t)), this,
            SLOT(changeMemoryViewZ80Address(uint16_t)));

    // Connect register and stack jump to disassembly signals
    connect(ui->registersWidget, SIGNAL(jumpToAddressInDisassembly(uint16_t)), ui->disassemblerWidget,
            SLOT(goToAddress(uint16_t)));
    connect(ui->stackWidget, SIGNAL(jumpToAddressInDisassembly(uint16_t)), ui->disassemblerWidget,
            SLOT(goToAddress(uint16_t)));

    // A register set from the registers pane: the disassembly (a new PC) and the stack follow
    connect(ui->registersWidget, &RegistersWidget::registerEdited, this, [this](const QString&) { updateState(); });

    _paneFocus = new PaneFocus(this, {{tr("Disassembly"), ui->disassemblerWidget, {}},
                                      {tr("Registers"), ui->registersWidget, ui->registersWidget->cursorCells()},
                                      {tr("Memory"), ui->hexView, {}},
                                      {tr("Stack"), ui->stackWidget, ui->stackWidget->cursorCells()}});

    // Inject toolbar on top of other widget lines
    ui->verticalLayout_2->insertWidget(0, toolBar);

    // Speed control widget — inserted below toolbar, with FDC status on the right
    m_speedControl = new SpeedControlWidget(this);
    m_speedControl->setEmulator(_emulator);
    m_speedControl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* speedRow = new QHBoxLayout();
    speedRow->setContentsMargins(0, 0, 0, 0);
    speedRow->addWidget(m_speedControl);
    speedRow->addStretch(1);
    ui->fdcStatusWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    speedRow->addWidget(ui->fdcStatusWidget);
    ui->verticalLayout_2->insertLayout(1, speedRow);

    // The stop line: which breakpoint stopped the machine, on what, by which condition (gone when it moves on)
    _stopBanner = new QLabel(this);
    _stopBanner->setTextFormat(Qt::RichText);
    _stopBanner->setWordWrap(true);
    _stopBanner->setTextInteractionFlags(Qt::TextBrowserInteraction);
    _stopBanner->setStyleSheet("QLabel { background: #FFF1C2; color: #202020; border: 1px solid #E0B040;"
                               " border-left: 4px solid #D03030; padding: 3px 6px; }");
    _stopBanner->setVisible(false);
    connect(_stopBanner, &QLabel::linkActivated, this, &DebuggerWindow::onStopBannerLink);
    ui->verticalLayout_2->insertWidget(2, _stopBanner);

    connect(m_speedControl, &SpeedControlWidget::stepped, this, [this]() {
        updateState();
        emit screenRefreshRequested();
    });

    // Synchronize toolbar state when speed widget starts/stops auto-stepping
    connect(m_speedControl, &SpeedControlWidget::runningChanged, this, [this](bool running) {
        // Track speed control state synchronously — this runs BEFORE any queued
        // stateChanged(StatePaused) events arrive via QueuedConnection, avoiding
        // the race where onBindingStateChanged overwrites the toolbar state.
        _speedControlActive = running;

        if (running)
        {
            // Speed widget is auto-stepping: disable Continue/Step, enable Pause
            updateToolbarActions(false, true, false, true, true, true);
        }
        else
        {
            // Speed widget stopped: emulator is paused, enable Continue/Step, disable Pause
            updateToolbarActions(true, false, true, true, true, true);
        }
    });

    // Set hex memory viewer to readonly mode
    ui->hexView->setReadOnly(true);
    // The memory view's cursor is the debugger's "memory" cursor (ttd.find_last_write without an address)
    connect(ui->hexView, &QHexView::positionChanged, this, &DebuggerWindow::publishMemoryCursor);

    // Set up hex viewer
    QHexOptions options = ui->hexView->options();
    options.linelength = 8;    // Display 8 hex bytes per line
    options.addresswidth = 4;  // Address is 4 hex digits [0000-FFFF]
    ui->hexView->setOptions(options);
    ui->hexView->setToolTipProvider([this](qint64 offset) { return memoryToolTip(offset); });
    // The labels and the metadata switched (Debug > Metadata, dbg.metadata_*): the disassembly and the colours again
    connect(&MetadataSettings::Instance(), &MetadataSettings::changed, this, [this]() {
        ui->disassemblerWidget->metadataSwitched();
        paintMemoryMetadata();
    });
    // The kinds the memory markup colours in the view
    _markupLegend = new QLabel(ui->memoryGroup);
    _markupLegend->setTextFormat(Qt::RichText);
    _markupLegend->setWordWrap(true);
    _markupLegend->setVisible(false);
    ui->memoryGroupLayout->addWidget(_markupLegend);

    /// region <Subscribe to events>
    // NOTE: DebuggerWindow is now FULLY DEPENDENT on MainWindow via EmulatorBinding.
    // We do NOT subscribe to ANY global MessageCenter events here!
    //
    // Problem: Global events (NC_EXECUTION_CPU_STEP, etc.)
    // don't carry emulator ID information. When multiple emulators exist, we can't
    // tell which emulator the event is from, leading to race conditions and crashes.
    //
    // Solution: DebuggerWindow receives state via EmulatorBinding signals from MainWindow.
    // Child widgets are connected to DebuggerWindow dispatch signals.
    //
    // The exception: NC_EXECUTION_BREAKPOINT carries the machine's id (BreakpointTriggeredPayload). The observer
    // copies the hit and posts it; the GUI thread keeps it only for the machine this window shows
    QPointer<DebuggerWindow> self(this);
    _breakpointObserver =
        MessageCenter::DefaultMessageCenter().AddObserver(NC_EXECUTION_BREAKPOINT, [self](int, Message* message) {
            auto* payload = message != nullptr ? dynamic_cast<BreakpointTriggeredPayload*>(message->obj) : nullptr;
            if (payload == nullptr)
                return;
            BreakpointHit hit;
            hit.emulatorId = payload->emulatorId.toString();
            hit.id = static_cast<uint16_t>(payload->_payloadNumber);
            hit.address = payload->address;
            hit.kind = payload->kind;
            hit.cpu = payload->cpu;
            hit.cpuPc = payload->cpuPc;
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, hit] {
                    if (self)
                        self->onBreakpointHit(hit);
                },
                Qt::QueuedConnection);
        });

    // The memory markup changed (a mark, a profile, an L2 build: memorymarkup.h): the memory view's colours and the
    // disassembly's data rows follow at once, paused or not
    _markupObserver =
        MessageCenter::DefaultMessageCenter().AddObserver(NC_READOUTS_CHANGED, [self](int, Message* message) {
            auto* payload = message != nullptr ? dynamic_cast<ReadoutsChangedPayload*>(message->obj) : nullptr;
            if (payload == nullptr || payload->area != "markup")
                return;
            const std::string id = payload->emulatorId;
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, id] {
                    if (!self || !self->_emulator || (!id.empty() && id != self->_emulator->GetId()))
                        return;
                    self->paintMemoryMetadata();
                    if (self->_emulator->IsPaused())
                        self->ui->disassemblerWidget->refresh();
                },
                Qt::QueuedConnection);
        });

    // Connect dispatch signals to child widgets (hierarchical pattern)
    connect(this, &DebuggerWindow::readyForChildren, ui->disassemblerWidget, &DisassemblerWidget::refresh);
    connect(this, &DebuggerWindow::readyForChildren, ui->registersWidget, &RegistersWidget::refresh);
    connect(this, &DebuggerWindow::readyForChildren, ui->stackWidget, &StackWidget::refresh);
    connect(this, &DebuggerWindow::readyForChildren, ui->memorypagesWidget, &MemoryPagesWidget::refresh);
    connect(this, &DebuggerWindow::readyForChildren, ui->fdcStatusWidget, &FdcStatusWidget::reset);

    connect(this, &DebuggerWindow::notReadyForChildren, ui->disassemblerWidget, &DisassemblerWidget::reset);
    connect(this, &DebuggerWindow::notReadyForChildren, ui->registersWidget, &RegistersWidget::reset);
    connect(this, &DebuggerWindow::notReadyForChildren, ui->stackWidget, &StackWidget::reset);
    connect(this, &DebuggerWindow::notReadyForChildren, ui->memorypagesWidget, &MemoryPagesWidget::reset);
    connect(this, &DebuggerWindow::notReadyForChildren, ui->fdcStatusWidget, &FdcStatusWidget::reset);

    /// endregion </Subscribe to events>

    // Start with toolbar disabled - will be enabled when emulator is adopted and running
    updateToolbarActions(false, false, false, false, false, false);
}

DebuggerWindow::~DebuggerWindow()
{
    qDebug() << "DebuggerWindow::~DebuggerWindow()";

    // Block all refreshes during shutdown
    _isClosing = true;
    // All other event handling is done via explicit method calls from MainWindow; the breakpoint observer goes here
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_EXECUTION_BREAKPOINT, _breakpointObserver);
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_READOUTS_CHANGED, _markupObserver);

    delete ui;
}

/// endregion </Constructor / destructors>

void DebuggerWindow::setEmulator(std::shared_ptr<Emulator> emulator)
{
    qDebug() << "DebuggerWindow::setEmulator() - Setting emulator to"
             << (emulator ? QString::fromStdString(emulator->GetId()) : "nullptr");

    _emulator = emulator;

    propagateEmulator();

    if (_emulator)
    {
        qDebug() << "DebuggerWindow::setEmulator() - Loading debugger state";

        // Load debugger state from disk
        loadState();

        qDebug() << "DebuggerWindow::setEmulator() - Checking emulator state";

        // Set toolbar based on actual emulator state
        EmulatorStateEnum state = _emulator->GetState();
        onBindingStateChanged(state);

        // Update widget state if emulator is not actively running
        if (state != StateRun && state != StateResumed)
        {
            qDebug() << "DebuggerWindow::setEmulator() - Updating debugger state (safe)";
            updateState();
        }
        else
        {
            qDebug() << "DebuggerWindow::setEmulator() - Emulator is running, deferring state update until pause/stop";
        }

        qDebug() << "DebuggerWindow::setEmulator() - Emulator setup complete";
    }
    else
    {
        qDebug() << "DebuggerWindow::setEmulator() - No emulator available, disabling all actions";

        // No emulator available, disable all actions
        // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
        updateToolbarActions(false, false, false, false, false, false);

        qDebug() << "DebuggerWindow::setEmulator() - Actions disabled, calling reset()";

        // Reset the debugger UI to clean state
        reset();

        qDebug() << "DebuggerWindow::setEmulator() - Reset complete";
    }

    qDebug() << "DebuggerWindow::setEmulator() - Method completed successfully";
}

Emulator* DebuggerWindow::getEmulator()
{
    // Prefer binding if available
    if (m_binding && m_binding->isBound())
    {
        return m_binding->emulator();
    }
    return _emulator.get();
}

void DebuggerWindow::setBinding(EmulatorBinding* binding)
{
    // Disconnect from old binding
    if (m_binding)
    {
        disconnect(m_binding, nullptr, this, nullptr);
    }

    m_binding = binding;

    if (m_binding)
    {
        // Connect to binding signals
        connect(m_binding, &EmulatorBinding::bound, this, &DebuggerWindow::onBindingBound);
        connect(m_binding, &EmulatorBinding::unbound, this, &DebuggerWindow::onBindingUnbound);
        connect(m_binding, &EmulatorBinding::stateChanged, this, &DebuggerWindow::onBindingStateChanged);
        connect(m_binding, &EmulatorBinding::ready, this, &DebuggerWindow::onBindingReady);
        connect(m_binding, &EmulatorBinding::notReady, this, &DebuggerWindow::onBindingNotReady);

        // Connect CPU step complete signal for automation-triggered steps (WebAPI, Python, Lua, CLI)
        // Use updateState() instead of onBindingReady() to ensure setZ80State() is called on widgets
        connect(m_binding, &EmulatorBinding::cpuStepComplete, this, &DebuggerWindow::updateState);

        // Connect frame refresh signal so TTD seek / step-back / step-forward
        // (which rebuild CPU + RAM + chipset state in-place via RestoreCheckpoint
        // and post NC_VIDEO_FRAME_REFRESH) refresh the debugger widgets too.
        // EmulatorBinding filters NC_VIDEO_FRAME_REFRESH by emulator ID before
        // emitting this signal, so we only get refreshes for OUR emulator —
        // no risk of cross-instance UI corruption when multiple emulators run.
        // Same handler as cpuStepComplete: updateState() rebuilds registers,
        // disassembler (with new PC), memory pages, stack, and hex view.
        connect(m_binding, &EmulatorBinding::frameRefresh, this, &DebuggerWindow::updateState);

        qDebug() << "DebuggerWindow: Connected to EmulatorBinding";

        // If binding already has an emulator, sync state
        if (m_binding->isBound())
        {
            _emulator = m_binding->emulatorShared();
            _emulatorState = m_binding->state();

            propagateEmulator();
            if (m_binding->isReady())
            {
                onBindingReady();
            }
        }
    }
    else
    {
        _emulator = nullptr;

        propagateEmulator();

        reset();
    }
}

void DebuggerWindow::propagateEmulator()
{
    // Every child that keeps the machine follows the debugger's: a removed one is let go here (BUGS.md #11)
    if (m_speedControl)
        m_speedControl->setEmulator(_emulator);
    if (_liveSourceWindow)
        _liveSourceWindow->setEmulator(_emulator);
    if (_visualizationWindow)
        _visualizationWindow->setEmulator(_emulator);
    _pcHistoryPane->setEmulator(_emulator);
    // Port events holds the machine it searches: another machine (or none) closes it
    if (_portEventsDialog && _portEventsDialog->emulator() != _emulator)
        _portEventsDialog->close();
    updateMachineActions();
}

void DebuggerWindow::updateMachineActions()
{
    // A tool the machine has nothing for is off, its tooltip says why (no empty dialog to open)
    const bool regions = DeviceMemoryDialog::HasRegions(_emulator.get());
    deviceMemoryAction->setEnabled(regions);
    deviceMemoryAction->setToolTip(regions ? "Memory a device owns outside RAM / ROM (Sprinter vram, TS-Conf cram / "
                                             "sfile, sound card RAM): view and edit"
                                           : "This machine has no device memory (memory a device owns outside RAM / "
                                             "ROM: Sprinter vram, TS-Conf cram / sfile, sound card RAM)");
    const bool drives = DiskSectorDialog::HasDrives(_emulator.get());
    diskSectorAction->setEnabled(drives);
    diskSectorAction->setToolTip(drives ? "View and edit a floppy sector's data field (CRC recalculated, the image "
                                          "counts as modified)"
                                        : "This machine has no floppy drives");
}

void DebuggerWindow::onBindingBound()
{
    qDebug() << "DebuggerWindow::onBindingBound()";
    if (m_binding)
    {
        _emulator = m_binding->emulatorShared();
        _emulatorState = m_binding->state();

        propagateEmulator();
        // Delegate to state handler - toolbar enabled based on actual state
        onBindingStateChanged(_emulatorState);
    }
}

void DebuggerWindow::onBindingUnbound()
{
    qDebug() << "DebuggerWindow::onBindingUnbound()";
    _emulator = nullptr;

    propagateEmulator();

    reset();
    emit notReadyForChildren();
}

void DebuggerWindow::onBindingStateChanged(EmulatorStateEnum state)
{
    qDebug() << "DebuggerWindow::onBindingStateChanged(" << getEmulatorStateName(state) << ")";
    _emulatorState = state;

    // Update toolbar based on state
    switch (state)
    {
        case StatePaused:
            if (_speedControlActive)
            {
                // Emulator is paused internally but speed widget is auto-stepping —
                // treat as "running" so Pause remains available from the toolbar.
                // Uses _speedControlActive (set synchronously via runningChanged signal)
                // instead of m_speedControl->isRunning() to avoid race with
                // QueuedConnection state changes that arrive before the timer starts.
                updateToolbarActions(false, true, false, true, true, true);
            }
            else
            {
                // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON, Labels: ON)
                updateToolbarActions(true, false, true, true, true, true);
            }
            break;
        case StateRun:
        case StateResumed:  // StateResumed = running after pause, same as StateRun
            // (Continue: OFF, Pause: ON, Step: OFF, Reset: ON, Breakpoints: ON, Labels: ON)
            updateToolbarActions(false, true, false, true, true, true);
            clearStopBanner();
            break;
        case StateStopped:
        case StateUnknown:
            // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
            updateToolbarActions(false, false, false, false, false, false);
            break;
        default:
            break;
    }

    if (m_speedControl)
        m_speedControl->setMachineRunning(state == StateRun || state == StateResumed);

    // Dispatch to children
    emit stateChangedForChildren(state);
}

void DebuggerWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (pcHistoryAction && pcHistoryAction->isChecked() && !_pcHistoryPane->isRecording())
        _pcHistoryPane->setRecording(true);
    emit visibilityChanged(true);
}

void DebuggerWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (_pcHistoryPane->isRecording())
        _pcHistoryPane->setRecording(false);  // a closed debugger costs the emulation nothing
    emit visibilityChanged(false);
}

void DebuggerWindow::onBindingReady()
{
    qDebug() << "DebuggerWindow::onBindingReady()";

    // Point the registers widget at the *bound* emulator's Z80 before it refreshes.
    // Its cached pointer may still belong to a previous, already destroyed emulator
    // (ready fires on the new emulator's first pause, before any CPU step refresh).
    ui->registersWidget->setZ80State(_emulator ? _emulator->GetZ80State() : nullptr);

    // Dispatch to children via signal (widgets are connected to readyForChildren)
    emit readyForChildren();
    updateMachineActions();  // the machine is complete now (its cards and drives fitted)
    showMemoryAtPc();
}

void DebuggerWindow::showMemoryAtPc()
{
    // The memory view follows PC (or the register memoryFollow names): its 16 KB page read again (a step changes
    // memory), the PC byte marked; a page picked by hand stays until the next stop. Off: the page stays, read again
    if (!_emulator || !_emulator->IsPaused() || !_emulator->GetZ80State())
        return;
    const Z80Registers& registers = *_emulator->GetZ80State();
    std::optional<uint16_t> address;
    if (_memoryFollow == QLatin1String("pc"))
        address = registers.pc;
    else if (_memoryFollow == QLatin1String("sp"))
        address = registers.sp;
    else if (_memoryFollow == QLatin1String("bc"))
        address = registers.bc;
    else if (_memoryFollow == QLatin1String("de"))
        address = registers.de;
    else if (_memoryFollow == QLatin1String("hl"))
        address = registers.hl;
    else if (_memoryFollow == QLatin1String("ix"))
        address = registers.ix;
    else if (_memoryFollow == QLatin1String("iy"))
        address = registers.iy;
    else
        address = memoryCursor();
    if (address)
        changeMemoryViewZ80Address(*address);  // paints the metadata, PC among it
}

namespace
{
/// Matte backgrounds, one per markup kind, the hues spread round the wheel at one lightness (black text stays
/// readable, neighbouring kinds stay apart); none for the unused / untouched filler
QColor MarkupKindColour(analysis::RegionKind kind)
{
    using analysis::RegionKind;
    switch (kind)
    {
        case RegionKind::Code: return {179, 200, 230};
        case RegionKind::Data: return {222, 222, 222};
        case RegionKind::Text: return {235, 199, 168};
        case RegionKind::Font: return {219, 181, 227};
        case RegionKind::Sprite: return {192, 223, 170};
        case RegionKind::Tile: return {173, 220, 204};
        case RegionKind::Screen: return {235, 188, 194};
        case RegionKind::Buffer: return {216, 206, 182};
        case RegionKind::InitTable: return {208, 212, 175};
        case RegionKind::RecordArray: return {163, 219, 224};
        case RegionKind::Variable: return {243, 230, 145};
        case RegionKind::PointerTable: return {200, 190, 218};
        case RegionKind::HandlerTable: return {196, 201, 238};
        case RegionKind::RngState: return {235, 186, 173};
        case RegionKind::MusicData: return {232, 186, 211};
        case RegionKind::Packed: return {195, 189, 182};
        case RegionKind::LevelMap: return {173, 220, 181};
        case RegionKind::Unused:
        case RegionKind::Untouched: return {};
    }
    return {};
}
}  // namespace

std::optional<uint16_t> DebuggerWindow::memoryViewBase() const
{
    // The view shows one 16 KB page: the CPU window mapping it now places the markup (none: not mapped)
    Memory* memory = _emulator ? _emulator->GetMemory() : nullptr;
    if (!memory)
        return std::nullopt;
    for (uint8_t bank = 0; bank < 4; bank++)
        if (memory->GetPhysicalOffsetForZ80Bank(bank) == _curPageOffset)
            return static_cast<uint16_t>(bank * 0x4000);
    return std::nullopt;
}

QString DebuggerWindow::memoryToolTip(qint64 offset)
{
    const std::optional<uint16_t> base = memoryViewBase();
    Memory* memory = _emulator ? _emulator->GetMemory() : nullptr;
    if (!memory || offset < 0 || offset > 0x3FFF)
        return {};
    if (!base)
        return tr("Page offset $%1 (the page is not mapped in the CPU view)")
            .arg(static_cast<int>(offset), 4, 16, QLatin1Char('0'))
            .toUpper();
    const auto address = static_cast<uint16_t>(*base + offset);
    const auto hex = [](unsigned value, int digits) {
        return QString("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
    };
    const auto read = [memory](uint16_t at) { return memory->DirectReadFromZ80Memory(at); };
    const auto escaped = [](const std::string& text) { return QString::fromStdString(text).toHtmlEscaped(); };

    // The cell: its address, the byte and the word from it
    const uint8_t byte = read(address);
    const unsigned word = byte | (read(static_cast<uint16_t>(address + 1)) << 8);
    QString tip = QString("<b>$%1</b>&nbsp; #%2 = %3").arg(hex(address, 4), hex(byte, 2)).arg(byte);
    if (byte >= 0x20 && byte < 0x7F)
        tip += QString(" '%1'").arg(QString(QChar(byte)).toHtmlEscaped());
    tip += QString("&nbsp; word #%1 = %2").arg(hex(word, 4)).arg(word);

    // The metadata's notes off: the cell and its label only
    markup::MemoryMarkup* markup = _emulator->GetDebugManager() &&
                                           MetadataSettings::Instance().Uses(MetadataSettings::Switch::Notes)
                                       ? _emulator->GetDebugManager()->GetMemoryMarkup()
                                       : nullptr;
    // The block holding the cell: the markup's spans are cut at its anchors (instruction starts), so the
    // neighbours of the same kind, title and layer on the page join into one
    std::optional<markup::Span> span;
    if (markup)
    {
        const std::vector<markup::Span> spans = markup->Ranges(*base, static_cast<uint16_t>(*base + 0x3FFF));
        const auto same = [](const markup::Span& a, const markup::Span& b) {
            return a.kind == b.kind && a.layer == b.layer && a.title == b.title;
        };
        for (size_t i = 0; i < spans.size(); i++)
            if (spans[i].lo <= address && address <= spans[i].hi)
            {
                span = spans[i];
                for (size_t j = i; j > 0 && spans[j - 1].hi + 1 == spans[j].lo && same(spans[j - 1], *span); j--)
                    span->lo = spans[j - 1].lo;
                for (size_t j = i + 1; j < spans.size() && spans[j].lo == spans[j - 1].hi + 1 && same(spans[j], *span);
                     j++)
                    span->hi = spans[j].hi;
                break;
            }
    }
    // The label at the cell or the nearest one before it (inside the block when there is one): the variable's name
    std::string labelComment;
    if (LabelManager* labels = _emulator->GetDebugManager() &&
                                       MetadataSettings::Instance().Uses(MetadataSettings::Switch::Labels)
                                   ? _emulator->GetDebugManager()->GetLabelManager()
                                   : nullptr)
    {
        const uint16_t from = span ? span->lo : static_cast<uint16_t>(std::max(0, address - 0xFF));
        std::shared_ptr<Label> nearest;
        for (const auto& label : labels->GetLabelsInRange(from, address))
            if (label->address <= address && (!nearest || label->address >= nearest->address) &&
                labels->GetLabelByZ80Address(label->address) == label)
                nearest = label;
        if (nearest)
        {
            tip += "<br><b>" + escaped(nearest->name) + "</b>";
            if (nearest->address != address)
                tip += QString("+%1").arg(address - nearest->address);
            if (!nearest->comment.empty())
                tip += " - " + escaped(nearest->comment);
            labelComment = nearest->comment;
        }
    }
    // The block: its kind, extent, the cell's place in it, the layer it comes from, its title; a small variable's value
    if (span)
    {
        const unsigned size = span->hi - span->lo + 1u;
        const QColor colour = MarkupKindColour(span->kind);
        tip += QString("<br><span style='background:%1'>&nbsp;%2&nbsp;</span> $%3-$%4, %5 byte(s), +%6 [%7]")
                   .arg(colour.isValid() ? colour.name() : QString("transparent"),
                        analysis::RegionKindName(span->kind), hex(span->lo, 4), hex(span->hi, 4))
                   .arg(size)
                   .arg(address - span->lo)
                   .arg(escaped(span->source.empty() ? markup::LayerName(span->layer) : span->source));
        if (span->kind == analysis::RegionKind::Variable && size <= 4)
        {
            unsigned value = 0;
            for (unsigned i = size; i-- > 0;)
                value = (value << 8) | read(static_cast<uint16_t>(span->lo + i));
            tip += QString("<br>value #%1 = %2").arg(hex(value, static_cast<int>(size) * 2)).arg(value);
        }
        if (!span->title.empty())
            tip += "<br>" + escaped(span->title);
    }
    // The comments on the cell (one the label already carries is not repeated)
    if (markup)
        for (const markup::CommentAt& comment : markup->Comments(address, address))
            if (comment.text != labelComment)
                tip += "<br><i>; " + escaped(comment.text) + "</i>";
    if (_emulator->IsPaused() && _emulator->GetZ80State() && _emulator->GetZ80State()->Z80Registers::pc == address)
        tip += "<br><b>PC</b>";
    return "<p style='white-space:nowrap'>" + tip + "</p>";
}

void DebuggerWindow::paintMemoryMetadata()
{
    QHexMetadata* metadata = ui->hexView->getMetadata();
    Memory* memory = _emulator ? _emulator->GetMemory() : nullptr;
    if (!metadata || !memory)
        return;
    metadata->clear();
    const std::optional<uint16_t> base = memoryViewBase();

    std::set<analysis::RegionKind> shown;
    const MetadataSettings& settings = MetadataSettings::Instance();
    markup::MemoryMarkup* markup = _emulator->GetDebugManager() ? _emulator->GetDebugManager()->GetMemoryMarkup()
                                                                : nullptr;
    if (markup && base)
    {
        // The metadata's colours, its comments: each switched apart (Debug > Metadata)
        const bool colours = settings.Uses(MetadataSettings::Switch::Colours);
        for (const markup::Span& span : colours ? markup->Ranges(*base, static_cast<uint16_t>(*base + 0x3FFF))
                                                : std::vector<markup::Span>{})
        {
            const QColor colour = MarkupKindColour(span.kind);
            if (!colour.isValid())
                continue;
            // The tooltip is memoryToolTip's (the cell, its label, block, value and comments)
            metadata->setMetadata(span.lo - *base, span.hi - *base + 1, QColor(), colour, QString());
            shown.insert(span.kind);
        }
        if (settings.Uses(MetadataSettings::Switch::Notes))
            for (const markup::CommentAt& comment : markup->Comments(*base, static_cast<uint16_t>(*base + 0x3FFF)))
                metadata->setComment(comment.address - *base, comment.address - *base + 1,
                                 QString::fromStdString(comment.text));
    }
    if (base && _emulator->IsPaused() && _emulator->GetZ80State())
    {
        const uint16_t pc = _emulator->GetZ80State()->Z80Registers::pc;
        if ((pc & 0xC000) == *base)
            metadata->setMetadata(pc & 0x3FFF, (pc & 0x3FFF) + 1, Qt::black, QColor(255, 255, 140), tr("PC"));
    }

    if (_markupLegend)
    {
        QString legend;
        for (analysis::RegionKind kind : shown)
            legend += QString("<span style='background:%1'>&nbsp;%2&nbsp;</span> ")
                          .arg(MarkupKindColour(kind).name(), analysis::RegionKindName(kind));
        _markupLegend->setText(legend);
        _markupLegend->setVisible(!shown.empty());
    }
    ui->hexView->update();
}

void DebuggerWindow::onBindingNotReady()
{
    qDebug() << "DebuggerWindow::onBindingNotReady()";

    // Clear hexView (not connected to signal)
    ui->hexView->reset();

    // Dispatch to children via signal (widgets are connected to notReadyForChildren)
    emit notReadyForChildren();
}

void DebuggerWindow::reset()
{
    ui->registersWidget->setZ80State(nullptr);  // Detach from the (possibly destroyed) emulator
    ui->registersWidget->reset();
    ui->hexView->reset();

    QHexOptions options = ui->hexView->options();
    options.linelength = 8;
    options.addresswidth = 4;
    options.flags = QHexFlags::HSeparator | QHexFlags::VSeparator;
    ui->hexView->setOptions(options);

    ui->memorypagesWidget->reset();
    ui->stackWidget->reset();
    ui->disassemblerWidget->reset();  // Clear disassembler content
    ui->fdcStatusWidget->reset();     // Re-seed FDC status strip from the current binding

    // Only update state if we have an emulator
    if (_emulator)
    {
        updateState();
    }
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
    // Block refreshes during shutdown to prevent crash
    if (_isClosing)
    {
        return;
    }

    // A refresh queued by a step's end (cpuStepComplete) or a frame can run while the machine is being removed (an
    // automation 'destroy' right after a 'run_frames'): its memory is freed under the disassembler (seen as a SIGSEGV
    // in Z80Disassembler with a null Memory). The lease keeps the removal from freeing anything until this refresh
    // ends, and is refused once the removal has begun
    Emulator::ContextLease lease;
    if (_emulator)
    {
        lease = _emulator->LeaseContext();
        if (!lease)
            return;
    }

    // Skip heavy UI updates while emulator is running to avoid race conditions
    // and excessive CPU usage (frameRefresh fires at 50fps during recording)
    if (_emulator && !_emulator->IsPaused())
    {
        return;
    }
    // Automation is stepping the machine on another thread (RunNFrames, ...): its memory map changes under
    // a reader (this crashed in the disassembler and the stack view). The end of the stepping refreshes
    if (_emulator && _emulator->IsDirectStepping())
    {
        return;
    }

    if (_emulator)
    {
        Z80State* state = _emulator->GetZ80State();

        // Refresh registers widget
        ui->registersWidget->setZ80State(state);
        ui->registersWidget->refresh();

        // Update disassembler widget: it follows PC itself (the view stays while PC is inside its margins)
        ui->disassemblerWidget->refresh();

        // Update memory banks widget
        ui->memorypagesWidget->refresh();

        // Update stack widget
        ui->stackWidget->refresh();

        _pcHistoryPane->refresh();
        showMemoryAtPc();
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

    // Note: debug instrumentation (DebugOn) is NOT switched on here. MainWindow
    // enables it only while this window is visible, so a hidden debugger costs
    // the emulator nothing (fast memory interface, no breakpoint dispatch).

    /// <Test>
    // brkManager.AddExecutionBreakpoint(0x272E);  // ROM128K::$272E - MENU_MOVE_UP
    // brkManager.AddExecutionBreakpoint(0x2731);  // ROM128K::$2731 - MENU_MOVE_DOWN

    // brkManager.AddExecutionBreakpoint(0x37A7);  // ROM128K::$37A7 - MENU_MOVE_UP
    // brkManager.AddExecutionBreakpoint(0x37B6);  // ROM128K::$37B6 - MENU_MOVE_DOWN

    // brkManager.AddExecutionBreakpoint(0x38A2);    // ROM48K:$38A2
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
void DebuggerWindow::updateToolbarActions(bool canContinue, bool canPause, bool canStep, bool canReset,
                                          bool canManageBreakpoints, bool canManageLabels)
{


    // Defensive checks - ensure all actions exist before accessing them
    if (!continueAction || !pauseAction || !resetAction || !stepInAction || !stepOverAction || !stepOutAction ||
        !frameStepAction || !waitInterruptAction || !breakpointsAction || !labelsAction)
    {
        qWarning() << "DebuggerWindow::updateToolbarActions - One or more actions are null!";
        return;
    }

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

/// endregion </Helper methods>

/// region <QT Helper methods>

/// Dispatch callback execution in QT main thread (GUI rendering)
/// @param callback
void DebuggerWindow::dispatchToMainThread(std::function<void()> callback)
{
    QThread* mainThread = qApp->thread();
    QThread* currentThread = QThread::currentThread();

    if (currentThread == mainThread)
    {
        // Direct execution - check closing flag
        if (!_isClosing)
        {
            callback();
        }
    }
    else
    {
        // Cross-thread dispatch - use QPointer to guard against object destruction
        QPointer<DebuggerWindow> guard(this);
        QTimer* timer = new QTimer();
        timer->moveToThread(qApp->thread());
        timer->setSingleShot(true);

        // This lambda will be called from main thread
        QObject::connect(timer, &QTimer::timeout, [=]() {
            // Check if DebuggerWindow still exists before executing callback
            if (guard && !guard->_isClosing)
            {
                try
                {
                    callback();
                }
                catch (...)
                {
                    // Just to prevent main thread from crashing
                }
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

    SimpleNumberPayload* payload = static_cast<SimpleNumberPayload*>(message->obj);
    EmulatorStateEnum newState = static_cast<EmulatorStateEnum>(payload->_payloadNumber);

    // NOTE: State change events are broadcast globally by all emulators
    // We must verify the state change is from OUR tracked emulator before reacting
    // If we don't have an emulator, ignore all state changes
    if (!_emulator)
    {
        return;
    }

    // Verify this state change is from OUR emulator by checking its actual state
    // (since messages don't carry emulator ID, we can't tell which emulator it's from)
    if (_emulator->GetState() != newState)
    {
        // State change is from a different emulator, ignore it
        return;
    }

    _emulatorState = newState;
    qDebug() << "DebuggerWindow::handleEmulatorStateChanged(" << getEmulatorStateName(_emulatorState) << ")";

    dispatchToMainThread([this]() {
        switch (_emulatorState)
        {
            case StateUnknown:
            case StateStopped: {
                // When emulator is stopped:
                // (Continue: OFF, Pause: OFF, Step: OFF, Reset: OFF, Breakpoints: OFF, Labels: OFF)
                updateToolbarActions(false, false, false, false, false, false);

                // Emulator already stopped working.
                // Time to disable all rendering activities and set controls to initial inactive state
                _emulator = nullptr;

                propagateEmulator();

                // Don't call reset() here since emulator is now null
                // Just clear the UI manually without trying to read emulator state
                ui->registersWidget->reset();
                ui->hexView->reset();

                QHexOptions options = ui->hexView->options();
                options.linelength = 8;
                options.addresswidth = 4;
                options.flags = QHexFlags::HSeparator | QHexFlags::VSeparator;
                ui->hexView->setOptions(options);

                ui->memorypagesWidget->reset();
                ui->stackWidget->reset();

                // Don't call updateState() since emulator is null
                break;
            }

            case StateInitialized:
            default:
                // When emulator is initialized:
                // (Continue: OFF, Pause: ON, Step: OFF, Reset: OFF, Breakpoints: ON, Labels: ON)
                updateToolbarActions(false, true, false, false, true, true);
                updateState();  // Safe to update when initialized
                break;

            case StateRun:
            case StateResumed:
                // When emulator is running:
                // (Continue: OFF, Pause: ON, Step: OFF, Reset: ON, Breakpoints: ON)
                updateToolbarActions(false, true, false, true, true, true);
                // DO NOT call updateState() while running - race condition!
                break;

            case StatePaused:
                // When emulator is paused:
                // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON, Labels: ON)
                updateToolbarActions(true, false, true, true, true, true);
                updateState();  // Safe to update when paused
                break;
        }
    });
}

void DebuggerWindow::onBreakpointHit(const BreakpointHit& hit)
{
    if (_isClosing || !_emulator || hit.emulatorId != _emulator->GetId())
        return;

    const uint16_t breakpointID = hit.id;
    BreakpointManager* bpManager = _emulator->GetDebugManager()->GetBreakpointsManager();
    if (!bpManager)
    {
        return;
    }

    // A copy of the breakpoint with that ID
    const BreakpointSnapshot breakpoint = bpManager->GetBreakpointById(breakpointID);
    if (!breakpoint)
    {
        return;
    }

    // Handle interrupt breakpoints if we're waiting for an interrupt
    if (_waitingForInterrupt)
    {
        // For IM2, we might hit a vector table read breakpoint first
        if (breakpoint->type == BRK_MEMORY && (breakpoint->memoryType & BRK_MEM_READ))
        {
            EmulatorContext* context = _emulator ? _emulator->GetContext() : nullptr;
            if (!context || !context->pCore)
                return;
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
            _emulator->Pause(execution::Requester{execution::Origin::Gui, {}}, "debugger");

            // Update the UI
            updateState();
            return;
        }
    }

    // Handle step over/out breakpoints
    if (breakpoint->note == STEP_OVER_NOTE)
    {
        // Step-over breakpoints are handled by core Emulator::StepOver() lambda
        // No action needed here - just let UI update proceed
    }
    else if (breakpoint->note == STEP_OUT_NOTE)
    {
        _inStepOutOperation = false;
        bpManager->RemoveBreakpointByID(breakpointID);
        _stepOutBreakpointID = BRK_INVALID;
    }

    // Stop speed control auto-run on breakpoint hit
    if (m_speedControl)
    {
        m_speedControl->stop();
        m_speedControl->setMachineRunning(false);
    }

    // The user's breakpoints speak; the step traps and the owners' (BASIC typer, adapters) stay silent
    if (BreakpointManager::IsUserBreakpoint(*breakpoint) && !breakpoint->hidden)
    {
        _stopBreakpointId = breakpointID;
        _stopBanner->setText(BreakpointStopLine(*breakpoint, hit));
        _stopBanner->setVisible(true);
    }

    // When a breakpoint is hit:
    // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON, Labels: ON)
    updateToolbarActions(true, false, true, true, true, true);
    updateState();
}

void DebuggerWindow::clearStopBanner()
{
    _stopBreakpointId = BRK_INVALID;
    if (_stopBanner)
        _stopBanner->setVisible(false);
}

void DebuggerWindow::onStopBannerLink(const QString& link)
{
    if (!_emulator)
        return;
    if (link == "edit" && _stopBreakpointId != BRK_INVALID)
    {
        if (!_emulator->GetBreakpointManager()->GetBreakpointById(_stopBreakpointId))
        {
            clearStopBanner();  // removed meanwhile
            return;
        }
        BreakpointEditor editor(_emulator, BreakpointEditor::Edit, _stopBreakpointId, this);
        editor.exec();
        updateState();
    }
    else if (link == "list")
        showBreakpointManager();
}

void DebuggerWindow::handleCPUStepMessage(int id, Message* message)
{
    qDebug() << "DebuggerWindow::handleCPUStepMessage()";

    if (_emulator && _emulator->IsPaused())
    {
        // Update debugger state in the main thread
        dispatchToMainThread([this]() { updateState(); });
    }
}

void DebuggerWindow::handleLabelChanged(int id, Message* message)
{
    qDebug() << "DebuggerWindow::handleLabelChanged() - Refreshing disassembler view due to label changes";

    // Forward the refresh to the disassembler widget on the main thread
    dispatchToMainThread([this]() { ui->disassemblerWidget->refresh(); });
}

void DebuggerWindow::continueExecution()
{
    clearStopBanner();  // the machine moves on
    qDebug() << "DebuggerWindow::continueExecution()";

    _breakpointTriggered = false;

    // Stop speed control auto-run when user manually continues
    if (m_speedControl)
        m_speedControl->stop();

    if (_emulator && _emulator->IsPaused())
    {
        _emulator->Resume(execution::Requester{execution::Origin::Gui, {}});

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

    // Stop speed control auto-run when user manually pauses
    if (m_speedControl)
        m_speedControl->stop();

    if (_emulator && _emulator->IsRunning() && !_emulator->IsPaused())
    {
        _emulator->Pause(execution::Requester{execution::Origin::Gui, {}}, "debugger");
        _emulator->DebugOn();

        // When emulator is paused:
        // (Continue: ON, Pause: OFF, Step: ON, Reset: ON, Breakpoints: ON)
        updateToolbarActions(true, false, true, true, true, true);

        updateState();
    }
}

void DebuggerWindow::stepIn()
{
    clearStopBanner();  // the machine moves on
    qDebug() << "DebuggerWindow::stepIn()";

    _breakpointTriggered = false;

    // Stop speed control auto-run when user manually steps
    if (m_speedControl)
        m_speedControl->stop();

    if (_emulator)
    {
        // Execute single Z80 command (Step execution does not trigger any breakpoints)
        bool skipBreakpoints = true;
        _emulator->RunSingleCPUCycle(skipBreakpoints);

        updateState();
    }
}

void DebuggerWindow::stepOver()
{
    clearStopBanner();  // the machine moves on
    qDebug() << "DebuggerWindow::stepOver()";

    _breakpointTriggered = false;

    // Stop speed control auto-run when user manually steps
    if (m_speedControl)
        m_speedControl->stop();

    if (!_emulator)
        return;

    // Use the new core Emulator::StepOver() method
    // Note: For step-in fallback (non-call instructions), this executes synchronously
    // and we need to update UI. For actual step-over (call instructions), the UI
    // will also update via breakpoint notification, but updating here is harmless.
    _emulator->StepOver();

    // Always update UI - handles both step-in fallback and ensures immediate feedback
    updateState();
}

void DebuggerWindow::stepOut()
{
    clearStopBanner();  // the machine moves on
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

        qDebug() << "Step Out: Return address found at"
                 << QString("0x%1").arg(returnAddress, 4, 16, QLatin1Char('0')).toUpper();

        // 2. Set a temporary breakpoint at the return address
        BreakpointManager* breakpointManager = _emulator->GetBreakpointManager();

        // Create a breakpoint descriptor with the note field already set
        BreakpointDescriptor* bpDesc = new BreakpointDescriptor();
        bpDesc->type = BreakpointTypeEnum::BRK_MEMORY;
        bpDesc->memoryType = BRK_MEM_EXECUTE;
        bpDesc->z80address = returnAddress;
        bpDesc->note = STEP_OUT_NOTE;
        bpDesc->hidden = true;

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

            qDebug() << "Step Out: Successfully set breakpoint ID:" << _stepOutBreakpointID
                     << "at address:" << QString("0x%1").arg(returnAddress, 4, 16, QLatin1Char('0')).toUpper();

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
    clearStopBanner();  // the machine moves on
    qDebug() << "DebuggerWindow::frameStep()";

    _breakpointTriggered = false;

    // Stop speed control auto-run when user manually frame-steps
    if (m_speedControl)
        m_speedControl->stop();

    if (_emulator)
    {
        // Execute one complete video frame
        bool skipBreakpoints = true;
        _emulator->RunFrame(skipBreakpoints);

        updateState();
    }
}

void DebuggerWindow::waitInterrupt()
{
    clearStopBanner();  // the machine moves on
    qDebug() << "DebuggerWindow::waitInterrupt()";

    if (!_emulator)
    {
        qWarning() << "No emulator instance";
        return;
    }

    // Clear any existing interrupt breakpoints
    clearInterruptBreakpoints();

    EmulatorContext* context = _emulator->GetContext();
    Z80* cpu = context && context->pCore ? context->pCore->GetZ80() : nullptr;
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
        qDebug() << "Set IM2 interrupt handler breakpoint at"
                 << QString("0x%1").arg(interruptHandler, 4, 16, QLatin1Char('0')).toUpper();
    }

    _waitingForInterrupt = true;
    updateState();

    // Continue execution automatically if paused
    if (_emulator && _emulator->IsPaused())
    {
        continueExecution();
    }
}

void DebuggerWindow::portOut()
{
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    if (!_emulator) return;

    bool ok;
    const QString text = QInputDialog::getText(this, "Port OUT", "Port and value (e.g. #13AF #20, 0x7FFD 16):",
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) return;

    const QStringList parts = text.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    uint16_t port = 0;
    uint8_t value = 0;
    std::string error = "enter a port and a value";
    if (parts.size() != 2 ||
        !PortWrite::Parse(parts[0].toStdString(), parts[1].toStdString(), port, value, error))
    {
        QMessageBox::warning(this, "Port OUT", QString::fromStdString(error));
        return;
    }
    const PortWrite::Result result = PortWrite::Write(_emulator.get(), port, value, "qt");
    if (!result.ok)
    {
        QMessageBox::warning(this, "Port OUT", QString::fromStdString(result.error));
        return;
    }
    updateState();
}

void DebuggerWindow::showDeviceMemory()
{
    if (!_emulator) return;

    DeviceMemoryDialog dialog(_emulator, this);
    dialog.exec();
    updateState();
}

void DebuggerWindow::showDiskSector()
{
    if (!_emulator) return;

    DiskSectorDialog dialog(_emulator, this);
    dialog.exec();
    updateState();
}

void DebuggerWindow::showDiskFiles()
{
    if (!_emulator) return;

    DiskFilesDialog dialog(_emulator, this);
    dialog.exec();
    updateState();
}

void DebuggerWindow::showLiveSource()
{
    if (!_emulator)
        return;
    if (!_liveSourceWindow)
    {
        _liveSourceWindow = new LiveSourceWindow(_emulator, this);
        connect(_liveSourceWindow, &QObject::destroyed, this, [this]() { _liveSourceWindow = nullptr; });
    }
    _liveSourceWindow->show();
    _liveSourceWindow->raise();
    _liveSourceWindow->activateWindow();
}

void DebuggerWindow::showPcHistory(bool shown)
{
    _pcHistoryPane->parentWidget()->setVisible(shown);
    _pcHistoryPane->setRecording(shown);
}

void DebuggerWindow::showPortWatch()
{
    if (!_emulator) return;

    PortWatchDialog dialog(_emulator, this);
    dialog.exec();
}

void DebuggerWindow::showRtc()
{
    if (!_emulator) return;

    RtcDialog dialog(_emulator, this);
    dialog.exec();
    updateState();
}

void DebuggerWindow::showPortEvents()
{
    if (!_emulator) return;

    // Not modal: the program runs (and records) while the window stays open
    if (!_portEventsDialog)
    {
        _portEventsDialog = new PortEventsDialog(_emulator, this);
        _portEventsDialog->setAttribute(Qt::WA_DeleteOnClose);
        // A hit sought: the panes show the machine there, the disassembly the instruction that made the access
        connect(_portEventsDialog, &PortEventsDialog::seekedTo, this, [this](uint16_t pc) {
            updateState();
            ui->disassemblerWidget->goToAddress(pc);
        });
    }
    _portEventsDialog->show();
    _portEventsDialog->raise();
    _portEventsDialog->activateWindow();
}

void DebuggerWindow::runTStates()
{
    clearStopBanner();  // the machine moves on
    if (!_emulator) return;

    bool ok;
    int tstates = QInputDialog::getInt(this, "Run T-States", "Number of T-states to execute:", 100, 1, 10000000, 1, &ok);
    if (!ok) return;

    _breakpointTriggered = false;
    _emulator->RunTStates(static_cast<unsigned>(tstates));
    updateState();
}

void DebuggerWindow::runToScanline()
{
    clearStopBanner();  // the machine moves on
    if (!_emulator) return;

    bool ok;
    int scanline = QInputDialog::getInt(this, "Run to Scanline", "Target scanline number:", 0, 0, 319, 1, &ok);
    if (!ok) return;

    _breakpointTriggered = false;
    _emulator->RunUntilScanline(static_cast<unsigned>(scanline));
    updateState();
}

void DebuggerWindow::runNScanlines()
{
    clearStopBanner();  // the machine moves on
    if (!_emulator) return;

    bool ok;
    int count = QInputDialog::getInt(this, "Run N Scanlines", "Number of scanlines to run:", 1, 1, 1000, 1, &ok);
    if (!ok) return;

    _breakpointTriggered = false;
    _emulator->RunNScanlines(static_cast<unsigned>(count));
    updateState();
}

void DebuggerWindow::runToPixel()
{
    clearStopBanner();  // the machine moves on
    if (!_emulator) return;

    _breakpointTriggered = false;
    _emulator->RunUntilNextScreenPixel();
    updateState();
}

void DebuggerWindow::runToInterrupt()
{
    clearStopBanner();  // the machine moves on
    if (!_emulator) return;

    _breakpointTriggered = false;
    _emulator->RunUntilInterrupt();
    updateState();
}

void DebuggerWindow::resetEmulator()
{
    clearStopBanner();  // the machine moves on
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

std::vector<std::pair<QWidget*, std::string>> DebuggerWindow::contextPanes() const
{
    return {{ui->disassemblerWidget, "debug.disasm"},
            {ui->registersWidget, "debug.registers"},
            {ui->hexView, "debug.memory"},
            {ui->stackWidget, "debug.stack"}};
}

void DebuggerWindow::toggleBreakpointAtCursor()
{
    // The widget refuses while an automation session holds the machine (RemoteControlGuard) and repaints its marks
    ui->disassemblerWidget->toggleBreakpointAtCursor();
}

void DebuggerWindow::openBreakpoints()
{
    showBreakpointManager();
}

void DebuggerWindow::focusRegistersPane()
{
    _paneFocus->Focus(1);
}

void DebuggerWindow::focusMemoryPane()
{
    _paneFocus->Focus(2);
}

void DebuggerWindow::focusNextPane()
{
    _paneFocus->Move(1);
}

void DebuggerWindow::focusPrevPane()
{
    _paneFocus->Move(-1);
}

void DebuggerWindow::paneKey(int key)
{
    _paneFocus->Key(key);
}

void DebuggerWindow::followPc()
{
    ui->disassemblerWidget->followPc();
}

void DebuggerWindow::toggleScrollMode()
{
    ui->disassemblerWidget->switchScrollMode();
}

void DebuggerWindow::goToDisassemblyAddress(int address)
{
    ui->disassemblerWidget->goToAddress(static_cast<uint16_t>(address & 0xFFFF));
}

void DebuggerWindow::publishMemoryCursor()
{
    if (!_emulator || !ui->hexView->getDocument())
        return;
    // The view shows one 16 KB page: the Z80 window that maps it now names the address
    Memory* memory = _emulator->GetMemory();
    if (memory == nullptr)
        return;
    const uint16_t offset = static_cast<uint16_t>(ui->hexView->offset() & 0x3FFF);
    for (uint8_t bank = 0; bank < 4; bank++)
        if (memory->GetPhysicalOffsetForZ80Bank(bank) == _curPageOffset)
        {
            actions::DebuggerCursors::Instance().Set(_emulator->GetId(), "memory",
                                                     static_cast<uint16_t>(bank * 0x4000 + offset));
            return;
        }
    actions::DebuggerCursors::Instance().Set(_emulator->GetId(), "memory", std::nullopt);  // not mapped now
}

DisassemblerWidget* DebuggerWindow::disassembler() const
{
    return ui->disassemblerWidget;
}

QHexView* DebuggerWindow::memoryView() const
{
    return ui->hexView;
}

const std::vector<QString>& DebuggerWindow::paneNames()
{
    // The order of the focus ring (_paneFocus)
    static const std::vector<QString> names = {QStringLiteral("disassembly"), QStringLiteral("registers"),
                                               QStringLiteral("memory"), QStringLiteral("stack")};
    return names;
}

QString DebuggerWindow::focusedPane() const
{
    const int focused = _paneFocus->Focused();
    const int index = focused >= 0 ? focused : _paneFocus->Last();
    return index >= 0 && static_cast<size_t>(index) < paneNames().size() ? paneNames()[static_cast<size_t>(index)]
                                                                          : QString();
}

bool DebuggerWindow::focusPane(const QString& name)
{
    const auto it = std::find(paneNames().begin(), paneNames().end(), name);
    if (it == paneNames().end())
        return false;
    _paneFocus->Focus(static_cast<int>(it - paneNames().begin()));
    return true;
}

void DebuggerWindow::movePaneFocus(int delta)
{
    _paneFocus->Move(delta);
}

void DebuggerWindow::sendPaneKey(int key)
{
    _paneFocus->Key(key);
}

bool DebuggerWindow::showMemoryAt(uint16_t address)
{
    if (!_emulator || !_emulator->GetMemory())
        return false;
    changeMemoryViewZ80Address(address);
    return true;
}

std::optional<uint16_t> DebuggerWindow::memoryCursor() const
{
    const std::optional<uint16_t> base = memoryViewBase();
    if (!base || !ui->hexView->getDocument())
        return std::nullopt;
    return static_cast<uint16_t>(*base + (ui->hexView->offset() & 0x3FFF));
}

bool DebuggerWindow::setMemoryFollow(const QString& name)
{
    static const QStringList names = {"pc", "sp", "bc", "de", "hl", "ix", "iy", "off"};
    if (!names.contains(name))
        return false;
    _memoryFollow = name;
    showMemoryAtPc();
    emit memoryViewChanged();
    return true;
}

void DebuggerWindow::changeMemoryViewZ80Address(uint16_t addr)
{
    qDebug("DebuggerWindow::changeMemoryViewZ80Address");

    Memory* memory = _emulator ? _emulator->GetMemory() : nullptr;
    if (!memory)
        return;
    uint8_t bank = memory->GetZ80BankFromAddress(addr);
    uint8_t* pageAddress = memory->GetPhysicalAddressForZ80Page(bank);
    if (!pageAddress)
        return;
    _curPageOffset = memory->GetPhysicalOffsetForZ80Bank(bank);
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;
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

    qDebug(
        "DebuggerWindow::changeMemoryViewAddress - address: %p, size: 0x%04X, offset: 0x%02X, currentAddress: 0x%02X",
        address, (uint16_t)size, offset, currentAddress);
    ;

    QByteArray data((const char*)address, size);
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(data);
    ui->hexView->setBaseAddress(offset);  // Set base offset for the whole hex view
    ui->hexView->setDocument(document);

    // Note: change offset position only after assigning document to HexView
    // otherwise widget is unaware of the document and where to jump so just skipping the request
    ui->hexView->gotoOffset(currentAddress);  // Position cursor on the byte with offset
    paintMemoryMetadata();
    emit memoryViewChanged();
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
    updateState();  // Refresh in case labels changed that affect disassembly, etc.
}

void DebuggerWindow::showVisualizationWindow()
{
    qDebug() << "DebuggerWindow::showVisualizationWindow()";

    if (!_emulator)
    {
        QMessageBox::warning(this, "Warning", "No emulator selected");
        return;
    }

    // Create visualization window if it doesn't exist yet
    if (!_visualizationWindow)
    {
        _visualizationWindow = new DebugVisualizationWindow(_emulator);
        // Set window flags to stay on top of the debugger window
        _visualizationWindow->setWindowFlags(_visualizationWindow->windowFlags() | Qt::Window);

        // Connect window closed signal to reset our pointer
        connect(_visualizationWindow, &QObject::destroyed, [this]() { _visualizationWindow = nullptr; });
    }

    // Show and activate the window
    _visualizationWindow->show();
    _visualizationWindow->activateWindow();
    _visualizationWindow->raise();
}

/// endregion </Event handlers / Slots>

void DebuggerWindow::prepareForShutdown()
{
    qDebug() << "DebuggerWindow::prepareForShutdown() - Propagating to all child widgets";

    // Set our own flag first
    _isClosing = true;

    // Propagate to all child widgets that have refresh methods
    if (ui)
    {
        if (ui->disassemblerWidget)
        {
            ui->disassemblerWidget->prepareForShutdown();
        }
        if (ui->registersWidget)
        {
            ui->registersWidget->prepareForShutdown();
        }
        if (ui->memorypagesWidget)
        {
            ui->memorypagesWidget->prepareForShutdown();
        }
        if (ui->stackWidget)
        {
            ui->stackWidget->prepareForShutdown();
        }
        // hexView is a 3rd party widget without our shutdown pattern
    }

    // Propagate to visualization window (separate window, not in UI)
    if (_visualizationWindow)
    {
        _visualizationWindow->prepareForShutdown();
        _visualizationWindow->close();
        delete _visualizationWindow;
        _visualizationWindow = nullptr;
    }

    qDebug() << "DebuggerWindow::prepareForShutdown() - All child widgets notified";
}
