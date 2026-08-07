#include "MainWindow.h"
#include "ScreenWidget.h"
#include "StatusIndicator.h"
#include "emulator/EmulatorWidget.h"
#include "emulator/AppSoundManager.h"
#include "emulator/FileManager.h"
#include "dialogs/AudioSettingsDialog.h"

#ifdef Q_OS_MACOS
#include "platform/macos/FullscreenHelper.h"
#include "platform/macos/MetalScreenWidget.h"
#endif

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFrame>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QDateTime>
#include <QVBoxLayout>

static QIcon themedIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Content frame - contains screen widget with layout for proper minimum size propagation
    m_contentFrame = new QFrame(this);
    m_contentFrame->setFrameStyle(QFrame::NoFrame);
    m_contentFrame->setAutoFillBackground(true);
    setCentralWidget(m_contentFrame);

#ifdef Q_OS_MACOS
    m_screen = new MetalScreenWidget(m_contentFrame);
#else
    m_screen = new ScreenWidget(m_contentFrame);
#endif

    // Use a layout so content frame respects screen widget's minimum size
    auto* layout = new QVBoxLayout(m_contentFrame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_screen);

    m_normalPalette = palette();

    // Timer to update zoom menu after resize finishes
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(150);
    connect(m_resizeTimer, &QTimer::timeout, this, &MainWindow::updateZoomCheck);

    // Initialize emulator and audio
    m_emulator = new EmulatorWidget(this);
    m_soundManager = new AppSoundManager(this);

    // Connect emulator frame signal to screen refresh
    connect(m_emulator, &EmulatorWidget::frameReady, this, [this]() {
        m_screen->refresh();
    });

    // Connect resolution change signal to update screen widget size
    connect(m_emulator, &EmulatorWidget::resolutionChanged, this, [this](int width, int height) {
        qDebug() << "Resolution changed:" << width << "x" << height;
        m_screen->attachFramebuffer(width, height, m_emulator->framebuffer());
        adjustWindowToFitScreen();
    });

    buildActions();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    setVideoMode(m_currentModeIndex);

    refreshTitle();
    restoreLayout();

    // Initialize audio
    if (m_soundManager->init()) {
        m_soundManager->start();
    }

    // Enable drag and drop
    setAcceptDrops(true);

    flash(tr("Ready — %1").arg(m_machine));
}

MainWindow::~MainWindow()
{
#ifdef Q_OS_MACOS
    // Stop display link first to prevent callbacks during destruction
    m_screen->setAnimating(false);
    FullscreenHelper::uninstall(windowHandle());
#endif

    // Clear audio callback before stopping
#ifdef HAS_EMULATOR_CORE
    if (m_emulator && m_emulator->emulator()) {
        m_emulator->emulator()->ClearAudioCallback();
    }
#endif

    // Stop emulator
    if (m_emulator) {
        m_emulator->stop();
    }

    // Stop audio
    if (m_soundManager) {
        m_soundManager->stop();
        m_soundManager->deinit();
    }
}

// ============================================================================
// Fullscreen Style Helpers
// ============================================================================

void MainWindow::applyFullscreenStyle()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] applyFullscreenStyle START";

    // Disable updates to prevent visible relayout during transition
    setUpdatesEnabled(false);

    // Set black background for fullscreen
    QPalette p;
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
    m_contentFrame->setPalette(p);

    menuBar()->hide();
    statusBar()->hide();
    m_toolBar->hide();

    setUpdatesEnabled(true);

    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] applyFullscreenStyle END";
}

void MainWindow::restoreNormalStyle()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] restoreNormalStyle START";

    // Batch the three bar-shows + palette into a single repaint
    setUpdatesEnabled(false);

    // Restore system theme palette
    setPalette(m_normalPalette);
    m_contentFrame->setPalette(m_normalPalette);

    if (m_actToolBar->isChecked())
        m_toolBar->show();
    if (m_actStatusBar->isChecked())
        statusBar()->show();
    menuBar()->show();

    setUpdatesEnabled(true);

    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] restoreNormalStyle END";
}

// ============================================================================
// Platform-Specific Fullscreen Handlers
// ============================================================================

#ifdef Q_OS_WIN
void MainWindow::toggleFullscreenWindows()
{
    if (m_fullscreenState == FullscreenState::Fullscreen)
    {
        m_fullscreenState = FullscreenState::Normal;
        restoreNormalStyle();
        setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);

        if (m_preFullscreenState & Qt::WindowMaximized)
        {
            showMaximized();
        }
        else
        {
            QRect savedGeom = m_normalGeometry;
            showNormal();
            setGeometry(savedGeom);
        }

        m_screen->setFocus();
    }
    else if (m_fullscreenState == FullscreenState::Normal)
    {
        m_preFullscreenState = (windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : Qt::WindowNoState;

        if (m_preFullscreenState & Qt::WindowMaximized)
            m_maximizedGeometry = geometry();
        else
            m_normalGeometry = geometry();

        m_fullscreenState = FullscreenState::Fullscreen;
        applyFullscreenStyle();
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        showFullScreen();
        m_screen->setFocus();
    }
}

void MainWindow::handleWindowStateChangeWindows(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    // Save normal geometry when entering maximized (not from fullscreen)
    if ((newState & Qt::WindowMaximized) && !(oldState & Qt::WindowMaximized) && !(oldState & Qt::WindowFullScreen))
    {
        if (m_fullscreenState == FullscreenState::Normal)
            m_normalGeometry = geometry();
    }
}
#endif

#ifdef Q_OS_MACOS
void MainWindow::toggleFullscreenMacOS()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] toggleFullscreenMacOS state=" << (int)m_fullscreenState;

    // Guard against rapid toggling during transitions
    if (m_fullscreenState == FullscreenState::EnteringFullscreen ||
        m_fullscreenState == FullscreenState::ExitingFullscreen)
        return;

    // Ensure delegate is installed (first call after window shown)
    static bool installed = false;
    if (!installed) {
        FullscreenHelper::install(windowHandle(), this);
        // Set callbacks for custom animation
        FullscreenHelper::setCallbacks(
            windowHandle(),
            [this]() { applyFullscreenStyle(); },
            [this]() { restoreNormalStyle(); }
        );
        installed = true;
    }

    if (m_fullscreenState == FullscreenState::Fullscreen)
    {
        qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] exiting fullscreen";
        m_fullscreenState = FullscreenState::ExitingFullscreen;
        m_screen->setFullscreenLayout(0);
        FullscreenHelper::exitFullscreen(windowHandle());
    }
    else
    {
        qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] entering fullscreen";
        m_preFullscreenState = (windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : Qt::WindowNoState;
        if (m_preFullscreenState & Qt::WindowMaximized)
            m_maximizedGeometry = geometry();
        else
            m_normalGeometry = geometry();

        m_fullscreenState = FullscreenState::EnteringFullscreen;

        QSize screenSize = FullscreenHelper::fullscreenSize(windowHandle());
        double screenAspect = (double)screenSize.width() / screenSize.height();
        qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] setFullscreenLayout aspect=" << screenAspect;
        m_screen->setFullscreenLayout(screenAspect);

        FullscreenHelper::enterFullscreen(windowHandle());
    }
}

void MainWindow::willEnterFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] willEnterFullscreen";

    // Start continuous rendering during transition
    m_screen->setAnimating(true);

    // Apply fullscreen style before transition starts
    applyFullscreenStyle();
}

void MainWindow::didEnterFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] didEnterFullscreen";

    // Stop continuous rendering - emulator drives updates now
    m_screen->setAnimating(false);

    // Keep state as EnteringFullscreen briefly to block spurious resize
    QTimer::singleShot(200, this, [this]() {
        m_fullscreenState = FullscreenState::Fullscreen;
        m_screen->setFocus();
    });
}

void MainWindow::willExitFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] willExitFullscreen";

    // Start continuous rendering during exit transition
    m_screen->setAnimating(true);
}

void MainWindow::didExitFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] didExitFullscreen";

    // Stop continuous rendering
    m_screen->setAnimating(false);

    // Restore normal style
    restoreNormalStyle();

    // Qt UI was restored in the exit-animation completion handler; here we only
    // finalize state and enforce the pre-fullscreen geometry. AppKit can end
    // the transition with the content area 28px taller (FullSizeContentView
    // leftover), so re-apply the saved geometry once everything settled.
    m_fullscreenState = FullscreenState::Normal;

    QTimer::singleShot(0, this, [this]() {
        if (m_fullscreenState != FullscreenState::Normal)
            return;
        if (!(m_preFullscreenState & Qt::WindowMaximized) && m_normalGeometry.isValid()
            && geometry() != m_normalGeometry)
        {
            setGeometry(m_normalGeometry);
        }

        m_screen->setFocus();
    });
}

void MainWindow::handleWindowStateChangeMacOS(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    // Save normal geometry when maximizing (not from fullscreen)
    bool wasFullscreen = oldState & Qt::WindowFullScreen;
    if ((newState & Qt::WindowMaximized) && !(oldState & Qt::WindowMaximized) && !wasFullscreen)
    {
        m_normalGeometry = geometry();
    }
}
#endif

#ifdef Q_OS_LINUX
void MainWindow::toggleFullscreenLinux()
{
    if (m_fullscreenState == FullscreenState::Fullscreen)
    {
        m_fullscreenState = FullscreenState::ExitingFullscreen;

        if (m_preFullscreenState & Qt::WindowMaximized)
            showMaximized();
        else
            showNormal();
    }
    else if (m_fullscreenState == FullscreenState::Normal)
    {
        m_preFullscreenState = (windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : Qt::WindowNoState;

        if (m_preFullscreenState & Qt::WindowMaximized)
            m_maximizedGeometry = geometry();
        else
            m_normalGeometry = geometry();

        m_fullscreenState = FullscreenState::EnteringFullscreen;
        applyFullscreenStyle();
        showFullScreen();
    }
}

void MainWindow::handleWindowStateChangeLinux(Qt::WindowStates oldState, Qt::WindowStates newState)
{
    bool wasFullscreen = oldState & Qt::WindowFullScreen;
    bool isFullscreen = newState & Qt::WindowFullScreen;

    if (!wasFullscreen && isFullscreen)
    {
        m_fullscreenState = FullscreenState::Fullscreen;
        m_screen->setFocus();
    }
    else if (wasFullscreen && !isFullscreen)
    {
        m_fullscreenState = FullscreenState::Normal;
        restoreNormalStyle();
        if (!(m_preFullscreenState & Qt::WindowMaximized))
            setGeometry(m_normalGeometry);
        m_screen->setFocus();
    }

    // Save normal geometry when maximizing (not from fullscreen)
    if ((newState & Qt::WindowMaximized) && !(oldState & Qt::WindowMaximized) && !wasFullscreen)
    {
        m_normalGeometry = geometry();
    }
}
#endif

// ============================================================================
// Event Handlers
// ============================================================================

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Forward to emulator
    if (m_emulator) {
        m_emulator->handleKeyPress(event);
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    // Forward to emulator
    if (m_emulator) {
        m_emulator->handleKeyRelease(event);
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        QList<QUrl> urlList = mimeData->urls();
        if (!urlList.isEmpty())
        {
            QString filePath = urlList.first().toLocalFile();
            qDebug() << "File dropped:" << filePath;
            loadFile(filePath);
        }
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        auto *e = static_cast<QWindowStateChangeEvent*>(event);
        Qt::WindowStates oldState = e->oldState();
        Qt::WindowStates newState = windowState();

        qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] WindowStateChange old=" << (int)oldState << " new=" << (int)newState;

#ifdef Q_OS_WIN
        handleWindowStateChangeWindows(oldState, newState);
#endif
#ifdef Q_OS_MACOS
        handleWindowStateChangeMacOS(oldState, newState);
#endif
#ifdef Q_OS_LINUX
        handleWindowStateChangeLinux(oldState, newState);
#endif

        // Update action checkstate to match actual window state
        bool isFs = (newState & Qt::WindowFullScreen);
        if (m_actFullscreen->isChecked() != isFs)
        {
            m_actFullscreen->blockSignals(true);
            m_actFullscreen->setChecked(isFs);
            m_actFullscreen->blockSignals(false);
        }
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] resizeEvent " << event->oldSize().width() << "x" << event->oldSize().height() << " -> " << event->size().width() << "x" << event->size().height();

    // Ignore resize events during fullscreen transitions - they cause visual glitches
    if (m_fullscreenState == FullscreenState::EnteringFullscreen ||
        m_fullscreenState == FullscreenState::ExitingFullscreen) {
        qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] resizeEvent IGNORED (transition)";
        return;
    }

    QMainWindow::resizeEvent(event);

    // Debounce zoom menu update
    m_resizeTimer->start();
}

void MainWindow::onToggleFullscreen()
{
#ifdef Q_OS_WIN
    toggleFullscreenWindows();
#elif defined(Q_OS_MACOS)
    toggleFullscreenMacOS();
#elif defined(Q_OS_LINUX)
    toggleFullscreenLinux();
#else
    // Fallback for other platforms
    if (m_fullscreenState == FullscreenState::Fullscreen)
    {
        m_fullscreenState = FullscreenState::Normal;
        restoreNormalStyle();
        showNormal();
        setGeometry(m_normalGeometry);
        m_screen->setFocus();
    }
    else if (m_fullscreenState == FullscreenState::Normal)
    {
        m_normalGeometry = geometry();
        m_fullscreenState = FullscreenState::Fullscreen;
        applyFullscreenStyle();
        showFullScreen();
        m_screen->setFocus();
    }
#endif
}

// ============================================================================
// Video Mode and Window Size
// ============================================================================

void MainWindow::setVideoMode(int modeIndex)
{
    int count = ScreenWidget::videoModeCount();
    if (modeIndex < 0 || modeIndex >= count)
        modeIndex = 2;

    m_currentModeIndex = modeIndex;
    m_screen->loadTestPattern(modeIndex);

    QList<QAction*> actions = m_videoModes->actions();
    if (modeIndex < actions.size())
        actions[modeIndex]->setChecked(true);

    if (m_fullscreenState == FullscreenState::Normal)
        adjustWindowToFitScreen();
}

void MainWindow::adjustWindowToFitScreen()
{
    if (!m_screen)
        return;

    QSize native = m_screen->nativeSize();

    int extraH = 0;
    if (m_toolBar && m_toolBar->isVisible())
        extraH += m_toolBar->height();
    if (statusBar() && statusBar()->isVisible())
        extraH += statusBar()->height();
    if (menuBar() && menuBar()->isVisible())
        extraH += menuBar()->height();

    QSize targetSize(native.width(), native.height() + extraH);

    QScreen* scr = screen();
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    QRect availableGeometry = scr->availableGeometry();

    bool needResize = m_enforce1to1 ||
        (size().width() < targetSize.width() || size().height() < targetSize.height());

    if (needResize)
    {
        int newW = qMin(targetSize.width(), availableGeometry.width());
        int newH = qMin(targetSize.height(), availableGeometry.height());
        resize(newW, newH);
    }

    setMinimumSize(native.width(), native.height() + extraH);
    m_screen->refresh();
}

// ============================================================================
// Actions and UI Building
// ============================================================================

void MainWindow::buildActions()
{
    // ---- File ------------------------------------------------------------
    m_actOpen = new QAction(themedIcon(QStringLiteral("open")), tr("&Open…"), this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, [this] {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Open media"), QString(),
            tr("ZX Spectrum media (*.tap *.tzx *.trd *.scl *.sna *.z80);;All files (*)"));
        if (!f.isEmpty()) {
            m_media = QFileInfo(f).fileName();
            refreshTitle();
            flash(tr("Loaded %1").arg(m_media));
        }
    });

    m_actSaveState = new QAction(tr("&Save Snapshot"), this);
    m_actSaveState->setShortcut(Qt::Key_F2);
    connect(m_actSaveState, &QAction::triggered, this, [this] { flash(tr("Snapshot saved — slot 3")); });

    m_actLoadState = new QAction(tr("&Load Snapshot"), this);
    m_actLoadState->setShortcut(Qt::Key_F3);
    connect(m_actLoadState, &QAction::triggered, this, [this] { flash(tr("Snapshot loaded — slot 3")); });

    m_actQuit = new QAction(tr("&Quit"), this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    m_actQuit->setMenuRole(QAction::QuitRole);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    // ---- Transport -------------------------------------------------------
    m_actStart = new QAction(themedIcon(QStringLiteral("start")), tr("Start"), this);
    m_actStart->setCheckable(true);
    m_actStart->setChecked(true);
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStart);

    m_actPause = new QAction(themedIcon(QStringLiteral("pause")), tr("Pause"), this);
    m_actPause->setCheckable(true);
    m_actPause->setShortcut(Qt::Key_Pause);
    connect(m_actPause, &QAction::triggered, this, &MainWindow::onPause);

    m_actRestart = new QAction(themedIcon(QStringLiteral("restart")), tr("Restart"), this);
    m_actRestart->setShortcut(Qt::Key_F12);
    connect(m_actRestart, &QAction::triggered, this, &MainWindow::onRestart);

    m_actRecord = new QAction(themedIcon(QStringLiteral("record")), tr("Start / stop recording"), this);
    m_actRecord->setCheckable(true);
    m_actRecord->setShortcut(Qt::Key_F6);
    connect(m_actRecord, &QAction::toggled, this, &MainWindow::onToggleRecord);

    // ---- View ------------------------------------------------------------
    m_actToolBar = new QAction(tr("&Toolbar"), this);
    m_actToolBar->setCheckable(true);
    m_actToolBar->setChecked(true);
    m_actToolBar->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));

    m_actStatusBar = new QAction(tr("&Status bar"), this);
    m_actStatusBar->setCheckable(true);
    m_actStatusBar->setChecked(true);
    m_actStatusBar->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));

    m_actFullscreen = new QAction(themedIcon(QStringLiteral("fullscreen")), tr("&Fullscreen"), this);
    m_actFullscreen->setCheckable(true);
    m_actFullscreen->setShortcuts({QKeySequence(QStringLiteral("Ctrl+F")), QKeySequence(Qt::Key_F11)});
    m_actFullscreen->setShortcutContext(Qt::WindowShortcut);
    connect(m_actFullscreen, &QAction::triggered, this, &MainWindow::onToggleFullscreen);

    m_actNextVideoMode = new QAction(themedIcon(QStringLiteral("videomode")), tr("Next Video Mode"), this);
    m_actNextVideoMode->setShortcut(Qt::Key_F5);
    m_actNextVideoMode->setToolTip(tr("Cycle to next video mode (F5)"));
    connect(m_actNextVideoMode, &QAction::triggered, this, &MainWindow::onNextVideoMode);

    m_act1to1 = new QAction(tr("1:1 Pixel Size"), this);
    m_act1to1->setCheckable(true);
    m_act1to1->setChecked(false);
    m_act1to1->setShortcut(Qt::Key_F4);
    m_act1to1->setToolTip(tr("Enforce 1:1 pixel display on mode change (F4)"));
    connect(m_act1to1, &QAction::toggled, this, &MainWindow::onToggle1to1);

    m_zoomGroup = new QActionGroup(this);
    m_zoomGroup->setExclusive(true);
    for (int i = 1; i <= 4; ++i) {
        QAction* a = new QAction(tr("%1x").arg(i), this);
        a->setCheckable(true);
        a->setData(i);
        a->setChecked(i == 2);  // Default to 2x
        m_zoomGroup->addAction(a);
    }
    connect(m_zoomGroup, &QActionGroup::triggered, this, &MainWindow::onZoomChanged);

    m_actResetLayout = new QAction(tr("Reset Layout"), this);
    connect(m_actResetLayout, &QAction::triggered, this, &MainWindow::onResetLayout);

    m_videoModes = new QActionGroup(this);
    m_videoModes->setExclusive(true);
    const auto* modes = ScreenWidget::videoModes();
    int count = ScreenWidget::videoModeCount();
    for (int i = 0; i < count; ++i)
    {
        QAction* a = new QAction(QString::fromLatin1(modes[i].name), this);
        a->setCheckable(true);
        a->setData(i);
        a->setChecked(i == m_currentModeIndex);
        m_videoModes->addAction(a);
    }
    connect(m_videoModes, &QActionGroup::triggered, this, &MainWindow::onVideoModeChanged);

    // ---- Machine ---------------------------------------------------------
    m_machines = new QActionGroup(this);
    m_machines->setExclusive(true);
    for (const QString &name : { QStringLiteral("ZX Spectrum 48K"),
                                 QStringLiteral("ZX Spectrum 128K"),
                                 QStringLiteral("Pentagon 128"),
                                 QStringLiteral("Scorpion ZS-256") }) {
        QAction *a = new QAction(name, this);
        a->setCheckable(true);
        a->setChecked(name == m_machine);
        m_machines->addAction(a);
    }
    connect(m_machines, &QActionGroup::triggered, this, &MainWindow::onMachineChanged);

    // ---- Media -----------------------------------------------------------
    m_actTape = new QAction(tr("Tape"), this);
    m_actTape->setCheckable(true);
    m_actTape->setChecked(true);
    m_actTape->setShortcut(Qt::Key_F8);

    m_actDisk = new QAction(tr("Disk drive"), this);
    m_actDisk->setCheckable(true);

    m_actHdd = new QAction(tr("Hard disk"), this);
    m_actHdd->setCheckable(true);

    m_actEject = new QAction(tr("Eject all"), this);
    connect(m_actEject, &QAction::triggered, this, &MainWindow::onEjectAll);

    // ---- Audio -----------------------------------------------------------
    m_actSound = new QAction(tr("Enable sound"), this);
    m_actSound->setCheckable(true);
    m_actSound->setChecked(true);
    m_actSound->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));

    m_actAy = new QAction(tr("AY-3-8912"), this);
    m_actAy->setCheckable(true);
    m_actAy->setChecked(true);

    m_actBeeper = new QAction(tr("Beeper"), this);
    m_actBeeper->setCheckable(true);
    m_actBeeper->setChecked(true);

    m_actAudioSettings = new QAction(tr("Audio Settings..."), this);
    connect(m_actAudioSettings, &QAction::triggered, this, [this] {
        if (!m_audioSettingsDialog) {
            m_audioSettingsDialog = new AudioSettingsDialog(this);
            m_audioSettingsDialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_audioSettingsDialog, &QDialog::destroyed, this, [this] {
                m_audioSettingsDialog = nullptr;
            });
        }
#ifdef HAS_EMULATOR_CORE
        if (m_emulator && m_emulator->isRunning()) {
            if (auto emu = m_emulator->emulator()) {
                EmulatorContext* ctx = emu->GetContext();
                if (ctx && ctx->pSoundManager) {
                    m_audioSettingsDialog->setContext(ctx);
                }
            }
        }
#endif
        m_audioSettingsDialog->show();
        m_audioSettingsDialog->raise();
        m_audioSettingsDialog->activateWindow();
    });

    // ---- Debug / Help ----------------------------------------------------
    m_actDebugger = new QAction(tr("&Debugger"), this);
    m_actDebugger->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_actDebugger, &QAction::triggered, this, [this] { flash(tr("Debugger opened")); });

    m_actAbout = new QAction(tr("&About unreal-ng"), this);
    m_actAbout->setMenuRole(QAction::AboutRole);
    connect(m_actAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About unreal-ng"),
                           tr("<b>unreal-ng 0.9.4</b><br>Qt6 ZX Spectrum emulator front end."));
    });
}

void MainWindow::buildMenus()
{
    QMenuBar *mb = menuBar();

    QMenu *file = mb->addMenu(tr("&File"));
    file->addAction(m_actOpen);
    file->addSeparator();
    file->addAction(m_actSaveState);
    file->addAction(m_actLoadState);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu *view = mb->addMenu(tr("&View"));
    view->addAction(m_actToolBar);
    view->addAction(m_actStatusBar);
    view->addSeparator();

    QMenu *videoMode = view->addMenu(tr("&Video Mode"));
    videoMode->addActions(m_videoModes->actions());
    videoMode->addSeparator();
    videoMode->addAction(m_actNextVideoMode);
    videoMode->addAction(m_act1to1);

    view->addSeparator();
    view->addAction(m_actFullscreen);
    view->addSeparator();
    QMenu *zoom = view->addMenu(tr("&Zoom"));
    zoom->addActions(m_zoomGroup->actions());
    zoom->addSeparator();
    zoom->addAction(m_actResetLayout);

    QMenu *machine = mb->addMenu(tr("&Machine"));
    machine->addActions(m_machines->actions());
    machine->addSeparator();
    machine->addAction(m_actRestart);

    QMenu *media = mb->addMenu(tr("Me&dia"));
    media->addAction(m_actTape);
    media->addAction(m_actDisk);
    media->addAction(m_actHdd);
    media->addSeparator();
    media->addAction(m_actEject);

    QMenu *audio = mb->addMenu(tr("&Audio"));
    audio->addAction(m_actSound);
    audio->addAction(m_actAy);
    audio->addAction(m_actBeeper);
    audio->addSeparator();
    audio->addAction(m_actAudioSettings);

    QMenu *debug = mb->addMenu(tr("De&bug"));
    debug->addAction(m_actDebugger);

    QMenu *help = mb->addMenu(tr("&Help"));
    help->addAction(m_actAbout);
}

void MainWindow::buildToolBar()
{
    m_toolBar = addToolBar(tr("Transport"));
    m_toolBar->setObjectName(QStringLiteral("transport"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_toolBar->addAction(m_actStart);
    m_toolBar->addAction(m_actPause);
    m_toolBar->addAction(m_actRestart);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actNextVideoMode);
    m_toolBar->addAction(m_actFullscreen);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actRecord);

    connect(m_actToolBar, &QAction::toggled, m_toolBar, &QWidget::setVisible);
    connect(m_actToolBar, &QAction::toggled, this, [this](bool on) {
        flash(on ? tr("Toolbar shown") : tr("Toolbar hidden"));
    });
}

void MainWindow::buildStatusBar()
{
    QStatusBar *sb = statusBar();
    sb->setSizeGripEnabled(true);

    m_indTape  = new StatusIndicator(QStringLiteral(":/icons/tape.svg"),  tr("Tape"),  this);
    m_indDisk  = new StatusIndicator(QStringLiteral(":/icons/disk.svg"),  tr("Disk"),  this);
    m_indHdd   = new StatusIndicator(QStringLiteral(":/icons/hdd.svg"),   tr("HDD"),   this);
    m_indSound = new StatusIndicator(QStringLiteral(":/icons/sound.svg"), tr("Sound"), this);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setFixedHeight(13);

    m_fps = new QLabel(QStringLiteral("50.0 FPS"), this);

    sb->addPermanentWidget(m_indTape);
    sb->addPermanentWidget(m_indDisk);
    sb->addPermanentWidget(m_indHdd);
    sb->addPermanentWidget(m_indSound);
    sb->addPermanentWidget(sep);
    sb->addPermanentWidget(m_fps);

    auto bind = [this](StatusIndicator *ind, QAction *act, const QString &on, const QString &off) {
        ind->setActive(act->isChecked());
        connect(act, &QAction::toggled, ind, &StatusIndicator::setActive);
        connect(ind, &StatusIndicator::toggled, this, [this, act, on, off](bool active) {
            act->setChecked(active);
            flash(active ? on : off);
        });
    };
    bind(m_indTape,  m_actTape,  tr("Tape playing"),  tr("Tape stopped"));
    bind(m_indDisk,  m_actDisk,  tr("Disk activity"), tr("Disk idle"));
    bind(m_indHdd,   m_actHdd,   tr("HDD activity"),  tr("HDD idle"));
    bind(m_indSound, m_actSound, tr("Sound on"),      tr("Sound muted"));

    connect(m_actStatusBar, &QAction::toggled, sb, &QWidget::setVisible);
}

// ============================================================================
// Slots
// ============================================================================

void MainWindow::onStart()
{
    qDebug() << "onStart() called, isRunning:" << m_emulator->isRunning();
    if (!m_emulator->isRunning()) {
        qDebug() << "Starting emulator...";
        if (m_emulator->start()) {
            qDebug() << "Emulator started, framebuffer:"
                     << m_emulator->framebufferWidth() << "x"
                     << m_emulator->framebufferHeight()
                     << "ptr:" << m_emulator->framebuffer();
            // Attach screen to emulator framebuffer (cross-platform interface)
            m_screen->attachFramebuffer(
                m_emulator->framebufferWidth(),
                m_emulator->framebufferHeight(),
                const_cast<void*>(m_emulator->framebuffer())
            );
#ifdef HAS_EMULATOR_CORE
            // Wire audio callback - this drives frame timing via NC_AUDIO_BUFFER_HALF_FULL
            if (auto emu = m_emulator->emulator()) {
                emu->SetAudioCallback(m_soundManager, &AppSoundManager::audioCallback);
                qDebug() << "Audio callback wired to emulator";
            }
#endif
            flash(tr("Emulator started"));
        } else {
            qDebug() << "Failed to start emulator!";
            flash(tr("Failed to start emulator"));
            return;
        }
    } else if (m_emulator->isPaused()) {
        m_emulator->resume();
        flash(tr("Resumed"));
    }
    m_actStart->setChecked(true);
    m_actPause->setChecked(false);
}

void MainWindow::onPause()
{
    if (m_emulator->isRunning() && !m_emulator->isPaused()) {
        m_emulator->pause();
        flash(tr("Paused"));
    }
    m_actPause->setChecked(true);
    m_actStart->setChecked(false);
}

void MainWindow::onRestart()
{
    if (m_emulator->isRunning()) {
        m_emulator->reset();
        flash(tr("Machine reset"));
    } else {
        onStart();
    }
}

void MainWindow::onToggle1to1(bool on)
{
    m_enforce1to1 = on;
    if (on)
        adjustWindowToFitScreen();
    flash(on ? tr("1:1 pixel mode on") : tr("1:1 pixel mode off"));
}

void MainWindow::onToggleRecord(bool on)
{
    flash(on ? tr("Recording RZX…") : tr("Recording stopped"));
}

void MainWindow::onZoomChanged(QAction *a)
{
    int scale = a->data().toInt();
    QSize native = m_screen->nativeSize();

    int extraH = 0;
    if (m_toolBar && m_toolBar->isVisible())
        extraH += m_toolBar->height();
    if (statusBar() && statusBar()->isVisible())
        extraH += statusBar()->height();
    if (menuBar() && menuBar()->isVisible())
        extraH += menuBar()->height();

    resize(native.width() * scale, native.height() * scale + extraH);
    flash(tr("Zoom: %1x").arg(scale));
}

void MainWindow::updateZoomCheck()
{
    if (!m_screen || !m_zoomGroup)
        return;

    QSize native = m_screen->nativeSize();
    if (native.width() <= 0 || native.height() <= 0)
        return;

    // Calculate current scale based on content frame size
    int contentW = m_contentFrame->width();
    int contentH = m_contentFrame->height();

    int scaleW = contentW / native.width();
    int scaleH = contentH / native.height();

    // Check if it's an exact integer scale (1-4)
    int matchedScale = 0;
    if (scaleW == scaleH && scaleW >= 1 && scaleW <= 4) {
        if (contentW == native.width() * scaleW && contentH == native.height() * scaleH) {
            matchedScale = scaleW;
        }
    }

    // Update checked state
    for (QAction* a : m_zoomGroup->actions()) {
        a->setChecked(a->data().toInt() == matchedScale);
    }
}

void MainWindow::onResetLayout()
{
    QSettings s;
    s.remove(QStringLiteral("geometry"));
    s.remove(QStringLiteral("windowState"));

    // Reset toolbar/statusbar visibility
    m_actToolBar->setChecked(true);
    m_toolBar->setVisible(true);
    m_actStatusBar->setChecked(true);
    statusBar()->setVisible(true);

    // Apply default 2x size and update zoom group
    for (QAction* a : m_zoomGroup->actions()) {
        a->setChecked(a->data().toInt() == 2);
    }
    onZoomChanged(m_zoomGroup->actions().at(1));  // 2x is second item

    flash(tr("Layout reset to default"));
}

void MainWindow::onMachineChanged(QAction *a)
{
    m_machine = a->text();
    refreshTitle();
    flash(tr("Machine: %1").arg(m_machine));
}

void MainWindow::onVideoModeChanged(QAction *a)
{
    int modeIndex = a->data().toInt();
    setVideoMode(modeIndex);

    const auto* modes = ScreenWidget::videoModes();
    flash(tr("Video: %1").arg(QString::fromLatin1(modes[modeIndex].name)));
}

void MainWindow::onNextVideoMode()
{
    int count = ScreenWidget::videoModeCount();
    int nextMode = (m_currentModeIndex + 1) % count;
    setVideoMode(nextMode);

    const auto* modes = ScreenWidget::videoModes();
    flash(tr("Video: %1").arg(QString::fromLatin1(modes[nextMode].name)));
}

void MainWindow::onEjectAll()
{
    m_actTape->setChecked(false);
    m_actDisk->setChecked(false);
    m_actHdd->setChecked(false);
    flash(tr("All media ejected"));
}

void MainWindow::flash(const QString &message)
{
    statusBar()->showMessage(message);
}

void MainWindow::refreshTitle()
{
    setWindowTitle(QStringLiteral("%1 — %2").arg(m_media, m_machine));
}

void MainWindow::restoreLayout()
{
    QSettings s;
    QByteArray savedGeom = s.value(QStringLiteral("geometry")).toByteArray();

    if (savedGeom.isEmpty()) {
        // Default to 2x ZX Spectrum resolution (352x288 -> 704x576) plus UI chrome
        int extraH = 0;
        if (m_toolBar && m_toolBar->isVisible())
            extraH += m_toolBar->sizeHint().height();
        if (statusBar() && statusBar()->isVisible())
            extraH += statusBar()->sizeHint().height();
        if (menuBar() && menuBar()->isVisible())
            extraH += menuBar()->sizeHint().height();
        resize(704, 576 + extraH);
    } else {
        restoreGeometry(savedGeom);
    }
    restoreState(s.value(QStringLiteral("windowState")).toByteArray());

    const bool tb = s.value(QStringLiteral("view/toolbar"), true).toBool();
    const bool st = s.value(QStringLiteral("view/statusbar"), true).toBool();
    m_actToolBar->setChecked(tb);
    m_toolBar->setVisible(tb);
    m_actStatusBar->setChecked(st);
    statusBar()->setVisible(st);

    adjustWindowToFitScreen();
}

void MainWindow::saveLayout()
{
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("windowState"), saveState());
    s.setValue(QStringLiteral("view/toolbar"), m_actToolBar->isChecked());
    s.setValue(QStringLiteral("view/statusbar"), m_actStatusBar->isChecked());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    setAcceptDrops(false);
    saveLayout();
    QMainWindow::closeEvent(e);
}

void MainWindow::loadFile(const QString &filePath)
{
    SupportedFileCategoriesEnum category = FileManager::determineFileCategoryByExtension(filePath);
    std::string file = filePath.toStdString();

    // Auto-start emulator if not running
    if (!m_emulator->isRunning() && category != FileSymbol && category != FileUnknown)
    {
        qDebug() << "Auto-starting emulator for file:" << filePath;
        onStart();
    }

#ifdef HAS_EMULATOR_CORE
    auto emu = m_emulator->emulator();
    if (!emu)
        return;

    switch (category)
    {
        case FileSnapshot:
            emu->LoadSnapshot(file);
            break;
        case FileTape:
            emu->LoadTape(file);
            break;
        case FileDisk:
            emu->LoadDisk(file);
            break;
        case FileSymbol:
            // Symbol loading requires debugger - not implemented in POC
            qDebug() << "Symbol file loading not implemented:" << filePath;
            break;
        default:
            qDebug() << "Unsupported file type:" << filePath;
            break;
    }
#else
    Q_UNUSED(file);
    Q_UNUSED(category);
#endif

    m_media = QFileInfo(filePath).fileName();
    refreshTitle();
    flash(tr("Loaded %1").arg(m_media));
}
