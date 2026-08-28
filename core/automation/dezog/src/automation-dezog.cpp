#include "automation-dezog.h"

#include "dezogdebugadapter.h"
#include "dzrpserver.h"

#include <cstdlib>
#include <iostream>
#include <string>

AutomationDezog::AutomationDezog() = default;

AutomationDezog::~AutomationDezog()
{
    try
    {
        stop();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in AutomationDezog destructor: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in AutomationDezog destructor" << std::endl;
    }
}

uint16_t AutomationDezog::resolvePort(uint16_t requested)
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
            std::cerr << "[DZRP] Ignoring invalid " << PORT_ENV_VAR << "='" << env << "'" << std::endl;
        }
    }

    return dzrp::DEFAULT_PORT;
}

bool AutomationDezog::start(uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_server)
        return true;  // Already running

    uint16_t resolvedPort = resolvePort(port);

    _adapter = std::make_unique<DezogDebugAdapter>();

    dzrp::ServerConfig config;
    config.port = resolvedPort;
    config.serverName = "Unreal-NG";
    config.serverVersion = "1.0.0";

    _server = std::make_unique<dzrp::Server>(_adapter.get(), config);

    // Route emulator stop events into the DZRP session as NTF_PAUSE
    dzrp::Server* server = _server.get();
    _adapter->setPauseNotifier(
        [server](dzrp::BreakReason reason, uint16_t addr, uint8_t bank) { server->notifyPause(reason, addr, bank); });

    std::cout << "Starting DeZog (DZRP) server on port " << resolvedPort << "..." << std::endl;

    if (!_server->start())
    {
        std::cerr << "Failed to start DeZog server on port " << resolvedPort << std::endl;
        _adapter->setPauseNotifier(nullptr);
        _server.reset();
        _adapter.reset();
        return false;
    }

    return true;
}

void AutomationDezog::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_server)
        return;

    std::cout << "Stopping DeZog server..." << std::endl;

    // Detach the notifier first so no in-flight breakpoint event touches a dying server
    if (_adapter)
        _adapter->setPauseNotifier(nullptr);

    _server->stop();
    _server.reset();
    _adapter.reset();

    std::cout << "DeZog server stopped" << std::endl;
}

bool AutomationDezog::isRunning() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _server && _server->isRunning();
}

uint16_t AutomationDezog::getPort() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _server ? _server->getPort() : 0;
}
