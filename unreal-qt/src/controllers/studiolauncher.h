#pragma once

/// @file studiolauncher.h
/// @brief Stream Deck Studio (the software deck, companions/streamdeck/studio) from Tools > Stream Deck...: where it
/// is beside this executable, opening it (started, or its window brought to the front when it runs) and whether it
/// runs. The state comes from Studio's own automation port (127.0.0.1:37052, GET /api/v1/connection: what its deck
/// shows), asked off the GUI thread while the dialog is visible; a Studio started with `--api-port 0` is seen through
/// the daemon's viewer count. Studio is its own application: it is not stopped when unreal-qt quits.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

#include "common/network/loopbackhttp.h"

class StudioLauncher : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Missing,   ///< not beside unreal-qt (a build without the studio)
        Off,       ///< not running
        Starting,  ///< started here, its port does not answer yet
        Running,   ///< its port answers (or the daemon counts a viewer)
        Failed     ///< it could not be started, or it never answered
    };

    /// Studio's automation port (its --api-port default)
    static constexpr int kApiPort = 37052;
    /// How long a started Studio may take before its port answers
    static constexpr qint64 kStartTimeoutMs = 20000;

    /// What the launcher does outside (tests: fakes)
    struct Hooks
    {
        /// `method` `path` on Studio's automation port (a worker thread calls it)
        std::function<netsock::HttpReply(const std::string& method, const std::string& path, const std::string& body)>
            http;
        /// Starts `path` with `args` detached; false and `error` when it cannot
        std::function<bool(const QString& path, const QStringList& args, QString& error)> launch;
        /// Milliseconds of a steady clock
        std::function<qint64()> clock;
    };

    /// The real hooks, Studio found by Locate()
    explicit StudioLauncher(QObject* parent = nullptr);
    StudioLauncher(Hooks hooks, QString path, QObject* parent = nullptr);

    /// Stream Deck Studio beside this executable, or "": the suite's UnrealNG.app/Contents/Applications/Stream Deck
    /// Studio.app and a build tree's bin/unreal-streamdeck-studio.app (macOS), else unreal-streamdeck-studio[.exe]
    /// beside unreal-qt (the Windows ZIP, the AppImage's usr/bin, the DEB) or beside a build tree's bundle
    static QString Locate();

    QString Path() const { return _path; }
    State GetState() const { return _state; }
    /// For people: "Running: mirrors deck ...", "Not running", ...
    QString StatusText() const;
    /// "Open Studio" when it does not run, "Show Studio" when it does
    QString ButtonText() const;
    /// The button does something: Studio is here and not starting
    bool CanOpen() const;

    /// Where the daemon is, so a Studio started here joins it (the supervisor's ports)
    void SetDaemonPorts(int viewersPort, int apiPort);
    /// The daemon's viewer count (-1: the daemon is not up): a Studio whose automation is off still counts
    void SetViewers(int viewers);

    /// Not running: start it; running: bring its window to the front
    void Open();
    /// Ask its port once (one question in flight; Changed() when the answer moves the state)
    void Probe();
    /// A probe's answer (the worker thread hands it to the GUI thread; tests call it directly)
    void TakeProbe(const netsock::HttpReply& reply);
    /// A probe is waiting for its answer
    bool Probing() const { return _probing; }

signals:
    void Changed();

private:
    void Set(State state, const QString& detail);
    /// The state the latest answer and the clock give
    void Settle();

    Hooks _hooks;
    QString _path;
    State _state = State::Off;
    QString _detail;           ///< the deck it shows, or why it failed
    bool _answered = false;    ///< its port answered the latest probe
    QString _connection;       ///< what the latest answer said its deck is
    int _viewers = -1;
    int _viewersPort = 37050;
    int _daemonApiPort = 37053;
    qint64 _startedAt = -1;    ///< Open() started it here at this time (-1: not starting)
    QString _launchError;
    bool _probing = false;
};
