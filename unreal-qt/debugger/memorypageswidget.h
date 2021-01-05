#ifndef MEMORYPAGESWIDGET_H
#define MEMORYPAGESWIDGET_H

#include <QLabel>
#include <QThread>
#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MemoryPagesWidget; }
QT_END_NAMESPACE

class DebuggerWindow;

class MemoryPagesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryPagesWidget(QWidget *parent = nullptr);
    virtual ~MemoryPagesWidget() override;

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();

public slots:
    void reset();
    void refresh();

signals:

private:
    Ui::MemoryPagesWidget* ui;
    QThread* m_mainThread;

    DebuggerWindow* m_debuggerWindow;

    QLabel* page0Value;
    QLabel* page1Value;
    QLabel* page2Value;
    QLabel* page3Value;

};

#endif // MEMORYPAGESWIDGET_H
