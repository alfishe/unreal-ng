#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>

class LuaEmulator
{
    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
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
        // Register the Emulator class with Sol2
        lua.new_usertype<Emulator>(
          "Emulator",
          "start", &Emulator::Start,
          "stop", &Emulator::Stop,
          "pause", &Emulator::Pause,
          "resume", &Emulator::Resume,
          "is_running", &Emulator::IsRunning,
          "is_paused", &Emulator::IsPaused
        );

        // Set the emulator instance in the Lua environment
        lua["emulator"] = _emulator;
    }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>

};