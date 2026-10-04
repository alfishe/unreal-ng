#include "emulator/io/serial/esp/zifiweather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Every rule here is the firmware's: ZiFi-ESP32-S3-Zero 2e5ba83 src/weather_parse.cpp (the parsers, the
// encodings) and src/weather_service.cpp (the paths). Numbers are read and written without the C library's
// locale-dependent strtod / printf("%f"): a host application may set a locale with a decimal comma.

namespace zifiweather
{
namespace
{
// --- A tiny JSON scan: keys at the top level of an object, other values skipped whole --------------------------

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

const char* SkipSpaces(const char* p, const char* end)
{
    while (p < end && IsSpace(*p))
        ++p;
    return p;
}

/// p at the opening quote; the position after the closing one
const char* SkipString(const char* p, const char* end)
{
    ++p;
    while (p < end)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '"')
            return p + 1;
        ++p;
    }
    return nullptr;
}

const char* SkipValue(const char* p, const char* end)
{
    p = SkipSpaces(p, end);
    if (p >= end)
        return nullptr;
    if (*p == '"')
        return SkipString(p, end);
    if (*p == '{' || *p == '[')
    {
        int depth = 0;
        while (p < end)
        {
            if (*p == '"')
            {
                p = SkipString(p, end);
                if (!p)
                    return nullptr;
                continue;
            }
            if (*p == '{' || *p == '[')
                ++depth;
            else if (*p == '}' || *p == ']')
            {
                if (--depth == 0)
                    return p + 1;
            }
            ++p;
        }
        return nullptr;
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']' && !IsSpace(*p))
        ++p;
    return p;
}

/// The value of `key` at the top level of the object [begin, end); nullptr = no such key
const char* FindKey(const char* begin, const char* end, const char* key)
{
    const size_t keyLength = std::strlen(key);
    const char* p = SkipSpaces(begin, end);
    if (p < end && *p == '{')
        ++p;
    while (true)
    {
        p = SkipSpaces(p, end);
        if (p >= end || *p != '"')
            return nullptr;
        const char* nameEnd = SkipString(p, end);
        if (!nameEnd)
            return nullptr;
        const bool match = static_cast<size_t>(nameEnd - p) == keyLength + 2 && std::memcmp(p + 1, key, keyLength) == 0;
        p = SkipSpaces(nameEnd, end);
        if (p >= end || *p != ':')
            return nullptr;
        p = SkipSpaces(p + 1, end);
        if (match)
            return p;
        p = SkipValue(p, end);
        if (!p)
            return nullptr;
        p = SkipSpaces(p, end);
        if (p < end && *p == ',')
            ++p;
    }
}

/// A JSON number (strtod's syntax for what JSON writes): sign, digits, fraction, exponent
bool ScanNumber(const char* p, const char* end, double& out, const char** next)
{
    const char* start = p;
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+'))
        negative = *p++ == '-';
    double mantissa = 0.0;
    int scale = 0;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9')
    {
        mantissa = mantissa * 10.0 + (*p++ - '0');
        digits = true;
    }
    if (p < end && *p == '.')
    {
        ++p;
        while (p < end && *p >= '0' && *p <= '9')
        {
            mantissa = mantissa * 10.0 + (*p++ - '0');
            --scale;
            digits = true;
        }
    }
    if (!digits)
    {
        (void)start;
        return false;
    }
    if (p < end && (*p == 'e' || *p == 'E'))
    {
        const char* e = p + 1;
        bool expNegative = false;
        if (e < end && (*e == '-' || *e == '+'))
            expNegative = *e++ == '-';
        int exponent = 0;
        bool expDigits = false;
        while (e < end && *e >= '0' && *e <= '9')
        {
            exponent = std::min(exponent * 10 + (*e++ - '0'), 400);
            expDigits = true;
        }
        if (expDigits)
        {
            scale += expNegative ? -exponent : exponent;
            p = e;
        }
    }
    // A negative power divides by an exact power of ten: 22.5 is 225 / 10, as strtod rounds it
    double value = mantissa;
    if (scale < 0)
        value = mantissa / std::pow(10.0, -scale);
    else if (scale > 0)
        value = mantissa * std::pow(10.0, scale);
    out = negative ? -value : value;
    *next = p;
    return true;
}

/// A number (also quoted); null reads as zero
bool ParseNumber(const char* p, const char* end, double& out, const char** next)
{
    p = SkipSpaces(p, end);
    if (p >= end)
        return false;
    bool quoted = false;
    if (*p == '"')
    {
        quoted = true;
        ++p;
    }
    if (end - p >= 4 && std::memcmp(p, "null", 4) == 0)
    {
        out = 0.0;
        p += 4;
    }
    else if (!ScanNumber(p, end, out, &p))
        return false;
    if (quoted)
    {
        if (p >= end || *p != '"')
            return false;
        ++p;
    }
    if (next)
        *next = p;
    return true;
}

void AppendUtf8(std::string& out, size_t capacity, uint32_t code)
{
    char buffer[4];
    size_t count;
    if (code < 0x80)
    {
        buffer[0] = static_cast<char>(code);
        count = 1;
    }
    else if (code < 0x800)
    {
        buffer[0] = static_cast<char>(0xC0 | (code >> 6));
        buffer[1] = static_cast<char>(0x80 | (code & 0x3F));
        count = 2;
    }
    else
    {
        buffer[0] = static_cast<char>(0xE0 | (code >> 12));
        buffer[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | (code & 0x3F));
        count = 3;
    }
    for (size_t i = 0; i < count && out.size() + 1 < capacity; ++i)
        out.push_back(buffer[i]);
}

/// A quoted string: \" \\ \/ and \uXXXX decoded (to UTF-8), \n \t \r as spaces; at most capacity - 1 bytes
bool ParseString(const char* p, const char* end, std::string& out, size_t capacity)
{
    p = SkipSpaces(p, end);
    if (p >= end || *p != '"' || capacity == 0)
        return false;
    ++p;
    out.clear();
    while (p < end && *p != '"')
    {
        char c = *p++;
        if (c == '\\' && p < end)
        {
            c = *p++;
            if (c == 'u' && end - p >= 4)
            {
                uint32_t code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char h = p[i];
                    const int v = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (v < 0)
                        break;   // strtoul stops at the first non-digit
                    code = code * 16 + static_cast<uint32_t>(v);
                }
                AppendUtf8(out, capacity, code);
                p += 4;
                continue;
            }
            if (c == 'n' || c == 't' || c == 'r')
                c = ' ';
        }
        if (out.size() + 1 < capacity)
            out.push_back(c);
    }
    return p < end;
}

/// A number array into out[0..max); the count read
size_t ParseNumberArray(const char* p, const char* end, double* out, size_t max)
{
    p = SkipSpaces(p, end);
    if (p >= end || *p != '[')
        return 0;
    ++p;
    size_t count = 0;
    while (true)
    {
        p = SkipSpaces(p, end);
        if (p >= end || *p == ']')
            return count;
        double value = 0.0;
        const char* next = nullptr;
        if (!ParseNumber(p, end, value, &next))
            return count;
        if (count < max)
            out[count] = value;
        ++count;
        p = SkipSpaces(next, end);
        if (p < end && *p == ',')
            ++p;
    }
}

bool ObjectNumber(const char* begin, const char* end, const char* key, double& out)
{
    const char* value = FindKey(begin, end, key);
    return value && ParseNumber(value, end, out, nullptr);
}

// --- The calendar ----------------------------------------------------------------------------------------------

/// The date of a day count from 1970-01-01 (Howard Hinnant's civil_from_days)
void CivilFromDays(int64_t z, int& year, unsigned& month, unsigned& day)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

struct LocalTime
{
    int year = 0;
    unsigned month = 0, day = 0;
    unsigned weekday = 0;   ///< 0 = Monday
    unsigned hour = 0, minute = 0;
};

LocalTime ToLocal(int64_t unixSeconds, int32_t offsetSeconds)
{
    const int64_t local = unixSeconds + offsetSeconds;
    int64_t days = local / 86400;
    int64_t seconds = local - days * 86400;
    if (seconds < 0)
    {
        seconds += 86400;
        --days;
    }
    LocalTime t;
    CivilFromDays(days, t.year, t.month, t.day);
    t.weekday = static_cast<unsigned>(((days + 3) % 7 + 7) % 7);   // 1970-01-01 was a Thursday
    t.hour = static_cast<unsigned>(seconds / 3600);
    t.minute = static_cast<unsigned>((seconds % 3600) / 60);
    return t;
}

uint8_t ClampU8(double v)
{
    const long r = std::lround(v);
    return static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
}

int8_t ClampI8(double v)
{
    const long r = std::lround(v);
    return static_cast<int8_t>(r < -128 ? -128 : (r > 127 ? 127 : r));
}

uint16_t ClampU16(double v)
{
    const long r = std::lround(v);
    return static_cast<uint16_t>(r < 0 ? 0 : (r > 65535 ? 65535 : r));
}

void PutU16(uint8_t* at, uint16_t v)
{
    at[0] = static_cast<uint8_t>(v);
    at[1] = static_cast<uint8_t>(v >> 8);
}

// --- Encodings -------------------------------------------------------------------------------------------------

/// Accented Latin (U+00C0..U+017F) to its ASCII base letter
char LatinBase(uint32_t code)
{
    static const struct
    {
        uint32_t first, last;
        char base;
    } kRanges[] = {
        {0xC0, 0xC5, 'A'},   {0xC7, 0xC7, 'C'},   {0xC8, 0xCB, 'E'},   {0xCC, 0xCF, 'I'},   {0xD1, 0xD1, 'N'},
        {0xD2, 0xD6, 'O'},   {0xD8, 0xD8, 'O'},   {0xD9, 0xDC, 'U'},   {0xDD, 0xDD, 'Y'},   {0xDF, 0xDF, 's'},
        {0xE0, 0xE5, 'a'},   {0xE7, 0xE7, 'c'},   {0xE8, 0xEB, 'e'},   {0xEC, 0xEF, 'i'},   {0xF1, 0xF1, 'n'},
        {0xF2, 0xF6, 'o'},   {0xF8, 0xF8, 'o'},   {0xF9, 0xFC, 'u'},   {0xFD, 0xFD, 'y'},   {0xFF, 0xFF, 'y'},
        {0x100, 0x105, 'a'}, {0x106, 0x10D, 'c'}, {0x10E, 0x111, 'd'}, {0x112, 0x11B, 'e'}, {0x11C, 0x123, 'g'},
        {0x124, 0x127, 'h'}, {0x128, 0x131, 'i'}, {0x134, 0x135, 'j'}, {0x136, 0x138, 'k'}, {0x139, 0x142, 'l'},
        {0x143, 0x149, 'n'}, {0x14C, 0x151, 'o'}, {0x154, 0x159, 'r'}, {0x15A, 0x161, 's'}, {0x162, 0x167, 't'},
        {0x168, 0x173, 'u'}, {0x174, 0x175, 'w'}, {0x176, 0x178, 'y'}, {0x179, 0x17E, 'z'},
    };
    for (const auto& range : kRanges)
    {
        if (code >= range.first && code <= range.last)
            return range.base;
    }
    return '?';
}

/// The signs outside А..я / Ё ё that CP866 can show (or an ASCII stand-in); 0 = none
char Cp866Extra(uint32_t code)
{
    static const struct
    {
        uint16_t code;
        uint8_t cp866;
    } kExtra[] = {
        {0x404, 0xF2},  {0x454, 0xF3},  {0x407, 0xF4},  {0x457, 0xF5},  {0x40E, 0xF6},  {0x45E, 0xF7},   // Є є Ї ї Ў ў
        {0x406, 'I'},   {0x456, 'i'},   {0x490, 0x83},  {0x491, 0xA3},                                   // І і Ґ ґ
        {0xB0, 0xF8},   {0xA0, ' '},                                                                     // ° nbsp
        {0x2BC, '\''},  {0x2018, '\''}, {0x2019, '\''}, {0x2013, '-'},  {0x2014, '-'},                   // ’ –
        {0xAB, '"'},    {0xBB, '"'},    {0x201C, '"'},  {0x201D, '"'},  {0x201E, '"'},
    };
    for (const auto& extra : kExtra)
    {
        if (code == extra.code)
            return static_cast<char>(extra.cp866);
    }
    return 0;
}

/// The Cyrillic letter of a CP866 code; 0 = no letter
uint32_t Cp866Letter(uint8_t code)
{
    static const uint16_t kF0[] = {0x401, 0x451, 0x404, 0x454, 0x407, 0x457, 0x40E, 0x45E};
    if (code >= 0x80 && code <= 0xAF)
        return 0x410 + (code - 0x80u);
    if (code >= 0xE0 && code <= 0xEF)
        return 0x440 + (code - 0xE0u);
    if (code >= 0xF0 && code <= 0xF7)
        return kF0[code - 0xF0];
    return 0;
}

/// The Cyrillic letter of a CP1251 code; 0 = no letter
uint32_t Cp1251Letter(uint8_t code)
{
    static const struct
    {
        uint8_t code;
        uint16_t letter;
    } kExtra[] = {
        {0xA8, 0x401}, {0xB8, 0x451}, {0xAA, 0x404}, {0xBA, 0x454}, {0xAF, 0x407}, {0xBF, 0x457},
        {0xB2, 0x406}, {0xB3, 0x456}, {0xA5, 0x490}, {0xB4, 0x491}, {0xA1, 0x40E}, {0xA2, 0x45E},
    };
    if (code >= 0xC0)
        return 0x410 + (code - 0xC0u);
    for (const auto& extra : kExtra)
    {
        if (code == extra.code)
            return extra.letter;
    }
    return 0;
}

/// The length of a valid UTF-8 sequence at p (1..4); 0 = not UTF-8 (a stray continuation, a cut, an overlong
/// form, a surrogate). `p` is NUL-terminated
size_t Utf8SequenceLength(const unsigned char* p)
{
    if (p[0] < 0x80)
        return 1;
    size_t count;
    if (p[0] >= 0xC2 && p[0] <= 0xDF)
        count = 2;
    else if (p[0] >= 0xE0 && p[0] <= 0xEF)
        count = 3;
    else if (p[0] >= 0xF0 && p[0] <= 0xF4)
        count = 4;
    else
        return 0;
    for (size_t i = 1; i < count; ++i)
    {
        if ((p[i] & 0xC0) != 0x80)
            return 0;
    }
    if ((p[0] == 0xE0 && p[1] < 0xA0) || (p[0] == 0xED && p[1] >= 0xA0) || (p[0] == 0xF0 && p[1] < 0x90) ||
        (p[0] == 0xF4 && p[1] >= 0x90))
        return 0;
    return count;
}

bool AppendEncoded(std::string& out, size_t capacity, const std::string& text)
{
    static const char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : text)
    {
        if (c == 0)
            break;
        const bool plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
        if (out.size() + (plain ? 1 : 3) >= capacity)
            return false;
        if (plain)
            out.push_back(static_cast<char>(c));
        else
        {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return true;
}

bool AppendText(std::string& out, size_t capacity, const char* text)
{
    if (out.size() + std::strlen(text) >= capacity)
        return false;
    out += text;
    return true;
}

/// printf("%.4f") of a coordinate, locale-free
std::string Fixed4(double value)
{
    const long long scaled = std::llround(value * 10000.0);
    const unsigned long long magnitude = static_cast<unsigned long long>(scaled < 0 ? -scaled : scaled);
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%s%llu.%04llu", scaled < 0 ? "-" : "", magnitude / 10000, magnitude % 10000);
    return buffer;
}
}  // namespace

std::string Utf8ToCp866(const std::string& text, size_t capacity)
{
    std::string out;
    if (capacity == 0)
        return out;
    const std::string terminated = text + std::string(4, '\0');   // the firmware reads p[1], p[2] past a cut
    const unsigned char* p = reinterpret_cast<const unsigned char*>(terminated.c_str());
    while (*p != 0 && out.size() + 1 < capacity)
    {
        uint32_t code;
        if (*p < 0x80)
            code = *p++;
        else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
        {
            code = ((*p & 0x1Fu) << 6) | (p[1] & 0x3Fu);
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
        {
            code = ((*p & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
            p += 3;
        }
        else
        {
            code = '?';
            ++p;
            while ((*p & 0xC0) == 0x80)
                ++p;
        }
        char c;
        if (code < 0x80)
            c = static_cast<char>(code);
        else if (code >= 0x410 && code <= 0x43F)
            c = static_cast<char>(0x80 + (code - 0x410));
        else if (code >= 0x440 && code <= 0x44F)
            c = static_cast<char>(0xE0 + (code - 0x440));
        else if (code == 0x401)
            c = static_cast<char>(0xF0);
        else if (code == 0x451)
            c = static_cast<char>(0xF1);
        else
        {
            c = Cp866Extra(code);
            if (c == 0)
                c = LatinBase(code);
        }
        out.push_back(c);
    }
    return out;
}

std::string IniTextToUtf8(const std::string& text)
{
    constexpr size_t kCapacity = 192;   // char name[kIniValueSize * 2]
    std::string out;
    const std::string terminated = text.substr(0, text.find('\0')) + std::string(4, '\0');
    const unsigned char* src = reinterpret_cast<const unsigned char*>(terminated.c_str());
    bool utf8 = true;
    for (const unsigned char* p = src; *p != 0;)
    {
        const size_t count = Utf8SequenceLength(p);
        if (count == 0)
        {
            utf8 = false;
            break;
        }
        p += count;
    }
    if (utf8)
    {
        for (const unsigned char* p = src; *p != 0;)
        {
            const size_t count = Utf8SequenceLength(p);
            if (out.size() + count >= kCapacity)
                break;
            out.append(reinterpret_cast<const char*>(p), count);
            p += count;
        }
        return out;
    }
    // One-byte Cyrillic: whichever of CP866 / CP1251 gives more letters (a tie is CP866)
    size_t letters866 = 0, letters1251 = 0;
    for (const unsigned char* p = src; *p != 0; ++p)
    {
        if (*p >= 0x80)
        {
            letters866 += Cp866Letter(*p) != 0 ? 1 : 0;
            letters1251 += Cp1251Letter(*p) != 0 ? 1 : 0;
        }
    }
    const bool cp1251 = letters1251 > letters866;
    for (const unsigned char* p = src; *p != 0; ++p)
    {
        uint32_t code = *p;
        if (code >= 0x80)
        {
            code = cp1251 ? Cp1251Letter(*p) : Cp866Letter(*p);
            if (code == 0)
                code = '?';
        }
        if (out.size() + (code < 0x80 ? 1 : 2) >= kCapacity)
            break;
        AppendUtf8(out, kCapacity, code);
    }
    return out;
}

bool CityPath(const std::string& city, const std::string& country, std::string& path)
{
    constexpr size_t kCapacity = 384;
    const std::string name = IniTextToUtf8(city);
    bool cyrillic = false;
    for (unsigned char c : name)
        cyrillic = cyrillic || (c >= 0xD0 && c <= 0xD3);   // the lead byte of U+0400..U+04FF
    path.clear();
    bool fits = AppendText(path, kCapacity, "/v1/search?name=") && AppendEncoded(path, kCapacity, name) &&
                AppendText(path, kCapacity, "&count=1&language=") && AppendText(path, kCapacity, cyrillic ? "ru" : "en") &&
                AppendText(path, kCapacity, "&format=json");
    if (fits && !country.empty())
        fits = AppendText(path, kCapacity, "&countryCode=") && AppendEncoded(path, kCapacity, country);
    return fits;
}

bool ZipPath(const std::string& country, const std::string& zip, std::string& path)
{
    constexpr size_t kCapacity = 96;
    path = "/";
    return AppendEncoded(path, kCapacity, country) && AppendText(path, kCapacity, "/") && AppendEncoded(path, kCapacity, zip);
}

std::string ForecastPath(float latitude, float longitude)
{
    return "/v1/forecast?latitude=" + Fixed4(static_cast<double>(latitude)) +
           "&longitude=" + Fixed4(static_cast<double>(longitude)) +
           "&current=temperature_2m,weather_code,is_day,wind_speed_10m,precipitation,surface_pressure"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,sunrise,sunset"
           "&timezone=auto&forecast_days=" +
           std::to_string(kMaxDays) + "&timeformat=unixtime";
}

bool ParseCitySearch(const std::string& json, GeoResult& out, bool& notFound, std::string& error)
{
    const char* begin = json.data();
    const char* end = begin + json.size();
    notFound = false;
    const char* p = SkipSpaces(begin, end);
    if (p >= end || *p != '{')
    {
        error = "city: broken json";
        return false;
    }
    const char* results = FindKey(begin, end, "results");
    if (!results)
    {
        notFound = true;   // nothing found: {"generationtime_ms":0.4}
        error = "city: not found";
        return false;
    }
    if (*results != '[')
    {
        error = "city: broken json";
        return false;
    }
    p = SkipSpaces(results + 1, end);
    if (p < end && *p == ']')
    {
        notFound = true;
        error = "city: not found";
        return false;
    }
    const char* placeEnd = (p < end && *p == '{') ? SkipValue(p, end) : nullptr;
    if (!placeEnd)
    {
        error = "city: broken json";
        return false;
    }
    double latitude = 0.0, longitude = 0.0;
    if (!ObjectNumber(p, placeEnd, "latitude", latitude) || !ObjectNumber(p, placeEnd, "longitude", longitude))
    {
        error = "city: no coordinates";
        return false;
    }
    GeoResult result;
    result.latitude = static_cast<float>(latitude);
    result.longitude = static_cast<float>(longitude);
    const char* name = FindKey(p, placeEnd, "name");
    if (!name || !ParseString(name, placeEnd, result.place, 64))
        result.place.clear();   // no name: the weather alone
    out = result;
    return true;
}

bool ParseZippopotam(const std::string& json, GeoResult& out, std::string& error)
{
    const char* begin = json.data();
    const char* end = begin + json.size();
    const char* places = FindKey(begin, end, "places");
    if (!places || *places != '[')
    {
        error = "zip: no places";
        return false;
    }
    const char* p = places + 1;
    bool found = false;
    while (true)
    {
        p = SkipSpaces(p, end);
        if (p >= end || *p != '{')
            break;
        const char* objectEnd = SkipValue(p, end);
        if (!objectEnd)
            break;
        GeoResult candidate;
        double latitude = 0.0, longitude = 0.0;
        const char* name = FindKey(p, objectEnd, "place name");
        if (ObjectNumber(p, objectEnd, "latitude", latitude) && ObjectNumber(p, objectEnd, "longitude", longitude) && name &&
            ParseString(name, objectEnd, candidate.place, 64))
        {
            candidate.latitude = static_cast<float>(latitude);
            candidate.longitude = static_cast<float>(longitude);
            out = candidate;   // the last place of the list stays
            found = true;
        }
        p = SkipSpaces(objectEnd, end);
        if (p < end && *p == ',')
            ++p;
    }
    if (!found)
        error = "zip: no coordinates";
    return found;
}

bool ParseOpenMeteo(const std::string& json, const std::string& placeUtf8, std::vector<uint8_t>& record, std::string& error)
{
    const char* begin = json.data();
    const char* end = begin + json.size();
    record.assign(kRecordSize, 0);
    record[kVersion] = kRecordVersion;

    double offset = 0.0;
    if (!ObjectNumber(begin, end, "utc_offset_seconds", offset))
    {
        error = "meteo: no utc offset";
        return false;
    }
    const int32_t offsetSeconds = static_cast<int32_t>(offset);
    const char* current = FindKey(begin, end, "current");
    const char* daily = FindKey(begin, end, "daily");
    if (!current || *current != '{' || !daily || *daily != '{')
    {
        error = "meteo: no current/daily";
        return false;
    }
    const char* currentEnd = SkipValue(current, end);
    const char* dailyEnd = SkipValue(daily, end);
    if (!currentEnd || !dailyEnd)
    {
        error = "meteo: broken json";
        return false;
    }
    double temperature, code, isDay, wind, precipitation, pressure, dataTime;
    if (!ObjectNumber(current, currentEnd, "temperature_2m", temperature) ||
        !ObjectNumber(current, currentEnd, "weather_code", code) || !ObjectNumber(current, currentEnd, "is_day", isDay) ||
        !ObjectNumber(current, currentEnd, "wind_speed_10m", wind) ||
        !ObjectNumber(current, currentEnd, "precipitation", precipitation) ||
        !ObjectNumber(current, currentEnd, "surface_pressure", pressure) || !ObjectNumber(current, currentEnd, "time", dataTime))
    {
        error = "meteo: current fields";
        return false;
    }
    double times[kMaxDays], codes[kMaxDays], tmax[kMaxDays], tmin[kMaxDays], sums[kMaxDays], sunrise[kMaxDays],
        sunset[kMaxDays];
    const struct
    {
        const char* key;
        double* out;
    } arrays[] = {{"time", times},
                  {"weather_code", codes},
                  {"temperature_2m_max", tmax},
                  {"temperature_2m_min", tmin},
                  {"precipitation_sum", sums},
                  {"sunrise", sunrise},
                  {"sunset", sunset}};
    size_t count = kMaxDays;
    for (const auto& array : arrays)
    {
        const char* value = FindKey(daily, dailyEnd, array.key);
        const size_t got = value ? ParseNumberArray(value, dailyEnd, array.out, kMaxDays) : 0;
        if (got == 0)
        {
            error = "meteo: daily fields";
            return false;
        }
        count = std::min(count, got);
    }

    record[kStatus] = 1;
    const std::string place = Utf8ToCp866(placeUtf8, kPlaceLength);
    std::memcpy(record.data() + kPlace, place.data(), place.size());
    record[kTemp] = static_cast<uint8_t>(ClampI8(temperature));
    record[kCode] = ClampU8(code);
    record[kIsDay] = isDay != 0.0 ? 1 : 0;
    PutU16(record.data() + kWind10, ClampU16(wind * 10.0));
    PutU16(record.data() + kPrecip10, ClampU16(precipitation * 10.0));
    PutU16(record.data() + kPressure, ClampU16(pressure * 0.750062));   // hPa to mm Hg
    const LocalTime rise = ToLocal(static_cast<int64_t>(sunrise[0]), offsetSeconds);
    const LocalTime set = ToLocal(static_cast<int64_t>(sunset[0]), offsetSeconds);
    const LocalTime now = ToLocal(static_cast<int64_t>(dataTime), offsetSeconds);
    record[kSunrise] = static_cast<uint8_t>(rise.hour);
    record[kSunrise + 1] = static_cast<uint8_t>(rise.minute);
    record[kSunset] = static_cast<uint8_t>(set.hour);
    record[kSunset + 1] = static_cast<uint8_t>(set.minute);
    record[kDataTime] = static_cast<uint8_t>(now.hour);
    record[kDataTime + 1] = static_cast<uint8_t>(now.minute);
    record[kDayCount] = static_cast<uint8_t>(count);
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t* day = record.data() + kDays + i * kDaySize;
        // Local noon of the day: the midnight's unixtime plus the offset is that date, noon keeps clear of the edge
        const LocalTime date = ToLocal(static_cast<int64_t>(times[i]) + 43200, offsetSeconds);
        day[0] = static_cast<uint8_t>(date.day);
        day[1] = static_cast<uint8_t>(date.month);
        day[2] = static_cast<uint8_t>(date.weekday);
        day[3] = ClampU8(codes[i]);
        day[4] = static_cast<uint8_t>(ClampI8(tmin[i]));
        day[5] = static_cast<uint8_t>(ClampI8(tmax[i]));
        PutU16(day + 6, ClampU16(sums[i] * 10.0));
    }
    return true;
}

}  // namespace zifiweather
