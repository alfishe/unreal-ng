#include "automation-zesarux.h"

#include "dezogdebugadapter.h"
#include "zrcpserver.h"

#include <cstdlib>
#include <iostream>
#include <string>

AutomationZesarux::AutomationZesarux() = default;

AutomationZesarux::~AutomationZesarux()
{
    try
    {
        stop();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in AutomationZesarux destructor: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in AutomationZesarux destructor" << std::endl;
    }
}

uint16_t AutomationZesarux::resolvePort(uint16_t requested)
{
    if (requested != 0)
        return requested;

    if (const char* env = std::getenv(PORT_ENV_VAR))
    {
        try
        {
            int value = std::stoi(env);
            if (value > 0 && value <= 0xFFFF)
                return static_cast<uint16_t>(value);
        }
        catch (...)
        {
            std::cerr << "[ZRCP] Ignoring invalid " << PORT_ENV_VAR << "='" << env << "'" << std::endl;
        }
    }

    return zrcp::DEFAULT_PORT;
}

bool AutomationZesarux::start(uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_server)
        return true;  // Already running

    const uint16_t resolvedPort = resolvePort(port);

    // Second, independent adapter instance: the DZRP module keeps its own
    // session/breakpoint state when both servers listen concurrently
    _adapter = std::make_unique<DezogDebugAdapter>();

    zrcp::ServerConfig config;
    config.port = resolvedPort;
    config.serverName = "Unreal-NG";

    _server = std::make_unique<zrcp::Server>(_adapter.get(), config);

    // Route emulator stop events into the ZRCP run-waiter
    zrcp::Server* server = _server.get();
    _adapter->setPauseNotifier(
        [server](dzrp::BreakReason reason, uint16_t addr, uint8_t bank) { server->notifyPause(reason, addr, bank); });

    std::cout << "Starting ZEsarUX (ZRCP) server on port " << resolvedPort << "..." << std::endl;

    if (!_server->start())
    {
        std::cerr << "Failed to start ZEsarUX server on port " << resolvedPort << std::endl;
        _adapter->setPauseNotifier(nullptr);
        _server.reset();
        _adapter.reset();
        return false;
    }

    return true;
}

void AutomationZesarux::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_server)
        return;

    std::cout << "Stopping ZEsarUX server..." << std::endl;

    // Detach the notifier first so no in-flight breakpoint event touches a dying server
    if (_adapter)
        _adapter->setPauseNotifier(nullptr);

    _server->stop();
    _server.reset();
    _adapter.reset();

    std::cout << "ZEsarUX server stopped" << std::endl;
}

bool AutomationZesarux::isRunning() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _server && _server->isRunning();
}

uint16_t AutomationZesarux::getPort() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _server ? _server->getPort() : 0;
}
