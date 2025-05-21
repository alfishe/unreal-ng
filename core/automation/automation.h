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

// Conditionally include CLI automation if enabled
#if ENABLE_CLI_AUTOMATION
#include <cli/include/automation-cli.h>
#endif

class Automation
{
    /// region <Fields>
protected:
#if ENABLE_LUA_AUTOMATION
    std::unique_ptr<AutomationLua> _lua;
#endif

#if ENABLE_PYTHON_AUTOMATION
    std::unique_ptr<AutomationPython> _python;
#endif

#if ENABLE_WEBAPI_AUTOMATION
    std::unique_ptr<AutomationWebAPI> _webAPI;
#endif

#if ENABLE_CLI_AUTOMATION
    std::unique_ptr<AutomationCLI> _cli;
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
    bool startCLI();

    void stopLua();
    void stopPython();
    void stopWebAPI();
    void stopCLI();
    /// endregion </Helper methods>
};

