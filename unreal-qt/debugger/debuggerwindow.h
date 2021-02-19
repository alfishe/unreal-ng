#ifndef DEBUGGERWINDOW_H
#define DEBUGGERWINDOW_H

#include <QAction>
#include <QDebug>
#include <QToolBar>
#include <QWidget>
#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/qhexview/qhexview.h"
#include "3rdparty/qhexview/document/buffer/qmemorybuffer.h"

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

    // Helper methods
protected:
    void updateState();
    void loadState();
    void saveState();

    /// region <Event handlers / Slots>
private slots:
    void handleMessageBreakpointTriggered(int id, Message* message);

    // Debug toolbar actions
    void continueExecution();
    void pauseExecution();
    void cpuStep();
    void frameStep();
    void waitInterrupt();

    void changeMemoryViewPage(uint8_t page);
    void changeMemoryViewAddress(uint8_t* address, size_t size, uint16_t offset);

    /// endregion </Event handlers / Slots>

signals:

    // Fields
protected:
    Emulator* _emulator = nullptr;
    bool _breakpointTriggered = false;

    // UI fields
private:
    Ui::DebuggerWindow* ui;

    QToolBar* toolBar;
    QAction* continueAction;
    QAction* pauseAction;
    QAction* cpuStepAction;
    QAction* frameStepAction;
    QAction* waitInterruptAction;
};

#endif // DEBUGGERWINDOW_H
