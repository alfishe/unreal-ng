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

public slots:
    void reset();
    void refresh();

signals:
    void bank0LabelClicked();

private:
    Ui::RegistersWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    Z80Registers* m_z80Registers;
};

#endif // REGISTERWIDGET_H
