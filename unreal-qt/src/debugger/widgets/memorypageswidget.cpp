#include "memorypageswidget.h"

#include "debugger/debuggerwindow.h"
#include "ui_memorypageswidget.h"

/// region <Constructors / Destructors>

MemoryPagesWidget::MemoryPagesWidget(QWidget *parent) : QWidget(parent), ui(new Ui::MemoryPagesWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = static_cast<DebuggerWindow*>(parent);

    // Shortcuts for value labels
    page0Value = ui->page_0_value_label;
    page1Value = ui->page_1_value_label;
    page2Value = ui->page_2_value_label;
    page3Value = ui->page_3_value_label;

    // Subscribe to double clicks on bank labels
    connect(page0Value, SIGNAL(doubleClicked()), this, SLOT(page0_doubleClicked()));
    connect(page1Value, SIGNAL(doubleClicked()), this, SLOT(page1_doubleClicked()));
    connect(page2Value, SIGNAL(doubleClicked()), this, SLOT(page2_doubleClicked()));
    connect(page3Value, SIGNAL(doubleClicked()), this, SLOT(page3_doubleClicked()));
}

MemoryPagesWidget::~MemoryPagesWidget()
{

}

/// endregion </Constructors / Destructors>

// Helper methods
Emulator* MemoryPagesWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* MemoryPagesWidget::getEmulatorContext()
{
    return m_debuggerWindow->getEmulator()->GetContext();
}

Memory* MemoryPagesWidget::getMemory()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pMemory;
}

/// region <Event handlers / Slots>

void MemoryPagesWidget::reset()
{
    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "reset", Qt::QueuedConnection);
    }
    else
    {
        page0Value->setText("<Bank 0>");
        page1Value->setText("<Bank 1>");
        page2Value->setText("<Bank 2>");
        page3Value->setText("<Bank 3>");

        update();
    }
}

void MemoryPagesWidget::refresh()
{
    // Block all operations during shutdown
    if (m_isShuttingDown)
    {
        return;
    }

    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    }
    else
    {
        Memory* memory = getMemory();
        QString page0Name = memory->GetCurrentBankName(0).c_str();
        QString page1Name = memory->GetCurrentBankName(1).c_str();
        QString page2Name = memory->GetCurrentBankName(2).c_str();
        QString page3Name = memory->GetCurrentBankName(3).c_str();

        page0Value->setText(page0Name);
        page1Value->setText(page1Name);
        page2Value->setText(page2Name);
        page3Value->setText(page3Name);

        update();
    }
}

void MemoryPagesWidget::page0_doubleClicked()
{
    qDebug("MemoryPagesWidget::page0_doubleClicked()");

    emit changeMemoryViewBank(0);
}

void MemoryPagesWidget::page1_doubleClicked()
{
    qDebug("MemoryPagesWidget::page1_doubleClicked()");

    emit changeMemoryViewBank(1);
}

void MemoryPagesWidget::page2_doubleClicked()
{
    qDebug("MemoryPagesWidget::page2_doubleClicked()");

    emit changeMemoryViewBank(2);
}

void MemoryPagesWidget::page3_doubleClicked()
{
    qDebug("MemoryPagesWidget::page3_doubleClicked()");

    emit changeMemoryViewBank(3);
}

/// endregion </Event handlers / Slots>

void MemoryPagesWidget::prepareForShutdown()
{
    qDebug() << "MemoryPagesWidget::prepareForShutdown()";
    m_isShuttingDown = true;
}
