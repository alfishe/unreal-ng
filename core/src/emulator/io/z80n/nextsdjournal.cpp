#include "stdafx.h"

#include "nextsdjournal.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

const char* NextSdKindName(NextSdKind kind)
{
    switch (kind)
    {
        case NextSdKind::ReadBlock: return "read";
        case NextSdKind::WriteBlock: return "write";
        default: return "command";
    }
}

std::string NextSdCommandName(uint8_t index, bool app)
{
    return std::string(app ? "ACMD" : "CMD") + std::to_string(index);
}

namespace
{
std::vector<std::string> Split(const std::string& text)
{
    std::vector<std::string> parts;
    std::string item;
    std::stringstream stream(text);
    while (std::getline(stream, item, ','))
        if (!item.empty())
            parts.push_back(item);
    return parts;
}

bool ParseNumber(const std::string& text, int64_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    value = std::strtoll(text.c_str(), &end, 10);
    return end && *end == 0;
}
}  // namespace

bool NextSdJournalQueryFromStrings(const std::string& kinds, const std::string& card, const std::string& since, const std::string& from,
                                   const std::string& to, const std::string& limit, NextSdJournalQuery& query, std::string& error)
{
    query = NextSdJournalQuery{};
    for (const std::string& item : Split(kinds))
    {
        if (item == "command")
            query.kinds.push_back(NextSdKind::Command);
        else if (item == "read")
            query.kinds.push_back(NextSdKind::ReadBlock);
        else if (item == "write")
            query.kinds.push_back(NextSdKind::WriteBlock);
        else
        {
            error = "kinds: '" + item + "' is not one of command, read, write";
            return false;
        }
    }
    int64_t value = 0;
    auto number = [&](const std::string& text, const char* name) {
        if (!ParseNumber(text, value) || value < 0)
        {
            error = std::string(name) + ": '" + text + "' is not a non-negative number";
            return false;
        }
        return true;
    };
    if (!card.empty())
    {
        if (!number(card, "card") || value > 1)
        {
            if (error.empty())
                error = "card: '" + card + "' is not 0 or 1";
            return false;
        }
        query.card = static_cast<int>(value);
    }
    if (!since.empty())
    {
        if (!number(since, "since"))
            return false;
        query.since = static_cast<uint64_t>(value);
    }
    if (!from.empty())
    {
        if (!number(from, "from"))
            return false;
        query.frameFrom = value;
    }
    if (!to.empty())
    {
        if (!number(to, "to"))
            return false;
        query.frameTo = value;
    }
    if (!limit.empty())
    {
        if (!number(limit, "limit"))
            return false;
        query.limit = static_cast<size_t>(value);
    }
    return true;
}

std::vector<NextSdEvent> NextSdJournal::Query(const NextSdJournalQuery& query) const
{
    return Select(query.since, query.limit, [&query](const NextSdEvent& e) {
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            return false;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            return false;
        if (query.card >= 0 && e.card != query.card)
            return false;
        return query.kinds.empty() || std::find(query.kinds.begin(), query.kinds.end(), e.kind) != query.kinds.end();
    });
}
