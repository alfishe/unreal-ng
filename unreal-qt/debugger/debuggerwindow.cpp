#include "debuggerwindow.h"

#include "ui_debuggerwindow.h"

#include <QBoxLayout>

#include "emulator/emulator.h"

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
    QSize toolbarSize = QSize(300, 32);
    toolBar->resize(toolbarSize);

    // Populate actions
    continueAction = toolBar->addAction("Continue");
    pauseAction = toolBar->addAction("Pause");
    cpuStepAction = toolBar->addAction("CPU step");
    frameStepAction = toolBar->addAction("Frame step");
    waitInterruptAction = toolBar->addAction("Wait INT");

    connect(continueAction, &QAction::triggered, this, &DebuggerWindow::continueExecution);
    connect(pauseAction, &QAction::triggered, this, &DebuggerWindow::pauseExecution);
    connect(cpuStepAction, &QAction::triggered, this, &DebuggerWindow::cpuStep);
    connect(frameStepAction, &QAction::triggered, this, &DebuggerWindow::frameStep);
    connect(waitInterruptAction, &QAction::triggered, this, &DebuggerWindow::waitInterrupt);

    // Inject toolbar on top of other widget lines
    ui->verticalLayout_2->insertWidget(0, toolBar);
}

DebuggerWindow::~DebuggerWindow()
{
    qDebug() << "DebuggerWindow::~DebuggerWindow()";
}

void DebuggerWindow::setEmulator(Emulator* emulator)
{
    _emulator = emulator;

    updateState();
}

Emulator* DebuggerWindow::getEmulator()
{
    return _emulator;
}

void DebuggerWindow::reset()
{
    ui->registersWidget->reset();

    updateState();
}

 void DebuggerWindow::updateState()
 {
    if (_emulator)
    {
        ui->registersWidget->setZ80State(_emulator->GetZ80State());
        ui->registersWidget->refresh();

        pauseAction->setEnabled(true);
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

void DebuggerWindow::continueExecution()
{
    qDebug() << "DebuggerWindow::continueExecution()";

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


        // Refresh registers widget
        ui->registersWidget->setZ80State(_emulator->GetZ80State());
        ui->registersWidget->refresh();
    }
}

void DebuggerWindow::cpuStep()
{
    qDebug() << "DebuggerWindow::cpuStep()";

    if (_emulator)
    {
        // Execute single Z80 command
        _emulator->RunSingleCPUCycle();

        // Refresh registers widget
        ui->registersWidget->setZ80State(_emulator->GetZ80State());
        ui->registersWidget->refresh();
    }
}

void DebuggerWindow::frameStep()
{
    qDebug() << "DebuggerWindow::frameStep()";

    // Refresh registers widget
    ui->registersWidget->setZ80State(_emulator->GetZ80State());
    ui->registersWidget->refresh();
}

void DebuggerWindow::waitInterrupt()
{
    qDebug() << "DebuggerWindow::waitInterrupt()";

    // Refresh registers widget
    ui->registersWidget->setZ80State(_emulator->GetZ80State());
    ui->registersWidget->refresh();
}
