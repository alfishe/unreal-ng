#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// @file statenode.h
/// @brief A small, dependency-free tree for device state reports.
///
/// Ground rule (2026-09-13): every device state that is useful for analysis
/// must be reachable from every automation interface - WebAPI, Python, Lua,
/// CLI and MCP. The core builds the report once as a StateNode tree
/// (see devicestate.h); each interface owns one generic converter
/// (StateNode -> Json::Value / sol::table / py::dict / text) and never
/// re-implements the report. Adding a new state means adding one builder
/// in the core and it is available everywhere.
///
/// The tree is deliberately plain: ordered object members (so text renders
/// and JSON keep the author's order), arrays, and scalar leaves.
struct StateNode
{
    enum class Kind
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        Object,
        Array
    };

    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<std::pair<std::string, StateNode>> members;  // Object
    std::vector<StateNode> items;                            // Array

    StateNode() = default;
    StateNode(bool v) : kind(Kind::Bool), b(v) {}
    StateNode(int v) : kind(Kind::Int), i(v) {}
    StateNode(unsigned v) : kind(Kind::Int), i(int64_t(v)) {}
    StateNode(int64_t v) : kind(Kind::Int), i(v) {}
    StateNode(uint64_t v) : kind(Kind::Int), i(int64_t(v)) {}
    StateNode(double v) : kind(Kind::Double), d(v) {}
    StateNode(const char* v) : kind(Kind::String), s(v) {}
    StateNode(std::string v) : kind(Kind::String), s(std::move(v)) {}

    static StateNode Object()
    {
        StateNode n;
        n.kind = Kind::Object;
        return n;
    }

    static StateNode Array()
    {
        StateNode n;
        n.kind = Kind::Array;
        return n;
    }

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }

    /// Object member access, created on first use (keeps insertion order)
    StateNode& operator[](const std::string& key)
    {
        if (kind != Kind::Object)
        {
            kind = Kind::Object;
            members.clear();
        }
        for (auto& m : members)
            if (m.first == key)
                return m.second;
        members.emplace_back(key, StateNode());
        return members.back().second;
    }

    const StateNode* find(const std::string& key) const
    {
        if (kind != Kind::Object)
            return nullptr;
        for (const auto& m : members)
            if (m.first == key)
                return &m.second;
        return nullptr;
    }

    /// Array append
    StateNode& push(StateNode value)
    {
        if (kind != Kind::Array)
        {
            kind = Kind::Array;
            items.clear();
        }
        items.push_back(std::move(value));
        return items.back();
    }

    size_t size() const { return kind == Kind::Array ? items.size() : (kind == Kind::Object ? members.size() : 0); }
};
