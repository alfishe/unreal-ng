#pragma once

#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QLabel>
#include <QWidget>
#include "logviewer/logviewer.h"
#include "common/modulelogger.h"

QT_BEGIN_NAMESPACE
namespace Ui { class LogWindow; }
QT_END_NAMESPACE

class LogWindow : public QWidget, public ModuleLoggerObserver
{
    Q_OBJECT

public:
    LogWindow();
    virtual ~LogWindow();

    void init();
    void reset();

public slots:
    void Out(const char* line, size_t len);
    void Out(QString line);

    // Fields
protected:
    QThread* m_mainThread;
    int m_logMessagesCount = 0;
    int m_logMessagesSize = 0;

    // UI fields
private:
    Ui::LogWindow* ui;

    LogViewer* logViewer;
    QLabel* statusText;
    QLabel* statusCounter1;
    QLabel* statusCounter2;
};

#endif // LOGWINDOW_H
