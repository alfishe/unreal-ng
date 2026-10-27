#include "dialects/common/macros.h"

#include <algorithm>
#include <cctype>

namespace unrealasm::dialects
{
bool NeedsExpansion(const std::vector<std::string>& body)
{
    for (const std::string& line : body)
        for (size_t k = 0; k + 1 < line.size(); ++k)
        {
            if (line[k] != '\\')
                continue;
            const char c = line[k + 1];
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (upper == 'C' || upper == 'N' || upper == 'S' || upper == 'P' || upper == 'R')
                return true;
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                const bool before = k > 0 && (std::isalnum(static_cast<unsigned char>(line[k - 1])) || line[k - 1] == '_');
                const bool after = k + 2 < line.size() && (std::isalnum(static_cast<unsigned char>(line[k + 2])) || line[k + 2] == '_' ||
                                                           line[k + 2] == '\\');
                if (before || after)
                    return true;
            }
        }
    return false;
}

size_t MacroArguments::FieldEnd(size_t from) const
{
    bool quote = false;
    for (size_t k = from; k < text.size(); ++k)
    {
        if (text[k] == '"')
            quote = !quote;
        else if (text[k] == ',' && !quote)
            return k;
    }
    return text.size();
}

std::string MacroArguments::Field(size_t index) const
{
    size_t start = std::min(pointer, text.size());
    for (size_t n = 0; n < index; ++n)
    {
        const size_t end = FieldEnd(start);
        if (end >= text.size())
            return {};
        start = end + 1;
    }
    return text.substr(start, FieldEnd(start) - start);
}

std::string Substitute(const std::string& line, MacroArguments& args)
{
    std::string out;
    for (size_t k = 0; k < line.size(); ++k)
    {
        if (line[k] != '\\' || k + 1 >= line.size())
        {
            out.push_back(line[k]);
            continue;
        }
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(line[k + 1])));
        ++k;
        if (std::isdigit(static_cast<unsigned char>(c)))
            out += args.Field(static_cast<size_t>(c - '0'));
        else if (c == 'P')
        {
            out += args.Field(0);
            const size_t end = args.FieldEnd(std::min(args.pointer, args.text.size()));
            args.pointer = end < args.text.size() ? end + 1 : args.text.size();
        }
        else if (c == 'R')
            args.pointer = 0;
        else if (c == 'C')
        {
            if (args.pointer < args.text.size())
                out.push_back(args.text[args.pointer]);
        }
        else if (c == 'N')
        {
            if (args.pointer < args.text.size())
                ++args.pointer;
        }
        else if (c == 'S' && k + 1 < line.size())
        {
            const char stop = line[++k];
            const size_t from = std::min(args.pointer, args.text.size());
            const size_t end = std::min(args.text.find(stop, from), args.text.size());
            out += args.text.substr(from, end - from);
        }
        else
        {
            out.push_back('\\');
            out.push_back(line[k]);   // not an operator: kept as written
        }
    }
    return out;
}
}  // namespace unrealasm::dialects
