#include "stackwidget.h"
#include "ui_stackwidget.h"

#include "debuggerwindow.h"
#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
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

    // Subscribe to double clicks on stack addresses
    connect(sp0Value, SIGNAL(doubleClicked()), this, SLOT(sp0Value_doubleClicked()));
    connect(sp1Value, SIGNAL(doubleClicked()), this, SLOT(sp1Value_doubleClicked()));
    connect(sp2Value, SIGNAL(doubleClicked()), this, SLOT(sp2Value_doubleClicked()));
    connect(sp3Value, SIGNAL(doubleClicked()), this, SLOT(sp3Value_doubleClicked()));
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
        uint16_t stackValues[4] = {};
        readStackIntoArray(stackValues, 4);

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

void StackWidget::sp0Value_doubleClicked()
{
    qDebug("StackWidget::sp0Value_doubleClicked()");

    uint16_t stackValues[4] = {};
    readStackIntoArray(stackValues, 4);
    uint16_t stackValue = stackValues[0];

    emit changeMemoryViewZ80Address(stackValue);
}

void StackWidget::sp1Value_doubleClicked()
{
    qDebug("StackWidget::sp1Value_doubleClicked()");

    uint16_t stackValues[4] = {};
    readStackIntoArray(stackValues, 4);
    uint16_t stackValue = stackValues[1];

    emit changeMemoryViewZ80Address(stackValue);
}

void StackWidget::sp2Value_doubleClicked()
{
    qDebug("StackWidget::sp2Value_doubleClicked()");

    uint16_t stackValues[4] = {};
    readStackIntoArray(stackValues, 4);
    uint16_t stackValue = stackValues[2];

    emit changeMemoryViewZ80Address(stackValue);
}

void StackWidget::sp3Value_doubleClicked()
{
    qDebug("StackWidget::sp3Value_doubleClicked()");

    uint16_t stackValues[4] = {};
    readStackIntoArray(stackValues, 4);
    uint16_t stackValue = stackValues[3];

    emit changeMemoryViewZ80Address(stackValue);
}

/// region </Event handlers / Slots>

/// region <Helper methods>
void StackWidget::readStackIntoArray(uint16_t* outArray, uint16_t depth)
{
    if (!outArray || !depth)
        return;

    Memory* memory = getMemory();
    Z80State* z80 = getEmulatorContext()->pCore->GetZ80();
    uint16_t sp = static_cast<uint16_t>(z80->Z80Registers::sp);

    for (unsigned long i = 0; i < depth; i++)
    {
        uint8_t loByte = memory->DirectReadFromZ80Memory(sp++);
        uint16_t hiByte = memory->DirectReadFromZ80Memory(sp++);

        outArray[i] = (hiByte << 8) | loByte;
    }
}
/// endregion </Helper methods>