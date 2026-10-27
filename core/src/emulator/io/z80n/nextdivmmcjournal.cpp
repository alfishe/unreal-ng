#include "stdafx.h"

#include "nextdivmmcjournal.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

const char* NextDivMmcKindName(NextDivMmcKind kind)
{
    switch (kind)
    {
        case NextDivMmcKind::MapIn: return "map_in";
        case NextDivMmcKind::MapOut: return "map_out";
        case NextDivMmcKind::Retn: return "retn";
        case NextDivMmcKind::Disabled: return "disabled";
        case NextDivMmcKind::Reset: return "reset";
        case NextDivMmcKind::ProgramStart: return "program_start";
        case NextDivMmcKind::Port: return "port";
        case NextDivMmcKind::MapramClear: return "mapram_clear";
        case NextDivMmcKind::Button: return "button";
        case NextDivMmcKind::EsxCall: return "esx_call";
    }
    return "map_in";
}

/// The names of the tbblue firmware's src/asm/dot_commands/esxapi.def ($85-$95, $9A-$B1); a code it does not list is reported by number
const char* NextEsxCallName(uint8_t code)
{
    switch (code)
    {
        case 0x85: return "DISK_FILEMAP";
        case 0x86: return "DISK_STRMSTART";
        case 0x87: return "DISK_STRMEND";
        case 0x88: return "M_DOSVERSION";
        case 0x89: return "M_GETSETDRV";
        case 0x8B: return "M_TAPEIN";
        case 0x8C: return "M_TAPEOUT";
        case 0x8D: return "M_GETHANDLE";
        case 0x8E: return "M_GETDATE";
        case 0x8F: return "M_EXECCMD";
        case 0x90: return "M_AUTOLOAD";
        case 0x91: return "M_SETCAPS";
        case 0x92: return "M_DRVAPI";
        case 0x93: return "M_GETERR";
        case 0x94: return "M_P3DOS";
        case 0x95: return "M_ERRH";
        case 0x9A: return "F_OPEN";
        case 0x9B: return "F_CLOSE";
        case 0x9C: return "F_SYNC";
        case 0x9D: return "F_READ";
        case 0x9E: return "F_WRITE";
        case 0x9F: return "F_SEEK";
        case 0xA0: return "F_FGETPOS";
        case 0xA1: return "F_FSTAT";
        case 0xA2: return "F_FTRUNCATE";
        case 0xA3: return "F_OPENDIR";
        case 0xA4: return "F_READDIR";
        case 0xA5: return "F_TELLDIR";
        case 0xA6: return "F_SEEKDIR";
        case 0xA7: return "F_REWINDDIR";
        case 0xA8: return "F_GETCWD";
        case 0xA9: return "F_CHDIR";
        case 0xAA: return "F_MKDIR";
        case 0xAB: return "F_RMDIR";
        case 0xAC: return "F_STAT";
        case 0xAD: return "F_UNLINK";
        case 0xAE: return "F_TRUNCATE";
        case 0xAF: return "F_CHMOD";
        case 0xB0: return "F_RENAME";
        case 0xB1: return "F_GETFREE";
        default: return nullptr;
    }
}

const char* NextDivMmcEntryName(uint16_t address)
{
    if (address == 0x0066)
        return "nmi";
    if (address < 0x40 && (address & 7) == 0)
        return "rst";
    if ((address >> 8) == 0x3D)
        return "3dxx";
    return "rom3_routine";
}

void NextDivMmcJournal::SetCapacity(size_t capacity)
{
    _ring.assign(std::max<size_t>(capacity, 1), NextDivMmcEvent{});
    _head = 0;
    _count = 0;
    _evicted = 0;
}

void NextDivMmcJournal::Clear()
{
    _head = 0;
    _count = 0;
    _evicted = 0;
}

void NextDivMmcJournal::Record(NextDivMmcEvent event)
{
    event.seq = ++_seq;
    _ring[_head] = event;
    _head = (_head + 1) % _ring.size();
    if (_count < _ring.size())
        _count++;
    else
        _evicted++;
}

std::vector<NextDivMmcEvent> NextDivMmcJournal::Query(const NextDivMmcJournalQuery& query) const
{
    std::vector<NextDivMmcEvent> out;
    const size_t first = (_head + _ring.size() - _count) % _ring.size();
    for (size_t i = 0; i < _count; i++)
    {
        const NextDivMmcEvent& e = _ring[(first + i) % _ring.size()];
        if (e.seq <= query.since)
            continue;
        if (query.frameFrom >= 0 && e.frame < query.frameFrom)
            continue;
        if (query.frameTo >= 0 && e.frame > query.frameTo)
            continue;
        if (!query.kinds.empty() && std::find(query.kinds.begin(), query.kinds.end(), e.kind) == query.kinds.end())
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

bool ParseNumber(const std::string& text, int64_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    value = std::strtoll(text.c_str(), &end, 10);
    return end && *end == 0;
}
}  // namespace

bool NextDivMmcJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from, const std::string& to,
                                       const std::string& limit, NextDivMmcJournalQuery& query, std::string& error)
{
    query = NextDivMmcJournalQuery{};
    for (const std::string& item : Split(kinds))
    {
        bool found = false;
        for (uint8_t k = 0; k <= static_cast<uint8_t>(NextDivMmcKind::EsxCall); k++)
            if (item == NextDivMmcKindName(static_cast<NextDivMmcKind>(k)))
            {
                query.kinds.push_back(static_cast<NextDivMmcKind>(k));
                found = true;
            }
        if (!found)
        {
            error = "kinds: '" + item + "' is not one of map_in, map_out, retn, disabled, reset, program_start, port, mapram_clear, button, esx_call";
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
