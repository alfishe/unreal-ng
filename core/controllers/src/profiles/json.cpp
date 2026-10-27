#include "profiles/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace controllers
{

/// region <Values>

JsonValue JsonValue::Bool(bool value)
{
    JsonValue v;
    v._type = Type::Bool;
    v._bool = value;
    return v;
}

JsonValue JsonValue::Number(double value)
{
    JsonValue v;
    v._type = Type::Number;
    v._number = value;
    return v;
}

JsonValue JsonValue::String(std::string value)
{
    JsonValue v;
    v._type = Type::String;
    v._string = std::move(value);
    return v;
}

JsonValue JsonValue::Array(std::vector<JsonValue> items)
{
    JsonValue v;
    v._type = Type::Array;
    v._items = std::move(items);
    return v;
}

JsonValue JsonValue::Object(Members members)
{
    JsonValue v;
    v._type = Type::Object;
    v._members = std::move(members);
    return v;
}

const JsonValue* JsonValue::Find(const std::string& key) const
{
    for (const auto& [name, value] : _members)
    {
        if (name == key)
            return &value;
    }
    return nullptr;
}

void JsonValue::Set(const std::string& key, JsonValue value)
{
    _type = Type::Object;
    for (auto& [name, member] : _members)
    {
        if (name == key)
        {
            member = std::move(value);
            return;
        }
    }
    _members.emplace_back(key, std::move(value));
}

bool JsonValue::Remove(const std::string& key)
{
    for (auto it = _members.begin(); it != _members.end(); ++it)
    {
        if (it->first == key)
        {
            _members.erase(it);
            return true;
        }
    }
    return false;
}

namespace
{
void Quote(std::string& out, const std::string& text)
{
    out += '"';
    for (const char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                }
                else
                    out += c;
        }
    }
    out += '"';
}
}  // namespace

void JsonValue::DumpTo(std::string& out, int indent, int depth) const
{
    auto newline = [&](int level) {
        if (indent == 0)
            return;
        out += '\n';
        out.append(static_cast<size_t>(indent * level), ' ');
    };
    switch (_type)
    {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += _bool ? "true" : "false";
            break;
        case Type::Number:
        {
            char text[32];
            if (std::floor(_number) == _number && std::fabs(_number) < 1e15)
                std::snprintf(text, sizeof(text), "%.0f", _number);
            else
                std::snprintf(text, sizeof(text), "%.17g", _number);
            out += text;
            break;
        }
        case Type::String:
            Quote(out, _string);
            break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < _items.size(); i++)
            {
                if (i != 0)
                    out += ',';
                newline(depth + 1);
                _items[i].DumpTo(out, indent, depth + 1);
            }
            if (!_items.empty())
                newline(depth);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < _members.size(); i++)
            {
                if (i != 0)
                    out += ',';
                newline(depth + 1);
                Quote(out, _members[i].first);
                out += indent == 0 ? ":" : ": ";
                _members[i].second.DumpTo(out, indent, depth + 1);
            }
            if (!_members.empty())
                newline(depth);
            out += '}';
            break;
    }
}

std::string JsonValue::Dump(int indent) const
{
    std::string out;
    DumpTo(out, indent, 0);
    return out;
}

/// endregion </Values>

/// region <Parser>

namespace
{
class Parser
{
public:
    explicit Parser(const std::string& text) : _text(text)
    {
    }

    bool Parse(JsonValue& out, std::string& error)
    {
        bool parsed = Value(out, 0);
        Skip();
        if (parsed && _pos != _text.size())
            parsed = Fail("text after the value");
        if (!parsed)
            error = _error;
        return parsed;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool Fail(const std::string& reason)
    {
        size_t line = 1;
        size_t column = 1;
        for (size_t i = 0; i < _pos && i < _text.size(); i++)
        {
            if (_text[i] == '\n')
            {
                line++;
                column = 1;
            }
            else
                column++;
        }
        _error = "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + reason;
        return false;
    }

    void Skip()
    {
        while (_pos < _text.size() &&
               (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' || _text[_pos] == '\r'))
            _pos++;
    }

    bool Literal(const char* word)
    {
        const std::string expected(word);
        if (_text.compare(_pos, expected.size(), expected) != 0)
            return false;
        _pos += expected.size();
        return true;
    }

    bool Value(JsonValue& out, int depth)
    {
        if (depth > kMaxDepth)
            return Fail("nested too deep");
        Skip();
        if (_pos >= _text.size())
            return Fail("a value expected");
        const char c = _text[_pos];
        if (c == '{')
            return ObjectValue(out, depth);
        if (c == '[')
            return ArrayValue(out, depth);
        if (c == '"')
        {
            std::string text;
            if (!StringValue(text))
                return false;
            out = JsonValue::String(std::move(text));
            return true;
        }
        if (Literal("true"))
        {
            out = JsonValue::Bool(true);
            return true;
        }
        if (Literal("false"))
        {
            out = JsonValue::Bool(false);
            return true;
        }
        if (Literal("null"))
        {
            out = JsonValue();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return NumberValue(out);
        return Fail(std::string("unexpected '") + c + "'");
    }

    bool NumberValue(JsonValue& out)
    {
        const size_t start = _pos;
        if (_text[_pos] == '-')
            _pos++;
        auto digits = [this]() {
            const size_t from = _pos;
            while (_pos < _text.size() && _text[_pos] >= '0' && _text[_pos] <= '9')
                _pos++;
            return _pos > from;
        };
        if (!digits())
            return Fail("a digit expected");
        if (_pos < _text.size() && _text[_pos] == '.')
        {
            _pos++;
            if (!digits())
                return Fail("a digit expected after '.'");
        }
        if (_pos < _text.size() && (_text[_pos] == 'e' || _text[_pos] == 'E'))
        {
            _pos++;
            if (_pos < _text.size() && (_text[_pos] == '+' || _text[_pos] == '-'))
                _pos++;
            if (!digits())
                return Fail("a digit expected in the exponent");
        }
        out = JsonValue::Number(std::strtod(_text.substr(start, _pos - start).c_str(), nullptr));
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80)
            out += static_cast<char>(code);
        else if (code < 0x800)
        {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool Hex4(unsigned& code)
    {
        if (_pos + 4 > _text.size())
            return Fail("four hex digits expected");
        code = 0;
        for (int i = 0; i < 4; i++)
        {
            const char h = _text[_pos++];
            code <<= 4;
            if (h >= '0' && h <= '9')
                code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f')
                code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                code |= static_cast<unsigned>(h - 'A' + 10);
            else
                return Fail("a hex digit expected");
        }
        return true;
    }

    bool StringValue(std::string& out)
    {
        _pos++;  // the opening quote
        while (_pos < _text.size())
        {
            const char c = _text[_pos++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return Fail("a control character in a string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (_pos >= _text.size())
                break;
            const char e = _text[_pos++];
            switch (e)
            {
                case '"':
                case '\\':
                case '/':
                    out += e;
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u':
                {
                    unsigned code = 0;
                    if (!Hex4(code))
                        return false;
                    if (code >= 0xD800 && code < 0xDC00 && _text.compare(_pos, 2, "\\u") == 0)
                    {
                        _pos += 2;
                        unsigned low = 0;
                        if (!Hex4(low))
                            return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    return Fail(std::string("unknown escape '\\") + e + "'");
            }
        }
        return Fail("an unterminated string");
    }

    bool ArrayValue(JsonValue& out, int depth)
    {
        _pos++;
        std::vector<JsonValue> items;
        Skip();
        if (_pos < _text.size() && _text[_pos] == ']')
        {
            _pos++;
            out = JsonValue::Array();
            return true;
        }
        while (true)
        {
            JsonValue item;
            if (!Value(item, depth + 1))
                return false;
            items.push_back(std::move(item));
            Skip();
            if (_pos < _text.size() && _text[_pos] == ',')
            {
                _pos++;
                continue;
            }
            if (_pos < _text.size() && _text[_pos] == ']')
            {
                _pos++;
                out = JsonValue::Array(std::move(items));
                return true;
            }
            return Fail("',' or ']' expected");
        }
    }

    bool ObjectValue(JsonValue& out, int depth)
    {
        _pos++;
        JsonValue::Members members;
        Skip();
        if (_pos < _text.size() && _text[_pos] == '}')
        {
            _pos++;
            out = JsonValue::Object();
            return true;
        }
        while (true)
        {
            Skip();
            if (_pos >= _text.size() || _text[_pos] != '"')
                return Fail("a member name expected");
            std::string key;
            if (!StringValue(key))
                return false;
            for (const auto& member : members)
            {
                if (member.first == key)
                    return Fail("member '" + key + "' twice");
            }
            Skip();
            if (_pos >= _text.size() || _text[_pos] != ':')
                return Fail("':' expected");
            _pos++;
            JsonValue value;
            if (!Value(value, depth + 1))
                return false;
            members.emplace_back(std::move(key), std::move(value));
            Skip();
            if (_pos < _text.size() && _text[_pos] == ',')
            {
                _pos++;
                continue;
            }
            if (_pos < _text.size() && _text[_pos] == '}')
            {
                _pos++;
                out = JsonValue::Object(std::move(members));
                return true;
            }
            return Fail("',' or '}' expected");
        }
    }

    const std::string& _text;
    size_t _pos = 0;
    std::string _error;
};
}  // namespace

bool ParseJson(const std::string& text, JsonValue& out, std::string& error)
{
    Parser parser(text);
    JsonValue value;
    if (!parser.Parse(value, error))
        return false;
    out = std::move(value);
    return true;
}

/// endregion </Parser>

}  // namespace controllers
