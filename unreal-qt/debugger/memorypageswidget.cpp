#include "memorypageswidget.h"

#include "ui_memorypageswidget.h"

#include "debuggerwindow.h"

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
}

MemoryPagesWidget::~MemoryPagesWidget()
{

}

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
