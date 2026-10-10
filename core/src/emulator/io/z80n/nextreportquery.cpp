#include "stdafx.h"

#include "nextreportquery.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A decimal number (or hex with 0x / #)
bool ParseCount(const std::string& text, uint32_t maxValue, uint32_t& value)
{
    std::string digits = text;
    int base = 10;
    if (!digits.empty() && digits[0] == '#')
        digits = digits.substr(1), base = 16;
    else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits = digits.substr(2), base = 16;
    if (digits.empty() || !std::isxdigit(static_cast<unsigned char>(digits[0])))
        return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(digits.c_str(), &end, base);
    if (!end || *end != 0 || parsed > maxValue)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}
}  // namespace

bool ParseNextHex(const std::string& text, uint32_t maxValue, uint32_t& value)
{
    std::string digits = Lower(text);
    const int base = 16;
    if (!digits.empty() && digits[0] == '#')
        digits = digits.substr(1);
    else if (digits.size() > 2 && digits[0] == '0' && digits[1] == 'x')
        digits = digits.substr(2);
    else if (digits.size() > 1 && digits.back() == 'h')
        digits.pop_back();
    if (digits.empty() || digits.size() > 8)
        return false;
    for (char c : digits)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    const unsigned long long parsed = std::strtoull(digits.c_str(), nullptr, base);
    if (parsed > maxValue)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseNextBool(const std::string& text, bool& value)
{
    const std::string t = Lower(text);
    if (t == "1" || t == "true" || t == "on" || t == "yes")
        value = true;
    else if (t == "0" || t == "false" || t == "off" || t == "no" || t.empty())
        value = false;
    else
        return false;
    return true;
}

const char* NextPaletteName(unsigned palette)
{
    static const char* kNames[8] = {"ula_1", "layer2_1", "sprites_1", "tilemap_1", "ula_2", "layer2_2", "sprites_2", "tilemap_2"};
    return kNames[palette & 7];
}

bool NextPaletteQueryFromStrings(const std::string& palette, const std::string& range, NextPaletteQuery& query, std::string& error)
{
    query = NextPaletteQuery{};
    const std::string p = Lower(palette);
    if (p.empty() || p == "selected")
        query.palette = NextPaletteQuery::kSelected;
    else if (p == "all")
        query.palette = NextPaletteQuery::kAll;
    else
    {
        query.palette = -2;
        for (unsigned i = 0; i < 8; i++)
            if (p == NextPaletteName(i))
                query.palette = static_cast<int>(i);
        if (query.palette == -2)
        {
            uint32_t number = 0;
            if (!ParseCount(p, 7, number))
            {
                error = "palette: '" + palette + "' is not 0-7, a palette name (ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 "
                        "tilemap_2), all or selected";
                return false;
            }
            query.palette = static_cast<int>(number);
        }
    }
    if (!range.empty())
    {
        const size_t dash = range.find('-');
        uint32_t first = 0, last = 0;
        const bool ok = dash == std::string::npos
                            ? (ParseCount(range, 255, first) && (last = first, true))
                            : (ParseCount(range.substr(0, dash), 255, first) && ParseCount(range.substr(dash + 1), 255, last));
        if (!ok || first > last)
        {
            error = "range: '" + range + "' is not N or A-B within 0-255 (A <= B)";
            return false;
        }
        query.first = first;
        query.last = last;
    }
    return true;
}

bool NextPortsQueryFromStrings(const std::string& port, const std::string& access, NextPortsQuery& query, std::string& error)
{
    query = NextPortsQuery{};
    const std::string a = Lower(access);
    if (a.empty() || a == "r" || a == "read" || a == "in")
        query.write = false;
    else if (a == "w" || a == "write" || a == "out")
        query.write = true;
    else
    {
        error = "access: '" + access + "' is not r / read or w / write";
        return false;
    }
    if (port.empty())
        return true;
    uint32_t number = 0;
    if (!ParseNextHex(port, 0xFFFF, number))
    {
        error = "port: '" + port + "' is not a port (hex 0-FFFF: 6B, 0x253B, #E3)";
        return false;
    }
    query.describe = true;
    query.port = static_cast<uint16_t>(number);
    return true;
}

bool NextRegReadQueryFromStrings(const std::string& reg, const std::string& changed, NextRegReadQuery& query, std::string& error)
{
    query = NextRegReadQuery{};
    if (!reg.empty())
    {
        uint32_t number = 0;
        if (!ParseNextHex(reg, 255, number))
        {
            error = "reg: '" + reg + "' is not a register number (hex 00-FF)";
            return false;
        }
        query.reg = static_cast<int>(number);
    }
    if (!ParseNextBool(changed, query.changed))
    {
        error = "changed: '" + changed + "' is not true / false";
        return false;
    }
    return true;
}

namespace
{
bool PageQuery(const std::string& from, const std::string& count, unsigned limit, const char* what, unsigned& first, unsigned& n,
               std::string& error)
{
    uint32_t value = 0;
    if (!from.empty())
    {
        if (!ParseCount(from, limit - 1, value))
        {
            error = "from: '" + from + "' is not a " + what + " number (0-" + std::to_string(limit - 1) + ")";
            return false;
        }
        first = value;
    }
    if (!count.empty())
    {
        if (!ParseCount(count, limit, value) || value == 0)
        {
            error = "count: '" + count + "' is not 1-" + std::to_string(limit);
            return false;
        }
        n = value;
    }
    return true;
}
}  // namespace

bool NextCopperQueryFromStrings(const std::string& from, const std::string& count, const std::string& raw, NextCopperQuery& query,
                                std::string& error)
{
    query = NextCopperQuery{};
    if (!PageQuery(from, count, 1024, "copper instruction", query.first, query.count, error))
        return false;
    if (!ParseNextBool(raw, query.raw))
    {
        error = "raw: '" + raw + "' is not true / false";
        return false;
    }
    return true;
}

bool NextSpritesQueryFromStrings(const std::string& from, const std::string& count, const std::string& all, NextSpritesQuery& query,
                                 std::string& error)
{
    query = NextSpritesQuery{};
    if (!PageQuery(from, count, 128, "sprite", query.first, query.count, error))
        return false;
    if (!ParseNextBool(all, query.all))
    {
        error = "all: '" + all + "' is not true / false";
        return false;
    }
    return true;
}
