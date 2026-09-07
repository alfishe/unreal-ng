//
// main_window.cpp — QMainWindow implementation.
//

#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QDockWidget>
#include <QFrame>

#include "../ttd/screen_renderer.h"

namespace ttd {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("TTD GUI PoC — Qt Scrubber & Screen Renderer"));
    resize(1024, 720);

    setupUi();
    setupMenuBar();
    setupToolBar();
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* hLayout = new QHBoxLayout(central);

    _screenWidget = new ScreenWidget(this);
    hLayout->addWidget(_screenWidget, 1);

    // Right-side panel
    auto* rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(4);

    _sessionLabel = new QLabel(this);
    _sessionLabel->setWordWrap(true);
    _sessionLabel->setFrameStyle(QFrame::StyledPanel);
    rightPanel->addWidget(_sessionLabel);

    _posLabel = new QLabel(this);
    _posLabel->setWordWrap(true);
    _posLabel->setFrameStyle(QFrame::StyledPanel);
    rightPanel->addWidget(_posLabel);

    // Marks list
    auto* marksGroup = new QGroupBox(QStringLiteral("User Marks"), this);
    auto* marksLayout = new QVBoxLayout(marksGroup);
    _marksList = new QListView(this);
    _marksList->setModel(&_marksModel);
    marksLayout->addWidget(_marksList);
    rightPanel->addWidget(marksGroup);

    rightPanel->addStretch();

    hLayout->addLayout(rightPanel, 0);

    setCentralWidget(central);

    // Bottom: timeline
    _timeline = new TimelineWidget(this);
    _timeline->setMarksModel(&_marksModel);

    auto* dockContainer = new QWidget(this);
    auto* dockLayout = new QVBoxLayout(dockContainer);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->addWidget(_timeline);

    auto* dock = new QDockWidget(QStringLiteral("Timeline"), this);
    dock->setWidget(dockContainer);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget());
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    // Status bar
    _statusLabel = new QLabel(this);
    statusBar()->addWidget(_statusLabel);

    // Connections
    connect(_timeline, &TimelineWidget::frameChanged, this, &MainWindow::onFrameChanged);
    connect(_timeline, &TimelineWidget::userMarkRequested, this, &MainWindow::onUserMarkRequested);
}

void MainWindow::setupMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("Open .ttd..."), QKeySequence::Open,
                        this, &MainWindow::onOpenFile);
    fileMenu->addAction(QStringLiteral("Load marks JSON..."), QKeySequence(Qt::CTRL | Qt::Key_M),
                        this, &MainWindow::onOpenMarks);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Quit"), QKeySequence::Quit,
                        qApp, &QApplication::quit);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Zoom In"), QKeySequence::ZoomIn,
                        this, &MainWindow::onZoomIn);
    viewMenu->addAction(QStringLiteral("Zoom Out"), QKeySequence::ZoomOut,
                        this, &MainWindow::onZoomOut);
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Toggle Mark Lanes"), this, &MainWindow::onToggleMarks);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("About..."), [this]() {
        QMessageBox::information(this, QStringLiteral("About"),
            QStringLiteral("TTD GUI PoC\n\n"
                           "Offline .ttd file scrubber with ZX Spectrum\n"
                           "screen rendering.\n\n"
                           "Shortcuts:\n"
                           "  Left/Right — step frame\n"
                           "  Shift+Left/Right — jump 10 frames\n"
                           "  Home/End — session start/end\n"
                           "  Space — play/pause\n"
                           "  Mouse wheel — zoom\n"
                           "  Double-click — add mark"));
    });
}

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(16, 16));

    tb->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                  QStringLiteral("Open"), this, &MainWindow::onOpenFile);

    tb->addSeparator();

    tb->addAction(style()->standardIcon(QStyle::SP_MediaSkipBackward),
                  QStringLiteral("Session Start"), [this]() {
        _timeline->setFrame(_timeline->sessionStart());
        onFrameChanged(_timeline->currentFrame());
    });

    tb->addAction(style()->standardIcon(QStyle::SP_ArrowLeft),
                  QStringLiteral("Step Back"), [this]() {
        if (_timeline->currentFrame() > _timeline->sessionStart()) {
            _timeline->setFrame(_timeline->currentFrame() - 1);
            onFrameChanged(_timeline->currentFrame());
        }
    });

    tb->addAction(style()->standardIcon(QStyle::SP_ArrowRight),
                  QStringLiteral("Step Forward"), [this]() {
        if (_timeline->currentFrame() < _timeline->sessionEnd()) {
            _timeline->setFrame(_timeline->currentFrame() + 1);
            onFrameChanged(_timeline->currentFrame());
        }
    });

    tb->addAction(style()->standardIcon(QStyle::SP_MediaSkipForward),
                  QStringLiteral("Session End"), [this]() {
        _timeline->setFrame(_timeline->sessionEnd());
        onFrameChanged(_timeline->currentFrame());
    });

    tb->addSeparator();

    tb->addAction(style()->standardIcon(QStyle::SP_MediaPlay),
                  QStringLiteral("Play/Pause"), [this]() {
        QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(_timeline, &spacePress);
    });

    tb->addSeparator();

    tb->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder),
                  QStringLiteral("Add Mark"), [this]() {
        onUserMarkRequested(_timeline->currentFrame());
    });
}

// ---------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------

bool MainWindow::loadTtd(const QString& path) {
    if (!_dump.load(path)) {
        QMessageBox::critical(this, QStringLiteral("Error"),
            QStringLiteral("Failed to load:\n%1\n\n%2").arg(path, _dump.error()));
        return false;
    }

    _materializer.clearCache();
    const auto& h = _dump.header();

    _timeline->setSessionRange(h.session_start_frame, h.session_end_frame);

    _currentCpIndex = -1;
    if (!_dump.checkpoints().empty()) {
        _currentCpIndex = 0;
        _timeline->setFrame(_dump.checkpoints()[0].frame);
        renderCurrentCheckpoint();
    }

    updateInfoLabels();
    _statusLabel->setText(QStringLiteral("Loaded %1 (%2 checkpoints, %3 slots)")
        .arg(path)
        .arg(static_cast<qulonglong>(_dump.checkpoints().size()))
        .arg(static_cast<qulonglong>(_dump.pageSlots().size())));

    return true;
}

bool MainWindow::loadMarks(const QString& path) {
    if (!_marksModel.loadSidecar(path)) {
        _statusLabel->setText(QStringLiteral("No marks loaded from %1").arg(path));
        return false;
    }
    _timeline->update();
    _statusLabel->setText(QStringLiteral("Loaded %1 marks, %2 sections")
        .arg(static_cast<int>(_marksModel.marks().size()))
        .arg(static_cast<int>(_marksModel.sections().size())));
    return true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void MainWindow::renderCurrentCheckpoint() {
    if (_currentCpIndex < 0 || _currentCpIndex >= static_cast<int>(_dump.checkpoints().size()))
        return;

    const auto& cp = _dump.checkpoints()[_currentCpIndex];
    try {
        auto ram = _materializer.materialize(_dump, cp);
        QImage img = RenderScreen(cp, ram);
        _screenWidget->setImage(img);
    } catch (const std::exception& e) {
        _statusLabel->setText(QStringLiteral("Render error: %1").arg(e.what()));
    }
}

void MainWindow::updateInfoLabels() {
    const auto& h = _dump.header();

    _sessionLabel->setText(QStringLiteral(
        "<b>Session Info</b><br>"
        "Schema: v%1<br>"
        "Model: %2 (RAM: %3 pages)<br>"
        "Frames: %4 – %5<br>"
        "Checkpoints: %6<br>"
        "Page store slots: %7<br>"
        "Emulator: %8"
    ).arg(h.schema_version)
     .arg(h.model_id)
     .arg(h.model_ram_pages)
     .arg(static_cast<qulonglong>(h.session_start_frame))
     .arg(static_cast<qulonglong>(h.session_end_frame))
     .arg(static_cast<qulonglong>(h.checkpoint_count))
     .arg(static_cast<qulonglong>(h.page_store_count))
     .arg(h.emulator_id));

    if (_currentCpIndex >= 0 && _currentCpIndex < static_cast<int>(_dump.checkpoints().size())) {
        const auto& cp = _dump.checkpoints()[_currentCpIndex];
        int bank = SelectedScreenBank(cp.chipset.p7ffd);
        _posLabel->setText(QStringLiteral(
            "<b>Current Position</b><br>"
            "Frame: %1<br>"
            "Checkpoint: #%2 (kind: %3)<br>"
            "PC: %4  SP: %5<br>"
            "Screen bank: %6  (p7ffd=0x%7)<br>"
            "Border attr: 0x%8<br>"
            "t-states: %9"
        ).arg(static_cast<qulonglong>(cp.frame))
         .arg(_currentCpIndex)
         .arg(cp.isKeyframe() ? QStringLiteral("KeyFrame") : QStringLiteral("DeltaFrame"))
         .arg(cp.cpu.pc, 4, 16, QChar('0'))
         .arg(cp.cpu.sp, 4, 16, QChar('0'))
         .arg(bank)
         .arg(cp.chipset.p7ffd, 2, 16, QChar('0'))
         .arg(cp.chipset.border_attr, 2, 16, QChar('0'))
         .arg(static_cast<qulonglong>(cp.chipset.t_states)));
    } else {
        _posLabel->setText(QStringLiteral("<b>Current Position</b><br>No checkpoint loaded"));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenFile() {
    QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Open .ttd File"), QString(),
        QStringLiteral("TTD Dump Files (*.ttd);;All Files (*)"));
    if (!path.isEmpty())
        loadTtd(path);
}

void MainWindow::onOpenMarks() {
    QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("Load Marks JSON"), QString(),
        QStringLiteral("Marks JSON (*.json);;All Files (*)"));
    if (!path.isEmpty())
        loadMarks(path);
}

void MainWindow::onFrameChanged(uint64_t frame) {
    const Checkpoint* cp = _dump.checkpointAtFrame(frame);
    if (!cp) return;

    int newIdx = static_cast<int>(cp - _dump.checkpoints().data());
    if (newIdx != _currentCpIndex) {
        _currentCpIndex = newIdx;
        renderCurrentCheckpoint();
    }
    updateInfoLabels();
}

void MainWindow::onUserMarkRequested(uint64_t frame) {
    bool ok;
    QString label = QInputDialog::getText(this,
        QStringLiteral("Add Mark"),
        QStringLiteral("Label for frame %1:").arg(static_cast<qulonglong>(frame)),
        QLineEdit::Normal, QStringLiteral("Mark %1").arg(_marksModel.rowCount() + 1), &ok);
    if (ok && !label.isEmpty()) {
        _marksModel.addMark(frame, label);
        _timeline->update();
    }
}

void MainWindow::onZoomIn() {
    _timeline->zoomIn();
}

void MainWindow::onZoomOut() {
    _timeline->zoomOut();
}

void MainWindow::onToggleMarks() {
    _marksVisible = !_marksVisible;
    _marksList->setVisible(_marksVisible);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Space:
            QCoreApplication::sendEvent(_timeline, event);
            break;
        default:
            QMainWindow::keyPressEvent(event);
    }
}

} // namespace ttd
