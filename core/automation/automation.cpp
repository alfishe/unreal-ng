#include "automation.h"

#include <thread>

/// region <Methods>

bool Automation::start()
{
    bool result = true;
    result &= startLua();
    result &= startPython();
    result &= startWebAPI();

    return result;
}

void Automation::stop()
{
    stopLua();
    stopPython();
    stopWebAPI();
}

/// endregion </Methods>

/// region <Helper methods>
bool Automation::startLua()
{
    bool result = true;

    _lua = new AutomationLua();
    _lua->start();

    return result;
}

bool Automation::startPython()
{
    bool result = true;

    _python = new AutomationPython();
    _python->start();

    return result;
}

bool Automation::startWebAPI()
{
    bool result = true;

    _webAPI = new AutomationWebAPI();
    _webAPI->start();

    return result;
}

void Automation::stopLua()
{
    if (_lua)
    {
        _lua->stop();

        delete _lua;
        _lua = nullptr;
    }
}

void Automation::stopPython()
{
    if (_python)
    {
        _python->stop();

        delete _python;
        _python = nullptr;
    }
}

void Automation::stopWebAPI()
{
    if (_webAPI)
    {
        _webAPI->stop();

        delete _webAPI;
        _webAPI = nullptr;
    }
}
/// endregion </Helper methods>