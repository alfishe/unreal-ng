#pragma once

// AutomationDezog - DeZog (DZRP v2.2.0) debug server automation module.
//
// Follows the AutomationCLI / AutomationWebAPI lifecycle contract:
//   Automation::start() → AutomationDezog::start(port)
//   Automation::stop()  → AutomationDezog::stop()
//
// Owns a DezogDebugAdapter (IDebugInterface over the live Emulator, bound
// dynamically to the most recent instance) and a dzrp::Server listening on
// 127.0.0.1:<port>. Breakpoint / pause events are forwarded as NTF_PAUSE.
//
// Port resolution: explicit start(port) argument, else UNREAL_DEZOG_PORT
// environment variable, else dzrp::DEFAULT_PORT (12000).

#include "dzrptypes.h"

#include <cstdint>
#include <memory>
#include <mutex>

class DezogDebugAdapter;
namespace dzrp
{
class Server;
}

class AutomationDezog
{
public:
    AutomationDezog();
    ~AutomationDezog();

    AutomationDezog(const AutomationDezog&) = delete;
    AutomationDezog& operator=(const AutomationDezog&) = delete;

    // port == 0 → resolve from UNREAL_DEZOG_PORT env var, else DEFAULT_PORT
    bool start(uint16_t port = 0);
    void stop();

    bool isRunning() const;
    uint16_t getPort() const;

    DezogDebugAdapter* getAdapter() { return _adapter.get(); }

    static uint16_t resolvePort(uint16_t requested);
    static constexpr const char* PORT_ENV_VAR = "UNREAL_DEZOG_PORT";

private:
    mutable std::mutex _mutex;
    std::unique_ptr<DezogDebugAdapter> _adapter;
    std::unique_ptr<dzrp::Server> _server;
};
