// Plays the edited scenario with the one engine, the daemon. The run file is a copy of the document (includes
// resolved) in the app data folder that follows every edit (debounced), so the deck updates live while the user's
// own file changes only on Save.
//   - a daemon answers on its automation port: the deck this window shows (its own deck, or the real deck it
//     mirrors) plays the run file, that deck only (PUT /api/v1/decks/{serial}/scenario, watched); Stop gives the
//     deck the daemon's start scenario back (DELETE);
//   - none: `unreal-streamdeck run <run file> --watch --wait --api PORT --with-parent` is started; Stop ends it.
#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

class DaemonControl;
class EditorDocument;

class RunController : public QObject
{
    Q_OBJECT
public:
    RunController(EditorDocument* doc, DaemonControl* daemon, QObject* parent = nullptr);
    ~RunController() override;

    // The deck to run on when Start gets none: the one the window shows ("" = the daemon's first real deck).
    void SetTarget(std::function<QString()> deck)
    {
        _target = std::move(deck);
    }
    bool Start(const QString& serial, QString* error);
    void Stop();
    [[nodiscard]] bool Running() const;
    [[nodiscard]] QString How() const;  // "deck <serial>" (a running daemon plays it there) or "started"
    [[nodiscard]] QString Deck() const
    {
        return _deck;
    }
    [[nodiscard]] QString RunFile() const
    {
        return _runFile;
    }
    [[nodiscard]] QStringList Output() const
    {
        return _output;
    }
    [[nodiscard]] int ApiPort() const;
    [[nodiscard]] QString ApiUrl() const;

signals:
    void StateChanged();
    void OutputLine(const QString& line);

private:
    void WriteRunFile();
    void Line(const QString& line);

    EditorDocument* _doc;
    DaemonControl* _daemon;
    std::function<QString()> _target;
    QTimer _autosave;
    QString _runFile, _deck;
    bool _onDeck = false;   // a running daemon plays our run file on _deck
    bool _started = false;  // we started a daemon on the run file
    QStringList _output;
};
