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

    /// region <QWidget events override>
protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void changeEvent(QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
/// endregion </QWidget events override>

protected:
    void updatePosition(QWidget *widget, QWidget *parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) - widget->rect().center());
    }

    void arrangeWindows();
    void adjust(QEvent* event, const QPoint& delta = QPoint{});
    void showFullscreen();
    void showNormal();

private:
    Ui::MainWindow* ui = nullptr;
    DebuggerWindow* debuggerWindow = nullptr;
    LogWindow* logWindow = nullptr;
    DeviceScreen* deviceScreen = nullptr;
    QPushButton* startButton = nullptr;
    QMutex lockMutex;

    EmulatorManager* _emulatorManager = nullptr;
    GUIEmulatorContext* _guiContext = nullptr;
    Emulator* _emulator = nullptr;
    uint32_t _lastFrameCount = 0;

    QPoint _lastCursorPos;
    QPalette _originalPalette;

    bool _inTransitionToFullScreen = false;
};
#endif // MAINWINDOW_H
