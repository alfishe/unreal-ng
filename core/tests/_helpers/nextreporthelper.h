#pragma once

/// @file nextreporthelper.h
/// @brief Reading a StateNode report in tests by a dotted path ("a.address", "palettes.0.entries.2.rgb9"): a scalar comes back as
/// text (bool "on" / "off", integer in decimal, string as is), anything missing or not a scalar as an empty string.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "emulator/state/statenode.h"

namespace NextReportHelper
{
inline const StateNode* Find(const StateNode& node, const std::string& path)
{
    const StateNode* current = &node;
    size_t start = 0;
    while (current && start <= path.size())
    {
        const size_t dot = path.find('.', start);
        const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (current->isArray())
        {
            char* end = nullptr;
            const unsigned long index = std::strtoul(part.c_str(), &end, 10);
            current = (!part.empty() && *end == 0 && index < current->items.size()) ? &current->items[index] : nullptr;
        }
        else
            current = current->find(part);
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return current;
}

inline std::string Path(const StateNode& node, const std::string& path)
{
    const StateNode* n = Find(node, path);
    if (!n)
        return std::string();
    switch (n->kind)
    {
        case StateNode::Kind::Bool: return n->b ? "on" : "off";
        case StateNode::Kind::Int: return std::to_string(n->i);
        case StateNode::Kind::Double: return std::to_string(n->d);
        case StateNode::Kind::String: return n->s;
        default: return std::string();
    }
}

inline std::string Hex8(unsigned value)
{
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", value & 0xFFu);
    return text;
}
}  // namespace NextReportHelper
