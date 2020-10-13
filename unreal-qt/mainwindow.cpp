#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <stdio.h>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QDebug>
#include <QTimer>

#include "common/modulelogger.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    // Intercept all keyboard and mouse events
    //qApp->installEventFilter(this);

    // Instantiate all child widgets (UI form auto-generated)
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

    // Create bridge between GUI and emulator
    _emulatorManager = EmulatorManager::defaultInstance();

    // Instantiate Logger window
    logWindow = new LogWindow();

    // Instantiate debugger window
    debuggerWindow = new DebuggerWindow();
    debuggerWindow->reset();
    debuggerWindow->show();
}

MainWindow::~MainWindow()
{
    if (debuggerWindow != nullptr)
    {
        debuggerWindow->hide();
        delete debuggerWindow;
    }

    if (logWindow != nullptr)
    {
        logWindow->hide();
        delete logWindow;
    }

    if (deviceScreen != nullptr)
        delete deviceScreen;

    if (_emulatorManager)
        delete _emulatorManager;

    if (_guiContext)
        delete _guiContext;

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

     // Stop emulator
     if (_emulator)
     {
        _emulator->Stop();
     }

     // Unsubscribe from message bus events
     MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
     std::string topic = "FRAME_REFRESH";
     Observer* observerInstance = static_cast<Observer*>(this);
     ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
     messageCenter.RemoveObserver(topic, observerInstance, callback);

     // Close debugger
     if (debuggerWindow)
     {
        debuggerWindow->hide();
        delete debuggerWindow;
        debuggerWindow = nullptr;
     }

     // Close LogViewer
     if (logWindow)
     {
         logWindow->hide();
         delete logWindow;
         logWindow = nullptr;
     }

     // Shutdown emulator
     if (deviceScreen)
     {
        deviceScreen->detach();
     }

     if (_emulator)
     {
         delete _emulator;
         _emulator = nullptr;
     }

     qDebug() << "QCloseEvent : Emulator shutdown complete";
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    //deviceScreen->move(this->rect().center() - deviceScreen->rect().center());

    // Keep widget center-aligned. Alignment policy is not working good
    updatePosition(deviceScreen, ui->contentFrame, 0.5, 0.5);

    QWidget::resizeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    qDebug() << "MainWindow : keyPressEvent , key : " << event->text();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    event->accept();

    qDebug() << "MainWindow : mousePressEvent";
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::KeyPress:
            {
                 QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
                 QString keyName = QKeySequence(keyEvent->key()).toString();
                 QString hexScanCode = QString("0x%1").arg(keyEvent->nativeScanCode(), 4, 16, QLatin1Char('0'));
                 QString hexVirtualKey = QString("0x%1").arg(keyEvent->nativeVirtualKey(), 4, 16, QLatin1Char('0'));

                 qDebug() << "MainWindow : eventFilter - keyPress, scan: "<< hexScanCode << "virt: " << hexVirtualKey << " key: " << keyName << " " << keyEvent->text();

                 /*
                 if (keyEvent->key() == Qt::Key_F1)
                 {
                        if( QWidget* w = QApplication::widgetAt( QCursor::pos() ) )
                        {
                                if( w == XXX )
                                {
                                       // show help
                                       return;
                                 }
                        }
                 }
                 */
            }
            break;
        case QEvent::KeyRelease:
            {
                QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
                QString keyName = QKeySequence(keyEvent->key()).toString();
                QString hexScanCode = QString("0x%1").arg(keyEvent->nativeScanCode(), 4, 16, QLatin1Char('0'));
                QString hexVirtualKey = QString("0x%1").arg(keyEvent->nativeVirtualKey(), 4, 16, QLatin1Char('0'));

                qDebug() << "MainWindow : eventFilter - keyRelease, scan: "<< hexScanCode << "virt: " << hexVirtualKey << " key: " << keyName << " " << keyEvent->text();
            }
            break;
        default:
            break;
    }

    return false;

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::handleStartButton()
{
    // Lock will be removed after method exit
    QMutexLocker ml(&lockMutex);

    if (_emulator == nullptr)
    {
        startButton->setEnabled(false);

        // Initialize emulator instance
        _emulator = new Emulator();
        if (_emulator->Init())
        {
            // Redirect all module logger output to LogWindow
            ModuleLogger* logger = _emulator->GetLogger();
            ModuleLoggerObserver* loggerObserverInstance = static_cast<ModuleLoggerObserver*>(logWindow);
            ModuleObserverObserverCallbackMethod loggerCallback = static_cast<ModuleObserverObserverCallbackMethod>(&LogWindow::Out);
            logger->SetLoggerOut(loggerObserverInstance, loggerCallback);
            logWindow->reset();
            logWindow->show();

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

            // Notify debugger about new emulator instance
            debuggerWindow->setEmulator(_emulator);


            fflush(stdout);
            fflush(stderr);
            startButton->setText("Stop");
            startButton->setEnabled(true);

            // DEBUG: Reset ZX-Spectrum each 2 seconds
            //QTimer *timer = new QTimer(this);
            //connect(timer, SIGNAL(timeout()), this, SLOT(resetEmulator()));
            //timer->start(2000);
        }
        else
        {
            delete _emulator;
            _emulator = nullptr;
        }
    }
    else
    {
        startButton->setEnabled(false);

        // Stop emulator instance
        _emulator->Stop();

        // Unsubscribe from message bus events
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        std::string topic = "FRAME_REFRESH";
        Observer* observerInstance = static_cast<Observer*>(this);
        ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&MainWindow::handleMessageScreenRefresh);
        messageCenter.RemoveObserver(topic, observerInstance, callback);

        // Detach framebuffer
        deviceScreen->detach();

        // Destroy emulator
        _emulator->Release();
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

void MainWindow::resetEmulator()
{
    if (_emulator)
    {
        _emulator->Reset();
    }
}
