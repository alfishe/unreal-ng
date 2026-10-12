#include "stdafx.h"

#include "emulator/bus/bustrace.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

/// region <Rings>

void BusTrace::EnableEvents(size_t capacity)
{
    if (!capacity)
        capacity = _events.Allocated() ? _events.Capacity() : kDefaultCapacity;
    if (!_events.Allocated() || _events.Capacity() != capacity)
        _events.Allocate(capacity);
}

void BusTrace::DisableEvents()
{
    _events.Free();
}

void BusTrace::EnableFindings()
{
    if (!_findings.Allocated())
        _findings.Allocate(kFindingCapacity);
}

void BusTrace::DisableFindings()
{
    _findings.Free();
}

void BusTrace::Clear()
{
    _events.Clear();
    _findings.Clear();
    _rewound = 0;
    std::fill(std::begin(_byRule), std::end(_byRule), 0);
}

uint64_t BusTrace::Append(BusTraceEvent event)
{
    if (!_events.Allocated())
        return _seq;
    // Back in time in the same epoch (a TTD seek, then the machine ran live): the later events never happen
    while (_events.Size() && Later(_events.Back(), event))
    {
        _events.PopBack();
        _rewound++;
    }
    event.seq = ++_seq;
    _events.Push(event);
    return event.seq;
}

uint64_t BusTrace::AddFinding(BusFinding finding)
{
    if (!_findings.Allocated())
        return 0;
    while (_findings.Size() && Later(_findings.Back(), finding))
        _findings.PopBack();
    finding.seq = ++_findingSeq;
    finding.eventSeq = _events.Allocated() ? _seq : 0;
    _findings.Push(finding);
    _byRule[static_cast<size_t>(finding.rule)]++;
    return finding.seq;
}

std::vector<BusTraceEvent> BusTrace::Events(const BusEventQuery& query) const
{
    std::vector<BusTraceEvent> out;
    _events.ForEach([&](const BusTraceEvent& e) {
        if (e.seq <= query.since)
            return;
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            return;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            return;
        if (query.slot >= 0 && e.slot != query.slot)
            return;
        if (!query.kinds.empty() && std::find(query.kinds.begin(), query.kinds.end(), e.kind) == query.kinds.end())
            return;
        out.push_back(e);
    });
    if (query.limit && out.size() > query.limit)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(query.limit));
    return out;
}

std::vector<BusFinding> BusTrace::Findings(const BusFindingQuery& query) const
{
    std::vector<BusFinding> out;
    _findings.ForEach([&](const BusFinding& f) {
        if (f.seq <= query.since)
            return;
        if (!query.rules.empty() && std::find(query.rules.begin(), query.rules.end(), f.rule) == query.rules.end())
            return;
        out.push_back(f);
    });
    if (query.limit && out.size() > query.limit)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(query.limit));
    return out;
}

/// endregion </Rings>

/// region <Queries from strings>

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

bool ParseCount(const std::string& text, const char* name, int64_t& value, std::string& error)
{
    char* end = nullptr;
    value = text.empty() ? -1 : std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || !end || *end != 0 || value < 0)
    {
        error = std::string(name) + ": '" + text + "' is not a non-negative number";
        return false;
    }
    return true;
}

std::string KindList()
{
    std::string list = "spi, sd, ata";
    for (size_t i = 0; i < static_cast<size_t>(BusEventKind::Count); i++)
        list += std::string(", ") + BusEventKindName(static_cast<BusEventKind>(i));
    return list;
}

std::string RuleList()
{
    std::string list;
    for (size_t i = 0; i < static_cast<size_t>(BusRule::Count); i++)
        list += std::string(i ? ", " : "") + BusRuleName(static_cast<BusRule>(i));
    return list;
}
}  // namespace

bool BusEventQueryFromStrings(const std::string& kinds, const std::string& slot, const std::string& since, const std::string& from,
                              const std::string& to, const std::string& limit, BusEventQuery& query, std::string& error)
{
    query = BusEventQuery{};
    for (const std::string& item : Split(kinds))
    {
        BusEventKind kind;
        if (item == "spi")
        {
            for (BusEventKind k : {BusEventKind::SpiSelect, BusEventKind::SpiDeselect, BusEventKind::SpiByte, BusEventKind::SpiEarly,
                                   BusEventKind::SpiTruncated})
                query.kinds.push_back(k);
        }
        else if (item == "ata")
        {
            for (BusEventKind k : {BusEventKind::AtaRegRead, BusEventKind::AtaRegWrite, BusEventKind::AtaCommand, BusEventKind::AtaData})
                query.kinds.push_back(k);
        }
        else if (item == "sd")
        {
            for (BusEventKind k : {BusEventKind::SdCommand, BusEventKind::SdReadBlock, BusEventKind::SdWriteBlock})
                query.kinds.push_back(k);
        }
        else if (BusEventKindFromName(item, kind))
            query.kinds.push_back(kind);
        else
        {
            error = "kinds: '" + item + "' is not one of " + KindList();
            return false;
        }
    }
    int64_t value = 0;
    if (!slot.empty())
    {
        if (!ParseCount(slot, "slot", value, error))
            return false;
        if (value > 255)
        {
            error = "slot: '" + slot + "' is not 0..255";
            return false;
        }
        query.slot = static_cast<int>(value);
    }
    if (!since.empty())
    {
        if (!ParseCount(since, "since", value, error))
            return false;
        query.since = static_cast<uint64_t>(value);
    }
    if (!from.empty())
    {
        if (!ParseCount(from, "from", value, error))
            return false;
        query.frameFrom = value;
    }
    if (!to.empty())
    {
        if (!ParseCount(to, "to", value, error))
            return false;
        query.frameTo = value;
    }
    if (!limit.empty())
    {
        if (!ParseCount(limit, "limit", value, error))
            return false;
        query.limit = static_cast<size_t>(value);
    }
    return true;
}

bool BusFindingQueryFromStrings(const std::string& rules, const std::string& since, const std::string& limit, BusFindingQuery& query,
                                std::string& error)
{
    query = BusFindingQuery{};
    for (const std::string& item : Split(rules))
    {
        BusRule rule;
        if (!BusRuleFromName(item, rule))
        {
            error = "rules: '" + item + "' is not one of " + RuleList();
            return false;
        }
        query.rules.push_back(rule);
    }
    int64_t value = 0;
    if (!since.empty())
    {
        if (!ParseCount(since, "since", value, error))
            return false;
        query.since = static_cast<uint64_t>(value);
    }
    if (!limit.empty())
    {
        if (!ParseCount(limit, "limit", value, error))
            return false;
        query.limit = static_cast<size_t>(value);
    }
    return true;
}

/// endregion </Queries from strings>
