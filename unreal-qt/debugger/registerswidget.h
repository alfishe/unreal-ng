#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include <QThread>
#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"

QT_BEGIN_NAMESPACE
namespace Ui { class RegistersWidget; }
QT_END_NAMESPACE

class DebuggerWindow;

class RegistersWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RegistersWidget(QWidget *parent = nullptr);
    virtual ~RegistersWidget() override;

    void setZ80State(Z80State* state);

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    Z80Registers* getRegisters();

    /// region <Event handlers / Slots>
public slots:
    void reset();
    void refresh();

    void bc_doubleClicked();
    void de_doubleClicked();
    void hl_doubleClicked();
    void bc1_doubleClicked();
    void de1_doubleClicked();
    void hl1_doubleClicked();
    void sp_doubleClicked();
    void pc_doubleClicked();
    void ix_doubleClicked();
    void iy_doubleClicked();

    /// endregion </Event handlers / Slots>

signals:
    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewAddress(uint8_t* addr, size_t size, uint16_t offset, uint16_t curaddr);

private:
    Ui::RegistersWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    Z80Registers* m_z80Registers;
};

#endif // REGISTERWIDGET_H
