#include "automation.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <thread>

// Include actual implementation headers (needed in .cpp for instantiation)
#if ENABLE_LUA_AUTOMATION
#include "lua/src/automation-lua.h"
#endif

#if ENABLE_PYTHON_AUTOMATION
#include "python/src/automation-python.h"
#endif

#if ENABLE_WEBAPI_AUTOMATION
#include "webapi/src/automation-webapi.h"
#endif

#if ENABLE_CLI_AUTOMATION
#include "cli/include/automation-cli.h"
#endif

#if ENABLE_DEZOG_AUTOMATION
#include "dezog/include/automation-dezog.h"
#endif

#if ENABLE_ZESARUX_AUTOMATION
#include "zesarux/include/automation-zesarux.h"
#endif

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

/// region <Singleton management>

Automation& Automation::GetInstance()
{
    // Meyer's Singleton: thread-safe in C++11+, automatic lifetime management
    static Automation instance;
    return instance;
}

/// endregion </Singleton management>

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

#if ENABLE_DEZOG_AUTOMATION
    result &= startDezog();
#endif

#if ENABLE_ZESARUX_AUTOMATION
    result &= startZesarux();
#endif

    return result;
}

void Automation::stop()
{
    if (_stopped)
        return;

    _stopped = true;

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

#if ENABLE_DEZOG_AUTOMATION
    stopDezog();
#endif

#if ENABLE_ZESARUX_AUTOMATION
    stopZesarux();
#endif
}

std::string Automation::getEmulatorIdOrFirst(const std::string& providedId)
{
    if (!providedId.empty()) {
        return providedId;
    }
    
    EmulatorManager* mgr = EmulatorManager::GetInstance();
    if (!mgr) return "";
    
    std::vector<std::string> uuids = mgr->GetEmulatorIds();
    if (!uuids.empty()) {
        return uuids.front();
    }
    
    return "";
}

bool Automation::SetVideowallSingleSyncMode(bool enable, const std::string& emulatorId)
{
    std::string targetId = enable ? getEmulatorIdOrFirst(emulatorId) : "";
    if (enable && targetId.empty())
    {
        std::cerr << "Cannot enable single sync mode: no emulator found." << std::endl;
        return false;
    }
    
    // Post to message center - VideoWallWindow listens for this
    auto* payload = new VideowallSyncModePayload(targetId, enable);
    MessageCenter::DefaultMessageCenter().Post(NC_VIDEOWALL_SINGLE_SYNC_MODE, payload);
    
    return true;
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

#if ENABLE_DEZOG_AUTOMATION
bool Automation::startDezog()
{
    bool result = true;

    _dezog = new AutomationDezog();

    if (_dezog)
    {
        result = _dezog->start();
    }
    else
    {
        result = false;
    }

    return result;
}

void Automation::stopDezog()
{
    if (_dezog)
    {
        _dezog->stop();
        delete _dezog;
        _dezog = nullptr;
    }
}
#endif

#if ENABLE_ZESARUX_AUTOMATION
bool Automation::startZesarux()
{
    bool result = true;

    _zesarux = new AutomationZesarux();

    if (_zesarux)
    {
        result = _zesarux->start();
    }
    else
    {
        result = false;
    }

    return result;
}

void Automation::stopZesarux()
{
    if (_zesarux)
    {
        _zesarux->stop();
        delete _zesarux;
        _zesarux = nullptr;
    }
}
#endif

/// endregion </Helper methods>