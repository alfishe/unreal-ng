#include "automation-gdb.h"

#include <cstdlib>
#include "gdbserver.h"

AutomationGDB::AutomationGDB()
    : _server(std::make_unique<GDBServer>())
{
}

AutomationGDB::~AutomationGDB()
{
    stop();
}

bool AutomationGDB::start()
{
    if (!_server)
    {
        _server = std::make_unique<GDBServer>();
    }

    _server->setAutoAttach(_autoAttach);
    // UNREAL_GDB_PORT overrides the port, as UNREAL_WEBAPI_PORT / _CLI_ / _MCP_ / _DEZOG_ / _ZRCP_ do for theirs:
    // several instances on one machine (parallel agents, tests) each get their own
    if (const char* env = std::getenv("UNREAL_GDB_PORT"))
    {
        char* end = nullptr;
        const long value = std::strtol(env, &end, 10);
        if (end != env && *end == '\0' && value > 0 && value <= 0xFFFF)
            _port = static_cast<uint16_t>(value);
    }
    return _server->start(_port, _bindAddress);
}

void AutomationGDB::stop()
{
    if (_server)
    {
        _server->stop();
    }
}

bool AutomationGDB::isRunning() const
{
    return _server && _server->isRunning();
}

uint16_t AutomationGDB::getPort() const
{
    return _server ? _server->getPort() : 0;
}

std::string AutomationGDB::getBindAddress() const
{
    return _bindAddress;
}

void AutomationGDB::setPort(uint16_t port)
{
    _port = port;
}

void AutomationGDB::setBindAddress(const std::string& address)
{
    _bindAddress = address;
}

void AutomationGDB::setAutoAttach(bool enable)
{
    _autoAttach = enable;
    if (_server)
    {
        _server->setAutoAttach(enable);
    }
}
