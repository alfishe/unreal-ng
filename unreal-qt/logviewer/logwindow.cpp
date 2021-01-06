#include "logwindow.h"
#include "ui_logwindow.h"

LogWindow::LogWindow() : QWidget(), ui(new Ui::LogWindow)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    // Expose LogViewer
    logViewer = ui->logViewer;

    // Expose LogStatusBar fields
    statusText = ui->logStatusBar->labelStatusText;
    statusCounter1 = ui->logStatusBar->labelCounter1;
    statusCounter2 = ui->logStatusBar->labelCounter2;

    init();
}

LogWindow::~LogWindow()
{
}

void LogWindow::init()
{
    m_mainThread = QApplication::instance()->thread();

    logViewer->setReadOnly(true);
}

void LogWindow::reset()
{
    m_logMessagesCount = 0;
    m_logMessagesSize = 0;

    QMetaObject::invokeMethod(this, "Out", Qt::QueuedConnection, Q_ARG(QString, ""));
}

///
/// \brief LogViewer::Out
/// \param line Text to print into log
/// \param len Length of text
///
void LogWindow::Out(const char* line, size_t len)
{
    m_logMessagesCount++;
    m_logMessagesSize += len;

    QThread* currentThread = QThread::currentThread();

    if (currentThread != m_mainThread)
    {
        // Invoke setPlainText() in main thread
        QMetaObject::invokeMethod(this, "Out", Qt::QueuedConnection, Q_ARG(QString, line));
    }
    else
    {
        Out(line);
    }
}

void LogWindow::Out(QString line)
{
#ifdef QT_DEBUG
    QThread* currentThread = QThread::currentThread();

    if (currentThread != m_mainThread)
    {
        throw std::logic_error("LogViewer::Out called from non-main thread");
    }
#endif

    //QString text = document()->toPlainText() + line + '\n';
    QString text = line + '\n';
    logViewer->document()->setPlainText(text);

    statusCounter1->setText(QString("Msg count: %1").arg(m_logMessagesCount));
    statusCounter2->setText(QString("Total size: %1").arg(m_logMessagesSize));
}

