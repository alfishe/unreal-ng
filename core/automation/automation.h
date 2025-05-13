#pragma once

#include <lua/src/automation-lua.h>
#include <python/src/automation-python.h>
#include <webapi/src/automation-webapi.h>

class Automation
{
    /// region <Fields>
protected:
    AutomationLua* _lua = nullptr;
    AutomationPython* _python = nullptr;
    AutomationWebAPI* _webAPI = nullptr;
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

