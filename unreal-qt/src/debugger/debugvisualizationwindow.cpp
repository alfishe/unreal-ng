#include "debugvisualizationwindow.h"
#include "ui_debugvisualizationwindow.h"

#include <QDebug>
#include <QMetaObject>
#include <QTimer>

#include "widgets/memorywidget.h"
#include "widgets/memorypagesviswidget.h"
#include "widgets/ulabeamwidget.h"
#include "widgets/bordertimingwidget.h"
#include "widgets/floppydiskwidget.h"

DebugVisualizationWindow::DebugVisualizationWindow(Emulator* emulator, QWidget *parent)
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
    
    // Set up update timer (for UI refresh)
    _updateTimer = new QTimer(this);
    connect(_updateTimer, &QTimer::timeout, this, &DebugVisualizationWindow::updateWidgets);
    _updateTimer->start(100); // Update every 100ms
    
    // Connect signal for main thread execution
    connect(this, &DebugVisualizationWindow::executeInMainThread, this, &DebugVisualizationWindow::updateWidgets);
    
    // Subscribe to emulator events
    if (_emulator)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        
        // Create and store lambda functions for observers
        _stateChangeObserver = [this](int id, Message* message) {
            this->handleEmulatorStateChanged(id, message);
        };
        
        _cpuStepObserver = [this](int id, Message* message) {
            this->handleCPUStepMessage(id, message);
        };
        
        // Register the observers
        messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
        messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
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
    
    // Subscribe to new emulator events
    if (_emulator)
    {
        // Create and store lambda functions for observers
        _stateChangeObserver = [this](int id, Message* message) {
            this->handleEmulatorStateChanged(id, message);
        };
        
        _cpuStepObserver = [this](int id, Message* message) {
            this->handleCPUStepMessage(id, message);
        };
        
        // Register the observers
        messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, _stateChangeObserver);
        messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, _cpuStepObserver);
        
        // Update widgets with new emulator
        if (_memoryWidget) _memoryWidget->setEmulator(_emulator);
        if (_memoryPagesWidget) _memoryPagesWidget->setEmulator(_emulator);
        if (_ulaBeamWidget) _ulaBeamWidget->setEmulator(_emulator);
        if (_borderTimingWidget) _borderTimingWidget->setEmulator(_emulator);
        if (_floppyDiskWidget) _floppyDiskWidget->setEmulator(_emulator);
    }
    
    updateState();
}

Emulator* DebugVisualizationWindow::getEmulator()
{
    return _emulator;
}

void DebugVisualizationWindow::reset()
{
    if (_memoryWidget) _memoryWidget->reset();
    if (_memoryPagesWidget) _memoryPagesWidget->reset();
    if (_ulaBeamWidget) _ulaBeamWidget->reset();
    if (_borderTimingWidget) _borderTimingWidget->reset();
    if (_floppyDiskWidget) _floppyDiskWidget->reset();
}

void DebugVisualizationWindow::updateState()
{
    if (!_emulator)
        return;
    
    _emulatorState = _emulator->GetState();
    
    // Update widgets based on emulator state
    dispatchToMainThread([this]() {
        if (_memoryWidget) _memoryWidget->refresh();
        if (_memoryPagesWidget) _memoryPagesWidget->refresh();
        if (_ulaBeamWidget) _ulaBeamWidget->refresh();
        if (_borderTimingWidget) _borderTimingWidget->refresh();
        if (_floppyDiskWidget) _floppyDiskWidget->refresh();
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
    if (!_emulator || _emulatorState != EmulatorStateEnum::StatePaused)
        return;
    
    // Update all widgets
    if (_memoryWidget) _memoryWidget->refresh();
    if (_memoryPagesWidget) _memoryPagesWidget->refresh();
    if (_ulaBeamWidget) _ulaBeamWidget->refresh();
    if (_borderTimingWidget) _borderTimingWidget->refresh();
    if (_floppyDiskWidget) _floppyDiskWidget->refresh();
}
