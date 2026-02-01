#pragma once

#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QLabel>
#include <QWidget>
#include <QPushButton>
#include "logviewer/logviewer.h"
#include "common/modulelogger.h"

QT_BEGIN_NAMESPACE
namespace Ui { class LogWindow; }
QT_END_NAMESPACE

class LogWindow : public QWidget, public ModuleLoggerObserver
{
    Q_OBJECT

    /// region <Constructors / destructors>
public:
    LogWindow();
    virtual ~LogWindow();
    /// endregion </Constructors / destructors>

    /// region <Initialization>
public:
    void init();
    void reset();
    void prepareForShutdown();  // Block refreshes during shutdown
    /// endregion </Initialization

public slots:
    void Out(const char* line, size_t len);
    void Out(QString line);

private slots:
    void handleSaveButtonClick();
    void handleClearButtonClick();

    // Fields
protected:
    QThread* m_mainThread;
    std::stringstream m_logStream;
    int m_logMessagesCount = 0;
    int m_logMessagesSize = 0;
    bool m_isShuttingDown = false;  // Flag to block updates during shutdown

    // UI fields
private:
    Ui::LogWindow* ui;

    LogViewer* logViewer;
    QLabel* statusText;
    QLabel* statusCounter1;
    QLabel* statusCounter2;
    QPushButton* clearButton;
    QPushButton* saveButton;
};

#endif // LOGWINDOW_H
