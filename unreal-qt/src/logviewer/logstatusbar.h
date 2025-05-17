#ifndef LOGSTATUSBAR_H
#define LOGSTATUSBAR_H

#include <QLabel>
#include <QStatusBar>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class LogStatusBar; }
QT_END_NAMESPACE

class LogStatusBar : public QWidget
{
    Q_OBJECT
public:
    LogStatusBar(QWidget *parent = nullptr);
    virtual ~LogStatusBar();

private:
    Ui::LogStatusBar* ui;

public:
    QLabel* labelStatusText;
    QLabel* labelCounter1;
    QLabel* labelCounter2;
};

#endif // LOGSTATUSBAR_H
