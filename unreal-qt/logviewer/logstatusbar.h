#ifndef LOGSTATUSBAR_H
#define LOGSTATUSBAR_H

#include <QStatusBar>
#include <QWidget>

class LogStatusBar : public QStatusBar
{
    Q_OBJECT
public:
    LogStatusBar();
    virtual ~LogStatusBar();
};

#endif // LOGSTATUSBAR_H
