#pragma once

/// @file lua_vdac2.h
/// @brief sol2 (Lua) bindings for the TS-Conf VDAC2 card (FT812): the bus
/// capture to an .evr replay stream. Same surface as the Python bindings,
/// through Vdac2Control like every interface.
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/ (vdac2-test-corpus.md §4)
///
///   ok, err = vdac2_capture_start(path)
///   ok, err = vdac2_capture_stop()
///   t = vdac2_capture_status()   -- {capturing, path, bytes, selects, exchanges, frames,
///                                --  start_clock, last_clock} or {error = "..."}

#include <sol/sol.hpp>

#include <functional>
#include <string>
#include <tuple>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platforms/tsconf/vdac2control.h>

namespace LuaVdac2
{

inline void registerBindings(sol::state& lua, std::function<Emulator*()> getEmulator)
{
    lua.set_function("vdac2_capture_start", [getEmulator](const std::string& path) -> std::tuple<bool, std::string> {
        Emulator* emulator = getEmulator();
        if (!emulator)
            return {false, "No emulator selected"};
        std::string error;
        const bool ok = Vdac2Control::StartCapture(emulator->GetContext(), path, &error);
        return {ok, error};
    });

    lua.set_function("vdac2_capture_stop", [getEmulator]() -> std::tuple<bool, std::string> {
        Emulator* emulator = getEmulator();
        if (!emulator)
            return {false, "No emulator selected"};
        std::string error;
        const bool ok = Vdac2Control::StopCapture(emulator->GetContext(), &error);
        return {ok, error};
    });

    lua.set_function("vdac2_capture_status", [getEmulator](sol::this_state s) -> sol::table {
        sol::state_view view(s);
        sol::table t = view.create_table();
        Emulator* emulator = getEmulator();
        Vdac2Control::CaptureStatus status;
        std::string error = "No emulator selected";
        if (!emulator || !Vdac2Control::GetCaptureStatus(emulator->GetContext(), status, &error))
        {
            t["error"] = error;
            return t;
        }
        t["capturing"] = status.capturing;
        t["path"] = status.path;
        t["bytes"] = status.bytesWritten;
        t["selects"] = status.selects;
        t["exchanges"] = status.exchanges;
        t["frames"] = status.frames;
        t["start_clock"] = status.startClock;
        t["last_clock"] = status.lastClock;
        return t;
    });
}

} // namespace LuaVdac2
