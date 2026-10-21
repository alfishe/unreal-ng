#include "runcontroller.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include "daemoncontrol.h"
#include "editordocument.h"
#include "sdb/scenarioio.h"

RunController::RunController(EditorDocument* doc, DaemonControl* daemon, QObject* parent)
    : QObject(parent), _doc(doc), _daemon(daemon)
{
    _autosave.setSingleShot(true);
    _autosave.setInterval(250);
    // the daemon watches the run file (both ways of running): writing it is the reload
    connect(&_autosave, &QTimer::timeout, this, &RunController::WriteRunFile);
    connect(_doc, &EditorDocument::Changed, this, [this] {
        if (Running())
            _autosave.start();
    });
    connect(_daemon, &DaemonControl::OutputLine, this, [this](const QString& l) {
        if (_started)
            Line(l);
    });
    connect(_daemon, &DaemonControl::StateChanged, this, [this] {
        if (_started && !_daemon->Launched())
            _started = false;  // our daemon ended (Stop, or on its own)
        emit StateChanged();
    });
}

RunController::~RunController()
{
    Stop();
}

void RunController::Line(const QString& line)
{
    _output.append(line);
    if (_output.size() > 300)
        _output.removeFirst();
    emit OutputLine(line);
}

int RunController::ApiPort() const
{
    return _daemon->ApiPort();
}

QString RunController::ApiUrl() const
{
    return _daemon->ApiUrl();
}

bool RunController::Running() const
{
    return _onDeck || (_started && _daemon->Launched());
}

QString RunController::How() const
{
    return _onDeck ? QStringLiteral("deck ") + _deck : Running() ? QStringLiteral("started") : QString();
}

void RunController::WriteRunFile()
{
    if (_runFile.isEmpty())
        return;
    std::string err;
    // includes expanded: the run file lives elsewhere, so relative include paths would not resolve from there
    if (!sdb::scn::io::SaveFile(_runFile.toStdString(), _doc->Resolved(), &err))
        Line(QStringLiteral("autosave failed: ") + QString::fromStdString(err));
}

bool RunController::Start(const QString& serial, QString* error)
{
    Stop();
    if (!_doc->Valid())
    {
        if (error)
            *error = QStringLiteral("the scenario has errors (see Issues)");
        return false;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/run");
    QDir().mkpath(dir);
    QString name = QString::fromStdString(_doc->Doc().value("name", "untitled"));
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    _runFile = QDir(dir).filePath(name + QStringLiteral(".deck.json"));
    WriteRunFile();
    _output.clear();

    // a daemon runs: the deck this window shows plays the run file, the other decks keep theirs
    if (_daemon->Answers())
    {
        QString deck = !serial.isEmpty() ? serial : _target ? _target() : QString();
        if (deck.isEmpty())
            deck = QStringLiteral("first");
        QString err;
        if (!_daemon->PlayScenario(deck, _runFile, &err))
        {
            if (error)
                *error = QStringLiteral("unreal-streamdeck did not take the scenario for deck %1: %2").arg(deck, err);
            return false;
        }
        _deck = deck;
        _onDeck = true;
        Line(QStringLiteral("unreal-streamdeck plays %1 on deck %2 (Stop gives it its start scenario back)")
                 .arg(_runFile, deck));
        emit StateChanged();
        return true;
    }

    // none: one on the run file, for the time of the run; it ends with the studio
    QString err;
    if (!_daemon->Launch({QStringLiteral("run"), _runFile, QStringLiteral("--watch"), QStringLiteral("--wait"),
                          QStringLiteral("--api"), QString::number(_daemon->ApiPort()),
                          QStringLiteral("--with-parent")},
                         &err))
    {
        for (const QString& l : _daemon->Output())
            Line(l);
        if (error)
            *error = err;
        return false;
    }
    _started = true;
    _deck.clear();
    for (const QString& l : _daemon->Output())
        Line(l);
    emit StateChanged();
    return true;
}

void RunController::Stop()
{
    _autosave.stop();
    if (_onDeck)
    {
        _onDeck = false;
        QString err;
        if (!_daemon->RestoreScenario(_deck, &err))
            Line(QStringLiteral("could not give deck %1 its start scenario back: %2").arg(_deck, err));
        else
            Line(QStringLiteral("deck %1 plays its start scenario again").arg(_deck));
        emit StateChanged();
    }
    if (_started)
    {
        _started = false;
        _daemon->Terminate();
        emit StateChanged();
    }
}
