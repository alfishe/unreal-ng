// Format("{} #{} {:+d}", ...): the subset of std::format the library uses, without std::format itself (its floating
// point formatter needs macOS 13.3, the project targets 12). Spec: {[:][+][0][width][.precision][d|x|X|f]}.
// Integers, floating point, strings, chars and bools; an unknown spec or a missing argument throws.
#pragma once
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace sdb
{

namespace detail
{

struct FormatSpec
{
    bool plus = false;
    bool zero = false;
    int width = 0;
    int precision = -1;
    char type = 0;
};

inline FormatSpec ParseSpec(std::string_view s)
{
    FormatSpec f;
    size_t i = 0;
    if (i < s.size() && s[i] == '+')
    {
        f.plus = true;
        ++i;
    }
    if (i < s.size() && s[i] == '0')
    {
        f.zero = true;
        ++i;
    }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        f.width = f.width * 10 + (s[i++] - '0');
    if (i < s.size() && s[i] == '.')
    {
        f.precision = 0;
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            f.precision = f.precision * 10 + (s[i++] - '0');
    }
    if (i < s.size())
        f.type = s[i++];
    if (i != s.size() || (f.type && f.type != 'd' && f.type != 'x' && f.type != 'X' && f.type != 'f'))
        throw std::invalid_argument("Format: unsupported spec {:" + std::string(s) + "}");
    return f;
}

inline std::string Pad(std::string s, const FormatSpec& f, bool numeric)
{
    if (static_cast<int>(s.size()) >= f.width)
        return s;
    const size_t n = static_cast<size_t>(f.width) - s.size();
    if (f.zero && numeric)
    {
        const size_t at = (!s.empty() && (s[0] == '+' || s[0] == '-')) ? 1 : 0;
        s.insert(at, n, '0');
        return s;
    }
    return numeric ? std::string(n, ' ') + s : s + std::string(n, ' ');
}

template <typename T>
std::string FormatOne(const T& v, const FormatSpec& f)
{
    using U = std::decay_t<T>;
    char buf[64];
    if constexpr (std::is_same_v<U, bool>)
    {
        return Pad(v ? "true" : "false", f, false);
    }
    else if constexpr (std::is_same_v<U, char>)
    {
        return Pad(std::string(1, v), f, false);
    }
    else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>)
    {
        using W = std::conditional_t<std::is_signed_v<U>, long long, unsigned long long>;
        const W w = static_cast<W>(v);
        if (f.type == 'x' || f.type == 'X')
            std::snprintf(buf, sizeof buf, f.type == 'x' ? "%llx" : "%llX", static_cast<unsigned long long>(w));
        else if constexpr (std::is_signed_v<U>)
            std::snprintf(buf, sizeof buf, f.plus ? "%+lld" : "%lld", static_cast<long long>(w));
        else
            std::snprintf(buf, sizeof buf, f.plus ? "+%llu" : "%llu", static_cast<unsigned long long>(w));
        return Pad(buf, f, true);
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        if (f.precision >= 0)
            std::snprintf(buf, sizeof buf, f.plus ? "%+.*f" : "%.*f", f.precision, static_cast<double>(v));
        else
            std::snprintf(buf, sizeof buf, f.plus ? "%+g" : "%g", static_cast<double>(v));
        return Pad(buf, f, true);
    }
    else
    {
        return Pad(std::string(std::string_view(v)), f, false);
    }
}

inline void FormatTo(std::string& out, std::string_view fmt)
{
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        if ((fmt[i] == '{' || fmt[i] == '}') && i + 1 < fmt.size() && fmt[i + 1] == fmt[i])
            ++i;
        else if (fmt[i] == '{')
            throw std::invalid_argument("Format: more {} than arguments");
        out += fmt[i];
    }
}

template <typename T, typename... Rest>
void FormatTo(std::string& out, std::string_view fmt, const T& first, const Rest&... rest)
{
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        const char c = fmt[i];
        if ((c == '{' || c == '}') && i + 1 < fmt.size() && fmt[i + 1] == c)
        {
            out += c;
            ++i;
            continue;
        }
        if (c != '{')
        {
            out += c;
            continue;
        }
        const size_t close = fmt.find('}', i);
        if (close == std::string_view::npos)
            throw std::invalid_argument("Format: unclosed {");
        std::string_view spec = fmt.substr(i + 1, close - i - 1);
        if (!spec.empty() && spec[0] == ':')
            spec.remove_prefix(1);
        out += FormatOne(first, ParseSpec(spec));
        FormatTo(out, fmt.substr(close + 1), rest...);
        return;
    }
    throw std::invalid_argument("Format: more arguments than {}");
}

}  // namespace detail

template <typename... Args>
[[nodiscard]] std::string Format(std::string_view fmt, const Args&... args)
{
    std::string out;
    out.reserve(fmt.size() + 16 * sizeof...(Args));
    detail::FormatTo(out, fmt, args...);
    return out;
}

}  // namespace sdb
