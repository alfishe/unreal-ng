#include "MainWindow.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>

#include "WebAPIClient.h"
#include "EmulatorList.h"
#include "ScreenViewer.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _webApiClient(std::make_unique<WebAPIClient>())
{
    setWindowTitle("Unreal Screen Viewer");
    setMinimumSize(800, 600);
    resize(1024, 768);

    setupUI();
    setupMenuBar();
    setupStatusBar();
    connectSignals();

    // Configure WebAPI client
    _webApiClient->setEndpoint(_webApiHost, _webApiPort);

    // Setup refresh timer (every 2 seconds)
    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshClicked);
    _refreshTimer->start(2000);

    // Initial refresh
    onRefreshClicked();
}

MainWindow::~MainWindow()
{
    // Disable shared memory on selected emulator before exit
    if (!_selectedEmulatorId.isEmpty() && _webApiClient)
    {
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
    }
}

void MainWindow::setupUI()
{
    // Create splitter for left/right panels
    _splitter = new QSplitter(Qt::Horizontal, this);
    
    // Left panel: Emulator list
    _emulatorList = new EmulatorList(_splitter);
    _emulatorList->setMinimumWidth(200);
    _emulatorList->setMaximumWidth(350);
    
    // Right panel: Screen viewer
    _screenViewer = new ScreenViewer(_splitter);
    
    _splitter->addWidget(_emulatorList);
    _splitter->addWidget(_screenViewer);
    _splitter->setStretchFactor(0, 0);  // List doesn't stretch
    _splitter->setStretchFactor(1, 1);  // Screen stretches
    
    setCentralWidget(_splitter);
}

void MainWindow::setupMenuBar()
{
    // File menu
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    
    QAction* refreshAction = fileMenu->addAction(tr("&Refresh"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::onRefreshClicked);
    
    fileMenu->addSeparator();
    
    QAction* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
    
    // Settings menu
    QMenu* settingsMenu = menuBar()->addMenu(tr("&Settings"));
    
    QAction* configureAction = settingsMenu->addAction(tr("&Configure Endpoint..."));
    connect(configureAction, &QAction::triggered, this, &MainWindow::openSettings);
    
    // Help menu
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    
    QAction* aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Screen Viewer"),
            tr("Unreal Screen Viewer v1.0.0\n\n"
               "A verification utility for viewing ZX Spectrum emulator screens "
               "via shared memory IPC.\n\n"
               "Part of the Unreal-NG project."));
    });

}

void MainWindow::setupStatusBar()
{
    _statusLabel = new QLabel("Connecting...");
    _emulatorLabel = new QLabel("");
    
    statusBar()->addWidget(_statusLabel);
    statusBar()->addPermanentWidget(_emulatorLabel);
}

void MainWindow::connectSignals()
{
    // Emulator list signals
    connect(_emulatorList, &EmulatorList::emulatorSelected,
            this, &MainWindow::onEmulatorSelected);
    connect(_emulatorList, &EmulatorList::emulatorDeselected,
            this, &MainWindow::onEmulatorDeselected);
    
    // WebAPI client signals
    connect(_webApiClient.get(), &WebAPIClient::connectionStatusChanged,
            this, &MainWindow::onConnectionStatusChanged);
    connect(_webApiClient.get(), &WebAPIClient::emulatorListReceived,
            _emulatorList, &EmulatorList::updateEmulatorList);
    connect(_webApiClient.get(), &WebAPIClient::sharedMemoryEnabled,
            _screenViewer, &ScreenViewer::attachToSharedMemory);
}

void MainWindow::onEmulatorSelected(const QString& emulatorId)
{
    // Disable shared memory on previous emulator
    if (!_selectedEmulatorId.isEmpty() && _selectedEmulatorId != emulatorId)
    {
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
        _screenViewer->detachFromSharedMemory();
    }
    
    _selectedEmulatorId = emulatorId;
    
    // Enable shared memory on new emulator
    _webApiClient->enableSharedMemory(emulatorId);
    
    updateStatusBar();
}

void MainWindow::onEmulatorDeselected()
{
    if (!_selectedEmulatorId.isEmpty())
    {
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
        _screenViewer->detachFromSharedMemory();
        _selectedEmulatorId.clear();
    }
    
    updateStatusBar();
}

void MainWindow::onConnectionStatusChanged(bool connected)
{
    _isConnected = connected;
    _emulatorList->setEnabled(connected);
    
    if (!connected)
    {
        _screenViewer->detachFromSharedMemory();
        _selectedEmulatorId.clear();
    }
    
    updateStatusBar();
}

void MainWindow::onRefreshClicked()
{
    _webApiClient->fetchEmulatorList();
}

void MainWindow::openSettings()
{
    bool ok;
    QString endpoint = QInputDialog::getText(this, tr("Configure Endpoint"),
        tr("WebAPI Endpoint (host:port):"),
        QLineEdit::Normal,
        QString("%1:%2").arg(_webApiHost).arg(_webApiPort),
        &ok);
    
    if (ok && !endpoint.isEmpty())
    {
        QStringList parts = endpoint.split(':');
        if (parts.size() == 2)
        {
            _webApiHost = parts[0];
            _webApiPort = parts[1].toInt();
            _webApiClient->setEndpoint(_webApiHost, _webApiPort);
            onRefreshClicked();
        }
    }
}

void MainWindow::updateStatusBar()
{
    if (_isConnected)
    {
        _statusLabel->setText(QString("🟢 Connected to %1:%2").arg(_webApiHost).arg(_webApiPort));
    }
    else
    {
        _statusLabel->setText(QString("🔴 Unreal-NG cannot be discovered (not started?)"));
    }
    
    if (!_selectedEmulatorId.isEmpty())
    {
        _emulatorLabel->setText(QString("Selected: %1").arg(_selectedEmulatorId.left(8)));
    }
    else
    {
        _emulatorLabel->setText("");
    }
}
