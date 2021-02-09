#include "stackwidget.h"
#include "ui_stackwidget.h"

#include "debuggerwindow.h"
#include "common/stringhelper.h"
#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

StackWidget::StackWidget(QWidget *parent) : QWidget(parent), ui(new Ui::StackWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = static_cast<DebuggerWindow*>(parent);

    // Shortcuts for value labels
    sp0Value = ui->sp0Value;
    sp1Value = ui->sp1Value;
    sp2Value = ui->sp2Value;
    sp3Value = ui->sp3Value;
}

StackWidget::~StackWidget()
{

}

// Helper methods
Emulator* StackWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* StackWidget::getEmulatorContext()
{
    return m_debuggerWindow->getEmulator()->GetContext();
}

Memory* StackWidget::getMemory()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pMemory;
}


void StackWidget::reset()
{
    if (getEmulator())
    {
        refresh();
    }
}

void StackWidget::refresh()
{
    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    }
    else
    {
        Memory* memory = getMemory();
        Z80State* z80 = getEmulatorContext()->pCPU->GetZ80();
        uint16_t sp = static_cast<uint16_t>(z80->Z80Registers::sp);

        uint16_t stackValues[4] = {};

        for (unsigned long i = 0; i < sizeof(stackValues) / sizeof(stackValues[0]); i++)
        {
            uint16_t hiByte = memory->DirectReadFromZ80Memory(sp--);
            uint8_t loByte = memory->DirectReadFromZ80Memory(sp--);

            stackValues[i] = (hiByte << 8) | loByte;
        }

        QString sp0Name = StringHelper::Format("$%04X", stackValues[0]).c_str();
        QString sp1Name = StringHelper::Format("$%04X", stackValues[1]).c_str();
        QString sp2Name = StringHelper::Format("$%04X", stackValues[2]).c_str();
        QString sp3Name = StringHelper::Format("$%04X", stackValues[3]).c_str();

        sp0Value->setText(sp0Name);
        sp1Value->setText(sp1Name);
        sp2Value->setText(sp2Name);
        sp3Value->setText(sp3Name);

        update();
    }
}
