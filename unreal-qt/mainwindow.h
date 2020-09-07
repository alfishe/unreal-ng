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

private:
    Ui::MainWindow* ui;
    DeviceScreen* deviceScreen;
    QPushButton* startButton;

    Emulator* _emulator = nullptr;
};
#endif // MAINWINDOW_H
