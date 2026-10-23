#include "sdb/valueface.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "sdb/surfacereadouts.h"

namespace sdb::scn
{

namespace
{
struct Spec
{
    bool plus = false, zero = false;
    int width = 0, precision = -1;
    char type = 0;
};

struct Piece
{
    bool placeholder = false;
    std::string text;  // a literal; a placeholder's field
    bool cond = false;
    std::string yes, no;
    Spec spec;
};

// The pieces of a template; false with `error` saying why
bool Split(std::string_view f, std::vector<Piece>& out, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    std::string lit;
    for (size_t i = 0; i < f.size(); ++i)
    {
        const char c = f[i];
        if (c == '}')
        {
            if (i + 1 < f.size() && f[i + 1] == '}')
            {
                lit += '}';
                ++i;
                continue;
            }
            return fail("unbalanced '}' (\"}}\" is a brace)");
        }
        if (c != '{')
        {
            lit += c;
            continue;
        }
        if (i + 1 < f.size() && f[i + 1] == '{')
        {
            lit += '{';
            ++i;
            continue;
        }
        const size_t end = f.find('}', i + 1);
        if (end == std::string_view::npos)
            return fail("unbalanced '{' (\"{{\" is a brace)");
        const std::string_view body = f.substr(i + 1, end - i - 1);
        if (body.find('{') != std::string_view::npos)
            return fail("'{' inside {" + std::string(body) + "}");
        if (!lit.empty())
            out.push_back({false, lit, false, {}, {}, {}});
        lit.clear();
        Piece p;
        p.placeholder = true;
        const size_t q = body.find('?'), colon = body.find(':');
        if (q != std::string_view::npos && (colon == std::string_view::npos || q < colon))
        {
            p.text = std::string(body.substr(0, q));
            const std::string_view rest = body.substr(q + 1);
            const size_t bar = rest.find('|');
            if (bar == std::string_view::npos)
                return fail("{" + std::string(body) + "}: a condition is ?yes|no");
            p.cond = true;
            p.yes = std::string(rest.substr(0, bar));
            p.no = std::string(rest.substr(bar + 1));
        }
        else
        {
            p.text = std::string(body.substr(0, colon));
            if (colon != std::string_view::npos)
            {
                const std::string_view s = body.substr(colon + 1);
                size_t k = 0;
                if (k < s.size() && s[k] == '+')
                    p.spec.plus = true, ++k;
                if (k < s.size() && s[k] == '0')
                    p.spec.zero = true, ++k;
                while (k < s.size() && s[k] >= '0' && s[k] <= '9' && p.spec.width < 100)
                    p.spec.width = p.spec.width * 10 + (s[k++] - '0');
                if (k < s.size() && s[k] == '.')
                {
                    ++k;
                    if (k >= s.size() || s[k] < '0' || s[k] > '9')
                        return fail("{" + std::string(body) + "}: a precision is digits after '.'");
                    p.spec.precision = 0;
                    while (k < s.size() && s[k] >= '0' && s[k] <= '9' && p.spec.precision < 20)
                        p.spec.precision = p.spec.precision * 10 + (s[k++] - '0');
                }
                if (k < s.size())
                {
                    const std::string_view types = "fdxX%bk#s";
                    if (types.find(s[k]) == std::string_view::npos || k + 1 != s.size())
                        return fail("unknown spec ':" + std::string(s) + "' in {" + std::string(body) +
                                    "} (types f d x X % b k # s)");
                    p.spec.type = s[k];
                }
            }
        }
        for (char ch : p.text)
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '-'))
                return fail("{" + std::string(body) + "}: a field is a dotted name (fps, frame_time_us.avg)");
        out.push_back(std::move(p));
        i = end;
    }
    if (!lit.empty())
        out.push_back({false, lit, false, {}, {}, {}});
    return true;
}

std::string Printf(const char* fmt, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

std::string Fixed(double v, int precision)
{
    char fmt[16];
    std::snprintf(fmt, sizeof fmt, "%%.%df", std::clamp(precision, 0, 12));
    return Printf(fmt, v);
}

// 1.5K, 340M: one decimal below 10 unless the precision says otherwise
std::string Scaled(double v, double step, const char* const* units, int count, int precision)
{
    int u = 0;
    double a = std::abs(v);
    while (a >= step && u + 1 < count)
    {
        a /= step;
        v /= step;
        ++u;
    }
    const int p = precision >= 0 ? precision : (u == 0 || a >= 10 ? 0 : 1);
    return Fixed(v, p) + units[u];
}

bool Truthy(const json& v)
{
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number())
        return v.get<double>() != 0;
    if (v.is_string())
        return !v.get<std::string>().empty();
    if (v.is_array() || v.is_object())
        return !v.empty();
    return false;
}

std::string Render(const Piece& p, const json& value)
{
    const json v = p.text.empty() ? value : FieldOf(value, p.text);
    if (p.cond)
        return v.is_null() ? std::string("--") : Truthy(v) ? p.yes : p.no;
    if (v.is_null() && p.spec.type != '#')
        return "--";
    const Spec& s = p.spec;
    std::string out;
    if (s.type == '#')
        out = v.is_array() || v.is_object() ? std::to_string(v.size())
              : v.is_string()               ? std::to_string(v.get<std::string>().size())
                                            : std::string("--");
    else if (v.is_boolean() && s.type != 'd')
        out = v.get<bool>() ? "ON" : "OFF";
    else if (v.is_string())
        out = v.get<std::string>();
    else if (v.is_number() || v.is_boolean())
    {
        const double d = v.is_boolean() ? (v.get<bool>() ? 1.0 : 0.0) : v.get<double>();
        static const char* const kBytes[] = {"B", "K", "M", "G", "T"};
        static const char* const kSi[] = {"", "k", "M", "G", "T"};
        switch (s.type)
        {
            case 'f':
                out = Fixed(d, s.precision >= 0 ? s.precision : 6);
                break;
            case 'd':
                out = Printf("%.0f", std::round(d));
                break;
            case 'x':
            case 'X':
            {
                char buf[32];
                std::snprintf(buf, sizeof buf, s.type == 'x' ? "%llx" : "%llX",
                              static_cast<unsigned long long>(static_cast<long long>(std::llround(d))));
                out = buf;
                break;
            }
            case '%':
                out = Fixed(d * 100, s.precision >= 0 ? s.precision : 0) + "%";
                break;
            case 'b':
                out = Scaled(d, 1024, kBytes, 5, s.precision);
                break;
            case 'k':
                out = Scaled(d, 1000, kSi, 5, s.precision);
                break;
            default:
                if (s.precision >= 0)
                    out = Fixed(d, s.precision);
                else if (v.is_number_integer() || v.is_boolean() || d == std::floor(d))
                    out = Printf("%.0f", d);
                else
                {
                    out = Fixed(d, 2);
                    while (out.back() == '0')
                        out.pop_back();
                    if (out.back() == '.')
                        out.pop_back();
                }
        }
        if (s.plus && d >= 0)
            out = "+" + out;
    }
    else
        out = v.dump();
    if (static_cast<int>(out.size()) < s.width)
    {
        const size_t pad = static_cast<size_t>(s.width) - out.size();
        if (s.zero && (v.is_number() || v.is_boolean()))
        {
            const size_t sign = !out.empty() && (out[0] == '-' || out[0] == '+') ? 1 : 0;
            out.insert(sign, pad, '0');
        }
        else
            out.insert(0, pad, ' ');
    }
    return out;
}
}  // namespace

std::string CheckValueFormat(std::string_view format)
{
    std::vector<Piece> pieces;
    std::string error;
    return Split(format, pieces, &error) ? std::string() : error;
}

std::string FormatValue(std::string_view format, const json& value)
{
    std::vector<Piece> pieces;
    if (format.empty())
        format = "{}";
    if (!Split(format, pieces, nullptr))
        return std::string(format);
    std::string out;
    for (const Piece& p : pieces)
        out += p.placeholder ? Render(p, value) : p.text;
    return out;
}

std::optional<double> ValueNumber(std::string_view format, const json& value)
{
    std::vector<Piece> pieces;
    std::string field;
    if (Split(format, pieces, nullptr))
        for (const Piece& p : pieces)
            if (p.placeholder)
            {
                field = p.text;
                break;
            }
    const json v = field.empty() ? value : FieldOf(value, field);
    if (v.is_number())
        return v.get<double>();
    if (v.is_boolean())
        return v.get<bool>() ? 1.0 : 0.0;
    return std::nullopt;
}

int ValueLevel(const FaceValue& v, std::optional<double> number)
{
    if (!number || std::isnan(*number))
        return 0;
    const bool low = v.warn && v.alert && *v.warn > *v.alert;  // the low values are the bad ones
    auto hit = [&](double t) { return low ? *number <= t : *number >= t; };
    if (v.alert && hit(*v.alert))
        return 2;
    if (v.warn && hit(*v.warn))
        return 1;
    return 0;
}

ValueShown ShowValue(const FaceValue& v, const json& value)
{
    return {FormatValue(v.format, value), ValueLevel(v, ValueNumber(v.format, value))};
}

}  // namespace sdb::scn
