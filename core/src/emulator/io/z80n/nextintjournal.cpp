#include "stdafx.h"

#include "nextintjournal.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

const char* NextIntKindName(NextIntKind kind)
{
    switch (kind)
    {
        case NextIntKind::IntAck: return "int_ack";
        case NextIntKind::Reti: return "reti";
        case NextIntKind::NmiRequest: return "nmi_request";
        case NextIntKind::Retn: return "retn";
        case NextIntKind::Reset: return "reset";
    }
    return "int_ack";
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

bool NextIntJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from, const std::string& to,
                                    const std::string& limit, NextIntJournalQuery& query, std::string& error)
{
    query = NextIntJournalQuery{};
    for (const std::string& item : Split(kinds))
    {
        bool found = false;
        for (uint8_t k = 0; k <= static_cast<uint8_t>(NextIntKind::Reset); k++)
            if (item == NextIntKindName(static_cast<NextIntKind>(k)))
            {
                query.kinds.push_back(static_cast<NextIntKind>(k));
                found = true;
            }
        if (!found)
        {
            error = "kinds: '" + item + "' is not one of int_ack, reti, nmi_request, retn, reset";
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

std::vector<NextIntEvent> NextIntJournal::Query(const NextIntJournalQuery& query) const
{
    return Select(query.since, query.limit, [&query](const NextIntEvent& e) {
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            return false;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            return false;
        return query.kinds.empty() || std::find(query.kinds.begin(), query.kinds.end(), e.kind) != query.kinds.end();
    });
}
