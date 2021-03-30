#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QPushButton>

#include "debugger/debuggerwindow.h"
#include "logviewer/logwindow.h"
#include "widgets/devicescreen.h"
#include "emulator/guiemulatorcontext.h"
#include "emulator/emulatormanager.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "emulator/emulator.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void handleStartButton();
    void handleMessageScreenRefresh(int id, Message* message);
    void resetEmulator();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void updatePosition(QWidget *widget, QWidget *parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) - widget->rect().center());
    }

private:
    Ui::MainWindow* ui;
    DebuggerWindow* debuggerWindow;
    LogWindow* logWindow;
    DeviceScreen* deviceScreen;
    QPushButton* startButton;
    QMutex lockMutex;

    EmulatorManager* _emulatorManager = nullptr;
    GUIEmulatorContext* _guiContext = nullptr;
    Emulator* _emulator = nullptr;
};
#endif // MAINWINDOW_H
