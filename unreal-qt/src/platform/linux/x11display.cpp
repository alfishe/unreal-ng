#include "platform/linux/x11display.h"

#ifdef __linux__

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QWindow>

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
// The X11 and RandR types this file uses, as libX11 and libXrandr define them (X11/Xlib.h, X11/extensions/Xrandr.h).
// Their layout is RandR 1.2's (2007) and part of the libraries' ABI (checked against the
// real headers: offsets and sizes the same on x86_64 and arm64)
using XID = unsigned long;
using Atom = unsigned long;
using XTime = unsigned long;
using XWindow = XID;
using RROutput = XID;
using RRCrtc = XID;
using RRMode = XID;
using Rotation = unsigned short;
using XStatus = int;
using XBool = int;

struct XRRModeInfo
{
    RRMode id;
    unsigned int width;
    unsigned int height;
    unsigned long dotClock;
    unsigned int hSyncStart;
    unsigned int hSyncEnd;
    unsigned int hTotal;
    unsigned int hSkew;
    unsigned int vSyncStart;
    unsigned int vSyncEnd;
    unsigned int vTotal;
    char* name;
    unsigned int nameLength;
    unsigned long modeFlags;
};

struct XRRScreenResources
{
    XTime timestamp;
    XTime configTimestamp;
    int ncrtc;
    RRCrtc* crtcs;
    int noutput;
    RROutput* outputs;
    int nmode;
    XRRModeInfo* modes;
};

struct XRROutputInfo
{
    XTime timestamp;
    RRCrtc crtc;
    char* name;
    int nameLen;
    unsigned long mmWidth;
    unsigned long mmHeight;
    unsigned short connection;
    unsigned short subpixelOrder;
    int ncrtc;
    RRCrtc* crtcs;
    int nclone;
    RROutput* clones;
    int nmode;
    int npreferred;
    RRMode* modes;
};

struct XRRCrtcInfo
{
    XTime timestamp;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    RRMode mode;
    Rotation rotation;
    int noutput;
    RROutput* outputs;
    Rotation rotations;
    int npossible;
    RROutput* possible;
};

struct XErrorEvent;
using XErrorHandler = int (*)(void*, XErrorEvent*);

constexpr Atom kXaCardinal = 6;    // XA_CARDINAL
constexpr Atom kAnyProperty = 0;   // AnyPropertyType
constexpr int kPropModeReplace = 0;
constexpr XTime kCurrentTime = 0;  // CurrentTime
constexpr XStatus kRRSetConfigSuccess = 0;

/// dlsym's pointer to the function's type (a cast warns under gcc's -Wcast-function-type)
template <typename Fn>
bool Resolve(void* library, const char* name, Fn& fn)
{
    void* symbol = library ? dlsym(library, name) : nullptr;
    static_assert(sizeof fn == sizeof symbol);
    std::memcpy(&fn, &symbol, sizeof fn);
    return symbol != nullptr;
}

/// Errors on the own connection (a mode refused, an output gone) are counted, never fatal: Xlib's default handler
/// ends the process
int gXErrors = 0;
int CountError(void*, XErrorEvent*)
{
    gXErrors++;
    return 0;
}
}  // namespace

namespace x11
{
linuxpresent::WindowSystem CurrentWindowSystem()
{
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return linuxpresent::WindowSystemOf(QGuiApplication::platformName().toStdString(), wayland && *wayland);
}

linuxpresent::VrrDecision DecideWindowVrr(const QWindow* window, bool nvidia)
{
    linuxpresent::VrrInputs in;
    in.system = CurrentWindowSystem();
    in.nvidia = nvidia;
    in.gsyncAllowed = std::getenv("__GL_GSYNC_ALLOWED");
    in.vrrAllowed = std::getenv("__GL_VRR_ALLOWED");
    const QScreen* screen = window ? window->screen() : nullptr;
    if (screen)
    {
        const QPoint origin = window->mapToGlobal(QPoint(0, 0));
        const QRect g = screen->geometry();
        in.coversScreen = linuxpresent::CoversScreen(origin.x(), origin.y(), window->width(), window->height(), g.x(),
                                                     g.y(), g.width(), g.height());
    }
    Display* x = in.system == linuxpresent::WindowSystem::X11 ? Display::Get() : nullptr;
    if (x && screen)
    {
        OutputState output;
        std::string error;
        if (x->Output(screen, output, error))
            in.vrrCapable = x->VrrCapable(output.output);
    }
    return linuxpresent::DecideVrr(in);
}

void OptInVariableRefresh(QWindow* window, bool on)
{
    Display* x = CurrentWindowSystem() == linuxpresent::WindowSystem::X11 ? Display::Get() : nullptr;
    if (!x || !window)
        return;
    QWindow* top = window;
    while (top->parent())
        top = top->parent();
    x->SetVariableRefresh(static_cast<unsigned long>(window->winId()), on);
    if (top != window)
        x->SetVariableRefresh(static_cast<unsigned long>(top->winId()), on);
}

struct Display::Api
{
    void* x11 = nullptr;
    void* xrandr = nullptr;
    void* (*openDisplay)(const char*) = nullptr;
    int (*closeDisplay)(void*) = nullptr;
    XWindow (*defaultRootWindow)(void*) = nullptr;
    Atom (*internAtom)(void*, const char*, XBool) = nullptr;
    int (*changeProperty)(void*, XWindow, Atom, Atom, int, int, const unsigned char*, int) = nullptr;
    int (*xfree)(void*) = nullptr;
    int (*sync)(void*, XBool) = nullptr;
    XErrorHandler (*setErrorHandler)(XErrorHandler) = nullptr;
    XBool (*queryExtension)(void*, int*, int*) = nullptr;
    XRRScreenResources* (*getResources)(void*, XWindow) = nullptr;
    void (*freeResources)(XRRScreenResources*) = nullptr;
    XRROutputInfo* (*getOutputInfo)(void*, XRRScreenResources*, RROutput) = nullptr;
    void (*freeOutputInfo)(XRROutputInfo*) = nullptr;
    XRRCrtcInfo* (*getCrtcInfo)(void*, XRRScreenResources*, RRCrtc) = nullptr;
    void (*freeCrtcInfo)(XRRCrtcInfo*) = nullptr;
    XStatus (*setCrtcConfig)(void*, XRRScreenResources*, RRCrtc, XTime, int, int, RRMode, Rotation, RROutput*,
                             int) = nullptr;
    int (*getOutputProperty)(void*, RROutput, Atom, long, long, XBool, XBool, Atom, Atom*, int*, unsigned long*,
                             unsigned long*, unsigned char**) = nullptr;

    ~Api()
    {
        if (xrandr)
            dlclose(xrandr);
        if (x11)
            dlclose(x11);
    }

    bool Load()
    {
        x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        xrandr = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_LOCAL);
        return Resolve(x11, "XOpenDisplay", openDisplay) && Resolve(x11, "XCloseDisplay", closeDisplay) &&
               Resolve(x11, "XDefaultRootWindow", defaultRootWindow) && Resolve(x11, "XInternAtom", internAtom) &&
               Resolve(x11, "XChangeProperty", changeProperty) && Resolve(x11, "XFree", xfree) &&
               Resolve(x11, "XSync", sync) && Resolve(x11, "XSetErrorHandler", setErrorHandler) &&
               Resolve(xrandr, "XRRQueryExtension", queryExtension) &&
               Resolve(xrandr, "XRRGetScreenResourcesCurrent", getResources) &&
               Resolve(xrandr, "XRRFreeScreenResources", freeResources) &&
               Resolve(xrandr, "XRRGetOutputInfo", getOutputInfo) &&
               Resolve(xrandr, "XRRFreeOutputInfo", freeOutputInfo) && Resolve(xrandr, "XRRGetCrtcInfo", getCrtcInfo) &&
               Resolve(xrandr, "XRRFreeCrtcInfo", freeCrtcInfo) &&
               Resolve(xrandr, "XRRSetCrtcConfig", setCrtcConfig) &&
               Resolve(xrandr, "XRRGetOutputProperty", getOutputProperty);
    }
};

namespace
{
/// Counts the X errors of a scope on the own connection (synced at its end)
class ErrorScope
{
public:
    ErrorScope(const Display::Api& api, void* display) : _api(api), _display(display)
    {
        gXErrors = 0;
        _previous = _api.setErrorHandler(CountError);
    }
    ~ErrorScope() { _api.setErrorHandler(_previous); }
    int Errors()
    {
        _api.sync(_display, 0);
        return gXErrors;
    }

private:
    const Display::Api& _api;
    void* _display;
    XErrorHandler _previous = nullptr;
};

linuxpresent::XMode ToMode(const XRRModeInfo& m)
{
    linuxpresent::XMode out;
    out.id = m.id;
    out.width = m.width;
    out.height = m.height;
    out.dotClock = m.dotClock;
    out.hTotal = m.hTotal;
    out.vTotal = m.vTotal;
    out.flags = m.modeFlags;
    return out;
}

struct ResourcesDeleter
{
    const Display::Api* api;
    void operator()(XRRScreenResources* r) const { api->freeResources(r); }
};
}  // namespace

Display* Display::Get()
{
    static std::unique_ptr<Display> instance;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        instance = Connect();
    }
    return instance.get();
}

std::unique_ptr<Display> Display::Connect()
{
    std::unique_ptr<Display> d(new Display());
    if (!d->Open())
        return nullptr;
    return d;
}

bool Display::Open()
{
    const char* name = std::getenv("DISPLAY");
    if (!name || !*name)
        return false;
    _api = new Api();
    if (!_api->Load())
        return false;
    _display = _api->openDisplay(nullptr);
    int eventBase = 0, errorBase = 0;
    return _display && _api->queryExtension(_display, &eventBase, &errorBase);
}

Display::~Display()
{
    if (_display && _api)
        _api->closeDisplay(_display);
    delete _api;
}

bool Display::Output(const QScreen* screen, OutputState& state, std::string& error)
{
    ErrorScope scope(*_api, _display);
    std::unique_ptr<XRRScreenResources, ResourcesDeleter> res(
        _api->getResources(_display, _api->defaultRootWindow(_display)), ResourcesDeleter{_api});
    if (!res)
    {
        error = "RandR gave no screen resources";
        return false;
    }
    const std::string wanted = screen ? screen->name().toStdString() : std::string();
    const QRect geometry = screen ? QRect(screen->geometry().topLeft() * screen->devicePixelRatio(),
                                          screen->geometry().size() * screen->devicePixelRatio())
                                  : QRect();
    bool found = false;
    for (int pass = 0; pass < 2 && !found; pass++)
    {
        for (int i = 0; i < res->noutput && !found; i++)
        {
            XRROutputInfo* info = _api->getOutputInfo(_display, res.get(), res->outputs[i]);
            if (!info)
                continue;
            const std::string name(info->name, static_cast<size_t>(info->nameLen));
            XRRCrtcInfo* crtc = info->crtc ? _api->getCrtcInfo(_display, res.get(), info->crtc) : nullptr;
            // By name first; then the lit CRTC at the screen's origin (a screen Qt named otherwise)
            const bool match =
                crtc && crtc->mode != 0 &&
                (pass == 0 ? name == wanted : crtc->x == geometry.x() && crtc->y == geometry.y());
            if (match)
            {
                found = true;
                state = {};
                state.name = name;
                state.output = res->outputs[i];
                state.crtc = info->crtc;
                state.x = crtc->x;
                state.y = crtc->y;
                state.width = crtc->width;
                state.height = crtc->height;
                state.rotation = crtc->rotation;
                state.outputs.assign(crtc->outputs, crtc->outputs + crtc->noutput);
                for (int m = 0; m < res->nmode; m++)
                {
                    const XRRModeInfo& mode = res->modes[m];
                    if (mode.id == crtc->mode)
                        state.current = ToMode(mode);
                    for (int k = 0; k < info->nmode; k++)
                        if (info->modes[k] == mode.id)
                            state.modes.push_back(ToMode(mode));
                }
            }
            if (crtc)
                _api->freeCrtcInfo(crtc);
            _api->freeOutputInfo(info);
        }
    }
    if (scope.Errors() > 0 && !found)
        error = "RandR refused the query";
    else if (!found)
        error = "no lit RandR output for the screen " + wanted;
    return found && state.current.id != 0;
}

int Display::VrrCapable(unsigned long output)
{
    ErrorScope scope(*_api, _display);
    const Atom property = _api->internAtom(_display, "vrr_capable", 1);
    if (property == 0)
        return -1;  // no output has the property: the atom was never made
    Atom type = 0;
    int format = 0;
    unsigned long items = 0, after = 0;
    unsigned char* data = nullptr;
    const int status = _api->getOutputProperty(_display, output, property, 0, 1, 0, 0, kAnyProperty, &type, &format,
                                               &items, &after, &data);
    int value = -1;
    if (status == 0 && data && items == 1 && format == 32)
        value = *reinterpret_cast<const long*>(data) != 0 ? 1 : 0;
    if (data)
        _api->xfree(data);
    return scope.Errors() > 0 ? -1 : value;
}

bool Display::SetMode(const OutputState& state, uint64_t modeId, std::string& error)
{
    ErrorScope scope(*_api, _display);
    std::unique_ptr<XRRScreenResources, ResourcesDeleter> res(
        _api->getResources(_display, _api->defaultRootWindow(_display)), ResourcesDeleter{_api});
    if (!res)
    {
        error = "RandR gave no screen resources";
        return false;
    }
    std::vector<RROutput> outputs(state.outputs.begin(), state.outputs.end());
    const XStatus status =
        _api->setCrtcConfig(_display, res.get(), state.crtc, kCurrentTime, state.x, state.y, modeId, state.rotation,
                            outputs.data(), static_cast<int>(outputs.size()));
    const int errors = scope.Errors();
    if (status != kRRSetConfigSuccess || errors > 0)
    {
        error = "the X server refused the mode (RRSetCrtcConfig status " + std::to_string(status) +
                (errors > 0 ? ", " + std::to_string(errors) + " X errors)" : ")");
        return false;
    }
    return true;
}

void Display::SetVariableRefresh(unsigned long window, bool on)
{
    if (window == 0)
        return;
    ErrorScope scope(*_api, _display);
    const Atom property = _api->internAtom(_display, "_VARIABLE_REFRESH", 0);
    const long value = on ? 1 : 0;
    _api->changeProperty(_display, window, property, kXaCardinal, 32, kPropModeReplace,
                         reinterpret_cast<const unsigned char*>(&value), 1);
    scope.Errors();
}
}  // namespace x11

#endif  // __linux__
