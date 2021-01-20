#ifndef DEBUGGERWINDOW_H
#define DEBUGGERWINDOW_H

#include <QAction>
#include <QDebug>
#include <QToolBar>
#include <QWidget>
#include "3rdparty/message-center/messagecenter.h"

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

private slots:
    void handleMessageBreakpointTriggered(int id, Message* message);

    void continueExecution();
    void pauseExecution();
    void cpuStep();
    void frameStep();
    void waitInterrupt();

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
