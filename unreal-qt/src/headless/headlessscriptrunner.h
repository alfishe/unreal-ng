#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <thread>

/// The headless test runner (gs-debugger requirements X13): unreal-qt --headless --script <file.lua> --exit-on-script.
/// Waits for the Lua interpreter and the selected emulator, mutes the emulator (--headless), runs the file on the
/// interpreter's thread and, with exitOnScript, closes the window (`quit`: the window's own shutdown) and leaves the
/// script's exit code for main (AutomationLua::runFile). Without a file to open the runner starts the machine itself (`startEmulator`, the window's
/// Start). The script runs on a worker thread: the GUI thread stays free for the emulator's events
class HeadlessScriptRunner : public QObject
{
public:
    HeadlessScriptRunner(QString path, bool exitOnScript, bool mute, std::function<void()> startEmulator,
                         std::function<void()> quit, QObject* parent = nullptr);
    ~HeadlessScriptRunner() override;

    void Start();
    /// The script ended (or could not run): its exit code
    bool Finished() const { return _finished; }
    int ExitCode() const { return _exitCode; }

private:
    void Poll();
    void Finish(int code, const QString& error);

    QString _path;
    bool _exitOnScript = false;
    bool _mute = false;
    std::function<void()> _startEmulator;   // empty: a file on the command line brings the machine
    bool _startRequested = false;
    std::function<void()> _quit;
    bool _finished = false;
    int _exitCode = 0;
    QTimer _poll;
    int _pollsLeft = 0;
    std::thread _worker;
};
