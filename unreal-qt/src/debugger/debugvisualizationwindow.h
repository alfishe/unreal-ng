#pragma once
#ifndef DEBUGVISUALIZATIONWINDOW_H
#define DEBUGVISUALIZATIONWINDOW_H

#include <QCheckBox>
#include <QGridLayout>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"

// Forward declarations
class MemoryWidget;
class MemoryPagesVisWidget;
class ULABeamWidget;
class BorderTimingWidget;
class FloppyDiskWidget;

QT_BEGIN_NAMESPACE
namespace Ui
{
class DebugVisualizationWindow;
}
QT_END_NAMESPACE

class DebugVisualizationWindow : public QWidget, public Observer
{
    Q_OBJECT
public:
    explicit DebugVisualizationWindow(Emulator* emulator = nullptr, QWidget* parent = nullptr);
    virtual ~DebugVisualizationWindow() override;

    void setEmulator(Emulator* emulator);
    Emulator* getEmulator();

    void reset();
    void prepareForShutdown();  // Block refreshes during shutdown

protected:
    void updateState();
    void dispatchToMainThread(std::function<void()> callback);

    /// @brief Synchronize checkbox states with the current emulator's FeatureManager
    void syncFeatureCheckboxes();

private slots:
    void handleEmulatorStateChanged(int id, Message* message);
    void handleCPUStepMessage(int id, Message* message);
    void updateWidgets();

    void onMemoryTrackingToggled(bool checked);
    void onCallTraceToggled(bool checked);
    void onOpcodeProfilerToggled(bool checked);
    void onResetCountersClicked();

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
    std::function<void(int, Message*)> _frameRefreshObserver;

    // Flag to prevent recursive signal handling during sync
    bool _updatingCheckboxes = false;

    // Flag to block refreshes during shutdown
    bool _isShuttingDown = false;
};

#endif  // DEBUGVISUALIZATIONWINDOW_H
