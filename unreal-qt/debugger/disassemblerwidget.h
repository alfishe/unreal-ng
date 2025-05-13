#ifndef DISASSEMBLERWIDGET_H
#define DISASSEMBLERWIDGET_H

#include <QObject>
#include <QWidget>
#include <QPlainTextEdit>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DisassemblerWidget; }
QT_END_NAMESPACE

class DebuggerWindow;

class DisassemblerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerWidget(QWidget *parent = nullptr);
    virtual ~DisassemblerWidget() override;

    void setDisassemblerAddress(uint16_t pc);

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    Z80Registers* getZ80Registers();
    std::unique_ptr<Z80Disassembler>& getDisassembler();

    std::string formatRuntimeInformation();

public slots:
    void reset();
    void refresh();

signals:

private:
    Ui::DisassemblerWidget* ui;
    QThread* m_mainThread;
    QPlainTextEdit* m_disassemblyTextEdit;

    DebuggerWindow* m_debuggerWindow;
};

#endif // DISASSEMBLERWIDGET_H
