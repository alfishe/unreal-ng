#include "platform/displaymodeswitch.h"

#ifdef __linux__

#include <QScreen>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

#include "platform/linux/x11display.h"
#include "platform/linuxpresent.h"

namespace
{
/// The signal handler's pipe to TerminationRestore's thread
int gPipe[2] = {-1, -1};

/// The modes to put back if the process is told to end (SIGTERM, SIGINT, SIGHUP): the X server keeps a mode after
/// the process that set it ends, and a signal skips the destructors. The handler only writes the signal to a pipe;
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

    void Keep(const void* owner, const x11::OutputState& original)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Install();
        _originals[owner] = original;
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
            std::unique_ptr<x11::Display> x = _originals.empty() ? nullptr : x11::Display::Connect();
            for (const auto& [owner, original] : _originals)
            {
                std::string error;
                if (x)
                    x->SetMode(original, original.current.id, error);
            }
            _originals.clear();
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }

    std::mutex _mutex;
    bool _installed = false;
    std::map<const void*, x11::OutputState> _originals;
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
            TerminationRestore::Instance().Keep(this, _original);
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
}  // namespace

std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    if (x11::CurrentWindowSystem() != linuxpresent::WindowSystem::X11)
        return nullptr;
    x11::Display* display = x11::Display::Get();
    if (!display)
        return nullptr;
    return std::make_unique<X11DisplayModeSwitch>(display);
}

std::string DisplayModeSwitch::Unavailable()
{
    const std::string why = linuxpresent::NoModeSwitchReason(x11::CurrentWindowSystem());
    return why.empty() ? "X11 without RandR (libXrandr.so.2 not loaded, or the server lacks the extension)" : why;
}

#endif  // __linux__
