#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <stdio.h>
#include <QHBoxLayout>

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

    QVBoxLayout* layout = new QVBoxLayout;
    layout->addWidget(deviceScreen);
    contentFrame->setLayout(layout);
    deviceScreen->setSizePolicy(contentFrame->sizePolicy());

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

void MainWindow::handleStartButton()
{
    if (_emulator == nullptr)
    {
        startButton->setEnabled(false);

        // Initialize emulator instance
        _emulator = new Emulator();
        _emulator->Init();

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
        // Invoke in main thread
        QMetaObject::invokeMethod(deviceScreen,"refresh", Qt::QueuedConnection);
    }
}
