#pragma once

#include <json/json.h>

#include <cstdint>
#include <string>

/// Unsigned integer from a JSON request field: a JSON number, or a string in
/// decimal, "0x1234", "#1234" or "$1234". JSON has no hex literals, so hex
/// values (addresses, register values) can only arrive as strings.
/// @return false when the value is not a non-negative integer, a string has
///         any other character, or the value exceeds maxValue
inline bool ParseJsonUInt(const Json::Value& value, uint32_t maxValue, uint32_t& out)
{
    uint64_t parsed = 0;

    if (value.isBool())
        return false;

    if (value.isNumeric())
    {
        if (!value.isIntegral())
            return false;
        if (value.isInt64() && value.asInt64() < 0)
            return false;
        parsed = value.asUInt64();
    }
    else if (value.isString())
    {
        const std::string text = value.asString();
        unsigned base = 10;
        size_t pos = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            pos = 2;
        }
        else if (text.size() > 1 && (text[0] == '#' || text[0] == '$'))
        {
            base = 16;
            pos = 1;
        }
        if (pos >= text.size())
            return false;

        for (; pos < text.size(); pos++)
        {
            const char c = text[pos];
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned>(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else if (base == 16 && c >= 'A' && c <= 'F')
                digit = static_cast<unsigned>(c - 'A' + 10);
            else
                return false;

            parsed = parsed * base + digit;
            if (parsed > maxValue)
                return false;
        }
    }
    else
    {
        return false;
    }

    if (parsed > maxValue)
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}
