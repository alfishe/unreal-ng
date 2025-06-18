#pragma once
#ifndef DEBUGVISUALIZATIONWINDOW_H
#define DEBUGVISUALIZATIONWINDOW_H

#include <QWidget>
#include <QGridLayout>
#include <QTimer>

#include "emulator/emulator.h"
#include "3rdparty/message-center/messagecenter.h"

// Forward declarations
class MemoryWidget;
class MemoryPagesVisWidget;
class ULABeamWidget;
class BorderTimingWidget;
class FloppyDiskWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class DebugVisualizationWindow; }
QT_END_NAMESPACE

class DebugVisualizationWindow : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit DebugVisualizationWindow(Emulator* emulator = nullptr, QWidget *parent = nullptr);
    virtual ~DebugVisualizationWindow() override;

    void setEmulator(Emulator* emulator);
    Emulator* getEmulator();

    void reset();

protected:
    void updateState();
    void dispatchToMainThread(std::function<void()> callback);

private slots:
    void handleEmulatorStateChanged(int id, Message* message);
    void handleCPUStepMessage(int id, Message* message);
    void updateWidgets();

signals:
    void executeInMainThread();

private:
    Ui::DebugVisualizationWindow* ui;
    Emulator* _emulator = nullptr;
    EmulatorStateEnum _emulatorState = EmulatorStateEnum::StateUnknown;
    
    // Widgets
    MemoryWidget* _memoryWidget = nullptr;
    MemoryPagesVisWidget* _memoryPagesWidget = nullptr;
    ULABeamWidget* _ulaBeamWidget = nullptr;
    BorderTimingWidget* _borderTimingWidget = nullptr;
    FloppyDiskWidget* _floppyDiskWidget = nullptr;
    
    QTimer* _updateTimer = nullptr;
    
    // Store lambda functions for MessageCenter observers
    std::function<void(int, Message*)> _stateChangeObserver;
    std::function<void(int, Message*)> _cpuStepObserver;
};

#endif // DEBUGVISUALIZATIONWINDOW_H
