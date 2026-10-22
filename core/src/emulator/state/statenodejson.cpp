#include "stdafx.h"

#include "statenodejson.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    void WriteString(std::string& out, const std::string& text)
    {
        out.push_back('"');
        for (unsigned char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char escaped[8];
                        std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                        out += escaped;
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                    }
            }
        }
        out.push_back('"');
    }

    void Write(std::string& out, const StateNode& node)
    {
        switch (node.kind)
        {
            case StateNode::Kind::Null: out += "null"; break;
            case StateNode::Kind::Bool: out += node.b ? "true" : "false"; break;
            case StateNode::Kind::Int: out += std::to_string(node.i); break;
            case StateNode::Kind::Double:
                if (std::isfinite(node.d))
                {
                    char number[32];
                    std::snprintf(number, sizeof(number), "%.17g", node.d);
                    out += number;
                }
                else
                {
                    out += "null";  // JSON has no NaN / infinity
                }
                break;
            case StateNode::Kind::String: WriteString(out, node.s); break;
            case StateNode::Kind::Array:
                out.push_back('[');
                for (size_t i = 0; i < node.items.size(); i++)
                {
                    if (i)
                        out.push_back(',');
                    Write(out, node.items[i]);
                }
                out.push_back(']');
                break;
            case StateNode::Kind::Object:
                out.push_back('{');
                for (size_t i = 0; i < node.members.size(); i++)
                {
                    if (i)
                        out.push_back(',');
                    WriteString(out, node.members[i].first);
                    out.push_back(':');
                    Write(out, node.members[i].second);
                }
                out.push_back('}');
                break;
        }
    }
}  // namespace

std::string StateNodeToJsonText(const StateNode& node)
{
    std::string out;
    Write(out, node);
    return out;
}

namespace
{
    class JsonReader
    {
    public:
        explicit JsonReader(const std::string& text) : _text(text) {}

        bool Document(StateNode& out)
        {
            if (!Value(out, 0))
                return false;
            Space();
            if (_at != _text.size())
                return Fail("text after the value");
            return true;
        }
        const std::string& Error() const { return _error; }

    private:
        static constexpr int kMaxDepth = 64;

        bool Fail(const char* what)
        {
            if (_error.empty())
                _error = std::string(what) + " at " + std::to_string(_at);
            return false;
        }
        void Space()
        {
            while (_at < _text.size() &&
                   (_text[_at] == ' ' || _text[_at] == '\t' || _text[_at] == '\n' || _text[_at] == '\r'))
                ++_at;
        }
        bool Word(const char* word)
        {
            const size_t length = std::char_traits<char>::length(word);
            if (_text.compare(_at, length, word) != 0)
                return false;
            _at += length;
            return true;
        }
        bool Value(StateNode& out, int depth)
        {
            if (depth > kMaxDepth)
                return Fail("nested too deep");
            Space();
            if (_at >= _text.size())
                return Fail("a value expected");
            const char c = _text[_at];
            if (c == '{')
                return Object(out, depth);
            if (c == '[')
                return Array(out, depth);
            if (c == '"')
            {
                std::string s;
                if (!String(s))
                    return false;
                out = StateNode(std::move(s));
                return true;
            }
            if (Word("true"))
            {
                out = StateNode(true);
                return true;
            }
            if (Word("false"))
            {
                out = StateNode(false);
                return true;
            }
            if (Word("null"))
            {
                out = StateNode();
                return true;
            }
            return Number(out);
        }
        bool Object(StateNode& out, int depth)
        {
            out = StateNode::Object();
            ++_at;
            Space();
            if (_at < _text.size() && _text[_at] == '}')
            {
                ++_at;
                return true;
            }
            for (;;)
            {
                Space();
                std::string key;
                if (_at >= _text.size() || _text[_at] != '"' || !String(key))
                    return Fail("a member name expected");
                Space();
                if (_at >= _text.size() || _text[_at] != ':')
                    return Fail("':' expected");
                ++_at;
                StateNode value;
                if (!Value(value, depth + 1))
                    return false;
                out[key] = std::move(value);
                Space();
                if (_at < _text.size() && _text[_at] == ',')
                {
                    ++_at;
                    continue;
                }
                if (_at < _text.size() && _text[_at] == '}')
                {
                    ++_at;
                    return true;
                }
                return Fail("',' or '}' expected");
            }
        }
        bool Array(StateNode& out, int depth)
        {
            out = StateNode::Array();
            ++_at;
            Space();
            if (_at < _text.size() && _text[_at] == ']')
            {
                ++_at;
                return true;
            }
            for (;;)
            {
                StateNode value;
                if (!Value(value, depth + 1))
                    return false;
                out.push(std::move(value));
                Space();
                if (_at < _text.size() && _text[_at] == ',')
                {
                    ++_at;
                    continue;
                }
                if (_at < _text.size() && _text[_at] == ']')
                {
                    ++_at;
                    return true;
                }
                return Fail("',' or ']' expected");
            }
        }
        bool Hex4(uint32_t& value)
        {
            if (_at + 4 > _text.size())
                return false;
            value = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char h = _text[_at++];
                value <<= 4;
                if (h >= '0' && h <= '9')
                    value |= static_cast<uint32_t>(h - '0');
                else if (h >= 'a' && h <= 'f')
                    value |= static_cast<uint32_t>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    value |= static_cast<uint32_t>(h - 'A' + 10);
                else
                    return false;
            }
            return true;
        }
        static void Utf8(std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        bool String(std::string& out)
        {
            ++_at;  // the opening quote
            while (_at < _text.size())
            {
                const unsigned char c = static_cast<unsigned char>(_text[_at++]);
                if (c == '"')
                    return true;
                if (c < 0x20)
                    return Fail("a control character in a string");
                if (c != '\\')
                {
                    out.push_back(static_cast<char>(c));
                    continue;
                }
                if (_at >= _text.size())
                    break;
                const char e = _text[_at++];
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
                            return Fail("a bad \\u escape");
                        if (cp >= 0xD800 && cp < 0xDC00 && _text.compare(_at, 2, "\\u") == 0)
                        {
                            _at += 2;
                            uint32_t low = 0;
                            if (!Hex4(low) || low < 0xDC00 || low >= 0xE000)
                                return Fail("a bad surrogate pair");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        Utf8(out, cp);
                        break;
                    }
                    default: return Fail("a bad escape");
                }
            }
            return Fail("an unterminated string");
        }
        bool Number(StateNode& out)
        {
            const size_t start = _at;
            if (_at < _text.size() && _text[_at] == '-')
                ++_at;
            bool integral = true, digits = false;
            while (_at < _text.size())
            {
                const char c = _text[_at];
                if (c >= '0' && c <= '9')
                    digits = true;
                else if (c == '.' || c == 'e' || c == 'E' || ((c == '+' || c == '-') && integral == false))
                    integral = false;
                else
                    break;
                ++_at;
            }
            if (!digits)
                return Fail("a value expected");
            const std::string text = _text.substr(start, _at - start);
            if (integral)
            {
                errno = 0;
                const long long value = std::strtoll(text.c_str(), nullptr, 10);
                if (errno != ERANGE)
                {
                    out = StateNode(static_cast<int64_t>(value));
                    return true;
                }
            }
            out = StateNode(std::strtod(text.c_str(), nullptr));
            return true;
        }

        const std::string& _text;
        size_t _at = 0;
        std::string _error;
    };
} // namespace

bool StateNodeFromJsonText(const std::string& text, StateNode& out, std::string* error)
{
    JsonReader reader(text);
    if (reader.Document(out))
        return true;
    out = StateNode();
    if (error)
        *error = reader.Error();
    return false;
}
