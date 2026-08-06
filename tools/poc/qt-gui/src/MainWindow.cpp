#include "MainWindow.h"
#include "ScreenWidget.h"
#include "StatusIndicator.h"

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
#include <QFrame>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QDateTime>

static QIcon themedIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
#ifdef Q_OS_MACOS
    m_screen = new MetalScreenWidget(this);
#else
    m_screen = new ScreenWidget(this);
#endif
    setCentralWidget(m_screen);

    m_normalPalette = palette();

    buildActions();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    setVideoMode(m_currentModeIndex);

    refreshTitle();
    restoreLayout();

    flash(tr("Running — %1").arg(m_machine));
}

MainWindow::~MainWindow()
{
#ifdef Q_OS_MACOS
    FullscreenHelper::uninstall(windowHandle());
#endif
}

// ============================================================================
// Fullscreen Style Helpers
// ============================================================================

void MainWindow::applyFullscreenStyle()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] applyFullscreenStyle START";

    // Disable updates to prevent visible relayout during transition
    setUpdatesEnabled(false);

    QPalette p;
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);

    menuBar()->hide();
    statusBar()->hide();
    m_toolBar->hide();

    setUpdatesEnabled(true);

    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] applyFullscreenStyle END";
}

void MainWindow::restoreNormalStyle()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] restoreNormalStyle START";

    setPalette(m_normalPalette);

    if (m_actToolBar->isChecked())
        m_toolBar->show();
    if (m_actStatusBar->isChecked())
        statusBar()->show();
    menuBar()->show();

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
}

void MainWindow::didEnterFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] didEnterFullscreen";
    // Keep state as EnteringFullscreen briefly to block spurious resize
    // The resize event filter checks this state
    QTimer::singleShot(200, this, [this]() {
        m_fullscreenState = FullscreenState::Fullscreen;
        m_screen->setFocus();
    });
}

void MainWindow::willExitFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] willExitFullscreen";
}

void MainWindow::didExitFullscreen()
{
    qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [MW] didExitFullscreen";
    // Now safe to show Qt UI - Space transition complete
    restoreNormalStyle();
    m_fullscreenState = FullscreenState::Normal;
    m_screen->setFocus();
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
    m_actStart->setChecked(true);
    m_actPause->setChecked(false);
    flash(tr("Running"));
}

void MainWindow::onPause()
{
    m_actPause->setChecked(true);
    m_actStart->setChecked(false);
    flash(tr("Paused"));
}

void MainWindow::onRestart()
{
    onStart();
    flash(tr("Machine restarted"));
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
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
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
    saveLayout();
    QMainWindow::closeEvent(e);
}
