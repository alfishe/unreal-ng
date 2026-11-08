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

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include "guicontrol/host.h"
#include "guicontrol/screenbinding.h"
#include "guicontrol/windowbinding.h"

#include "WebAPIClient.h"
#include "EmulatorList.h"
#include "ScreenViewer.h"
#include "emulator/buildinfo.h"
#include "ModeToolbar.h"

MainWindow::MainWindow(const QString& endpoint, QWidget* parent)
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
    const QStringList parts = endpoint.split(':');
    if (parts.size() == 2 && parts[1].toInt() > 0)
    {
        _webApiHost = parts[0];
        _webApiPort = parts[1].toInt();
        _endpointGiven = true;
    }
    _webApiClient->setEndpoint(_webApiHost, _webApiPort);

    startGuiControl();

    // Initial refresh
    onRefreshClicked();
}

MainWindow::~MainWindow()
{
    _screenControl = nullptr;
    _guiHost.reset();

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
            tr("Unreal Screen Viewer v%1 - %2 @ %3\n\n"
               "A verification utility for viewing ZX Spectrum emulator screens "
               "via shared memory IPC.\n\n"
               "Part of the Unreal-NG project.")
                .arg(QLatin1String(buildinfo::kVersion),
                     QLatin1String(buildinfo::kGitBranch),
                     QLatin1String(buildinfo::kGitCommit)));
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
    if (_screenControl)
        _screenControl->Publish();
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
    if (_screenControl)
        _screenControl->Publish();
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
    if (_screenControl)
        _screenControl->Publish();
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
            _endpointGiven = false;
            if (_guiHost)
                _guiHost->RefreshDiscovery();
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
    if (!_endpointGiven)
    {
        settings.setValue("webApiHost", _webApiHost);
        settings.setValue("webApiPort", _webApiPort);
    }
}

QString MainWindow::endpointUrl() const
{
    return QStringLiteral("http://%1:%2").arg(_webApiHost).arg(_webApiPort);
}

void MainWindow::startGuiControl()
{
    guicontrol::HostOptions options;
    options.program = QStringLiteral("unreal-screen-viewer");
    options.version = QCoreApplication::applicationVersion();
    options.FromEnvironment();
    // The WebAPI watched: where a client crosses from the screen to its machine
    options.endpoints = [this]() { return QJsonArray{endpointUrl()}; };
    _guiHost = std::make_unique<guicontrol::Host>(std::move(options));
    QString error;
    if (!_guiHost->Start(&error))
    {
        qWarning().noquote() << "GUI control: not started:" << error;
        _guiHost.reset();
        return;
    }
    new guicontrol::WindowBinding(*_guiHost, this, QStringLiteral("main"));
    guicontrol::ScreenBinding::Hooks hooks;
    hooks.nativeSize = [this]() { return _screenViewer->pictureSize(); };
    hooks.zoom = [this](int factor) {
        // The screen widget at the picture times the factor: the window grows by what it adds around it
        if (isFullScreen() || isMaximized())
            showNormal();
        resize(size() - _screenViewer->size() + _screenViewer->pictureSize() * factor);
        return QString();
    };
    hooks.instance = [this]() {
        if (_selectedEmulatorId.isEmpty())
            return QJsonObject();
        return QJsonObject{{"endpoint", endpointUrl()}, {"id", _selectedEmulatorId}};
    };
    _screenControl = new guicontrol::ScreenBinding(*_guiHost, this, _screenViewer, QStringLiteral("main"),
                                                   QStringLiteral("screen"), std::move(hooks));
    connect(_screenViewer, &ScreenViewer::viewModeChanged, _screenControl, &guicontrol::ScreenBinding::Publish);
    connect(_screenViewer, &ScreenViewer::dualLayoutChanged, _screenControl, &guicontrol::ScreenBinding::Publish);
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

