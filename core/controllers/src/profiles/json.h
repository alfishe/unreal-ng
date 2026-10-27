#pragma once

/// @file json.h
/// @brief The JSON the profile files need (mapping-and-profiles.md §7): objects keep their key order, numbers are
/// doubles, errors name the line and column. The library's own: rapidyaml is built into the core, whose symbols the
/// library does not use.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace controllers
{

class JsonValue
{
public:
    enum class Type : uint8_t
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };
    using Members = std::vector<std::pair<std::string, JsonValue>>;

    JsonValue() = default;
    static JsonValue Bool(bool value);
    static JsonValue Number(double value);
    static JsonValue String(std::string value);
    static JsonValue Array(std::vector<JsonValue> items = {});
    static JsonValue Object(Members members = {});

    Type GetType() const
    {
        return _type;
    }
    bool IsNull() const
    {
        return _type == Type::Null;
    }
    bool IsObject() const
    {
        return _type == Type::Object;
    }
    bool IsArray() const
    {
        return _type == Type::Array;
    }
    bool IsString() const
    {
        return _type == Type::String;
    }
    bool IsNumber() const
    {
        return _type == Type::Number;
    }
    bool IsBool() const
    {
        return _type == Type::Bool;
    }

    bool AsBool() const
    {
        return _bool;
    }
    double AsNumber() const
    {
        return _number;
    }
    const std::string& AsString() const
    {
        return _string;
    }
    const std::vector<JsonValue>& Items() const
    {
        return _items;
    }
    const Members& GetMembers() const
    {
        return _members;
    }

    /// An object's member, or nullptr
    const JsonValue* Find(const std::string& key) const;
    /// Set (replace in place, or append) an object's member
    void Set(const std::string& key, JsonValue value);
    /// Remove an object's member; false when there was none
    bool Remove(const std::string& key);

    /// Compact text when @p indent is 0, else pretty with that many spaces
    std::string Dump(int indent = 0) const;

private:
    void DumpTo(std::string& out, int indent, int depth) const;

    Type _type = Type::Null;
    bool _bool = false;
    double _number = 0;
    std::string _string;
    std::vector<JsonValue> _items;
    Members _members;
};

/// False with "line L, column C: reason"
bool ParseJson(const std::string& text, JsonValue& out, std::string& error);

}  // namespace controllers
