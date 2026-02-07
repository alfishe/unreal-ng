#include "debugvisualizationwindow.h"

#include <QDebug>
#include <QMetaObject>
#include <QTimer>

#include "base/featuremanager.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "ui_debugvisualizationwindow.h"
#include "widgets/bordertimingwidget.h"
#include "widgets/floppydiskwidget.h"
#include "widgets/memorypagesviswidget.h"
#include "widgets/memorywidget.h"
#include "widgets/ulabeamwidget.h"

DebugVisualizationWindow::DebugVisualizationWindow(Emulator* emulator, QWidget* parent)
    : QWidget(parent), ui(new Ui::DebugVisualizationWindow)
{
    _emulator = emulator;

    // Setup UI
    ui->setupUi(this);

    // Create widgets
    _memoryWidget = new MemoryWidget(this);
    _memoryPagesWidget = new MemoryPagesVisWidget(this);
    _ulaBeamWidget = new ULABeamWidget(this);
    _borderTimingWidget = new BorderTimingWidget(this);
    _floppyDiskWidget = new FloppyDiskWidget(this);

    // Add widgets to layout
    QGridLayout* layout = ui->mainLayout;
    layout->addWidget(_memoryWidget, 0, 0, 1, 2);
    layout->addWidget(_memoryPagesWidget, 0, 2, 1, 1);
    layout->addWidget(_ulaBeamWidget, 1, 0, 1, 1);
    layout->addWidget(_borderTimingWidget, 1, 1, 1, 1);
    layout->addWidget(_floppyDiskWidget, 1, 2, 1, 1);

    // Connect page matrix clicks to free memory viewers
    // Signal emits (pageNumber, viewerSlot), slot expects (viewerSlot, pageNumber)
    connect(_memoryPagesWidget, &MemoryPagesVisWidget::pageClickedForFreeViewer,
            _memoryWidget, [this](int pageNumber, int viewerSlot) {
                _memoryWidget->setFreePageNumber(viewerSlot, pageNumber);
            });

    // Column stretches: ~75% for memory views (cols 0+1), ~25% for right panel (col 2)
    layout->setColumnStretch(0, 37);
    layout->setColumnStretch(1, 37);
    layout->setColumnStretch(2, 26);

    layout->setRowStretch(0, 3);
    layout->setRowStretch(1, 2);

    // Set up update timer (for UI refresh)
    _updateTimer = new QTimer(this);
    connect(_updateTimer, &QTimer::timeout, this, &DebugVisualizationWindow::updateWidgets);
    _updateTimer->start(20);

    // Connect signal for main thread execution
    connect(this, &DebugVisualizationWindow::executeInMainThread, this, &DebugVisualizationWindow::updateWidgets);

    if (ui->memoryTrackingCheckbox)
        connect(ui->memoryTrackingCheckbox, &QCheckBox::toggled, this,
                &DebugVisualizationWindow::onMemoryTrackingToggled);
    if (ui->callTraceCheckbox)
        connect(ui->callTraceCheckbox, &QCheckBox::toggled, this, &DebugVisualizationWindow::onCallTraceToggled);
    if (ui->opcodeProfilerCheckbox)
        connect(ui->opcodeProfilerCheckbox, &QCheckBox::toggled, this,
                &DebugVisualizationWindow::onOpcodeProfilerToggled);
    if (ui->resetCountersButton)
        connect(ui->resetCountersButton, &QPushButton::clicked, this,
                &DebugVisualizationWindow::onResetCountersClicked);

    syncFeatureCheckboxes();

    if (_emulator)
    {
        // Propagate emulator to child widgets
        if (_memoryWidget)
            _memoryWidget->setEmulator(_emulator);
        if (_memoryPagesWidget)
            _memoryPagesWidget->setEmulator(_emulator);
        if (_ulaBeamWidget)
            _ulaBeamWidget->setEmulator(_emulator);
        if (_borderTimingWidget)
            _borderTimingWidget->setEmulator(_emulator);
        if (_floppyDiskWidget)
            _floppyDiskWidget->setEmulator(_emulator);

        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

        // Create and store lambda functions for observers
        _stateChangeObserver = [this](int id, Message* message) { this->handleEmulatorStateChanged(id, message); };

        _cpuStepObserver = [this](int id, Message* message) { this->handleCPUStepMessage(id, message); };

        _frameRefreshObserver = [this](int id, Message* message) {
            QMetaObject::invokeMethod(this, "updateWidgets", Qt::QueuedConnection);
        };

        // Register the observers
        messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
        messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
        messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserver);
    }

    setWindowTitle("Debug Visualization");
    resize(800, 600);
}

DebugVisualizationWindow::~DebugVisualizationWindow()
{
    // Unsubscribe from MessageCenter using the stored observer functions
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    if (_stateChangeObserver)
    {
        messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
    }

    if (_cpuStepObserver)
    {
        messageCenter.RemoveObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
    }

    if (_frameRefreshObserver)
    {
        messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserver);
    }

    delete ui;
}

void DebugVisualizationWindow::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    // Unsubscribe existing observers
    if (_stateChangeObserver)
    {
        messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
    }

    if (_cpuStepObserver)
    {
        messageCenter.RemoveObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
    }

    if (_frameRefreshObserver)
    {
        messageCenter.RemoveObserver(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserver);
    }

    // Subscribe to new emulator events
    if (_emulator)
    {
        // Create and store lambda functions for observers
        _stateChangeObserver = [this](int id, Message* message) { this->handleEmulatorStateChanged(id, message); };

        _cpuStepObserver = [this](int id, Message* message) { this->handleCPUStepMessage(id, message); };

        _frameRefreshObserver = [this](int id, Message* message) {
            QMetaObject::invokeMethod(this, "updateWidgets", Qt::QueuedConnection);
        };

        // Register the observers
        messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
        messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
        messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserver);

        // Update widgets with new emulator
        if (_memoryWidget)
            _memoryWidget->setEmulator(_emulator);
        if (_memoryPagesWidget)
            _memoryPagesWidget->setEmulator(_emulator);
        if (_ulaBeamWidget)
            _ulaBeamWidget->setEmulator(_emulator);
        if (_borderTimingWidget)
            _borderTimingWidget->setEmulator(_emulator);
        if (_floppyDiskWidget)
            _floppyDiskWidget->setEmulator(_emulator);

        syncFeatureCheckboxes();
    }

    updateState();
}

Emulator* DebugVisualizationWindow::getEmulator()
{
    return _emulator;
}

void DebugVisualizationWindow::reset()
{
    if (_memoryWidget)
        _memoryWidget->reset();
    if (_memoryPagesWidget)
        _memoryPagesWidget->reset();
    if (_ulaBeamWidget)
        _ulaBeamWidget->reset();
    if (_borderTimingWidget)
        _borderTimingWidget->reset();
    if (_floppyDiskWidget)
        _floppyDiskWidget->reset();
}

void DebugVisualizationWindow::updateState()
{
    if (_isShuttingDown || !_emulator)
        return;

    _emulatorState = _emulator->GetState();

    // Update widgets based on emulator state
    dispatchToMainThread([this]() {
        if (_memoryWidget)
            _memoryWidget->refresh();
        if (_memoryPagesWidget)
            _memoryPagesWidget->refresh();
        if (_ulaBeamWidget)
            _ulaBeamWidget->refresh();
        if (_borderTimingWidget)
            _borderTimingWidget->refresh();
        if (_floppyDiskWidget)
            _floppyDiskWidget->refresh();
    });
}

void DebugVisualizationWindow::dispatchToMainThread(std::function<void()> callback)
{
    // Execute in main thread using Qt's event loop
    QMetaObject::invokeMethod(this, "executeInMainThread", Qt::QueuedConnection);
}

void DebugVisualizationWindow::handleEmulatorStateChanged(int id, Message* message)
{
    updateState();
}

void DebugVisualizationWindow::handleCPUStepMessage(int id, Message* message)
{
    // Update on CPU step (when in debug mode)
    updateState();
}

void DebugVisualizationWindow::updateWidgets()
{
    if (_isShuttingDown || !_emulator)
        return;

    // Update all widgets
    if (_memoryWidget)
        _memoryWidget->refresh();
    if (_memoryPagesWidget)
        _memoryPagesWidget->refresh();
    if (_ulaBeamWidget)
        _ulaBeamWidget->refresh();
    if (_borderTimingWidget)
        _borderTimingWidget->refresh();
    if (_floppyDiskWidget)
        _floppyDiskWidget->refresh();
}

void DebugVisualizationWindow::prepareForShutdown()
{
    qDebug() << "DebugVisualizationWindow::prepareForShutdown()";
    _isShuttingDown = true;
}

void DebugVisualizationWindow::syncFeatureCheckboxes()
{
    if (!_emulator)
        return;

    FeatureManager* fm = _emulator->GetFeatureManager();
    if (!fm)
        return;

    _updatingCheckboxes = true;

    if (ui->memoryTrackingCheckbox)
        ui->memoryTrackingCheckbox->setChecked(fm->isEnabled(Features::kMemoryTracking));
    if (ui->callTraceCheckbox)
        ui->callTraceCheckbox->setChecked(fm->isEnabled(Features::kCallTrace));
    if (ui->opcodeProfilerCheckbox)
        ui->opcodeProfilerCheckbox->setChecked(fm->isEnabled(Features::kOpcodeProfiler));

    _updatingCheckboxes = false;
}

void DebugVisualizationWindow::onMemoryTrackingToggled(bool checked)
{
    if (_updatingCheckboxes || !_emulator)
        return;

    FeatureManager* fm = _emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature(Features::kMemoryTracking, checked);

        Memory* memory = _emulator->GetMemory();
        if (memory)
        {
            MemoryAccessTracker& tracker = memory->GetAccessTracker();
            if (checked)
                tracker.StartMemorySession();
            else
                tracker.StopMemorySession();
        }
    }
}

void DebugVisualizationWindow::onCallTraceToggled(bool checked)
{
    if (_updatingCheckboxes || !_emulator)
        return;

    FeatureManager* fm = _emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature(Features::kCallTrace, checked);

        Memory* memory = _emulator->GetMemory();
        if (memory)
        {
            MemoryAccessTracker& tracker = memory->GetAccessTracker();
            if (checked)
                tracker.StartCalltraceSession();
            else
                tracker.StopCalltraceSession();
        }
    }
}

void DebugVisualizationWindow::onOpcodeProfilerToggled(bool checked)
{
    if (_updatingCheckboxes || !_emulator)
        return;

    FeatureManager* fm = _emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature(Features::kOpcodeProfiler, checked);
    }
}

void DebugVisualizationWindow::onResetCountersClicked()
{
    if (!_emulator)
        return;

    Memory* mem = _emulator->GetMemory();
    if (mem)
    {
        mem->GetAccessTracker().ResetCounters();
    }

    updateWidgets();
}
