#include "emulator/actions/actionparams.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace actions
{
namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

std::optional<std::string> ScalarText(const StateNode& node)
{
    switch (node.kind)
    {
        case StateNode::Kind::String:
            return node.s;
        case StateNode::Kind::Int:
            return std::to_string(node.i);
        case StateNode::Kind::Bool:
            return std::string(node.b ? "true" : "false");
        case StateNode::Kind::Double: {
            if (std::isfinite(node.d) && node.d == std::floor(node.d) && std::fabs(node.d) < 9.0e15)
                return std::to_string(static_cast<int64_t>(node.d));
            return std::to_string(node.d);
        }
        default:
            return std::nullopt;
    }
}
}  // namespace

std::optional<std::string> TextParam(const ActionValue& value, const char* name)
{
    const StateNode* node = value.params.find(name);
    return node != nullptr ? ScalarText(*node) : std::nullopt;
}

std::optional<int64_t> IntParam(const ActionValue& value, const char* name, std::string& error)
{
    const StateNode* node = value.params.find(name);
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return std::nullopt;
    if (node->kind == StateNode::Kind::Int)
        return node->i;
    if (node->kind == StateNode::Kind::Double && std::isfinite(node->d) && node->d == std::floor(node->d) &&
        std::fabs(node->d) < 9.0e15)
        return static_cast<int64_t>(node->d);
    if (node->kind == StateNode::Kind::String)
    {
        const std::string text = Trim(node->s);
        if (!text.empty())
        {
            errno = 0;
            char* end = nullptr;
            const long long parsed = std::strtoll(text.c_str(), &end, 0);  // 0: decimal, 0x hex
            if (errno == 0 && end != nullptr && *end == '\0')
                return static_cast<int64_t>(parsed);
        }
    }
    error = std::string(name) + ": expected an integer";
    return std::nullopt;
}

std::optional<double> NumberParam(const ActionValue& value, const char* name, std::string& error)
{
    const StateNode* node = value.params.find(name);
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return std::nullopt;
    if (node->kind == StateNode::Kind::Int)
        return static_cast<double>(node->i);
    if (node->kind == StateNode::Kind::Double && std::isfinite(node->d))
        return node->d;
    if (node->kind == StateNode::Kind::String)
    {
        const std::string text = Trim(node->s);
        char* end = nullptr;
        const double parsed = text.empty() ? 0.0 : std::strtod(text.c_str(), &end);
        if (!text.empty() && end != nullptr && *end == '\0' && std::isfinite(parsed))
            return parsed;
    }
    error = std::string(name) + ": expected a number";
    return std::nullopt;
}

std::optional<bool> BoolParam(const ActionValue& value, const char* name, std::string& error)
{
    const StateNode* node = value.params.find(name);
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return std::nullopt;
    if (node->kind == StateNode::Kind::Bool)
        return node->b;
    if (node->kind == StateNode::Kind::Int && (node->i == 0 || node->i == 1))
        return node->i == 1;
    if (node->kind == StateNode::Kind::String)
    {
        const std::string text = Lower(Trim(node->s));
        if (text == "true" || text == "on" || text == "yes" || text == "1")
            return true;
        if (text == "false" || text == "off" || text == "no" || text == "0")
            return false;
    }
    error = std::string(name) + ": expected true or false";
    return std::nullopt;
}

std::vector<std::string> ListParam(const ActionValue& value, const char* name)
{
    std::vector<std::string> out;
    const StateNode* node = value.params.find(name);
    if (node == nullptr)
        return out;
    if (node->kind == StateNode::Kind::Array)
    {
        for (const StateNode& item : node->items)
            if (std::optional<std::string> text = ScalarText(item); text && !Trim(*text).empty())
                out.push_back(Trim(*text));
        return out;
    }
    const std::optional<std::string> text = ScalarText(*node);
    if (!text)
        return out;
    size_t start = 0;
    while (start <= text->size())
    {
        const size_t stop = text->find_first_of("+,", start);
        const std::string part =
            Trim(text->substr(start, stop == std::string::npos ? std::string::npos : stop - start));
        if (!part.empty())
            out.push_back(part);
        if (stop == std::string::npos)
            break;
        start = stop + 1;
    }
    return out;
}

std::map<std::string, std::string> ParamOptions(const StateNode& params, std::initializer_list<const char*> skip)
{
    std::map<std::string, std::string> options;
    for (const auto& [key, node] : params.members)
    {
        if (std::find_if(skip.begin(), skip.end(), [&key = key](const char* name) { return key == name; }) !=
            skip.end())
            continue;
        if (std::optional<std::string> text = ScalarText(node))
            options[key] = *text;
    }
    return options;
}

std::string ErrorForStatus(int status)
{
    switch (status)
    {
        case 400:
            return "Bad Request";
        case 403:
            return "companion_forbidden";
        case 404:
            return "Not Found";
        case 501:
            return "Not Implemented";
        default:
            break;
    }
    if (status >= 500)
        return "Internal";
    return "Conflict";
}

}  // namespace actions
