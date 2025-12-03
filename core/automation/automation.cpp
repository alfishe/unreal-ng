#include "automation.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <thread>

/// region <Methods>

bool Automation::start()
{
    bool result = true;

#if ENABLE_LUA_AUTOMATION
    result &= startLua();
#endif

#if ENABLE_PYTHON_AUTOMATION
    result &= startPython();
#endif

#if ENABLE_WEBAPI_AUTOMATION
    result &= startWebAPI();
#endif

#if ENABLE_CLI_AUTOMATION
    result &= startCLI();
#endif

    return result;
}

void Automation::stop()
{
#if ENABLE_LUA_AUTOMATION
    stopLua();
#endif

#if ENABLE_PYTHON_AUTOMATION
    stopPython();
#endif

#if ENABLE_WEBAPI_AUTOMATION
    stopWebAPI();
#endif

#if ENABLE_CLI_AUTOMATION
    stopCLI();
#endif
}

/// endregion </Methods>

/// region <Helper methods>
#if ENABLE_LUA_AUTOMATION
bool Automation::startLua()
{
    bool result = true;

    _lua = new AutomationLua();
    if (_lua)
    {
        _lua->start();
    }
    else
    {
        result = false;
    }

    return result;
}
#endif

#if ENABLE_PYTHON_AUTOMATION
bool Automation::startPython()
{
    bool result = true;

    _python = new AutomationPython();
    if (_python)
    {
        _python->start();
    }
    else
    {
        result = false;
    }

    return result;
}
#endif

#if ENABLE_WEBAPI_AUTOMATION
bool Automation::startWebAPI()
{
    bool result = true;

    _webAPI = new AutomationWebAPI();
    if (_webAPI)
    {
        _webAPI->start();
    }
    else
    {
        result = false;
    }

    return result;
}
#endif

#if ENABLE_LUA_AUTOMATION
void Automation::stopLua()
{
    if (_lua)
    {
        _lua->stop();
        delete _lua;
        _lua = nullptr;
    }
}
#endif

#if ENABLE_PYTHON_AUTOMATION
void Automation::stopPython()
{
    if (_python)
    {
        _python->stop();
        delete _python;
        _python = nullptr;
    }
}
#endif

#if ENABLE_WEBAPI_AUTOMATION
void Automation::stopWebAPI()
{
    if (_webAPI)
    {
        _webAPI->stop();
        delete _webAPI;
        _webAPI = nullptr;
    }
}
#endif

#if ENABLE_CLI_AUTOMATION
bool Automation::startCLI()
{
    bool result = true;

    _cli = new AutomationCLI();

    if (_cli)
    {
        result = _cli->start();
    }
    else
    {
        result = false;
    }

    return result;
}

void Automation::stopCLI()
{
    if (_cli)
    {
        _cli->stop();
        delete _cli;
        _cli = nullptr;
    }
}
#endif

/// endregion </Helper methods>