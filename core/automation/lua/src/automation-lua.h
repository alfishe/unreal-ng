#pragma once

#include <thread>
#include <sol/sol.hpp>
#include "emulator/emulator.h"

class AutomationLua
{
    /// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;

    sol::state* _lua = nullptr;
    LuaEmulator* _luaEmulator = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationLua() = default;
    virtual ~AutomationLua()
    {
        if (_thread)
        {
            stop();

            delete _thread;
            _thread = nullptr;
        }
    }
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void start();
    void stop();
    /// endregion </Methods>

    /// region <Handlers>
protected:
    static void threadFunc(AutomationLua* lua);

    void onStart()
    {
        sol::state lua;
        // open some common libraries
        lua.open_libraries(sol::lib::base, sol::lib::package);
        lua.script("print('AutomationLua::onStart - lua test script executed!')");

    }

    void onFinish()
    {

    }

    /// endregion </Handlers>
};
