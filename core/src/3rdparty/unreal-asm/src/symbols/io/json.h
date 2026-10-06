#pragma once

// A small JSON reader and writer for the native symbol file (standard library only). Objects keep their member order;
// integers stay exact (int64), other numbers are doubles.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace unrealasm::symbols::json
{
struct Value
{
    enum class Type : uint8_t
    {
        Null,
        Bool,
        Integer,
        Real,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    int64_t integer = 0;
    double real = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    /// The member, or null when there is none (or this is no object)
    const Value* Get(std::string_view name) const;
    bool IsNumber() const { return type == Type::Integer || type == Type::Real; }
};

/// false with the reason and the byte offset when the text is no JSON document
bool Parse(std::string_view text, Value& out, std::string& error, size_t& errorOffset);
/// Compact JSON (no blanks)
std::string Write(const Value& value);
/// A JSON string literal of UTF-8 text (quotes and escapes)
std::string Quote(std::string_view text);
}  // namespace unrealasm::symbols::json
