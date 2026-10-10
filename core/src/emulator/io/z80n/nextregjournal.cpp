#include "stdafx.h"

#include "nextregjournal.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

const char* NextRegSourceName(NextRegSource source)
{
    switch (source)
    {
        case NextRegSource::NextReg: return "nextreg";
        case NextRegSource::Port: return "port";
        case NextRegSource::Copper: return "copper";
        default: return "internal";
    }
}

void NextRegJournal::SetCapacity(size_t capacity)
{
    _ring.assign(std::max<size_t>(capacity, 1), NextRegWriteEvent{});
    _head = 0;
    _count = 0;
    _evicted = 0;
}

void NextRegJournal::Clear()
{
    _head = 0;
    _count = 0;
    _evicted = 0;
}

void NextRegJournal::Record(uint32_t frame, uint32_t t, uint16_t pc, NextRegSource source, uint8_t reg, uint8_t value,
                            uint8_t previous)
{
    NextRegWriteEvent& e = _ring[_head];
    e.seq = ++_seq;
    e.frame = frame;
    e.t = t;
    e.pc = pc;
    e.source = source;
    e.reg = reg;
    e.value = value;
    e.previous = previous;
    _head = (_head + 1) % _ring.size();
    if (_count < _ring.size())
        _count++;
    else
        _evicted++;
}

std::vector<NextRegWriteEvent> NextRegJournal::Query(const NextRegJournalQuery& query) const
{
    std::vector<NextRegWriteEvent> out;
    const size_t first = (_head + _ring.size() - _count) % _ring.size();
    for (size_t i = 0; i < _count; i++)
    {
        const NextRegWriteEvent& e = _ring[(first + i) % _ring.size()];
        if (e.seq <= query.since)
            continue;
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            continue;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            continue;
        if (!query.regs.empty() && std::find(query.regs.begin(), query.regs.end(), e.reg) == query.regs.end())
            continue;
        if (!query.sources.empty() && std::find(query.sources.begin(), query.sources.end(), e.source) == query.sources.end())
            continue;
        out.push_back(e);
    }
    if (query.limit && out.size() > query.limit)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(query.limit));
    return out;
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

bool ParseNumber(const std::string& text, int64_t& value, int base)
{
    std::string digits = text;
    if (!digits.empty() && digits[0] == '#')
        digits = digits.substr(1), base = 16;
    else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits = digits.substr(2), base = 16;
    if (digits.empty())
        return false;
    char* end = nullptr;
    value = std::strtoll(digits.c_str(), &end, base);
    return end && *end == 0;
}
}  // namespace

bool NextRegJournalQueryFromStrings(const std::string& regs, const std::string& sources, const std::string& since,
                                    const std::string& from, const std::string& to, const std::string& limit,
                                    NextRegJournalQuery& query, std::string& error)
{
    query = NextRegJournalQuery{};
    for (const std::string& item : Split(regs))
    {
        int64_t value;
        if (!ParseNumber(item, value, 16) || value < 0 || value > 255)
        {
            error = "regs: '" + item + "' is not a register number (hex 00-FF)";
            return false;
        }
        query.regs.push_back(static_cast<uint8_t>(value));
    }
    for (const std::string& item : Split(sources))
    {
        if (item == "nextreg")
            query.sources.push_back(NextRegSource::NextReg);
        else if (item == "port")
            query.sources.push_back(NextRegSource::Port);
        else if (item == "copper")
            query.sources.push_back(NextRegSource::Copper);
        else if (item == "internal")
            query.sources.push_back(NextRegSource::Internal);
        else
        {
            error = "sources: '" + item + "' is not one of nextreg, port, copper, internal";
            return false;
        }
    }
    auto number = [&](const std::string& text, const char* name, int64_t& value) {
        if (text.empty())
            return true;
        if (!ParseNumber(text, value, 10) || value < 0)
        {
            error = std::string(name) + ": '" + text + "' is not a non-negative number";
            return false;
        }
        return true;
    };
    int64_t value = 0;
    if (!number(since, "since", value))
        return false;
    if (!since.empty())
        query.since = static_cast<uint64_t>(value);
    query.frameFrom = -1;
    if (!from.empty())
    {
        if (!number(from, "from", value))
            return false;
        query.frameFrom = value;
    }
    if (!to.empty())
    {
        if (!number(to, "to", value))
            return false;
        query.frameTo = value;
    }
    if (!limit.empty())
    {
        if (!number(limit, "limit", value))
            return false;
        query.limit = static_cast<size_t>(value);
    }
    return true;
}
