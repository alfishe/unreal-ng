#include "windowviewprovider.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <chrono>
#include <future>
#include <memory>

#include "devicescreenwrapper.h"

namespace
{
/// How long a surface thread waits for the GUI thread: a busy GUI (a modal dialog runs its own loop and still serves
/// this) answers in a frame or two; switching the GPU backend re-creates the screen, still well inside
constexpr std::chrono::milliseconds kGuiTimeout{3000};

/// Run `task` on the GUI thread (in place when already there) and wait for its answer. The task holds only copies and
/// guarded pointers, and the state is shared: a task that finishes after the timeout writes into it, not into the
/// caller's gone stack
template <typename Out>
bool OnGuiThread(std::function<bool(Out&, std::string&)> task, Out& out, std::string& error)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
    {
        error = "The GUI is gone";
        return false;
    }
    if (QThread::currentThread() == app->thread())
        return task(out, error);

    struct Job
    {
        Out out;
        std::string error;
        std::promise<bool> done;
    };
    auto job = std::make_shared<Job>();
    std::future<bool> result = job->done.get_future();
    const bool queued = QMetaObject::invokeMethod(
        app,
        [task = std::move(task), job]() {
            const bool ok = task(job->out, job->error);
            job->done.set_value(ok);
        },
        Qt::QueuedConnection);
    if (!queued)
    {
        error = "The GUI thread does not take requests";
        return false;
    }
    if (result.wait_for(kGuiTimeout) != std::future_status::ready)
    {
        error = "The GUI thread did not answer within " + std::to_string(kGuiTimeout.count()) + " ms";
        return false;
    }
    const bool ok = result.get();
    out = std::move(job->out);
    error = job->error;
    return ok;
}
}  // namespace

WindowViewProvider::WindowViewProvider(DeviceScreenWrapper* screen, ShownEmulatorId shownEmulatorId, Display display,
                                       Parameters parameters)
    : _screen(screen),
      _shownEmulatorId(std::move(shownEmulatorId)),
      _display(std::move(display)),
      _parameters(std::move(parameters))
{
}

bool WindowViewProvider::GrabWindowView(const std::string& emulatorId, WindowViewGrab& out, std::string& error)
{
    std::function<bool(WindowViewGrab&, std::string&)> task = [screen = _screen, shownEmulatorId = _shownEmulatorId,
                                                               emulatorId](WindowViewGrab& grab, std::string& why) {
        return GrabHere(screen, shownEmulatorId, emulatorId, grab, why);
    };
    return OnGuiThread(std::move(task), out, error);
}

bool WindowViewProvider::GetWindowDisplay(WindowDisplaySettings& out, std::string& error)
{
    std::function<bool(WindowDisplaySettings&, std::string&)> task = [display = _display](WindowDisplaySettings& s,
                                                                                         std::string& why) {
        return display ? display(nullptr, s, why) : false;
    };
    return OnGuiThread(std::move(task), out, error);
}

bool WindowViewProvider::SetWindowDisplay(const WindowDisplayChange& change, WindowDisplaySettings& out,
                                          std::string& error)
{
    std::function<bool(WindowDisplaySettings&, std::string&)> task = [display = _display,
                                                                      change](WindowDisplaySettings& s, std::string& why) {
        return display ? display(&change, s, why) : false;
    };
    return OnGuiThread(std::move(task), out, error);
}

bool WindowViewProvider::GetCrtParameters(CrtParameters& out, std::string& error)
{
    std::function<bool(CrtParameters&, std::string&)> task = [parameters = _parameters](CrtParameters& p,
                                                                                       std::string& why) {
        if (!parameters)
        {
            why = "This window offers no CRT parameters";
            return false;
        }
        return parameters(nullptr, p, why);
    };
    return OnGuiThread(std::move(task), out, error);
}

bool WindowViewProvider::SetCrtParameters(const CrtParameterChange& change, CrtParameters& out, std::string& error)
{
    std::function<bool(CrtParameters&, std::string&)> task = [parameters = _parameters, change](CrtParameters& p,
                                                                                               std::string& why) {
        if (!parameters)
        {
            why = "This window offers no CRT parameters";
            return false;
        }
        return parameters(&change, p, why);
    };
    return OnGuiThread(std::move(task), out, error);
}

bool WindowViewProvider::GrabHere(const QPointer<DeviceScreenWrapper>& screen, const ShownEmulatorId& shownEmulatorId,
                                  const std::string& emulatorId, WindowViewGrab& out, std::string& error)
{
    if (!screen)
    {
        error = "The window is closed";
        return false;
    }
    const std::string shown = shownEmulatorId ? shownEmulatorId() : std::string();
    if (shown.empty())
    {
        error = "The window shows no emulator";
        return false;
    }
    if (shown != emulatorId)
    {
        error = "The window shows another emulator (" + shown + ")";
        return false;
    }
    return screen->grabWindowView(out, error);
}
