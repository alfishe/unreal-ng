#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include "widgets/devicescreen.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void handleStartButton();
    void handleMessageScreenRefresh(int id, Message* message);

protected:
    void showEvent(QShowEvent *event);
    void closeEvent(QCloseEvent *event);
    void resizeEvent(QResizeEvent *event);

    void updatePosition(QWidget *widget, QWidget *parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) - widget->rect().center());
    }

private:
    Ui::MainWindow* ui;
    DeviceScreen* deviceScreen;
    QPushButton* startButton;

    Emulator* _emulator = nullptr;
};
#endif // MAINWINDOW_H
