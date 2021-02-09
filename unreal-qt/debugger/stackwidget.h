#ifndef STACKWIDGET_H
#define STACKWIDGET_H

#include <QLabel>
#include <QThread>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class StackWidget; }
QT_END_NAMESPACE

class DebuggerWindow;
class Emulator;
class EmulatorContext;
class Memory;

class StackWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StackWidget(QWidget *parent = nullptr);
    virtual ~StackWidget() override;

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();

public slots:
    void reset();
    void refresh();

signals:

private:
    Ui::StackWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    QLabel* sp0Value;
    QLabel* sp1Value;
    QLabel* sp2Value;
    QLabel* sp3Value;
};

#endif // STACKWIDGET_H
