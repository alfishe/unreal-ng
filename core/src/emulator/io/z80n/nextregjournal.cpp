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

void NextRegJournal::Record(uint32_t frame, uint32_t t, uint16_t pc, NextRegSource source, uint8_t reg, uint8_t value,
                            uint8_t previous, int16_t beamLine, int16_t beamHc)
{
    NextRegWriteEvent e;
    e.frame = frame;
    e.t = t;
    e.pc = pc;
    e.source = source;
    e.reg = reg;
    e.value = value;
    e.previous = previous;
    e.beamLine = beamLine;
    e.beamHc = beamHc;
    NextJournal::Record(e);
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

std::vector<NextRegWriteEvent> NextRegJournal::Query(const NextRegJournalQuery& query) const
{
    return Select(query.since, query.limit, [&query](const NextRegWriteEvent& e) {
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            return false;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            return false;
        if (!query.regs.empty() && std::find(query.regs.begin(), query.regs.end(), e.reg) == query.regs.end())
            return false;
        return query.sources.empty() || std::find(query.sources.begin(), query.sources.end(), e.source) != query.sources.end();
    });
}
