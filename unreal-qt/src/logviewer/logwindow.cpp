#include <QFileDialog>
#include "logwindow.h"
#include "ui_logwindow.h"

/// region <Constructors / destructors>

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

    // Expose clear button and add click handler
    clearButton = ui->clearButton;
    connect(clearButton, &QPushButton::clicked, this, &LogWindow::handleClearButtonClick);

    // Expose save button and add click handler
    saveButton = ui->saveButton;
    connect(saveButton, &QPushButton::clicked, this, &LogWindow::handleSaveButtonClick);


    init();
}

LogWindow::~LogWindow()
{
}

/// endregion </Constructors / destructors>

/// region <Initialization>

void LogWindow::init()
{
    m_mainThread = QApplication::instance()->thread();

    logViewer->setReadOnly(true);
}

void LogWindow::reset()
{
    m_logMessagesCount = 0;
    m_logMessagesSize = 0;
    m_logStream.str("");

    QMetaObject::invokeMethod(this, "Out", Qt::QueuedConnection, Q_ARG(QString, ""));
}

/// endregion </Initialization>

///
/// \brief LogViewer::Out
/// \param line Text to print into log
/// \param len Length of text
///
void LogWindow::Out(const char* line, size_t len)
{
    m_logMessagesCount++;
    m_logMessagesSize += len;
    m_logStream.write(line, len);
    m_logStream << std::endl;

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

void LogWindow::handleClearButtonClick()
{
    reset();
}

void LogWindow::handleSaveButtonClick()
{
    QString defaultSavePath = "/Users/dev/Downloads";
    QString defaultFileName = "unreal_ng_log.txt";
    QString filePath = QFileDialog::getSaveFileName(nullptr, "Save File", defaultSavePath + QDir::separator() + defaultFileName, "Text Files (*.txt)");

    if (!filePath.isEmpty())
    {
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream << QString::fromStdString(m_logStream.str());
            file.close();
        }
    }
}

void LogWindow::prepareForShutdown()
{
    qDebug() << "LogWindow::prepareForShutdown()";
    m_isShuttingDown = true;
}
