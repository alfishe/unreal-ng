#include "platform/linux/gamescopecontrol.h"

#ifdef __linux__

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>

#include "platform/linuxpresent.h"

namespace gamescope
{
namespace
{
// libwayland-client's ABI (wayland-util.h, wayland-client-core.h): stable since 1.0, wl_proxy_marshal_flags since
// 1.20 (2021)
struct WlMessage
{
    const char* name;
    const char* signature;
    const void* const* types;
};

struct WlInterface
{
    const char* name;
    int version;
    int methodCount;
    const WlMessage* methods;
    int eventCount;
    const WlMessage* events;
};

struct WlArray
{
    size_t size;
    size_t alloc;
    void* data;
};

constexpr uint32_t kMarshalDestroy = 1;  // WL_MARSHAL_FLAG_DESTROY

// Every argument of these messages a plain value: no interface types
const void* const kNoTypes[8] = {};

// gamescope_control up to version 2: what this client sends and reads. A bound version 2 gets no later events
const WlMessage kControlRequests[] = {
    {"destroy", "", kNoTypes},
    {"set_app_target_refresh_cycle", "2uu", kNoTypes},
};
const WlMessage kControlEvents[] = {
    {"feature_support", "uuu", kNoTypes},
    {"active_display_info", "2sssua", kNoTypes},
};
const WlInterface kControlInterface = {"gamescope_control", 2, 2, kControlRequests, 2, kControlEvents};
constexpr uint32_t kControlVersion = 2;
constexpr uint32_t kControlDestroy = 0;
constexpr uint32_t kControlSetRefreshCycle = 1;

constexpr uint32_t kDisplayGetRegistry = 1;  // wl_display.get_registry
constexpr uint32_t kRegistryBind = 0;        // wl_registry.bind

template <typename Fn>
bool Resolve(void* library, const char* name, Fn& fn)
{
    fn = library ? reinterpret_cast<Fn>(dlsym(library, name)) : nullptr;
    return fn != nullptr;
}
}  // namespace

struct Control::Api
{
    void* library = nullptr;
    void* (*connect)(const char*) = nullptr;
    void (*disconnect)(void*) = nullptr;
    int (*roundtrip)(void*) = nullptr;
    int (*flush)(void*) = nullptr;
    int (*error)(void*) = nullptr;
    void* (*marshalFlags)(void*, uint32_t, const WlInterface*, uint32_t, uint32_t, ...) = nullptr;
    int (*addListener)(void*, void (**)(void), void*) = nullptr;
    void (*destroy)(void*) = nullptr;
    uint32_t (*version)(void*) = nullptr;
    const WlInterface* registryInterface = nullptr;

    ~Api()
    {
        if (library)
            dlclose(library);
    }

    bool Load()
    {
        library = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
        return Resolve(library, "wl_display_connect", connect) &&
               Resolve(library, "wl_display_disconnect", disconnect) &&
               Resolve(library, "wl_display_roundtrip", roundtrip) && Resolve(library, "wl_display_flush", flush) &&
               Resolve(library, "wl_display_get_error", error) &&
               Resolve(library, "wl_proxy_marshal_flags", marshalFlags) &&
               Resolve(library, "wl_proxy_add_listener", addListener) &&
               Resolve(library, "wl_proxy_destroy", destroy) && Resolve(library, "wl_proxy_get_version", version) &&
               Resolve(library, "wl_registry_interface", registryInterface);
    }
};

struct Control::State
{
    const Api* api = nullptr;
    void* display = nullptr;
    void* registry = nullptr;
    void* control = nullptr;
    uint32_t controlName = 0;
    uint32_t controlVersion = 0;
    bool haveDisplay = false;
    std::string connector;
    bool internal = false;
    std::vector<double> rates;
};

namespace
{
using State = Control::State;

void OnGlobal(void* data, void* registry, uint32_t name, const char* interface, uint32_t version)
{
    (void)registry;
    State* s = static_cast<State*>(data);
    if (interface && std::strcmp(interface, kControlInterface.name) == 0 && version >= kControlVersion)
    {
        s->controlName = name;
        s->controlVersion = version;
    }
}

void OnGlobalRemove(void*, void*, uint32_t) {}

void OnFeature(void*, void*, uint32_t, uint32_t, uint32_t) {}

void OnDisplayInfo(void* data, void* control, const char* connector, const char* make, const char* model,
                   uint32_t flags, WlArray* rates)
{
    (void)control;
    (void)make;
    (void)model;
    State* s = static_cast<State*>(data);
    s->haveDisplay = true;
    s->connector = connector ? connector : "";
    s->internal = (flags & 0x1) != 0;  // display_flag.internal_display
    s->rates = rates && rates->data ? linuxpresent::GamescopeRates(static_cast<const uint32_t*>(rates->data),
                                                                   rates->size / sizeof(uint32_t))
                                    : std::vector<double>();
}

// The listeners: one function per event, in the events' order
void (*kRegistryListener[])(void) = {reinterpret_cast<void (*)(void)>(OnGlobal),
                                     reinterpret_cast<void (*)(void)>(OnGlobalRemove)};
void (*kControlListener[])(void) = {reinterpret_cast<void (*)(void)>(OnFeature),
                                    reinterpret_cast<void (*)(void)>(OnDisplayInfo)};
}  // namespace

std::unique_ptr<Control> Control::Connect(std::string& error)
{
    std::unique_ptr<Control> c(new Control());
    if (!c->Open(error))
        return nullptr;
    return c;
}

bool Control::Open(std::string& error)
{
    const char* socket = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
    if (!socket || !*socket)
    {
        error = "not under gamescope (no GAMESCOPE_WAYLAND_DISPLAY)";
        return false;
    }
    _api = new Api();
    _state = new State();
    _state->api = _api;
    if (!_api->Load())
    {
        error = "libwayland-client.so.0 (1.20 or later) not loaded";
        return false;
    }
    _state->display = _api->connect(socket);
    if (!_state->display)
    {
        error = std::string("gamescope's Wayland socket ") + socket + " refused the connection";
        return false;
    }
    _state->registry = _api->marshalFlags(_state->display, kDisplayGetRegistry, _api->registryInterface,
                                          _api->version(_state->display), 0, nullptr);
    if (!_state->registry || _api->addListener(_state->registry, kRegistryListener, _state) != 0 ||
        !Roundtrip(error))
    {
        if (error.empty())
            error = "gamescope's Wayland registry not read";
        return false;
    }
    if (_state->controlName == 0)
    {
        error = "this gamescope has no gamescope_control of version 2 or later";
        return false;
    }
    _state->control = _api->marshalFlags(_state->registry, kRegistryBind, &kControlInterface, kControlVersion, 0,
                                         _state->controlName, kControlInterface.name, kControlVersion, nullptr);
    if (!_state->control || _api->addListener(_state->control, kControlListener, _state) != 0)
    {
        error = "gamescope_control not bound";
        return false;
    }
    // The binding sends the features and the active display at once
    return Roundtrip(error);
}

bool Control::Roundtrip(std::string& error)
{
    if (_api->roundtrip(_state->display) >= 0)
        return true;
    error = "gamescope's Wayland connection failed (error " + std::to_string(_api->error(_state->display)) + ")";
    return false;
}

Control::~Control()
{
    if (_state && _api)
    {
        if (_state->control)
            _api->marshalFlags(_state->control, kControlDestroy, nullptr, _api->version(_state->control),
                               kMarshalDestroy);
        if (_state->registry)
            _api->destroy(_state->registry);
        if (_state->display)
        {
            _api->flush(_state->display);
            _api->disconnect(_state->display);
        }
    }
    delete _state;
    delete _api;
}

bool Control::Display(std::string& connector, bool& internal, std::vector<double>& rates, std::string& error)
{
    if (!Roundtrip(error))
        return false;
    if (!_state->haveDisplay)
    {
        error = "gamescope told no active display";
        return false;
    }
    connector = _state->connector;
    internal = _state->internal;
    rates = _state->rates;
    return true;
}

bool Control::SetRefreshCycle(uint32_t fps, bool internal, std::string& error)
{
    _api->marshalFlags(_state->control, kControlSetRefreshCycle, nullptr, _api->version(_state->control), 0, fps,
                       linuxpresent::GamescopeRefreshFlags(internal));
    return Roundtrip(error);
}
}  // namespace gamescope

#endif  // __linux__
