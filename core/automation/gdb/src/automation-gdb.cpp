#include "automation-gdb.h"
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
