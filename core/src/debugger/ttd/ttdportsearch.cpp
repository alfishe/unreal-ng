#include "ttdportsearch.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <unordered_map>

#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/io/keyboard/keyboard.h"

namespace ttd
{

namespace
{

// 128K AY decoding: A15 and A1 select the chip, A14 register (1) or data (0)
constexpr uint16_t kAyMask = 0xC002;
constexpr uint16_t kAyRegisterPort = 0xC000;  // #FFFD: select / read
constexpr uint16_t kAyDataPort = 0x8000;      // #BFFD: write

bool IsAyRegisterPort(uint16_t port) { return (port & kAyMask) == kAyRegisterPort; }

/// The AY register a #FFFD write selects: 0-15; TurboSound / TSFM control
/// values (#F0-#FF) keep the selection; anything else selects no register
int SelectedRegister(uint8_t value, int current)
{
    if (value < 16)
        return value;
    if (value >= 0xF0)
        return current;
    return -1;
}

bool ValuePasses(const TTDPortQuery& q, uint8_t value)
{
    const uint8_t masked = value & q.valueMask;
    switch (q.valueMatch)
    {
        case TTDValueMatch::Any:
            return true;
        case TTDValueMatch::Equals:
            return masked == (q.value & q.valueMask);
        case TTDValueMatch::AnyBitClear:
            return masked != q.valueMask;
        case TTDValueMatch::AnyBitSet:
            return masked != 0;
    }
    return false;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ParseRegister(const std::string& arg, int& reg, std::string& err)
{
    if (arg.empty())
    {
        reg = -1;
        return true;
    }
    try
    {
        size_t used = 0;
        const int base = (arg.size() > 2 && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) ? 16 : 10;
        const long v = std::stol(arg, &used, base);
        if (used == arg.size() && v >= 0 && v <= 15)
        {
            reg = static_cast<int>(v);
            return true;
        }
    }
    catch (...)
    {
    }
    err = "AY register must be 0..15, got '" + arg + "'";
    return false;
}

}  // namespace

namespace
{
bool ParseNumber(const std::string& text, uint64_t max, uint64_t& out)
{
    std::string s = text;
    int base = 10;
    if (s.size() > 1 && (s[0] == '#' || s[0] == '$'))
    {
        s = s.substr(1);
        base = 16;
    }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s = s.substr(2);
        base = 16;
    }
    if (s.empty())
        return false;
    try
    {
        size_t used = 0;
        const unsigned long long v = std::stoull(s, &used, base);
        if (used != s.size() || v > max)
            return false;
        out = v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseTime(const std::string& text, TTDTimePoint& out)
{
    const size_t colon = text.find(':');
    uint64_t frame = 0;
    uint64_t t = 0;
    if (!ParseNumber(text.substr(0, colon), UINT64_MAX, frame))
        return false;
    if (colon != std::string::npos && !ParseNumber(text.substr(colon + 1), UINT32_MAX, t))
        return false;
    out = TTDTimePoint{frame, static_cast<uint32_t>(t)};
    return true;
}
}  // namespace

const char* PortDirectionName(TTDPortJournal::Direction d)
{
    return d == TTDPortJournal::Direction::Read ? "in" : "out";
}

bool ApplyPortQueryOption(TTDPortQuery& q, const std::string& rawName, const std::string& value, std::string& err)
{
    const std::string name = Lower(rawName);
    const std::string v = Lower(value);
    uint64_t n = 0;
    auto bad = [&](const std::string& what) {
        err = "'" + rawName + "': " + what + ", got '" + value + "'";
        return false;
    };
    if (name == "limit")
    {
        if (!ParseNumber(v, 1'000'000, n) || n == 0)
            return bad("1..1000000");
        q.limit = static_cast<size_t>(n);
        return true;
    }
    if (name == "newest" || name == "newest_first")
    {
        if (v != "true" && v != "false" && v != "1" && v != "0")
            return bad("true or false");
        q.newestFirst = v == "true" || v == "1";
        return true;
    }
    if (name == "from" || name == "to")
    {
        TTDTimePoint t;
        if (!ParseTime(v, t))
            return bad("a frame, or frame:T-state");
        if (name == "from")
            q.from = t;
        else
            q.to = t;
        return true;
    }
    if (name == "port" || name == "port_mask")
    {
        if (!ParseNumber(v, 0xFFFF, n))
            return bad("0..65535");
        if (name == "port")
        {
            q.portValue = static_cast<uint16_t>(n);
            if (q.portMask == 0)
                q.portMask = 0xFFFF;
        }
        else
        {
            q.portMask = static_cast<uint16_t>(n);
        }
        q.portValue &= q.portMask;
        return true;
    }
    if (name == "value" || name == "value_mask")
    {
        if (!ParseNumber(v, 0xFF, n))
            return bad("0..255");
        if (name == "value")
        {
            q.value = static_cast<uint8_t>(n);
            if (q.valueMatch == TTDValueMatch::Any)
                q.valueMatch = TTDValueMatch::Equals;
        }
        else
        {
            q.valueMask = static_cast<uint8_t>(n);
        }
        return true;
    }
    if (name == "match")
    {
        if (v == "any")
            q.valueMatch = TTDValueMatch::Any;
        else if (v == "equals")
            q.valueMatch = TTDValueMatch::Equals;
        else if (v == "any-clear")
            q.valueMatch = TTDValueMatch::AnyBitClear;
        else if (v == "any-set")
            q.valueMatch = TTDValueMatch::AnyBitSet;
        else
            return bad("any, equals, any-clear or any-set");
        return true;
    }
    if (name == "trigger")
    {
        if (v == "every")
            q.trigger = TTDPortTrigger::Every;
        else if (v == "rising")
            q.trigger = TTDPortTrigger::Rising;
        else if (v == "change")
            q.trigger = TTDPortTrigger::Change;
        else
            return bad("every, rising or change");
        return true;
    }
    if (name == "stream_mask")
    {
        if (!ParseNumber(v, 0xFFFF, n))
            return bad("0..65535");
        q.streamMask = static_cast<uint16_t>(n);
        return true;
    }
    if (name == "ay_register")
    {
        if (!ParseNumber(v, 15, n))
            return bad("0..15");
        q.ayRegister = static_cast<int>(n);
        return true;
    }
    err = "unknown option '" + rawName + "'";
    return false;
}

const std::vector<std::string>& PortEventNames()
{
    static const std::vector<std::string> names = {"key",       "ear",    "ay-read", "ay-write", "ay-select",
                                                   "border",    "beeper", "in",      "out"};
    return names;
}

bool BuildPortEventQuery(const std::string& eventName, const std::string& arg, TTDPortQuery& q, std::string& err)
{
    const std::string event = Lower(eventName);
    using Dir = TTDPortJournal::Direction;

    if (event == "key")
    {
        q.direction = Dir::Read;
        q.portMask = 0x0001;  // the ULA answers every even port
        q.portValue = 0;
        q.valueMatch = TTDValueMatch::AnyBitClear;
        q.valueMask = 0x1F;  // the five key bits of a half-row
        q.trigger = TTDPortTrigger::Rising;
        if (!arg.empty())
        {
            const ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(arg);
            const KeyDescriptor* found = nullptr;
            for (const KeyDescriptor& d : Keyboard::_keys)
            {
                if (key != ZXKEY_NONE && d.key == key)
                    found = &d;
            }
            if (!found)
            {
                err = "'" + arg + "' is not a key of the ZX Spectrum matrix";
                return false;
            }
            // Only reads that select the key's half-row alone: a read of
            // several half-rows at once cannot tell the keys of one column
            // apart (A and Q are both bit 0) - the program could not know it
            // was this key. Those reads are what "key" without a name finds.
            // The key is down when its bit of the value is 0
            q.portMask = 0xFF01;
            q.portValue = static_cast<uint16_t>(found->port & 0xFF00);
            q.valueMask = static_cast<uint8_t>(~found->match & 0x1F);
        }
        return true;
    }
    if (event == "ear")
    {
        q.direction = Dir::Read;
        q.portMask = 0x0001;
        q.portValue = 0;
        q.valueMatch = TTDValueMatch::Any;
        q.valueMask = 0x40;
        q.trigger = TTDPortTrigger::Change;
        q.streamMask = 0x0001;  // the tape bit does not depend on the half-row read
        return arg.empty() || (err = "'ear' takes no argument", false);
    }
    if (event == "ay-read" || event == "ay-write" || event == "ay-select")
    {
        int reg = -1;
        if (!ParseRegister(arg, reg, err))
            return false;
        q.direction = event == "ay-read" ? Dir::Read : Dir::Write;
        q.portMask = kAyMask;
        q.portValue = event == "ay-write" ? kAyDataPort : kAyRegisterPort;
        q.trigger = TTDPortTrigger::Every;
        q.ayRegister = -1;
        if (event == "ay-select")
        {
            if (reg >= 0)
            {
                q.valueMatch = TTDValueMatch::Equals;
                q.valueMask = 0xFF;
                q.value = static_cast<uint8_t>(reg);
            }
        }
        else
        {
            q.ayRegister = reg;
        }
        return true;
    }
    if (event == "border" || event == "beeper")
    {
        q.direction = Dir::Write;
        q.portMask = 0x0001;
        q.portValue = 0;
        q.valueMatch = TTDValueMatch::Any;
        q.valueMask = event == "border" ? 0x07 : 0x10;
        q.trigger = TTDPortTrigger::Change;
        q.streamMask = 0x0001;  // the ULA decodes A0 alone: OUT #10FE and #00FE are one port
        return arg.empty() || (err = "'" + event + "' takes no argument", false);
    }
    if (event == "in" || event == "out")
    {
        q.direction = event == "in" ? Dir::Read : Dir::Write;
        return arg.empty() || (err = "'" + event + "' takes no argument (use the port and value filters)", false);
    }
    err = "unknown event '" + eventName + "'";
    return false;
}

TTDPortSearchResult SearchPortEvents(const TTDPortJournal& reads, const TTDPortJournal& writes, const TTDPortQuery& q)
{
    TTDPortSearchResult result;
    if (q.limit == 0)
    {
        result.error = "limit must be at least 1";
        return result;
    }
    if (q.to < q.from)
    {
        result.error = "the time window ends before it starts";
        return result;
    }

    const bool isRead = q.direction == TTDPortJournal::Direction::Read;
    const TTDPortJournal& journal = isRead ? reads : writes;
    const bool followAy = q.ayRegister >= 0;

    // The trigger needs each port's history from the session start, so the
    // scan starts there when it compares with the previous record; a plain
    // match starts at the window
    const bool needsHistory = q.trigger != TTDPortTrigger::Every || followAy;
    // Private caches: the emulation thread may be replaying through the same
    // journals with their own cache
    TTDPortJournal::ReadCache cache;
    TTDPortJournal::ReadCache writeCache;
    uint64_t index = needsHistory ? 0 : journal.LowerBound(q.from, cache);

    std::unordered_map<uint16_t, uint8_t> previous;  // per port: masked value (Change) or test result (Rising)
    std::deque<TTDPortHit> hits;
    int selected = -1;
    uint64_t writeIndex = 0;
    TTDPortRecord w;

    TTDPortRecord r;
    for (; journal.Get(index, r, cache); ++index)
    {
        if (q.to < r.Time())
            break;
        ++result.scanned;

        // The AY register selected at this access
        if (followAy)
        {
            if (isRead)
            {
                // OUTs before this IN (never the same instruction)
                while (writes.Get(writeIndex, w, writeCache) && w.Time() < r.Time())
                {
                    if (IsAyRegisterPort(w.port))
                        selected = SelectedRegister(w.value, selected);
                    ++writeIndex;
                }
            }
            else if (IsAyRegisterPort(r.port))
            {
                selected = SelectedRegister(r.value, selected);
            }
        }

        if ((r.port & q.portMask) != q.portValue)
            continue;
        if (followAy && selected != q.ayRegister)
            continue;

        const bool passes = ValuePasses(q, r.value);
        bool hit = false;
        switch (q.trigger)
        {
            case TTDPortTrigger::Every:
                hit = passes;
                break;
            case TTDPortTrigger::Rising:
            {
                const uint16_t stream = r.port & q.streamMask;
                const auto it = previous.find(stream);
                hit = passes && (it == previous.end() || it->second == 0);
                previous[stream] = passes ? 1 : 0;
                break;
            }
            case TTDPortTrigger::Change:
            {
                const uint8_t masked = r.value & q.valueMask;
                const uint16_t stream = r.port & q.streamMask;
                const auto it = previous.find(stream);
                hit = passes && it != previous.end() && it->second != masked;
                previous[stream] = masked;
                break;
            }
        }
        if (!hit || r.Time() < q.from)
            continue;

        hits.push_back(TTDPortHit{r, index, followAy ? selected : -1});
        if (hits.size() > q.limit)
        {
            result.truncated = true;
            if (!q.newestFirst)
            {
                hits.pop_back();
                break;  // the first `limit` hits are known
            }
            hits.pop_front();
        }
    }

    result.hits.assign(hits.begin(), hits.end());
    if (q.newestFirst)
        std::reverse(result.hits.begin(), result.hits.end());
    result.ok = true;
    return result;
}

}  // namespace ttd
