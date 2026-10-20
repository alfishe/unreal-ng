#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <span>

#include "deckclient.h"
#include "deckwidget.h"
#include "editor/designcanvas.h"
#include "editor/editorapi.h"
#include "editor/editordocument.h"
#include "editor/editorwidget.h"
#include "editor/runcontroller.h"
#include "listenserver.h"
#include "recorder.h"
#include "studioapi.h"
#include "textedit.h"
#include "virtualdeck.h"

using namespace sdb;

namespace
{
constexpr int kMirrorItem = -1;  // the role combo's data for "mirror the real deck"
const char* const kDocsUrl = "https://github.com/alfishe/unreal-ng/blob/master/docs/companions/streamdeck/studio.md";
QString NameOf(const Model& m)
{
    return QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size()));
}
}  // namespace

MainWindow::MainWindow(const StudioOptions& options) : _options(options)
{
    setWindowTitle(QStringLiteral("Stream Deck Studio"));
    _deck = new VirtualDeck(this);
    _view = new DeckWidget(_deck, this);
    setCentralWidget(_view);

    auto* bar = addToolBar(QStringLiteral("Deck"));
    bar->setMovable(false);
    bar->addWidget(new QLabel(QStringLiteral(" Deck ")));
    _roles = new QComboBox(this);
    if (!_options.listen)
    {
        _roles->addItem(QStringLiteral("Real deck (mirror)"), kMirrorItem);
        _roles->setItemData(0, QStringLiteral("Show the real deck the daemon drives; clicks here reach its runtime"),
                            Qt::ToolTipRole);
    }
    for (const auto& m : AllModels())
        _roles->addItem(QStringLiteral("%1%2  (0x%3)")
                            .arg(_options.listen ? QString() : QStringLiteral("Own: "), NameOf(m))
                            .arg(m.pid, 4, 16, QLatin1Char('0')),
                        static_cast<int>(m.pid));
    _roles->setToolTip(_options.listen ? QStringLiteral("The model this deck is (a host may switch it: SetModel)")
                                       : QStringLiteral("Mirror the real deck, or a deck of its own: the daemon runs "
                                                        "a scenario for it"));
    bar->addWidget(_roles);
    bar->addSeparator();
    auto* mask = _mask = new QCheckBox(QStringLiteral("Bezel mask"), this);
    mask->setChecked(true);
    mask->setToolTip(QStringLiteral("Off: show the whole LCD including what lies behind the bezel"));
    bar->addWidget(mask);
    auto* safe = _safe = new QCheckBox(QStringLiteral("Visible area"), this);
    safe->setToolTip(QStringLiteral("Outline the part of each key that is visible head-on (measured on the + XL)"));
    bar->addWidget(safe);
    auto* quirks = _quirks = new QCheckBox(QStringLiteral("Firmware quirks"), this);
    quirks->setChecked(true);
    quirks->setToolTip(
        QStringLiteral("Reproduce measured firmware behaviour, e.g. the + XL ignores partial window "
                       "writes that are not full height"));
    bar->addWidget(quirks);
    bar->addSeparator();
    _recorder = new Recorder(
        [this] {  // 2x whatever screen the window is on: sharp text in the video
            QImage img(_view->size() * 2, QImage::Format_RGBA8888);
            img.setDevicePixelRatio(2);
            img.fill(Qt::black);
            _view->render(&img);
            return img;
        },
        this);
    _rec = new QToolButton(this);
    _rec->setText(QStringLiteral("● Rec"));
    _rec->setCheckable(true);
    _rec->setToolTip(QStringLiteral("Record the deck to the Movies folder (Record menu: pick the file and format)"));
    bar->addWidget(_rec);
    bar->addSeparator();
    _editorButton = new QToolButton(this);
    _editorButton->setText(QStringLiteral("Scenario editor"));
    _editorButton->setCheckable(true);
    _editorButton->setToolTip(
        QStringLiteral("Menus and actions for the deck; its own window or docked here (drag its title bar)"));
    bar->addWidget(_editorButton);

    auto* dock = new QDockWidget(QStringLiteral("Protocol"), this);
    dock->setObjectName(QStringLiteral("protocol"));
    _log = new QPlainTextEdit(dock);
    MakeSelectableLog(_log);
    _log->setMaximumBlockCount(800);
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    _log->setFont(mono);
    auto* panel = new QWidget(dock);
    auto* filters = new QHBoxLayout;
    filters->setContentsMargins(4, 2, 4, 2);
    filters->setSpacing(4);
    const struct
    {
        Src s;
        const char* label;
        const char* tip;
    } kinds[] = {
        {Src::Daemon, "◀ DAEMON", "What the daemon sends this deck: images, features"},
        {Src::Viewer, "▶ VIEWER", "Input from this window (mouse)"},
        {Src::Deck, "▶ DECK", "Input on the real deck, relayed by the daemon"},
        {Src::App, "● APP", "What the runtime did with the input: actions, navigation"},
        {Src::Info, "· info", "The studio: connection, model, snapshots, recording"},
    };
    for (const auto& k : kinds)
    {
        auto* b = new QToolButton(panel);
        b->setText(QString::fromUtf8(k.label));
        b->setToolTip(QString::fromUtf8(k.tip));
        b->setCheckable(true);
        b->setChecked(true);
        b->setAutoRaise(true);
        connect(b, &QToolButton::toggled, this, [this, s = k.s](bool on) { _show[static_cast<int>(s)] = on; });
        _filterButtons[static_cast<int>(k.s)] = b;
        filters->addWidget(b);
    }
    filters->addStretch(1);
    auto* clear = new QToolButton(panel);
    clear->setText(QStringLiteral("Clear"));
    clear->setAutoRaise(true);
    connect(clear, &QToolButton::clicked, _log, &QPlainTextEdit::clear);
    filters->addWidget(clear);
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addLayout(filters);
    col->addWidget(_log);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    _protocolDock = dock;

    _status = new QLabel(this);
    statusBar()->addWidget(_status, 1);

    connect(mask, &QCheckBox::toggled, this, [this](bool on) {
        _view->bezelMask = on;
        _view->update();
    });
    connect(safe, &QCheckBox::toggled, this, [this](bool on) {
        _view->showSafeArea = on;
        _view->update();
    });
    connect(quirks, &QCheckBox::toggled, this, [this](bool on) { _deck->quirks = on; });
    connect(_deck, &VirtualDeck::Log, this, [this](const QString& line) {
        AddLog(line.startsWith(QLatin1String("model:")) ? Src::Info : Src::Daemon, line);
    });
    connect(_deck, &VirtualDeck::InputReport, this, [this](const QByteArray& r) { LogInput(Src::Viewer, r); });
    connect(_deck, &VirtualDeck::ModelChanged, this, [this] {
        _viewerParser.emplace(_deck->GetModel());
        _deckParser.emplace(_deck->GetModel());
        SyncRoleCombo();
        RefreshStatus();
    });

    // ---- what drives the deck: the daemon (default) or one v1 host (--listen)
    if (_options.listen)
    {
        _server = new ListenServer(_deck, this);
        _link = _server;
        connect(_server, &ListenServer::ModelRequested, this, [this](quint16 pid) {
            const int i = _roles->findData(static_cast<int>(pid));
            if (i >= 0)
                _roles->setCurrentIndex(i);
            else
                AddLog(Src::Info, QStringLiteral("host asked for unknown PID 0x%1").arg(pid, 4, 16, QLatin1Char('0')));
        });
        connect(_server, &ListenServer::RecordRequested, this, [this](const QString& path, int fps) {
            if (path.isEmpty())
            {
                if (!_recorder->Active())
                    _server->RecordDone(false, QStringLiteral("not recording"));
                else
                    _server->RecordDone(true, StopRecording());
                return;
            }
            QString err;
            const bool ok = StartRecording(path, fps, &err);
            _server->RecordDone(ok, ok ? QStringLiteral("recording %1").arg(path) : err);
        });
    }
    else
    {
        _client = new DeckClient(_deck, this);
        _client->SetAddress(_options.daemonHost, _options.daemonPort);
        _link = _client;
    }
    connect(_link, &DeckLink::HostInput, this, [this](const QByteArray& r) { LogInput(Src::Deck, r); });
    connect(_link, &DeckLink::HostNote, this, [this](const QString& line) { AddLog(Src::App, line); });
    connect(_link, &DeckLink::HostPage, this, [this](const QByteArray& j) {
        _page = j;
        _pageTime = QDateTime::currentDateTime();
    });
    connect(_link, &DeckLink::Info, this, [this](const QString& line) { AddLog(Src::Info, line); });
    connect(_link, &DeckLink::SnapshotRequested, this, &MainWindow::OnSnapshot);
    connect(_link, &DeckLink::StateChanged, this, [this] {
        if (!_link->Live())
            _page.clear();
        SyncRoleCombo();
        RefreshStatus();
    });

    connect(_rec, &QToolButton::clicked, this, [this](bool on) {
        if (!on)
        {
            StopRecording();
            return;
        }
        QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        const QString name = NameOf(_deck->GetModel()).replace(QLatin1Char(' '), QLatin1Char('-'));
        StartRecording(QDir(dir).filePath(
                           QStringLiteral("streamdeck-%1-%2.mp4")
                               .arg(name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")))),
                       30);
    });
    connect(_recorder, &Recorder::StateChanged, this, [this] {
        _rec->setChecked(_recorder->Active());
        _rec->setStyleSheet(_recorder->Active() ? QStringLiteral("color: #e02020; font-weight: bold") : QString());
        RefreshStatus();
    });
    _tick = new QTimer(this);
    connect(_tick, &QTimer::timeout, this, [this] {
        RefreshStatus();
        if (_deck->Asleep())
            _view->update();
    });
    _tick->start(1000);

    resize(1280, 820);
    _editor = new EditorWidget(this);
    _editorDock = new QDockWidget(QStringLiteral("Scenario editor"), this);
    _editorDock->setObjectName(QStringLiteral("editor"));
    _editorDock->setWidget(_editor);
    addDockWidget(Qt::LeftDockWidgetArea, _editorDock);
    // Floating, a dock is a tool window that stays above the studio; as its own window it takes part in the ordinary
    // window order (the studio can come in front of it) and gets the normal title bar and buttons.
    connect(_editorDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (!floating)
            return;
        const QRect g = _editorDock->geometry();
        _editorDock->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                    Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        _editorDock->setGeometry(g);
        _editorDock->show();
    });
    SetEditorMode(QStringLiteral("hidden"));
    connect(_editorButton, &QToolButton::toggled, this,
            [this](bool on) { SetEditorMode(on ? QStringLiteral("window") : QStringLiteral("hidden")); });
    connect(_editorDock, &QDockWidget::visibilityChanged, this, [this](bool v) {
        QSignalBlocker b(_editorButton);
        _editorButton->setChecked(v);
    });
    BuildMenus();

    // ---- start: the initial model, then the link
    if (const Model* m = FindModelByName(_options.model.toStdString()); m && (_options.listen || _options.own))
        _deck->SetModel(*m);
    SyncRoleCombo();
    connect(_roles, &QComboBox::currentIndexChanged, this, [this](int i) {
        const int data = _roles->itemData(i).toInt();
        if (data == kMirrorItem)
        {
            QString err;
            if (!SetRole(false, _options.deck, &err))
                AddLog(Src::Info, err);
        }
        else
            SelectModel(static_cast<quint16>(data));
    });
    if (_server)
    {
        QString err;
        if (!_server->Listen(_options.listenPort, _options.anyInterface, &err))
            AddLog(Src::Info, QStringLiteral("cannot listen on port %1: %2").arg(_options.listenPort).arg(err));
    }
    else
    {
        DeckClient::Role role;
        role.own = _options.own;
        role.deck = _options.deck;
        role.model = DeckClient::ModelToken(_deck->GetModel());
        _client->Start(role);
    }
    if (_options.apiPort > 0)
    {
        _api = new StudioApi(this);
        AddEditorRoutes(_api->GetApi(), _editor, [this]() -> QWidget* { return _editorDock; });
        QString err;
        if (_api->Start(_options.apiPort, &err))
            AddLog(Src::Info,
                   QStringLiteral("automation: http://127.0.0.1:%1/api/v1 (WebAPI), /mcp (MCP)").arg(_options.apiPort));
        else
            AddLog(Src::Info, QStringLiteral("automation FAILED: %1").arg(err));
    }
    RefreshStatus();
}

MainWindow::~MainWindow()
{
    delete _api;  // stop the HTTP threads before the widgets they use go away
}

bool MainWindow::SetRole(bool own, const QString& deckOrModel, QString* error)
{
    if (!_client)
    {
        if (error)
            *error = QStringLiteral("--listen: the studio is a server for one host, it has no role");
        return false;
    }
    DeckClient::Role role;
    role.own = own;
    if (own)
    {
        const Model* m = FindModelByName(deckOrModel.toStdString());
        if (!m)
        {
            if (error)
                *error = QStringLiteral("unknown model '%1'").arg(deckOrModel);
            return false;
        }
        role.model = DeckClient::ModelToken(*m);
    }
    else
        role.deck = deckOrModel.isEmpty() ? QStringLiteral("first") : deckOrModel;
    if (!(role == _client->GetRole()) || _client->GetState() == DeckClient::State::Failed ||
        _client->GetState() == DeckClient::State::Stopped)
        _client->Start(role);
    SyncRoleCombo();
    return true;
}

void MainWindow::SelectModel(quint16 pid)
{
    const Model* m = FindModel(kElgatoVid, pid);
    if (!m)
        return;
    if (_client)
    {
        QString err;
        if (!SetRole(true, DeckClient::ModelToken(*m), &err))
            AddLog(Src::Info, err);
        return;
    }
    _deck->SetModel(*m);
    _view->update();
}

void MainWindow::SyncRoleCombo()
{
    const int data = _client && !_client->GetRole().own ? kMirrorItem : static_cast<int>(_deck->GetModel().pid);
    const QSignalBlocker b(_roles);
    _roles->setCurrentIndex(std::max(0, _roles->findData(data)));
}

bool MainWindow::StartRecording(const QString& path, int fps, QString* error)
{
    QString err;
    if (!_recorder->Start(path, fps, &err))
    {
        AddLog(Src::Info, QStringLiteral("recording FAILED: %1").arg(err));
        if (error)
            *error = err;
        _rec->setChecked(false);
        return false;
    }
    AddLog(Src::Info, QStringLiteral("recording %1").arg(path));
    return true;
}

QString MainWindow::StopRecording()
{
    const QString summary = _recorder->Stop();
    if (!summary.isEmpty())
        AddLog(Src::Info, QStringLiteral("recorded %1").arg(summary));
    return summary;
}

void MainWindow::OnSnapshot(const QString& path)
{
    const bool ok = Screenshot(QStringLiteral("deck")).save(path, "PNG");
    AddLog(Src::Info,
           ok ? QStringLiteral("snapshot saved: %1").arg(path) : QStringLiteral("snapshot FAILED: %1").arg(path));
    if (_server)
        _server->SnapshotDone(ok);
}

QImage MainWindow::Screenshot(const QString& area)
{
    if (area == QLatin1String("lcd"))
        return _deck->Lcd().copy();
    if (area == QLatin1String("window"))
        return grab().toImage();
    if (area == QLatin1String("canvas"))
        return _editor->Canvas()->RenderImage(2);
    if (area == QLatin1String("editor"))
        return _editorDock->grab().toImage();
    QImage d(_view->size() * 2, QImage::Format_ARGB32);  // the device, sharp
    d.setDevicePixelRatio(2);
    d.fill(Qt::black);
    _view->render(&d);
    d.setDevicePixelRatio(1);
    return d;
}

void MainWindow::BuildMenus()
{
    QMenuBar* mb = menuBar();
    auto editorUp = [this] {
        if (!_editorDock->isVisible())
            SetEditorMode(QStringLiteral("window"));
    };

    // ---- File: scenarios and pictures
    QMenu* file = mb->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("New Scenario..."), QKeySequence::New, this, [this, editorUp] {
        editorUp();
        _editor->CmdNew();
    });
    file->addAction(QStringLiteral("Open Scenario..."), QKeySequence::Open, this, [this, editorUp] {
        editorUp();
        _editor->CmdOpen();
    });
    file->addAction(QStringLiteral("Save Scenario"), QKeySequence::Save, this, [this] { _editor->CmdSave(); });
    file->addAction(QStringLiteral("Save Scenario As..."), QKeySequence::SaveAs, this,
                    [this] { _editor->CmdSaveAs(); });
    file->addAction(QStringLiteral("Export a Copy (JSON / YAML / CSV / TSV)..."), this,
                    [this] { _editor->CmdExportCopy(); });
    file->addSeparator();
    QMenu* imp = file->addMenu(QStringLiteral("Import into the Shown Menu"));
    imp->addAction(QStringLiteral("As a Submenu (copy)..."), this, [this, editorUp] {
        editorUp();
        _editor->CmdImport(QStringLiteral("menu"));
    });
    imp->addAction(QStringLiteral("Its Keys Here (copy)..."), this, [this, editorUp] {
        editorUp();
        _editor->CmdImport(QStringLiteral("items"));
    });
    imp->addAction(QStringLiteral("As a Link (include)..."), this, [this, editorUp] {
        editorUp();
        _editor->CmdImport(QStringLiteral("link"));
    });
    file->addAction(QStringLiteral("Export Selected Subtree..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                    [this] { _editor->CmdExportSelected(false); });
    file->addAction(QStringLiteral("Move Selected to File (link)..."), this,
                    [this] { _editor->CmdExportSelected(true); });
    file->addSeparator();
    QMenu* shots = file->addMenu(QStringLiteral("Save Screenshot"));
    QMenu* copies = file->addMenu(QStringLiteral("Copy Screenshot"));
    const std::pair<const char*, const char*> areas[] = {{"lcd", "Display Only"},
                                                         {"deck", "Device"},
                                                         {"window", "Studio Window"},
                                                         {"canvas", "Editor Canvas"},
                                                         {"editor", "Editor Window"}};
    for (const auto& [area, label] : areas)
    {
        const QString a = QString::fromLatin1(area);
        shots->addAction(QString::fromLatin1(label) + QStringLiteral("..."), this, [this, a] {
            const QString f = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save screenshot"), QDir::homePath() + QStringLiteral("/streamdeck-%1.png").arg(a),
                QStringLiteral("PNG (*.png)"));
            if (!f.isEmpty() && !Screenshot(a).save(f, "PNG"))
                AddLog(Src::Info, QStringLiteral("screenshot FAILED: ") + f);
        });
        copies->addAction(QString::fromLatin1(label), this, [this, a] {
            QGuiApplication::clipboard()->setImage(Screenshot(a));
            AddLog(Src::Info, QStringLiteral("screenshot (%1) copied").arg(a));
        });
    }
    file->addSeparator();
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows)
        ->setMenuRole(QAction::QuitRole);

    // ---- Edit: the scenario
    QMenu* edit = mb->addMenu(QStringLiteral("&Edit"));
    edit->addAction(_editor->UndoAction());
    edit->addAction(_editor->RedoAction());
    edit->addSeparator();
    // a focused text field (log, inspector, JSON) takes the edit commands; otherwise they act on the selected node
    edit->addAction(QStringLiteral("Cut"), QKeySequence::Cut, this, [this] {
        if (!TextCommand(QKeySequence::Cut))
            _editor->CmdCut();
    });
    edit->addAction(QStringLiteral("Copy"), this, [this] {
        if (!TextCommand(QKeySequence::Copy))
            _editor->CopySelected();
    });
    edit->addAction(QStringLiteral("Paste"), this, [this] {
        if (TextCommand(QKeySequence::Paste))
            return;
        QString e;
        if (!_editor->Paste(&e))
            _editor->ShowMessage(e);
    });
    edit->addAction(QStringLiteral("Duplicate"), this, [this] { _editor->CmdDuplicate(); });
    edit->addAction(QStringLiteral("Delete"), this, [this] {
        if (TextCommand(QKeySequence::Delete))
            return;
        QString e;
        if (!_editor->DeleteSelected(&e))
            _editor->ShowMessage(e);
    });
    edit->addAction(QStringLiteral("Select All"), this, [] { TextCommand(QKeySequence::SelectAll); });
    edit->addSeparator();
    edit->addAction(QStringLiteral("Inline Include (editable copy)"), this, [this] { _editor->CmdInlineSelected(); });
    edit->addAction(QStringLiteral("Select None"), this, [this] { _editor->Document()->Select(QString(), false); });

    // ---- Deck: the role (or model), the view
    QMenu* dev = mb->addMenu(QStringLiteral("&Deck"));
    QMenu* roles = dev->addMenu(_client ? QStringLiteral("Role") : QStringLiteral("Model"));
    auto* group = new QActionGroup(this);
    for (int i = 0; i < _roles->count(); ++i)
    {
        QAction* a = roles->addAction(_roles->itemText(i));
        a->setCheckable(true);
        a->setData(i);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { _roles->setCurrentIndex(i); });
        if (_client && i == 0)
            roles->addSeparator();
    }
    auto syncRoles = [this, group] {
        for (QAction* a : group->actions())
            a->setChecked(a->data().toInt() == _roles->currentIndex());
    };
    connect(_roles, &QComboBox::currentIndexChanged, this, syncRoles);
    connect(_link, &DeckLink::StateChanged, this, syncRoles);
    syncRoles();
    if (_client)
    {
        dev->addAction(QStringLiteral("Reconnect to unreal-streamdeck"), this,
                       [this] { _client->Start(_client->GetRole()); });
        dev->addAction(QStringLiteral("Connection..."), this, [this] {
            QMessageBox::information(this, QStringLiteral("Connection"),
                                     QString::fromStdString(_link->Describe().dump(2)));
        });
    }
    dev->addSeparator();
    auto mirrorCheck = [dev](const QString& label, QCheckBox* box) {
        QAction* a = dev->addAction(label);
        a->setCheckable(true);
        a->setChecked(box->isChecked());
        connect(a, &QAction::toggled, box, &QCheckBox::setChecked);
        connect(box, &QCheckBox::toggled, a, &QAction::setChecked);
    };
    mirrorCheck(QStringLiteral("Bezel Mask"), _mask);
    mirrorCheck(QStringLiteral("Visible Area Guide"), _safe);
    mirrorCheck(QStringLiteral("Firmware Quirks"), _quirks);
    dev->addSeparator();
    dev->addAction(QStringLiteral("Show Logo (reset the screen)"), this, [this] { _deck->ResetScreen(); });
    dev->addAction(QStringLiteral("Clear Screen"), this, [this] { _deck->ClearScreen(); });

    // ---- Scenario: the editor and the run
    QMenu* scn = mb->addMenu(QStringLiteral("&Scenario"));
    scn->addAction(QStringLiteral("Editor in Its Own Window"), QKeySequence(Qt::CTRL | Qt::Key_E), this,
                   [this] { SetEditorMode(QStringLiteral("window")); });
    scn->addAction(QStringLiteral("Editor Docked Here"), this, [this] { SetEditorMode(QStringLiteral("dock")); });
    scn->addAction(QStringLiteral("Hide Editor"), this, [this] { SetEditorMode(QStringLiteral("hidden")); });
    scn->addSeparator();
    QAction* run = scn->addAction(QStringLiteral("Run on the Deck"), QKeySequence(Qt::CTRL | Qt::Key_R), this, [this] {
        QString e;
        if (!_editor->RunOn({}, &e))
            _editor->ShowMessage(e);
    });
    QAction* stop = scn->addAction(QStringLiteral("Stop"), QKeySequence(Qt::CTRL | Qt::Key_Period), this,
                                   [this] { _editor->StopRun(); });
    auto syncRun = [this, run, stop] {
        const bool r = _editor->Runner()->Running();
        run->setEnabled(!r);
        stop->setEnabled(r);
    };
    connect(_editor, &EditorWidget::RunStateChanged, this, syncRun);
    syncRun();
    scn->addSeparator();
    scn->addAction(QStringLiteral("Show Issues"), this, [this, editorUp] {
        editorUp();
        _editor->ShowIssues();
    });
    scn->addAction(QStringLiteral("Canvas: Up One Level"), this, [this] { _editor->Document()->Up(); });
    scn->addAction(QStringLiteral("Canvas: Home"), this, [this] {
        _editor->Document()->Enter(QString::fromStdString(_editor->Document()->Doc()["root"].value("id", "home")));
    });

    // ---- Record
    QMenu* rec = mb->addMenu(QStringLiteral("&Record"));
    QAction* recStart = rec->addAction(QStringLiteral("Start Video Recording..."), this, [this] {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Record the deck"),
                                                       dir + QStringLiteral("/streamdeck.mp4"),
                                                       QStringLiteral("Video (*.mp4 *.mov *.mkv *.webm *.gif)"));
        if (!f.isEmpty())
            StartRecording(f, 30);
    });
    QAction* recStop = rec->addAction(QStringLiteral("Stop Recording"), this, [this] { StopRecording(); });
    auto syncRec = [this, recStart, recStop] {
        recStart->setEnabled(!_recorder->Active());
        recStop->setEnabled(_recorder->Active());
    };
    connect(_recorder, &Recorder::StateChanged, this, syncRec);
    syncRec();

    // ---- View
    QMenu* view = mb->addMenu(QStringLiteral("&View"));
    QAction* proto = _protocolDock->toggleViewAction();
    proto->setText(QStringLiteral("Protocol Panel"));
    view->addAction(proto);
    QMenu* shown = view->addMenu(QStringLiteral("Protocol Shows"));
    for (QToolButton* b : _filterButtons)
    {
        QAction* a = shown->addAction(b->text());
        a->setCheckable(true);
        a->setChecked(b->isChecked());
        connect(a, &QAction::toggled, b, &QToolButton::setChecked);
        connect(b, &QToolButton::toggled, a, &QAction::setChecked);
    }
    view->addAction(QStringLiteral("Clear Protocol"), this, [this] {
        _log->clear();
        _logStore.clear();
    });
    view->addSeparator();
    view->addAction(QStringLiteral("Full Screen"), QKeySequence::FullScreen, this,
                    [this] { isFullScreen() ? showNormal() : showFullScreen(); });

    // ---- Automation
    QMenu* aut = mb->addMenu(QStringLiteral("&Automation"));
    const int apiPort = _options.apiPort;
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(apiPort);
    QAction* info = aut->addAction(apiPort > 0 ? QStringLiteral("WebAPI + MCP on %1").arg(base)
                                               : QStringLiteral("Automation is off (--api-port 0)"));
    info->setEnabled(false);
    aut->addAction(QStringLiteral("Copy WebAPI URL"), this,
                   [base] { QGuiApplication::clipboard()->setText(base + QStringLiteral("/api/v1")); })
        ->setEnabled(apiPort > 0);
    aut->addAction(QStringLiteral("Copy MCP Config (.mcp.json)"), this,
                   [this, base] {
                       // the suite's stdio bridge, beside the studio when shipped, else on PATH
                       QString bridge =
                           QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("unreal-mcp-bridge"));
                       if (!QFileInfo::exists(bridge))
                           bridge = QStringLiteral("unreal-mcp-bridge");
                       const QString j = QStringLiteral(
                                             "{\n  \"mcpServers\": {\n    \"streamdeck-studio\": {\n      \"command\": "
                                             "\"%1\",\n      \"args\": [\"--url\", \"%2/mcp\"]\n    }\n  }\n}\n")
                                             .arg(bridge, base);
                       QGuiApplication::clipboard()->setText(j);
                       AddLog(Src::Info, QStringLiteral("MCP config copied"));
                   })
        ->setEnabled(apiPort > 0);
    aut->addAction(QStringLiteral("Open the API Description (OpenAPI)"), this,
                   [base] { QDesktopServices::openUrl(QUrl(base + QStringLiteral("/api/v1/openapi.json"))); })
        ->setEnabled(apiPort > 0);

    // ---- Help
    QMenu* help = mb->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Stream Deck Studio Documentation"), this,
                    [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDocsUrl))); });
    help->addAction(QStringLiteral("Keyboard and Mouse"), this, [this] {
        QMessageBox::information(
            this, QStringLiteral("Keyboard and mouse"),
            QStringLiteral("Software deck: click = press, wheel over a dial = turn, drag on the strip = swipe, hold = "
                           "long press.\n\n"
                           "Editor canvas: drop palette items on keys; drag keys to move / swap / into a menu; "
                           "double-click a menu to enter, the back key to go up; right-click for import / export / "
                           "links.\n"
                           "Esc = up, Delete = delete, Cmd+D = duplicate, Cmd+C / Cmd+V = copy / paste, Cmd+Z / "
                           "Shift+Cmd+Z = undo / redo,\n"
                           "Cmd+R = run on the deck, Cmd+. = stop, Cmd+E = editor window."));
    });
    help->addAction(QStringLiteral("About Stream Deck Studio"), this,
                    [this] {
                        QMessageBox::about(
                            this, QStringLiteral("Stream Deck Studio"),
                            QStringLiteral("The software Stream Deck and scenario editor of unreal-ng: a viewer of "
                                           "unreal-streamdeck (device wire v2), with automation planes (WebAPI + "
                                           "MCP). GPL-3.0."));
                    })
        ->setMenuRole(QAction::AboutRole);
}

void MainWindow::SetEditorMode(const QString& mode)
{
    if (mode == QLatin1String("hidden"))
    {
        _editorDock->hide();
        return;
    }
    _editorDock->show();
    if (mode == QLatin1String("window"))
    {
        if (!_editorDock->isFloating())
        {
            _editorDock->setFloating(true);
            _editorDock->resize(1400, 820);
            _editorDock->move(geometry().topLeft() + QPoint(40, 40));
        }
    }
    else
    {
        _editorDock->setFloating(false);
        resizeDocks({_editorDock}, {std::max(700, width() / 2)}, Qt::Horizontal);
    }
    _editorDock->raise();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    _editor->StopRun();
    StopRecording();  // finalize the file while the deck can still be grabbed
    if (_client)
        _client->Stop();  // Leave: a clean disconnect
    QMainWindow::closeEvent(e);
}

void MainWindow::AddLog(Src s, const QString& line)
{
    _logStore.push_back({++_logSeq, QDateTime::currentDateTime(), s, line});
    if (_logStore.size() > 5000)
        _logStore.pop_front();
    if (!_show[static_cast<int>(s)])
        return;
    static const char* const kMarker[] = {"◀ DAEMON", "▶ VIEWER", "▶ DECK  ", "● APP   ", "· info  "};
    static const char* const kColour[] = {"#8fa8c8", "#38c8e0", "#ff9a3c", "#58d058", "#909090"};
    const int i = static_cast<int>(s);
    _log->appendHtml(QStringLiteral("<span style='color:#707070'>%1</span> <span style='color:%2'><b>%3</b> %4</span>")
                         .arg(QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz")), QLatin1String(kColour[i]),
                              QString::fromUtf8(kMarker[i]).replace(QLatin1Char(' '), QStringLiteral("&nbsp;")),
                              line.toHtmlEscaped()));
}

void MainWindow::LogInput(Src s, const QByteArray& r)
{
    auto& parser = s == Src::Deck ? _deckParser : _viewerParser;
    if (!parser)
        parser.emplace(_deck->GetModel());
    const auto* p = reinterpret_cast<const std::uint8_t*>(r.constData());
    for (const auto& e : parser->Feed(std::span(p, static_cast<size_t>(r.size()))))
        AddLog(s, QString::fromStdString(sdb::Describe(e)));
}

void MainWindow::RefreshStatus()
{
    const auto s = _deck->GetStats();
    const quint64 rps = s.reports - _lastReports, ips = s.images - _lastImages;
    _lastReports = s.reports;
    _lastImages = s.images;
    const QString rec =
        _recorder->Active()
            ? QStringLiteral("  |  ● REC %1 s, %2 frames").arg(_recorder->Seconds(), 0, 'f', 0).arg(_recorder->Frames())
            : QString();
    _status->setText(QStringLiteral("  %1  |  %2 reports/s, %3 images/s  |  serial %4%5")
                         .arg(_link->StatusText())
                         .arg(rps)
                         .arg(ips)
                         .arg(_deck->Serial(), rec));
    _view->SetBanner(_link->Banner());
    setWindowTitle(QStringLiteral("Stream Deck Studio - %1 %2 · %3")
                       .arg(NameOf(_deck->GetModel()), _deck->Serial(), _link->RoleText()));
}
