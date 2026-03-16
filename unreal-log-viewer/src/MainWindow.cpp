#include "MainWindow.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QToolBar>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QEventLoop>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTabWidget>

#include "WebAPIClient.h"
#include "EmulatorList.h"
#include "LogViewWidget.h"
#include "CategoryPanel.h"
#include "FramebufferWidget.h"
#include "HeatmapWidget.h"
#include "TraceWidget.h"
#include "ChipStateWidget.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _webApiClient(std::make_unique<WebAPIClient>())
{
    setWindowTitle("Unreal Companion");
    setMinimumSize(900, 600);
    resize(1200, 800);

    setupUI();
    setupToolBar();
    setupMenuBar();
    setupStatusBar();
    connectSignals();

    _webApiClient->setEndpoint(_webApiHost, _webApiPort);

    // Refresh timer — poll emulator list every 2 seconds
    _refreshTimer = new QTimer(this);
    connect(_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshClicked);
    _refreshTimer->start(2000);

    loadSettings();
    onRefreshClicked();
}

MainWindow::~MainWindow()
{
    saveSettings();

    // Disable shared memory on selected emulator before exit
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

// =============================================================================
// UI Setup
// =============================================================================

void MainWindow::setupUI()
{
    _mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: vertical splitter with EmulatorList (top) + CategoryPanel (bottom)
    _leftSplitter = new QSplitter(Qt::Vertical);

    _emulatorList = new EmulatorList(_leftSplitter);
    _emulatorList->setMinimumWidth(200);
    _emulatorList->setMaximumHeight(200);

    _categoryPanel = new CategoryPanel(_leftSplitter);

    _leftSplitter->addWidget(_emulatorList);
    _leftSplitter->addWidget(_categoryPanel);
    _leftSplitter->setStretchFactor(0, 0);
    _leftSplitter->setStretchFactor(1, 1);

    // Right panel: Tab widget with visualization tabs
    _tabWidget = new QTabWidget();

    _logView = new LogViewWidget();
    _framebufferView = new FramebufferWidget();
    _heatmapView = new HeatmapWidget();
    _traceView = new TraceWidget();
    _chipView = new ChipStateWidget();

    _tabWidget->addTab(_logView, "Logs");
    _tabWidget->addTab(_framebufferView, "Screen");
    _tabWidget->addTab(_heatmapView, "Heatmap");
    _tabWidget->addTab(_traceView, "Traces");
    _tabWidget->addTab(_chipView, "Chips");

    connect(_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    _mainSplitter->addWidget(_leftSplitter);
    _mainSplitter->addWidget(_tabWidget);
    _mainSplitter->setStretchFactor(0, 0);
    _mainSplitter->setStretchFactor(1, 1);
    _mainSplitter->setSizes({420, 800});

    // Collapse "ear" button on the splitter handle
    QSplitterHandle* handle = _mainSplitter->handle(1);
    auto* handleLayout = new QVBoxLayout(handle);
    handleLayout->setContentsMargins(0, 0, 0, 0);
    handleLayout->setSpacing(0);

    handleLayout->addStretch();
    auto* collapseBtn = new QPushButton(QString::fromUtf8("\xC2\xAB"), handle);
    collapseBtn->setFixedSize(16, 48);
    collapseBtn->setToolTip("Collapse/expand side panel");
    collapseBtn->setStyleSheet(
        "QPushButton { background: #ddd; border: 1px solid #bbb; border-radius: 3px;"
        " font-size: 11px; padding: 0; }"
        "QPushButton:hover { background: #ccc; }");
    handleLayout->addWidget(collapseBtn);
    collapseBtn->setCursor(Qt::ArrowCursor);
    handleLayout->addStretch();

    connect(collapseBtn, &QPushButton::clicked, this, [this, collapseBtn]() {
        bool collapsed = (_mainSplitter->sizes().at(0) == 0);
        if (collapsed)
        {
            _mainSplitter->setSizes({420, 800});
            collapseBtn->setText(QString::fromUtf8("\xC2\xAB"));
        }
        else
        {
            _mainSplitter->setSizes({0, 1});
            collapseBtn->setText(QString::fromUtf8("\xC2\xBB"));
        }
    });

    setCentralWidget(_mainSplitter);
}

void MainWindow::setupToolBar()
{
    // Log controls (Clear, Auto-scroll) are now local to LogViewWidget
    // No global toolbar needed
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

    // View menu
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    QAction* clearAction = viewMenu->addAction(tr("&Clear Log"));
    clearAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(clearAction, &QAction::triggered, _logView, &LogViewWidget::clearLog);

    QAction* autoScrollAction = viewMenu->addAction(tr("&Auto-scroll"));
    autoScrollAction->setCheckable(true);
    autoScrollAction->setChecked(true);
    connect(autoScrollAction, &QAction::toggled, _logView, &LogViewWidget::setAutoScroll);

    // Settings menu
    QMenu* settingsMenu = menuBar()->addMenu(tr("&Settings"));

    QAction* configureAction = settingsMenu->addAction(tr("&Configure Endpoint..."));
    connect(configureAction, &QAction::triggered, this, &MainWindow::openSettings);

    // Help menu
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction* aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Unreal Companion"),
            tr("Unreal Companion v1.2.0\n\n"
               "Real-time companion for the Unreal-NG ZX Spectrum emulator.\n"
               "Connects via shared memory IPC for zero-copy log streaming,\n"
               "framebuffer display, memory heatmaps, execution traces,\n"
               "and live chip register inspection.\n\n"
               "Part of the Unreal-NG project."));
    });
}

void MainWindow::setupStatusBar()
{
    _statusLabel = new QLabel("Connecting...");
    _statsLabel = new QLabel("");
    _shmLabel = new QLabel("");

    statusBar()->addWidget(_statusLabel);
    statusBar()->addPermanentWidget(_statsLabel);
    statusBar()->addPermanentWidget(_shmLabel);
}

// =============================================================================
// Signal wiring
// =============================================================================

void MainWindow::connectSignals()
{
    // EmulatorList
    connect(_emulatorList, &EmulatorList::emulatorSelected,
            this, &MainWindow::onEmulatorSelected);
    connect(_emulatorList, &EmulatorList::emulatorDeselected,
            this, &MainWindow::onEmulatorDeselected);

    // WebAPI → EmulatorList
    connect(_webApiClient.get(), &WebAPIClient::connectionStatusChanged,
            this, &MainWindow::onConnectionStatusChanged);
    connect(_webApiClient.get(), &WebAPIClient::emulatorListReceived,
            _emulatorList, &EmulatorList::updateEmulatorList);

    // WebAPI → LogViewWidget (shared memory enabled → attach to SHM)
    connect(_webApiClient.get(), &WebAPIClient::sharedMemoryEnabled,
            _logView, &LogViewWidget::attachToShm);

    // LogViewWidget stats → status bar
    connect(_logView, &LogViewWidget::statsUpdated,
            this, &MainWindow::onStatsUpdated);

    // LogViewWidget attach/detach → SHM sharing with visualization widgets
    connect(_logView, &LogViewWidget::attached, this, [this](const QString& name) {
        _shmLabel->setText(QString("SHM: %1").arg(name));
        _logView->requestCategoryList();
        _logView->requestModuleList();

        // Notify widgets about FIFO availability for event-driven refresh
        bool fifo = _logView->isFifoActive();
        _framebufferView->setFifoActive(fifo);
        _heatmapView->setFifoActive(fifo);
        _traceView->setFifoActive(fifo);
        _chipView->setFifoActive(fifo);
    });
    connect(_logView, &LogViewWidget::detached, this, [this]() {
        _shmLabel->setText("");
    });

    // SHM sharing: forward to all visualization widgets
    connect(_logView, &LogViewWidget::shmAttached, _framebufferView, &FramebufferWidget::attachToShm);
    connect(_logView, &LogViewWidget::shmAttached, _heatmapView, &HeatmapWidget::attachToShm);
    connect(_logView, &LogViewWidget::shmAttached, _traceView, &TraceWidget::attachToShm);
    connect(_logView, &LogViewWidget::shmAttached, _chipView, &ChipStateWidget::attachToShm);

    connect(_logView, &LogViewWidget::shmDetached, _framebufferView, &FramebufferWidget::detachFromShm);
    connect(_logView, &LogViewWidget::shmDetached, _heatmapView, &HeatmapWidget::detachFromShm);
    connect(_logView, &LogViewWidget::shmDetached, _traceView, &TraceWidget::detachFromShm);
    connect(_logView, &LogViewWidget::shmDetached, _chipView, &ChipStateWidget::detachFromShm);

    // Frame notification: FIFO → all visualization widgets
    connect(_logView, &LogViewWidget::frameReady,
            _framebufferView, &FramebufferWidget::onFrameReady);
    connect(_logView, &LogViewWidget::frameReady,
            _heatmapView, &HeatmapWidget::onFrameReady);
    connect(_logView, &LogViewWidget::frameReady,
            _traceView, &TraceWidget::onFrameReady);
    connect(_logView, &LogViewWidget::frameReady,
            _chipView, &ChipStateWidget::onFrameReady);

    // LogViewWidget → CategoryPanel (category list response)
    connect(_logView, &LogViewWidget::categoriesReceived,
            _categoryPanel, &CategoryPanel::updateCategories);

    // CategoryPanel → LogViewWidget (emulog control commands)
    connect(_categoryPanel, &CategoryPanel::enableRequested,
            _logView, &LogViewWidget::enableCategory);
    connect(_categoryPanel, &CategoryPanel::disableRequested,
            _logView, &LogViewWidget::disableCategory);
    connect(_categoryPanel, &CategoryPanel::levelChangeRequested,
            _logView, &LogViewWidget::setLogLevel);
    connect(_categoryPanel, &CategoryPanel::refreshRequested,
            _logView, &LogViewWidget::requestCategoryList);

    // CategoryPanel → WebAPI (ModuleLogger control via HTTP)
    // Create WebAPIClient if not yet created
    if (!_webApiClient)
    {
        _webApiClient = std::make_unique<WebAPIClient>(this);
        _webApiClient->setEndpoint(_webApiHost, _webApiPort);
    }
    _categoryPanel->setApiClient(_webApiClient.get(), _selectedEmulatorId.isEmpty() ? "0" : _selectedEmulatorId);
    _heatmapView->setApiClient(_webApiClient.get(), _selectedEmulatorId.isEmpty() ? "0" : _selectedEmulatorId);

    // CategoryPanel → filter (double-click module/submodule to filter logs)
    connect(_categoryPanel, &CategoryPanel::filterRequested,
            this, [this](int moduleId, int submoduleId) {
                _logView->setModuleFilter(moduleId, submoduleId);
            });

    // CategoryPanel View tab → LogViewWidget display filtering
    connect(_categoryPanel, &CategoryPanel::visibilityChanged,
            _logView, &LogViewWidget::setModuleVisible);
    connect(_categoryPanel, &CategoryPanel::submoduleVisibilityChanged,
            _logView, &LogViewWidget::setSubmoduleVisibility);

    // LogViewWidget → CategoryPanel activity indicators
    connect(_logView, &LogViewWidget::moduleActivity,
            _categoryPanel, &CategoryPanel::onModuleActivity);

    // LogViewWidget → CategoryPanel logger state sync (SHM-based, automatic)
    connect(_logView, &LogViewWidget::loggerStateReceived,
            _categoryPanel, &CategoryPanel::onLoggerStateReceived);
}

// =============================================================================
// Slots
// =============================================================================

void MainWindow::onEmulatorSelected(const QString& emulatorId)
{
    // Disable shared memory on previous emulator
    if (!_selectedEmulatorId.isEmpty() && _selectedEmulatorId != emulatorId)
    {
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
        _logView->detachFromShm();
    }

    _selectedEmulatorId = emulatorId;

    // Update heatmap widget with the real emulator ID for bank mapping API calls
    _heatmapView->setApiClient(_webApiClient.get(), emulatorId);

    // Enable shared memory (single IPC mechanism — enables both RAM export and monitoring)
    _webApiClient->enableSharedMemory(emulatorId);

    updateStatusBar();
}

void MainWindow::onEmulatorDeselected()
{
    if (!_selectedEmulatorId.isEmpty())
    {
        _webApiClient->disableSharedMemory(_selectedEmulatorId);
        _logView->detachFromShm();
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
        _logView->detachFromShm();
        _selectedEmulatorId.clear();
    }

    updateStatusBar();
}

void MainWindow::onRefreshClicked()
{
    _webApiClient->fetchEmulatorList();
}

void MainWindow::onStatsUpdated(uint64_t msgCount, uint64_t dropped)
{
    _statsLabel->setText(QString("%1 msgs, %2 dropped")
                            .arg(msgCount).arg(dropped));
}

void MainWindow::onTabChanged(int /*index*/)
{
    // Left panel and toolbar remain visible on all tabs for consistent layout
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
        _statusLabel->setText(QString("Connected to %1:%2").arg(_webApiHost).arg(_webApiPort));
    }
    else
    {
        _statusLabel->setText("Not connected");
    }
}

// =============================================================================
// Settings persistence
// =============================================================================

void MainWindow::saveSettings()
{
    QSettings settings("UnrealNG", "LogViewer");
    settings.setValue("webApiHost", _webApiHost);
    settings.setValue("webApiPort", _webApiPort);
    settings.setValue("autoScroll", _logView->autoScroll());
    settings.setValue("activeTab", _tabWidget->currentIndex());
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::loadSettings()
{
    QSettings settings("UnrealNG", "LogViewer");
    _webApiHost = settings.value("webApiHost", "localhost").toString();
    _webApiPort = settings.value("webApiPort", 8090).toInt();
    _webApiClient->setEndpoint(_webApiHost, _webApiPort);

    bool autoScroll = settings.value("autoScroll", true).toBool();
    _logView->setAutoScroll(autoScroll);

    int activeTab = settings.value("activeTab", 0).toInt();
    if (activeTab >= 0 && activeTab < _tabWidget->count())
        _tabWidget->setCurrentIndex(activeTab);

    QByteArray geom = settings.value("geometry").toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
}
