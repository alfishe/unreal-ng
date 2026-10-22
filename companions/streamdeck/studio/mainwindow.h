// Stream Deck Studio's window: the software deck (a viewer of unreal-streamdeck, or a v1 server with --listen), the
// protocol log, recording, the scenario editor (a dock or its own window) and the automation planes.
#pragma once
#include <QDateTime>
#include <QImage>
#include <QMainWindow>
#include <deque>
#include <optional>

#include "sdb/input.h"

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QTimer;
class QToolButton;
class DaemonControl;
class DeckClient;
class DeckLink;
class DeckWidget;
class EditorWidget;
class ListenServer;
class Recorder;
class StudioApi;
class VirtualDeck;

struct StudioOptions
{
    QString daemonHost = QStringLiteral("127.0.0.1");
    quint16 daemonPort = 37050;
    bool own = false;                          // start with a deck of its own
    QString deck = QStringLiteral("first");    // mirror: which real deck
    QString model = QStringLiteral("plusxl");  // own / listen: the model
    bool listen = false;                       // v1 server for one host (protocol tests)
    quint16 listenPort = 37050;
    bool anyInterface = false;
    int apiPort = 37052;  // 0 = off
    int daemonApiPort = 37053;  // the daemon's automation (decks, brightness, scenario per deck)
    bool startDaemon = true;    // no daemon answers at launch: start one (unreal-streamdeck run --wait)
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const StudioOptions& options);
    ~MainWindow() override;
    bool StartRecording(const QString& path, int fps, QString* error = nullptr);
    QString StopRecording();

    // Client mode: switch the role (mirror a real deck / a deck of its own of a model).
    bool SetRole(bool own, const QString& deckOrModel, QString* error);

protected:
    void closeEvent(QCloseEvent* e) override;

    // Protocol log sources, each with its own marker and colour
    enum class Src
    {
        Daemon,  // what the daemon (or a --listen host) sends: images, features
        Viewer,  // input from this window
        Deck,    // input on the real deck, relayed
        App,     // what the runtime did with the input
        Info     // the studio: connection, model, recording
    };
    static constexpr int kSources = 5;
    struct LogEntry
    {
        qint64 seq;
        QDateTime time;
        Src src;
        QString text;
    };

private:
    friend class StudioApi;  // the automation planes drive the window like a user
    void AddLog(Src s, const QString& line);
    void LogInput(Src s, const QByteArray& report);
    void SelectModel(quint16 pid);  // the model combo: listen mode = the deck's model; client mode = an own deck
    void RefreshStatus();
    void SyncRoleCombo();
    void SetEditorMode(const QString& mode);  // window | dock | hidden
    void BuildMenus();
    void OnSnapshot(const QString& path);
    QImage Screenshot(const QString& area);  // lcd | deck | window | canvas | editor
    void StartDaemonIfNone();
    void PairWithHost();  // Deck > Pair with UnrealNG: the daemon's POST /api/v1/pair, its state on the status bar
    [[nodiscard]] QString ShownDeck() const;  // the serial of the deck this window shows (joined), else ""
    void FetchBrightness();                   // the shown deck's brightness from the daemon into the slider

    StudioOptions _options;
    VirtualDeck* _deck;
    DeckLink* _link = nullptr;
    DeckClient* _client = nullptr;    // client mode (default)
    ListenServer* _server = nullptr;  // --listen
    DeckWidget* _view;
    QComboBox* _roles;  // client: "mirror" + own models; listen: the models
    DaemonControl* _daemon = nullptr;
    QSlider* _brightness = nullptr;  // the shown deck's brightness, set through the daemon
    QTimer* _brightnessSend = nullptr;
    QTimer* _pairPoll = nullptr;  // the pairing's state while it waits for the approval
    QString _pairState;
    QString _brightnessDeck;  // the deck the slider shows
    QLabel* _status;
    QPlainTextEdit* _log;
    QTimer* _tick;
    Recorder* _recorder;
    QToolButton* _rec;
    QCheckBox *_mask, *_safe, *_quirks;
    QToolButton* _filterButtons[kSources] = {};
    QDockWidget* _protocolDock = nullptr;
    QDockWidget* _editorDock = nullptr;
    EditorWidget* _editor = nullptr;
    QToolButton* _editorButton = nullptr;
    std::deque<LogEntry> _logStore;  // the last 5000 lines, all sources (the panel filters only what it shows)
    qint64 _logSeq = 0;
    QByteArray _page;  // the runtime's last page description (JSON), empty = none
    QDateTime _pageTime;
    StudioApi* _api = nullptr;
    std::optional<sdb::InputParser> _viewerParser, _deckParser;  // per source: each report carries its full state
    bool _show[kSources] = {true, true, true, true, true};
    quint64 _lastReports = 0, _lastImages = 0;
};
