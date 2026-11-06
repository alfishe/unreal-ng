#pragma once

/// @file lua_callbackhit.h
/// @brief A script callback's hit as a Lua table (docs/features/script-callbacks.md "The hit"): the script state's
/// deferred callbacks and the hook state's frame and sync callbacks get the same table.

#include <string>

#include <sol/sol.hpp>

#include "debugger/callbacks/scriptcallbacks.h"

inline sol::table LuaCallbackHitTable(sol::state_view lua, const CallbackHit& hit, const std::string& event)
{
    sol::table t = lua.create_table();
    t["frame"] = hit.frame;
    t["t"] = hit.t;
    t["pc"] = hit.pc;
    t["address"] = hit.address;
    if (hit.value >= 0)
        t["value"] = hit.value;
    t["breakpoint"] = hit.breakpointId;
    t["kind"] = CallbackHitKindName(hit.kind);
    t["cpu"] = DebugCpuName(static_cast<DebugCpuId>(hit.cpu));
    if (!event.empty())
        t["event"] = event;
    if (hit.arg >= 0)
        t["arg"] = hit.arg;
    sol::table regs = lua.create_table();
    regs["af"] = hit.af; regs["bc"] = hit.bc; regs["de"] = hit.de; regs["hl"] = hit.hl;
    regs["af_"] = hit.af2; regs["bc_"] = hit.bc2; regs["de_"] = hit.de2; regs["hl_"] = hit.hl2;
    regs["ix"] = hit.ix; regs["iy"] = hit.iy; regs["sp"] = hit.sp; regs["pc"] = hit.pc;
    regs["i"] = hit.i; regs["r"] = hit.r;
    regs["a"] = hit.af >> 8; regs["f"] = hit.af & 0xFF;
    t["regs"] = regs;
    return t;
}

/// A shared value (shared_get / shared_set) to Lua and back: nil, boolean, integer, number or string
inline sol::object LuaFromCallbackValue(sol::state_view lua, const CallbackValue& value)
{
    switch (value.kind)
    {
        case CallbackValue::Kind::Boolean: return sol::make_object(lua, value.b);
        case CallbackValue::Kind::Integer: return sol::make_object(lua, value.i);
        case CallbackValue::Kind::Number: return sol::make_object(lua, value.n);
        case CallbackValue::Kind::String: return sol::make_object(lua, value.s);
        case CallbackValue::Kind::Nil: break;
    }
    return sol::make_object(lua, sol::lua_nil);
}

/// False for a table, a function or userdata (only plain values cross between the states)
inline bool LuaToCallbackValue(const sol::object& object, CallbackValue& value)
{
    value = CallbackValue{};
    switch (object.get_type())
    {
        case sol::type::lua_nil:
        case sol::type::none: return true;
        case sol::type::boolean:
            value.kind = CallbackValue::Kind::Boolean;
            value.b = object.as<bool>();
            return true;
        case sol::type::number:
            {
                object.push();
                const bool integer = lua_isinteger(object.lua_state(), -1) != 0;
                lua_pop(object.lua_state(), 1);
                if (integer)
                {
                    value.kind = CallbackValue::Kind::Integer;
                    value.i = object.as<int64_t>();
                }
                else
                {
                    value.kind = CallbackValue::Kind::Number;
                    value.n = object.as<double>();
                }
            }
            return true;
        case sol::type::string:
            value.kind = CallbackValue::Kind::String;
            value.s = object.as<std::string>();
            return true;
        default: return false;
    }
}
