#include "automation.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <memory>
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

    _lua = std::make_unique<AutomationLua>();
    _lua->start();

    return result;
}
#endif

#if ENABLE_PYTHON_AUTOMATION
bool Automation::startPython()
{
    bool result = true;

    _python = std::make_unique<AutomationPython>();
    _python->start();

    return result;
}
#endif

#if ENABLE_WEBAPI_AUTOMATION
bool Automation::startWebAPI()
{
    bool result = true;

    _webAPI = std::make_unique<AutomationWebAPI>();
    _webAPI->start();

    return result;
}
#endif

#if ENABLE_LUA_AUTOMATION
void Automation::stopLua()
{
    if (_lua)
    {
        _lua->stop();
        _lua.reset();
    }
}
#endif

#if ENABLE_PYTHON_AUTOMATION
void Automation::stopPython()
{
    if (_python)
    {
        _python->stop();
        _python.reset();
    }
}
#endif

#if ENABLE_WEBAPI_AUTOMATION
void Automation::stopWebAPI()
{
    if (_webAPI)
    {
        _webAPI->stop();
        _webAPI.reset();
    }
}
#endif

#if ENABLE_CLI_AUTOMATION
bool Automation::startCLI()
{
    bool result = true;

    _cli = createAutomationCLI();
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
        _cli.reset();
    }
}
#endif

/// endregion </Helper methods>