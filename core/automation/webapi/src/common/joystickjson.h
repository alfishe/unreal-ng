#pragma once

/// Kempston joystick WebAPI logic without Drogon: request parsing, the manager call, the state JSON and the
/// status / body of every reply. api/joystick_api.cpp only resolves the emulator and wraps the result in an
/// HTTP response, so core-tests can run the exact routes' behavior against a real emulator (joystick TDD §5,
/// JOY-13). Range checks and messages come from DebugJoystickManager, shared with every other surface.

#include <json/json.h>

#include <cstdint>
#include <limits>
#include <string>

#include "debugger/joystick/debugjoystickmanager.h"

namespace JoystickWeb
{

struct Reply
{
    int status = 200;  // HTTP status
    Json::Value body;
};

inline Reply Error(int status, const char* reason, const std::string& message)
{
    Reply reply;
    reply.status = status;
    reply.body["error"] = reason;
    reply.body["message"] = message;
    return reply;
}

inline Reply BadRequest(const std::string& message)
{
    return Error(400, "Bad Request", message);
}

inline Json::Value StateToJson(const JoystickStateSnapshot& state)
{
    Json::Value json;
    json["available"] = state.available;
    json["present"] = state.present;
    json["wired"] = state.wired;
    json["state"] = state.state;
    json["port_value"] = state.portValue;

    const std::vector<std::string> names = DebugJoystickManager::GetAllButtonNames();
    json["buttons"] = Json::Value(Json::objectValue);
    for (const std::string& name : names)
        json["buttons"][name] = false;
    json["pressed"] = Json::Value(Json::arrayValue);
    for (const std::string& name : state.buttons)
    {
        json["buttons"][name] = true;
        json["pressed"].append(name);
    }

    json["button_names"] = Json::Value(Json::arrayValue);
    for (const std::string& name : names)
        json["button_names"].append(name);

    json["keys"] = state.keys;
    if (state.pendingTapMask != 0)
    {
        json["pending_tap"]["mask"] = state.pendingTapMask;
        json["pending_tap"]["frames_left"] = state.pendingTapFramesLeft;
    }
    else
    {
        json["pending_tap"] = Json::Value(Json::nullValue);
    }
    return json;
}

/// GET /joystick: the state plus the warning the input verbs give
inline Reply StatusReply(const DebugJoystickManager& manager, const std::string& emulatorId)
{
    const JoystickStateSnapshot state = manager.GetState();
    Reply reply;
    reply.body = StateToJson(state);
    reply.body["emulator_id"] = emulatorId;
    if (!state.available)
        reply.body["warning"] = "Joystick device not available";
    else if (!state.present)
        reply.body["warning"] = "joystick not present: the guest reads 0x00 on the joystick port";
    else if (!state.wired)
        reply.body["warning"] = "this machine does not decode a Kempston joystick port: the guest cannot see the buttons";
    return reply;
}

/// Manager result -> reply. Success: `body` (the echoed input) plus success / message / warning / state.
inline Reply ResultReply(const DebugJoystickManager& manager, const JoystickInjectResult& result, Json::Value body)
{
    switch (result.status)
    {
        case JoystickInjectStatus::Ok:
            break;
        case JoystickInjectStatus::InvalidArgument:
            return BadRequest(result.message);
        case JoystickInjectStatus::ReplayActive:
            return Error(409, "Conflict", result.message);
        case JoystickInjectStatus::NoDevice:
        default:
            return Error(500, "Internal Error", result.message);
    }

    body["success"] = true;
    body["message"] = result.message;
    if (!result.warning.empty())
        body["warning"] = result.warning;
    body["state"] = StateToJson(manager.GetState());
    Reply reply;
    reply.body = body;
    return reply;
}

inline bool IsJsonInteger(const Json::Value& value)
{
    return value.type() == Json::intValue || value.type() == Json::uintValue;
}

/// Integer field (JSON int only: strings and floats rejected). Beyond int64 saturates, so the manager
/// reports it as out of range instead of it wrapping.
inline bool ReadInteger(const Json::Value& value, long long& out)
{
    if (!IsJsonInteger(value))
        return false;
    out = value.isInt64() ? static_cast<long long>(value.asInt64()) : std::numeric_limits<long long>::max();
    return true;
}

/// "buttons" (or "button"): a string ("up+fire", "up,fire") or an array of names -> one list string.
/// Returns false with `error` set when it is missing or has a wrong type.
inline bool ReadButtons(const Json::Value* json, std::string& names, std::string& error)
{
    names.clear();
    if (!json || !json->isObject())
    {
        error = "Missing 'buttons' field in request body";
        return false;
    }
    const Json::Value* field = json->isMember("buttons") ? &(*json)["buttons"] : (json->isMember("button") ? &(*json)["button"] : nullptr);
    if (!field)
    {
        error = "Missing 'buttons' field in request body";
        return false;
    }
    if (field->isString())
    {
        names = field->asString();
        return true;
    }
    if (field->isArray())
    {
        for (const Json::Value& item : *field)
        {
            if (!item.isString())
            {
                error = "'buttons' must be a string or an array of button names";
                return false;
            }
            names += (names.empty() ? "" : ",") + item.asString();
        }
        return true;
    }
    error = "'buttons' must be a string or an array of button names";
    return false;
}

/// POST /joystick/{verb}; verb: press | release | set | tap. `json` may be null (no / bad body).
inline Reply Handle(DebugJoystickManager& manager, const std::string& verb, const Json::Value* json)
{
    std::string names;
    std::string error;
    Json::Value echo(Json::objectValue);

    if (verb == "press" || verb == "release")
    {
        if (!ReadButtons(json, names, error))
            return BadRequest(error);
        echo["buttons"] = names;
        return ResultReply(manager, verb == "press" ? manager.Press(names) : manager.Release(names), echo);
    }

    if (verb == "tap")
    {
        if (!ReadButtons(json, names, error))
            return BadRequest(error);
        long long frames = DebugJoystickManager::DEFAULT_TAP_FRAMES;
        if (json->isMember("frames") && !ReadInteger((*json)["frames"], frames))
            return BadRequest("'frames' must be an integer");
        echo["buttons"] = names;
        echo["frames"] = static_cast<Json::Int64>(frames);
        return ResultReply(manager, manager.TapChecked(names, frames), echo);
    }

    if (verb == "set")
    {
        if (!json || !json->isObject() || (!json->isMember("state") && !json->isMember("buttons") && !json->isMember("button")))
            return BadRequest("Missing 'state' or 'buttons' field in request body");
        long long state = 0;
        if (json->isMember("state"))
        {
            if (!ReadInteger((*json)["state"], state))
                return BadRequest("'state' must be an integer");
        }
        else
        {
            if (!ReadButtons(json, names, error))
                return BadRequest(error);
            if (!names.empty())
            {
                const uint8_t mask = DebugJoystickManager::ResolveButtonNames(names);
                if (mask == 0)  // the manager words the unknown-name error
                    return ResultReply(manager, manager.Press(names), echo);
                state = mask;
            }
        }
        echo["requested_state"] = static_cast<Json::Int64>(state);  // "state" is the resulting-state object
        return ResultReply(manager, manager.SetStateChecked(state), echo);
    }

    return Error(404, "Not Found", "Unknown joystick operation '" + verb + "'");
}

}  // namespace JoystickWeb
