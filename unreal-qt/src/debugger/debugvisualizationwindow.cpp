#include "debugvisualizationwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
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


    // Throttled refresh timer — coalesces frame notifications to ~30Hz
    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &DebugVisualizationWindow::onRefreshTimer);
    _refreshTimer->start(33);  // ~30 FPS max

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

    // Programmatically add "Dump Viz" button next to Reset
    if (ui->resetCountersButton && ui->resetCountersButton->parentWidget())
    {
        auto* parentLayout = qobject_cast<QHBoxLayout*>(ui->resetCountersButton->parentWidget()->layout());
        if (parentLayout)
        {
            auto* dumpBtn = new QPushButton("Dump Viz", ui->resetCountersButton->parentWidget());
            dumpBtn->setMaximumWidth(80);
            dumpBtn->setToolTip("Export memory + counters + call trace to a .uzvd file for the visualization PoC");
            connect(dumpBtn, &QPushButton::clicked, this, &DebugVisualizationWindow::onDumpVizDataClicked);
            parentLayout->addWidget(dumpBtn);
        }
    }

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

        // Register observers and store their IDs for cleanup
        _stateChangeObserverId = messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE,
            [this](int id, Message* message) { this->handleEmulatorStateChanged(id, message); });

        _cpuStepObserverId = messageCenter.AddObserver(NC_EXECUTION_CPU_STEP,
            [this](int id, Message* message) { this->handleCPUStepMessage(id, message); });

        _frameRefreshObserverId = messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH,
            [this](int id, Message* message) {
                _frameDirty.store(true, std::memory_order_relaxed);
            });
    }

    setWindowTitle("Debug Visualization");
    resize(800, 600);
}

DebugVisualizationWindow::~DebugVisualizationWindow()
{
    // Unsubscribe from MessageCenter using the stored observer IDs
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    if (_stateChangeObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, _stateChangeObserverId);
    }

    if (_cpuStepObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EXECUTION_CPU_STEP, _cpuStepObserverId);
    }

    if (_frameRefreshObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserverId);
    }

    delete ui;
}

void DebugVisualizationWindow::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    // Unsubscribe existing observers
    if (_stateChangeObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, _stateChangeObserverId);
        _stateChangeObserverId = 0;
    }

    if (_cpuStepObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EXECUTION_CPU_STEP, _cpuStepObserverId);
        _cpuStepObserverId = 0;
    }

    if (_frameRefreshObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserverId);
        _frameRefreshObserverId = 0;
    }

    // Subscribe to new emulator events
    if (_emulator)
    {
        // Register observers and store their IDs for cleanup
        _stateChangeObserverId = messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE,
            [this](int id, Message* message) { this->handleEmulatorStateChanged(id, message); });

        _cpuStepObserverId = messageCenter.AddObserver(NC_EXECUTION_CPU_STEP,
            [this](int id, Message* message) { this->handleCPUStepMessage(id, message); });

        _frameRefreshObserverId = messageCenter.AddObserver(NC_VIDEO_FRAME_REFRESH,
            [this](int id, Message* message) {
                _frameDirty.store(true, std::memory_order_relaxed);
            });

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

void DebugVisualizationWindow::onRefreshTimer()
{
    // Only refresh if a frame actually completed since last check
    if (_frameDirty.exchange(false, std::memory_order_relaxed))
    {
        updateWidgets();
    }
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

    // Stop the refresh timer to prevent further callbacks
    if (_refreshTimer)
    {
        _refreshTimer->stop();
    }

    // Remove MessageCenter observers to prevent cross-thread dispatches during destruction
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    if (_stateChangeObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, _stateChangeObserverId);
        _stateChangeObserverId = 0;
    }

    if (_cpuStepObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_EXECUTION_CPU_STEP, _cpuStepObserverId);
        _cpuStepObserverId = 0;
    }

    if (_frameRefreshObserverId != 0)
    {
        messageCenter.RemoveObserverById(NC_VIDEO_FRAME_REFRESH, _frameRefreshObserverId);
        _frameRefreshObserverId = 0;
    }

    // Null out emulator reference to prevent any stale access
    _emulator = nullptr;
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

void DebugVisualizationWindow::onDumpVizDataClicked()
{
    if (!_emulator)
        return;

    Memory* mem = _emulator->GetMemory();
    if (!mem)
        return;

    QString path = QFileDialog::getSaveFileName(
        this, "Save Visualization Data", "emulator_dump.uzvd",
        "Unreal Viz Data (*.uzvd);;All Files (*)");

    if (path.isEmpty())
        return;

    bool ok = mem->GetAccessTracker().DumpVisualizationData(path.toStdString());
    if (ok)
    {
        QMessageBox::information(this, "Dump Saved",
            QString("Visualization data saved to:\n%1").arg(path));
    }
    else
    {
        QMessageBox::warning(this, "Dump Failed",
            "Failed to write visualization data file.");
    }
}
