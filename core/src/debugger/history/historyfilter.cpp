#include "debugger/history/historyfilter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace history
{

namespace
{

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Decimal, or hex as #B3 / $B3 / 0xB3; the whole text must be the number
bool ParseNumber(const std::string& text, int64_t& value)
{
    size_t at = 0;
    int base = 10;
    if (!text.empty() && (text[0] == '#' || text[0] == '$'))
    {
        at = 1;
        base = 16;
    }
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        at = 2;
        base = 16;
    }
    if (at >= text.size() || text.size() - at > 15)
        return false;
    int64_t result = 0;
    for (; at < text.size(); at++)
    {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[at])));
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return false;
        result = result * base + digit;
    }
    value = result;
    return true;
}

}  // namespace

bool RecordFilter::FlagWord(const std::string& word, bool& value)
{
    if (word == "true" || word == "yes" || word == "on")
        value = true;
    else if (word == "false" || word == "no" || word == "off")
        value = false;
    else
        return false;
    return true;
}

bool RecordFilter::Parse(const std::string& text, RecordFilter& filter, std::string& error)
{
    static const char* kForm = " (a term is field=value[,value...], a value a number, a range a-b or a word)";
    RecordFilter parsed;
    std::istringstream words(text);
    std::string word;
    while (words >> word)
    {
        const size_t equals = word.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == word.size())
        {
            error = "filter: '" + word + "' is not field=value" + kForm;
            return false;
        }
        FilterTerm term;
        term.field = Lower(word.substr(0, equals));
        std::istringstream values(word.substr(equals + 1));
        std::string value;
        while (std::getline(values, value, ','))
        {
            if (value.empty())
            {
                error = "filter: '" + word + "' has an empty value" + kForm;
                return false;
            }
            FilterRange range;
            const size_t dash = value.find('-', 1);
            if (ParseNumber(value, range.lo))
            {
                range.hi = range.lo;
                term.numbers.push_back(range);
            }
            else if (dash != std::string::npos && ParseNumber(value.substr(0, dash), range.lo) &&
                     ParseNumber(value.substr(dash + 1), range.hi))
            {
                if (range.hi < range.lo)
                    std::swap(range.lo, range.hi);
                term.numbers.push_back(range);
            }
            else
            {
                term.words.push_back(Lower(value));
            }
        }
        parsed._text += (parsed._text.empty() ? "" : " ") + word;
        parsed._terms.push_back(std::move(term));
    }
    filter = std::move(parsed);
    return true;
}

bool RecordFilter::Matches(const StateNode& record) const
{
    for (const FilterTerm& term : _terms)
    {
        const StateNode* field = record.find(term.field);
        if (field == nullptr)
            return false;
        bool holds = false;
        if (field->kind == StateNode::Kind::Int || field->kind == StateNode::Kind::Bool)
        {
            const int64_t value = field->kind == StateNode::Kind::Int ? field->i : (field->b ? 1 : 0);
            for (const FilterRange& range : term.numbers)
                holds = holds || (value >= range.lo && value <= range.hi);
            if (field->kind == StateNode::Kind::Bool)
            {
                for (const std::string& word : term.words)
                {
                    bool flag = false;
                    holds = holds || (FlagWord(word, flag) && flag == field->b);
                }
            }
        }
        else if (field->kind == StateNode::Kind::String)
        {
            const std::string value = Lower(field->s);
            for (const std::string& word : term.words)
                holds = holds || word == value;
        }
        if (!holds)
            return false;
    }
    return true;
}

}  // namespace history
