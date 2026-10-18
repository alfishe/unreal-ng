#include "profiles/yaml.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <set>

#include "3rdparty/rapidyaml/ryml_all.hpp"

namespace controllers
{

namespace
{
/// rapidyaml's default error handler aborts the process; this one throws (per parse, not the global callbacks the
/// core sets for its own files)
struct YamlError : std::exception
{
    std::string message;
    size_t line = 0;
    size_t column = 0;
};

[[noreturn]] void ThrowYamlError(const char* message, size_t length, ryml::Location location, void*)
{
    YamlError error;
    error.message.assign(message, length);
    error.line = location.line;
    error.column = location.col;
    throw error;
}

std::string Text(ryml::csubstr text)
{
    return text.str == nullptr ? std::string() : std::string(text.str, text.len);
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// YAML 1.2 core schema: [-+]? ( digits ( . digits? )? | . digits ) ( [eE] [-+]? digits )?, 0x hex, 0o octal
bool PlainNumber(const std::string& text, double& out)
{
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'o'))
    {
        const int base = text[1] == 'x' ? 16 : 8;
        for (size_t i = 2; i < text.size(); i++)
            if (base == 16 ? !std::isxdigit(static_cast<unsigned char>(text[i])) : (text[i] < '0' || text[i] > '7'))
                return false;
        out = static_cast<double>(std::strtoull(text.c_str() + 2, nullptr, base));
        return true;
    }
    size_t i = 0;
    if (i < text.size() && (text[i] == '-' || text[i] == '+'))
        i++;
    size_t digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
    {
        i++;
        digits++;
    }
    if (i < text.size() && text[i] == '.')
    {
        i++;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        {
            i++;
            digits++;
        }
    }
    if (digits == 0)
        return false;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
    {
        i++;
        if (i < text.size() && (text[i] == '-' || text[i] == '+'))
            i++;
        size_t exponent = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        {
            i++;
            exponent++;
        }
        if (exponent == 0)
            return false;
    }
    if (i != text.size())
        return false;
    out = std::strtod(text.c_str(), nullptr);
    return true;
}

bool IsNullWord(const std::string& text)
{
    return text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL";
}

JsonValue Scalar(ryml::ConstNodeRef node)
{
    const std::string text = Text(node.val());
    if (node.is_val_quoted())
        return JsonValue::String(text);
    if (IsNullWord(text))
        return {};
    if (text == "true" || text == "True" || text == "TRUE")
        return JsonValue::Bool(true);
    if (text == "false" || text == "False" || text == "FALSE")
        return JsonValue::Bool(false);
    double number = 0;
    if (PlainNumber(text, number))
        return JsonValue::Number(number);
    return JsonValue::String(text);
}

JsonValue Convert(const ryml::Parser& parser, ryml::ConstNodeRef node)
{
    if (node.is_map())
    {
        JsonValue::Members members;
        std::set<std::string> seen;
        for (ryml::ConstNodeRef child : node.children())
        {
            const std::string key = Text(child.key());
            if (!seen.insert(key).second)
            {
                const ryml::Location location = parser.location(child);
                YamlError error;
                error.message = "duplicate key '" + key + "'";
                error.line = location.line;
                error.column = location.col;
                throw error;
            }
            members.emplace_back(key, Convert(parser, child));
        }
        return JsonValue::Object(std::move(members));
    }
    if (node.is_seq())
    {
        std::vector<JsonValue> items;
        for (ryml::ConstNodeRef child : node.children())
            items.push_back(Convert(parser, child));
        return JsonValue::Array(std::move(items));
    }
    if (node.has_val())
        return Scalar(node);
    return {};
}

bool IsMarker(const std::string& text, size_t lineStart, size_t lineEnd)
{
    if (lineEnd - lineStart < 3 || text.compare(lineStart, 3, "---") != 0)
        return false;
    return lineEnd - lineStart == 3 || text[lineStart + 3] == ' ' || text[lineStart + 3] == '\t' ||
           text[lineStart + 3] == '\r';
}

/// Blank lines, comments and document markers only
bool IsEmptyDocument(const std::string& text, size_t start, size_t end)
{
    size_t line = start;
    while (line < end)
    {
        size_t next = text.find('\n', line);
        next = next == std::string::npos || next > end ? end : next;
        size_t first = line;
        while (first < next && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r'))
            first++;
        if (first < next && text[first] != '#' && !IsMarker(text, line, next))
            return false;
        line = next + 1;
    }
    return true;
}

/// A string YAML (1.2, and 1.1 readers such as PyYAML) reads back as the same string without quotes
bool PlainSafe(const std::string& text)
{
    if (text.empty() || text.front() == ' ' || text.back() == ' ')
        return false;
    const unsigned char first = static_cast<unsigned char>(text.front());
    if (!std::isalnum(first) && first != '_' && first < 0x80)
        return false;
    for (const char c : text)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && u < 0x80 && c != ' ' && c != '_' && c != '.' && c != '/' && c != '+' && c != '-' &&
            c != '(' && c != ')')
            return false;
    }
    static const std::set<std::string> reserved = {"y", "n", "yes", "no", "on", "off", "true", "false", "null"};
    double number = 0;
    return reserved.count(Lower(text)) == 0 && !PlainNumber(text, number);
}

std::string String(const std::string& text)
{
    return PlainSafe(text) ? text : JsonValue::String(text).Dump();
}

std::string Number(double value)
{
    char text[32];
    if (std::floor(value) == value && std::fabs(value) < 1e15)
        std::snprintf(text, sizeof(text), "%.0f", value);
    else
        std::snprintf(text, sizeof(text), "%.15g", value);
    return text;
}

std::string Flow(const JsonValue& value)
{
    switch (value.GetType())
    {
        case JsonValue::Type::Null:
            return "null";
        case JsonValue::Type::Bool:
            return value.AsBool() ? "true" : "false";
        case JsonValue::Type::Number:
            return Number(value.AsNumber());
        case JsonValue::Type::String:
            return String(value.AsString());
        case JsonValue::Type::Array:
        {
            std::string out = "[";
            for (const JsonValue& item : value.Items())
                out += (out.size() > 1 ? ", " : "") + Flow(item);
            return out + "]";
        }
        case JsonValue::Type::Object:
        {
            if (value.GetMembers().empty())
                return "{}";
            std::string out = "{ ";
            bool first = true;
            for (const auto& [key, member] : value.GetMembers())
            {
                out += (first ? "" : ", ") + String(key) + ": " + Flow(member);
                first = false;
            }
            return out + " }";
        }
    }
    return "null";
}

bool HasContainers(const JsonValue& array)
{
    for (const JsonValue& item : array.Items())
        if (item.IsArray() || item.IsObject())
            return true;
    return false;
}

void Block(std::string& out, const JsonValue& object, int depth, int flowDepth)
{
    const std::string indent(static_cast<size_t>(depth * 2), ' ');
    for (const auto& [key, value] : object.GetMembers())
    {
        out += indent + String(key) + ":";
        if (depth + 1 < flowDepth && value.IsObject() && !value.GetMembers().empty())
        {
            out += "\n";
            Block(out, value, depth + 1, flowDepth);
        }
        else if (depth + 1 < flowDepth && value.IsArray() && HasContainers(value))
        {
            out += "\n";
            for (const JsonValue& item : value.Items())
                out += indent + "  - " + Flow(item) + "\n";
        }
        else
            out += " " + Flow(value) + "\n";
    }
}
}  // namespace

bool ParseYaml(const std::string& text, JsonValue& out, std::string& error)
{
    const ryml::Callbacks callbacks(nullptr, nullptr, nullptr, &ThrowYamlError);
    try
    {
        ryml::EventHandlerTree handler(callbacks);
        ryml::Parser parser(&handler, ryml::ParserOptions().locations(true));
        ryml::Tree tree(callbacks);
        ryml::parse_in_arena(&parser, ryml::to_csubstr(text), &tree);
        tree.resolve();
        ryml::ConstNodeRef root = tree.crootref();
        if (root.is_stream())
        {
            if (root.num_children() > 1)
            {
                error = "one YAML document is one profile; a bundle's documents are read one by one";
                return false;
            }
            if (root.num_children() == 0)
            {
                out = {};
                return true;
            }
            root = root.first_child();
        }
        out = Convert(parser, root);
        return true;
    }
    catch (const YamlError& e)
    {
        // rapidyaml counts lines from 0
        error = "line " + std::to_string(e.line + 1) + ", column " + std::to_string(e.column + 1) + ": " + e.message;
        return false;
    }
}

std::vector<std::pair<size_t, size_t>> SplitYamlDocuments(const std::string& text)
{
    std::vector<size_t> starts = {0};
    for (size_t line = 0; line < text.size();)
    {
        size_t next = text.find('\n', line);
        next = next == std::string::npos ? text.size() : next;
        if (line != 0 && IsMarker(text, line, next))
            starts.push_back(line);
        line = next + 1;
    }
    std::vector<std::pair<size_t, size_t>> documents;
    for (size_t i = 0; i < starts.size(); i++)
    {
        const size_t end = i + 1 < starts.size() ? starts[i + 1] : text.size();
        if (!IsEmptyDocument(text, starts[i], end))
            documents.emplace_back(starts[i], end - starts[i]);
    }
    return documents;
}

std::string DumpYaml(const JsonValue& value, int flowDepth)
{
    if (!value.IsObject() || value.GetMembers().empty() || flowDepth < 1)
        return Flow(value) + "\n";
    std::string out;
    Block(out, value, 0, flowDepth);
    return out;
}

}  // namespace controllers
