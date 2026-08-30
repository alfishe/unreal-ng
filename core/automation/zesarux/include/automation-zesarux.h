#pragma once

// AutomationZesarux - ZEsarUX ZRCP (text protocol) debug server automation
// module.
//
// Serves the same emulator through a second adapter + server pair next to the
// DZRP module, so DeZog can connect with either "remoteType":
//   - "zrcp"  → this server (watchpoints, state, native reverse debugging)
//   - "cspect"→ the DZRP server (automation_dezog)
// Both servers may listen concurrently; each adapter keeps its own
// session/breakpoint state (only one DeZog session should own breakpoints at
// a time).
//
// Port resolution: explicit start(port) argument, else UNREAL_ZRCP_PORT
// environment variable, else zrcp::DEFAULT_PORT (10000).

#include "zrcptypes.h"

#include <cstdint>
#include <memory>
#include <mutex>

class DezogDebugAdapter;
namespace zrcp
{
class Server;
}

class AutomationZesarux
{
public:
    AutomationZesarux();
    ~AutomationZesarux();

    AutomationZesarux(const AutomationZesarux&) = delete;
    AutomationZesarux& operator=(const AutomationZesarux&) = delete;

    // port == 0 → resolve from UNREAL_ZRCP_PORT env var, else DEFAULT_PORT
    bool start(uint16_t port = 0);
    void stop();

    bool isRunning() const;
    uint16_t getPort() const;

    DezogDebugAdapter* getAdapter() { return _adapter.get(); }

    static uint16_t resolvePort(uint16_t requested);
    static constexpr const char* PORT_ENV_VAR = "UNREAL_ZRCP_PORT";

private:
    mutable std::mutex _mutex;
    std::unique_ptr<DezogDebugAdapter> _adapter;
    std::unique_ptr<zrcp::Server> _server;
};
