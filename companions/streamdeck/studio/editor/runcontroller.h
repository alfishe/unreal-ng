// Plays the edited scenario on the deck with the one engine, the daemon. The run file is a copy of the document
// (includes resolved) in the app data folder that follows every edit (debounced), so the deck updates live while
// the user's own file changes only on Save.
//   - a daemon with its automation on the run API port: it reloads the run file (POST /api/v1/reload); Stop gives
//     it its own file back;
//   - none: `unreal-streamdeck run <run file> --watch --api PORT --with-parent` is started; Stop ends it.
// The software deck shows the run as a viewer of that daemon (mirror of the real deck).
#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class EditorDocument;

class RunController : public QObject
{
    Q_OBJECT
public:
    static constexpr int kApiPort = 37053;  // the daemon's automation (runtime state, navigate, press, reload)

    explicit RunController(EditorDocument* doc, QObject* parent = nullptr);
    ~RunController() override;

    // serial picks a USB deck for a daemon this starts (empty = the first)
    bool Start(const QString& serial, QString* error);
    void Stop();
    [[nodiscard]] bool Running() const;
    [[nodiscard]] QString How() const;  // "started" (our daemon) or "reloaded" (a running one)
    [[nodiscard]] QString RunFile() const
    {
        return _runFile;
    }
    [[nodiscard]] QStringList Output() const
    {
        return _output;
    }
    [[nodiscard]] int ApiPort() const
    {
        return kApiPort;
    }
    [[nodiscard]] QString ApiUrl() const;
    // The daemon executable: $UNREAL_STREAMDECK_BIN, beside the studio (or its bundle, or the suite's bundle), PATH.
    [[nodiscard]] static QString DaemonPath();

signals:
    void StateChanged();
    void OutputLine(const QString& line);

private:
    void WriteRunFile();
    void Line(const QString& line);
    bool Reload(const QString& file, QString* error);

    EditorDocument* _doc;
    QProcess _proc;
    QTimer _autosave;
    QString _runFile, _previousFile;
    bool _reloaded = false;  // a running daemon plays our run file
    QStringList _output;
};
