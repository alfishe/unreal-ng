#include "headlessscriptrunner.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <iostream>
#include <string>
#include <vector>

#include "automation/automation.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/sound/audiomixer.h"

namespace
{
/// The interpreter and the emulator come up within a few seconds; a start that never ends fails the run
constexpr int kPollIntervalMs = 50;
constexpr int kPollTimeoutMs = 30000;

/// The emulator the script drives (Lua's effectiveEmulator is the selected one): the selection, or the window's sole
/// instance, selected here (the GUI never selects it)
Emulator* SelectedEmulator()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (!manager)
        return nullptr;
    std::string id = manager->GetSelectedEmulatorId();
    if (id.empty())
    {
        const std::vector<std::string> ids = manager->GetEmulatorIds();
        if (ids.size() != 1 || !manager->SetSelectedEmulatorId(ids.front()))
            return nullptr;
        id = ids.front();
    }
    return manager->GetEmulator(id).get();
}
}  // namespace

HeadlessScriptRunner::HeadlessScriptRunner(QString path, bool exitOnScript, bool mute,
                                           std::function<void()> startEmulator, std::function<void()> quit,
                                           QObject* parent)
    : QObject(parent), _path(std::move(path)), _exitOnScript(exitOnScript), _mute(mute),
      _startEmulator(std::move(startEmulator)), _quit(std::move(quit))
{
    _poll.setInterval(kPollIntervalMs);
    connect(&_poll, &QTimer::timeout, this, [this]() { Poll(); });
}

HeadlessScriptRunner::~HeadlessScriptRunner()
{
    // The interpreter finishes the script before it stops: the worker ends with it
    if (_worker.joinable())
        _worker.join();
}

void HeadlessScriptRunner::Start()
{
    if (!QFileInfo::exists(_path))
    {
        // After app.exec() starts: an exit before it is lost
        QTimer::singleShot(0, this, [this]() { Finish(1, QStringLiteral("no script file: %1").arg(_path)); });
        return;
    }
    _pollsLeft = kPollTimeoutMs / kPollIntervalMs;
    _poll.start();
}

void HeadlessScriptRunner::Poll()
{
    const bool luaReady = Automation::GetInstance().IsLuaReady();
    Emulator* emulator = luaReady ? SelectedEmulator() : nullptr;
    if (luaReady && !emulator && _startEmulator && !_startRequested)
    {
        _startRequested = true;
        _startEmulator();
        emulator = SelectedEmulator();
    }
    if (!emulator)
    {
        if (--_pollsLeft <= 0)
        {
            _poll.stop();
            Finish(1, Automation::GetInstance().IsLuaReady() ? QStringLiteral("no emulator to run the script on")
                                                             : QStringLiteral("the Lua interpreter did not start"));
        }
        return;
    }
    _poll.stop();

    if (_mute)
    {
        AudioMixer::Change change;
        change.muted = 1;
        std::string error;
        if (!AudioMixer::Apply(emulator->GetContext(), "master", change, error))
            std::cerr << "headless: could not mute: " << error << std::endl;
    }

    const std::string path = QFileInfo(_path).absoluteFilePath().toStdString();
    _worker = std::thread([this, path]() {
        std::string error;
        const int code = Automation::GetInstance().RunLuaFile(path, error);
        QMetaObject::invokeMethod(this, [this, code, error]() { Finish(code, QString::fromStdString(error)); },
                                  Qt::QueuedConnection);
    });
}

void HeadlessScriptRunner::Finish(int code, const QString& error)
{
    if (!error.isEmpty())
        std::cerr << "script: " << error.toStdString() << std::endl;
    std::cout << "script exit code " << code << std::endl;
    _finished = true;
    _exitCode = code;
    if (!_exitOnScript)
        return;
    // The window's close stops the machine and the automation in order; QCoreApplication::exit alone left them to
    // the static destructors
    if (_quit)
        _quit();
    else
        QCoreApplication::exit(code);
}
