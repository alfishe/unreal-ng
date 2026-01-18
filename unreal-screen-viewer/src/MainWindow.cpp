#include "MainWindow.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QEventLoop>
#include <QSettings>
#include <QVBoxLayout>

#include "WebAPIClient.h"
#include "EmulatorList.h"
#include "ScreenViewer.h"
#include "ModeToolbar.h"

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

    // Load saved settings
    loadSettings();

    // Initial refresh
    onRefreshClicked();
}

MainWindow::~MainWindow()
{
    // Save settings before exit
    saveSettings();

    // Disable shared memory on selected emulator before exit
    // Wait up to 500ms for the request to complete, then quit anyway
    if (!_selectedEmulatorId.isEmpty() && _webApiClient)
    {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        
        connect(_webApiClient.get(), &WebAPIClient::sharedMemoryDisabled, &loop, &QEventLoop::quit);
        connect(_webApiClient.get(), &WebAPIClient::errorOccurred, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
        timeout.start(500);
        loop.exec();
    }
}

void MainWindow::setupUI()
{
    // Create splitter for left/right panels
    _splitter = new QSplitter(Qt::Horizontal, this);
    
    // Left panel: Container with emulator list + mode toolbar
    QWidget* leftPanel = new QWidget(_splitter);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    _emulatorList = new EmulatorList(leftPanel);
    _emulatorList->setMinimumWidth(200);
    
    _modeToolbar = new ModeToolbar(leftPanel);
    
    leftLayout->addWidget(_emulatorList, 1);  // List stretches
    leftLayout->addWidget(_modeToolbar, 0);   // Toolbar fixed height
    leftPanel->setLayout(leftLayout);
    leftPanel->setMaximumWidth(350);
    
    // Right panel: Screen viewer
    _screenViewer = new ScreenViewer(_splitter);
    
    _splitter->addWidget(leftPanel);
    _splitter->addWidget(_screenViewer);
    _splitter->setStretchFactor(0, 0);  // Left panel doesn't stretch
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
    
    // Mode toolbar signals
    connect(_modeToolbar, &ModeToolbar::viewModeChanged, this, [this](ModeToolbar::ViewMode mode) {
        _screenViewer->setViewMode(mode == ModeToolbar::ViewMode::Dual 
                                   ? ScreenViewer::ViewMode::Dual 
                                   : ScreenViewer::ViewMode::Single);
    });
    connect(_modeToolbar, &ModeToolbar::dualLayoutChanged, this, [this](ModeToolbar::DualLayout layout) {
        _screenViewer->setDualLayout(layout == ModeToolbar::DualLayout::Vertical 
                                     ? ScreenViewer::DualLayout::Vertical 
                                     : ScreenViewer::DualLayout::Horizontal);
    });
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

void MainWindow::saveSettings()
{
    QSettings settings("UnrealNG", "ScreenViewer");
    
    settings.setValue("viewMode", static_cast<int>(_modeToolbar->viewMode()));
    settings.setValue("dualLayout", static_cast<int>(_modeToolbar->dualLayout()));
    settings.setValue("webApiHost", _webApiHost);
    settings.setValue("webApiPort", _webApiPort);
}

void MainWindow::loadSettings()
{
    QSettings settings("UnrealNG", "ScreenViewer");
    
    // Restore view mode
    int viewMode = settings.value("viewMode", 0).toInt();
    _modeToolbar->setViewMode(static_cast<ModeToolbar::ViewMode>(viewMode));
    
    // Restore dual layout
    int dualLayout = settings.value("dualLayout", 0).toInt();
    _modeToolbar->setDualLayout(static_cast<ModeToolbar::DualLayout>(dualLayout));
    
    // Restore WebAPI settings
    _webApiHost = settings.value("webApiHost", "localhost").toString();
    _webApiPort = settings.value("webApiPort", 8090).toInt();
}

