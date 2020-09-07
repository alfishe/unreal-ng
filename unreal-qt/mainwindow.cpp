#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <stdio.h>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    startButton = ui->startEmulator;

    // Create layoyt
    if (this->layout() == nullptr)
    {
        //QHBoxLayout* boxLayout = new QHBoxLayout();

        //window()->setLayout(boxLayout);
    }

    // Put emulator screen into resizable content frame
    QFrame* contentFrame = ui->contentFrame;
    deviceScreen = new DeviceScreen(contentFrame);

    QHBoxLayout* layout = new QHBoxLayout;
    layout->addWidget(deviceScreen, Qt::AlignHCenter);
    contentFrame->setLayout(layout);
    QSizePolicy dp;
    dp.setHorizontalPolicy(QSizePolicy::Expanding);
    dp.setVerticalPolicy(QSizePolicy::Expanding);
    deviceScreen->setSizePolicy(dp);

    // Center device screen within content frame
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);

    // Connect button signal to appropriate slot
    connect(startButton, SIGNAL (released()), this, SLOT (handleStartButton()));
}

MainWindow::~MainWindow()
{
    if (deviceScreen != nullptr)
        delete deviceScreen;

    QLayout* ptrLayout = layout();
    if (ptrLayout != nullptr)
    {
        window()->setLayout(nullptr);
        delete ptrLayout;
    }

    delete ui;
}

void MainWindow::showEvent(QShowEvent *event)
{
    // Center device screen within content frame
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);

    QWidget::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
     event->accept();
     qDebug() << "QCloseEvent : Closing application";

     // Unsubscribe from message bus events
     MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
     std::string topic = "FRAME_REFRESH";
     Observer* observerInstance = static_cast<Observer*>(this);
     ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
     messageCenter.RemoveObserver(topic, observerInstance, callback);

     // Shutdown emulator
     _emulator->Stop();
     deviceScreen->detach();
     delete _emulator;
     _emulator = nullptr;

     qDebug() << "QCloseEvent : Emulator shutdown complete";
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    //deviceScreen->move(this->rect().center() - deviceScreen->rect().center());
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);

    QWidget::resizeEvent(event);
}

void MainWindow::handleStartButton()
{
    if (_emulator == nullptr)
    {
        startButton->setEnabled(false);

        // Initialize emulator instance
        _emulator = new Emulator();
        _emulator->Init();
        Logger::MuteSilent();

        // Attach emulator framebuffer to GUI
        FramebufferDescriptor framebufferDesc = _emulator->GetFramebuffer();
        this->deviceScreen->init(framebufferDesc.width, framebufferDesc.height, framebufferDesc.memoryBuffer);

        // Subscribe to frame refresh events
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        std::string topic = "FRAME_REFRESH";
        Observer* observerInstance = static_cast<Observer*>(this);
        ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
        messageCenter.AddObserver(topic, observerInstance, callback);

        // Start in async own thread
        _emulator->StartAsync();


        fflush(stdout);
        fflush(stderr);
        startButton->setText("Stop");
        startButton->setEnabled(true);
    }
    else
    {
        startButton->setEnabled(false);

        // Unsubscribe from message bus events
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        std::string topic = "FRAME_REFRESH";
        Observer* observerInstance = static_cast<Observer*>(this);
        ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
        messageCenter.RemoveObserver(topic, observerInstance, callback);

        // Stop emulator instance
        _emulator->Stop();

        // Detach framebuffer
        deviceScreen->detach();

        // Destroy emulator
        delete _emulator;
        _emulator = nullptr;


        fflush(stdout);
        fflush(stderr);
        startButton->setText("Start");
        startButton->setEnabled(true);
    }
}

void MainWindow::handleMessageScreenRefresh(int id, Message* message)
{
    if (deviceScreen)
    {
        // Invoke deviceScreen->refresh() in main thread
        QMetaObject::invokeMethod(deviceScreen,"refresh", Qt::QueuedConnection);
    }
}
