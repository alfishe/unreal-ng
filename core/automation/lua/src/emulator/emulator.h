#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>

class LuaEmulator
{
    /// region <Fields>
protected:
    Emulator* _emulator;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LuaEmulator() = default;
    virtual ~LuaEmulator() = default;
    /// endregion </Constructors / destructors>

    /// region <Lua SOL lifecycle>
public:
    void registerType(sol::state& lua)
    {
        lua.new_usertype<Emulator>(
          "Emulator",
          "start", &Emulator::Start,
          "stop", &Emulator::Stop,
          "pause", &Emulator::Pause,
          "resume", &Emulator::Resume,
          "is_running", &Emulator::IsRunning,
          "is_paused", &Emulator::IsPaused
        );

        lua["emulator"] = _emulator;
    }

    void unregisterType(sol::state& lua)
    {

    }
    /// endregion </Lua SOL lifecycle>

};