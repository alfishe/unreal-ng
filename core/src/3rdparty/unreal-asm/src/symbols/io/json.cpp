#include "symbols/io/json.h"

#include <charconv>
#include <cstdio>
#include <locale>
#include <sstream>

#include "encoding/utf8.h"

namespace unrealasm::symbols::json
{
namespace
{
constexpr int kMaxDepth = 256;

class Parser
{
public:
    explicit Parser(std::string_view text) : _t(text) {}

    bool Document(Value& out)
    {
        Blank();
        if (!ParseValue(out, 0))
            return false;
        Blank();
        if (_p != _t.size())
            return Fail("text after the document");
        return true;
    }

    std::string error;
    size_t errorOffset = 0;

private:
    bool Fail(const char* what)
    {
        if (error.empty())
        {
            error = what;
            errorOffset = _p;
        }
        return false;
    }

    void Blank()
    {
        while (_p < _t.size() && (_t[_p] == ' ' || _t[_p] == '\t' || _t[_p] == '\n' || _t[_p] == '\r'))
            ++_p;
    }

    bool Literal(std::string_view word)
    {
        if (_t.substr(_p, word.size()) != word)
            return Fail("unknown word");
        _p += word.size();
        return true;
    }

    bool ParseValue(Value& v, int depth)
    {
        if (depth > kMaxDepth)
            return Fail("nested too deep");
        if (_p >= _t.size())
            return Fail("unexpected end");
        switch (_t[_p])
        {
            case '{': return ParseObject(v, depth);
            case '[': return ParseArray(v, depth);
            case '"':
                v.type = Value::Type::String;
                return ParseString(v.string);
            case 't':
                v.type = Value::Type::Bool;
                v.boolean = true;
                return Literal("true");
            case 'f':
                v.type = Value::Type::Bool;
                return Literal("false");
            case 'n':
                v.type = Value::Type::Null;
                return Literal("null");
            default: return ParseNumber(v);
        }
    }

    bool ParseObject(Value& v, int depth)
    {
        v.type = Value::Type::Object;
        ++_p;
        Blank();
        if (_p < _t.size() && _t[_p] == '}')
        {
            ++_p;
            return true;
        }
        for (;;)
        {
            Blank();
            if (_p >= _t.size() || _t[_p] != '"')
                return Fail("a member name expected");
            std::string name;
            if (!ParseString(name))
                return false;
            Blank();
            if (_p >= _t.size() || _t[_p] != ':')
                return Fail("':' expected");
            ++_p;
            Blank();
            Value member;
            if (!ParseValue(member, depth + 1))
                return false;
            v.object.emplace_back(std::move(name), std::move(member));
            Blank();
            if (_p < _t.size() && _t[_p] == ',')
            {
                ++_p;
                continue;
            }
            if (_p < _t.size() && _t[_p] == '}')
            {
                ++_p;
                return true;
            }
            return Fail("',' or '}' expected");
        }
    }

    bool ParseArray(Value& v, int depth)
    {
        v.type = Value::Type::Array;
        ++_p;
        Blank();
        if (_p < _t.size() && _t[_p] == ']')
        {
            ++_p;
            return true;
        }
        for (;;)
        {
            Blank();
            Value item;
            if (!ParseValue(item, depth + 1))
                return false;
            v.array.push_back(std::move(item));
            Blank();
            if (_p < _t.size() && _t[_p] == ',')
            {
                ++_p;
                continue;
            }
            if (_p < _t.size() && _t[_p] == ']')
            {
                ++_p;
                return true;
            }
            return Fail("',' or ']' expected");
        }
    }

    bool Hex4(uint32_t& out)
    {
        if (_p + 4 > _t.size())
            return Fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = _t[_p++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return Fail("bad \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& out)
    {
        ++_p;   // the opening quote
        for (;;)
        {
            if (_p >= _t.size())
                return Fail("unclosed string");
            const char c = _t[_p++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return Fail("a control character in a string");
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (_p >= _t.size())
                return Fail("unclosed string");
            const char e = _t[_p++];
            switch (e)
            {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!Hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp < 0xDC00)   // a surrogate pair
                    {
                        uint32_t low = 0;
                        if (_t.substr(_p, 2) != "\\u")
                            return Fail("a lone surrogate");
                        _p += 2;
                        if (!Hex4(low) || low < 0xDC00 || low >= 0xE000)
                            return Fail("a lone surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp < 0xE000)
                        return Fail("a lone surrogate");
                    utf8::Append(out, static_cast<char32_t>(cp));
                    break;
                }
                default: return Fail("bad escape");
            }
        }
    }

    bool ParseNumber(Value& v)
    {
        const size_t start = _p;
        if (_p < _t.size() && _t[_p] == '-')
            ++_p;
        bool real = false;
        while (_p < _t.size())
        {
            const char c = _t[_p];
            if (c >= '0' && c <= '9')
                ++_p;
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || (c == '-' && _p > start))
            {
                real = true;
                ++_p;
            }
            else
                break;
        }
        const char* first = _t.data() + start;
        const char* last = _t.data() + _p;
        if (first == last || (last - first == 1 && *first == '-'))
            return Fail("a value expected");
        if (!real)
        {
            const auto [end, ec] = std::from_chars(first, last, v.integer);
            if (ec == std::errc() && end == last)
            {
                v.type = Value::Type::Integer;
                return true;
            }
        }
        // The classic locale: a host application may have switched the C locale's decimal point
        std::istringstream in(std::string(first, last));
        in.imbue(std::locale::classic());
        in >> v.real;
        if (in.fail() || in.peek() != std::char_traits<char>::eof())
            return Fail("a bad number");
        v.type = Value::Type::Real;
        return true;
    }

    std::string_view _t;
    size_t _p = 0;
};

void WriteTo(const Value& v, std::string& out)
{
    switch (v.type)
    {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case Value::Type::Integer: out += std::to_string(v.integer); break;
        case Value::Type::Real:
        {
            // 17 digits round-trip a double; the classic locale keeps the decimal point (floating to_chars needs a newer
            // macOS than the deployment target)
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text.precision(17);
            text << v.real;
            out += text.str();
            break;
        }
        case Value::Type::String: out += Quote(v.string); break;
        case Value::Type::Array:
            out.push_back('[');
            for (size_t i = 0; i < v.array.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                WriteTo(v.array[i], out);
            }
            out.push_back(']');
            break;
        case Value::Type::Object:
            out.push_back('{');
            for (size_t i = 0; i < v.object.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                out += Quote(v.object[i].first);
                out.push_back(':');
                WriteTo(v.object[i].second, out);
            }
            out.push_back('}');
            break;
    }
}
}  // namespace

const Value* Value::Get(std::string_view name) const
{
    for (const auto& [key, value] : object)
        if (key == name)
            return &value;
    return nullptr;
}

bool Parse(std::string_view text, Value& out, std::string& error, size_t& errorOffset)
{
    Parser p(text);
    out = {};
    if (p.Document(out))
        return true;
    error = p.error;
    errorOffset = p.errorOffset;
    return false;
}

std::string Write(const Value& value)
{
    std::string out;
    WriteTo(value, out);
    return out;
}

std::string Quote(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                }
                else
                    out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}
}  // namespace unrealasm::symbols::json
