#include "platform/displaymodeswitch.h"

#ifdef __linux__

#include <QScreen>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "platform/linux/gamescopecontrol.h"
#include "platform/linux/x11display.h"
#include "platform/linuxpresent.h"

namespace
{
/// The signal handler's pipe to TerminationRestore's thread
int gPipe[2] = {-1, -1};

/// The modes to put back if the process is told to end (SIGTERM, SIGINT, SIGHUP): the X server (and gamescope) keeps
/// a mode after the process that set it ends, and a signal skips the destructors. The handler only writes the signal to a pipe;
/// a thread of its own restores on a connection of its own (the GUI thread may be the one interrupted), then the
/// signal's default action ends the process as before
class TerminationRestore
{
public:
    static TerminationRestore& Instance()
    {
        static TerminationRestore* instance = new TerminationRestore();  // never destroyed: the thread uses it
        return *instance;
    }

    /// `restore` runs on the restoring thread, on connections of its own
    void Keep(const void* owner, std::function<void()> restore)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Install();
        _originals[owner] = std::move(restore);
    }

    void Forget(const void* owner)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _originals.erase(owner);
    }

private:
    TerminationRestore() = default;

    /// _mutex held: the handlers and the thread, once; a signal the application handles itself is left alone
    void Install()
    {
        if (_installed)
            return;
        _installed = true;
        if (pipe2(gPipe, O_CLOEXEC) != 0)
            return;
        for (int sig : {SIGTERM, SIGINT, SIGHUP})
        {
            struct sigaction old = {};
            if (sigaction(sig, nullptr, &old) != 0 || old.sa_handler != SIG_DFL)
                continue;
            struct sigaction action = {};
            action.sa_handler = OnSignal;
            sigemptyset(&action.sa_mask);
            sigaction(sig, &action, nullptr);
        }
        std::thread([this] { Run(); }).detach();
    }

    static void OnSignal(int sig)
    {
        const unsigned char byte = static_cast<unsigned char>(sig);
        [[maybe_unused]] const ssize_t written = write(gPipe[1], &byte, 1);  // async-signal-safe
    }

    void Run()
    {
        unsigned char byte = 0;
        ssize_t got = 0;
        do
            got = read(gPipe[0], &byte, 1);
        while (got < 0 && errno == EINTR);
        const int sig = got == 1 ? byte : SIGTERM;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& [owner, restore] : _originals)
                restore();
            _originals.clear();
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }

    std::mutex _mutex;
    bool _installed = false;
    std::map<const void*, std::function<void()>> _originals;
};

/// X11: RandR's CRTC under the screen on another mode of its resolution. Unlike macOS and Windows the X server keeps
/// a mode a process set after it ends, so Restore runs from the destructor (and the window's quit / focus loss), and
/// TerminationRestore covers the signals that end the process (a crash still leaves the mode)
class X11DisplayModeSwitch : public DisplayModeSwitch
{
public:
    explicit X11DisplayModeSwitch(x11::Display* display) : _x(display) {}
    ~X11DisplayModeSwitch() override { Restore(); }

    bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) override
    {
        rates.clear();
        x11::OutputState state;
        std::string error;
        if (!_x->Output(screen, state, error))
            return false;
        currentHz = std::round(linuxpresent::ModeRateHz(state.current) * 100.0) / 100.0;
        rates = linuxpresent::RatesAtResolution(state.modes, state.current);
        return currentHz > 0.0 && !rates.empty();
    }

    bool Set(const QScreen* screen, double hz, std::string& error) override
    {
        x11::OutputState state;
        if (!_x->Output(screen, state, error))
            return false;
        if (_switched && state.crtc != _original.crtc)
            Restore();
        const linuxpresent::XMode* mode = linuxpresent::ModeForRate(state.modes, state.current, hz);
        if (!mode)
        {
            error = "the display has no such mode";
            return false;
        }
        if (!_x->SetMode(state, mode->id, error))
            return false;
        // The display's own mode, kept from the first switch on
        if (!_switched)
        {
            _original = state;
            _switched = true;
            TerminationRestore::Instance().Keep(this, [original = _original] {
                std::string error;
                if (std::unique_ptr<x11::Display> x = x11::Display::Connect())
                    x->SetMode(original, original.current.id, error);
            });
        }
        return true;
    }

    void Restore() override
    {
        if (!_switched)
            return;
        _switched = false;
        TerminationRestore::Instance().Forget(this);
        std::string error;
        _x->SetMode(_original, _original.current.id, error);
    }

    bool Switched() const override { return _switched; }

private:
    x11::Display* _x;
    x11::OutputState _original;  ///< the CRTC as it was before the first switch
    bool _switched = false;
};

/// gamescope (the Steam Deck's Game Mode, any gamescope session): the game is an XWayland client whose RandR is
/// emulated, the panel's mode gamescope's. gamescope_control's target refresh cycle, Steam's refresh rate slider's
/// request, switches it: the machine's rate among the display's valid ones (the Deck LCD 40..60 Hz in whole Hz,
/// gamescope making the mode). 0 gives the display its own rate back. The setting is gamescope's, not this
/// process's: it outlives the process (Restore on quit, focus loss and the signals; a crash leaves it until Steam
/// sends its own, on the next game or slider move), and Steam's slider changes it too (the last request wins)
class GamescopeDisplayModeSwitch : public DisplayModeSwitch
{
public:
    explicit GamescopeDisplayModeSwitch(std::unique_ptr<gamescope::Control> control) : _control(std::move(control)) {}
    ~GamescopeDisplayModeSwitch() override { Restore(); }

    bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) override
    {
        std::string connector, error;
        if (!_control->Display(connector, _internal, rates, error) || rates.empty())
            return false;
        // gamescope does not tell the rate running: the one asked, else the display's own (the highest valid
        // rate, what gamescope runs without a request)
        currentHz = _switched ? _hz : rates.back();
        (void)screen;
        return true;
    }

    bool Set(const QScreen* screen, double hz, std::string& error) override
    {
        (void)screen;
        const uint32_t fps = linuxpresent::GamescopeFps(hz);
        if (fps == 0 || !_control->SetRefreshCycle(fps, _internal, error))
        {
            if (error.empty())
                error = "no such rate";
            return false;
        }
        _hz = hz;
        if (!_switched)
        {
            _switched = true;
            TerminationRestore::Instance().Keep(this, [internal = _internal] {
                std::string why;
                if (std::unique_ptr<gamescope::Control> c = gamescope::Control::Connect(why))
                    c->SetRefreshCycle(0, internal, why);
            });
        }
        return true;
    }

    void Restore() override
    {
        if (!_switched)
            return;
        _switched = false;
        TerminationRestore::Instance().Forget(this);
        std::string error;
        _control->SetRefreshCycle(0, _internal, error);
    }

    bool Switched() const override { return _switched; }

private:
    std::unique_ptr<gamescope::Control> _control;
    bool _internal = true;
    bool _switched = false;
    double _hz = 0.0;
};

/// Why gamescope_control is not there (empty when not under gamescope at all), from the last Create
std::string& GamescopeNote()
{
    static std::string note;
    return note;
}
}  // namespace

std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    // Under gamescope its protocol, whatever Qt runs on (XWayland, with no WAYLAND_DISPLAY: it looks like X11)
    GamescopeNote().clear();
    const char* gamescopeSocket = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
    if (gamescopeSocket && *gamescopeSocket)
    {
        std::string error;
        if (std::unique_ptr<gamescope::Control> control = gamescope::Control::Connect(error))
            return std::make_unique<GamescopeDisplayModeSwitch>(std::move(control));
        GamescopeNote() = "gamescope: " + error;
        return nullptr;
    }
    if (x11::CurrentWindowSystem() != linuxpresent::WindowSystem::X11)
        return nullptr;
    x11::Display* display = x11::Display::Get();
    if (!display)
        return nullptr;
    return std::make_unique<X11DisplayModeSwitch>(display);
}

std::string DisplayModeSwitch::Unavailable()
{
    if (!GamescopeNote().empty())
        return GamescopeNote();
    const std::string why = linuxpresent::NoModeSwitchReason(x11::CurrentWindowSystem());
    return why.empty() ? "X11 without RandR (libXrandr.so.2 not loaded, or the server lacks the extension)" : why;
}

#endif  // __linux__
