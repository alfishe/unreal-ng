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

signals:
    void changeMemoryViewBank(uint8_t bank);
    void changeMemoryViewAddress(uint8_t* addr, size_t size, uint16_t offset);


    /// region <Event handlers / Slots>
public slots:
    void reset();
    void refresh();

    void page0_doubleClicked();
    void page1_doubleClicked();
    void page2_doubleClicked();
    void page3_doubleClicked();
    /// endregion </Event handlers / Slots>


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
