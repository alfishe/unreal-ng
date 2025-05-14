#pragma once

#if ENABLE_LUA_AUTOMATION
    #include <lua/src/automation-lua.h>
#endif

// Conditionally include Python automation if enabled
#if ENABLE_PYTHON_AUTOMATION
#include <python/src/automation-python.h>
#endif

// Conditionally include WebAPI automation if enabled
#if ENABLE_WEBAPI_AUTOMATION
#include <webapi/src/automation-webapi.h>
#endif

class Automation
{
    /// region <Fields>
protected:
#if ENABLE_LUA_AUTOMATION
    AutomationLua* _lua = nullptr;
#endif

#if ENABLE_PYTHON_AUTOMATION
    AutomationPython* _python = nullptr;
#endif

#if ENABLE_WEBAPI_AUTOMATION
    AutomationWebAPI* _webAPI = nullptr;
#endif
    /// endregion </Fields>

    /// region <Methods>
public:
    bool start();
    void stop();
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    bool startLua();
    bool startPython();
    bool startWebAPI();

    void stopLua();
    void stopPython();
    void stopWebAPI();
    /// endregion </Helper methods>
};

