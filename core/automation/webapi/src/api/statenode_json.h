#pragma once

#include <json/json.h>

#include "emulator/state/statenode.h"

/// StateNode -> Json::Value. The one converter the WebAPI needs for every
/// DeviceState report (see core/src/emulator/state/devicestate.h).
inline Json::Value StateNodeToJson(const StateNode& node)
{
    switch (node.kind)
    {
        case StateNode::Kind::Bool:
            return Json::Value(node.b);
        case StateNode::Kind::Int:
            return Json::Value(Json::Int64(node.i));
        case StateNode::Kind::Double:
            return Json::Value(node.d);
        case StateNode::Kind::String:
            return Json::Value(node.s);
        case StateNode::Kind::Object:
        {
            Json::Value obj(Json::objectValue);
            for (const auto& m : node.members)
                obj[m.first] = StateNodeToJson(m.second);
            return obj;
        }
        case StateNode::Kind::Array:
        {
            Json::Value arr(Json::arrayValue);
            for (const auto& item : node.items)
                arr.append(StateNodeToJson(item));
            return arr;
        }
        default:
            return Json::Value(Json::nullValue);
    }
}
