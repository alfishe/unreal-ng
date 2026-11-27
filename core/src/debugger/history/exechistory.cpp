#include "exechistory.h"

#include <string>

#include "emulator/state/statenode.h"

namespace
{
const char* KindName(uint8_t kind)
{
    return kind == 0 ? "rom" : kind == 1 ? "ram" : kind == 2 ? "cache" : "none";
}
}  // namespace

history::FieldGetter<ExecRecord> ExecRecord::FilterField(const std::string& name)
{
    using R = ExecRecord;
    // A field is there for the records ToState shows it for: an instruction, or an event of the time line
    if (name == "frame")
        return [](const R& r, int64_t& v) { v = r.frame; return true; };
    if (name == "t")
        return [](const R& r, int64_t& v) { v = r.t; return true; };
    if (name == "pc")
        return [](const R& r, int64_t& v) { v = r.pc; return true; };
    if (name == "event")
        return [](const R& r, int64_t& v) { v = r.kind; return r.IsEvent(); };
    // The kinds as ToState names them: everything past cache is "none"
    if (name == "kind")
        return [](const R& r, int64_t& v) { v = r.kind < 3 ? r.kind : 3; return !r.IsEvent(); };
    if (name == "page")
        return [](const R& r, int64_t& v) { v = r.page; return !r.IsEvent(); };
    if (name == "idle_iterations")
        return [](const R& r, int64_t& v) { v = r.idle; return !r.IsEvent() && r.idle != 0; };
    if (name == "from_hz")
        return [](const R& r, int64_t& v) { v = int64_t(r.page) * 1000000; return r.kind == kClock; };
    if (name == "clock_hz")
        return [](const R& r, int64_t& v) { v = r.idle; return r.kind == kClock; };
    return nullptr;
}

bool ExecRecord::FilterWord(const std::string& field, const std::string& word, int64_t& value)
{
    if (field == "event")
    {
        if (word == "int")
            value = kInt;
        else if (word == "nmi")
            value = kNmi;
        else if (word == "clock_change")
            value = kClock;
        else
            return false;
        return true;
    }
    if (field != "kind")
        return false;
    for (uint8_t kind = 0; kind < 4; kind++)
    {
        if (word == KindName(kind))
        {
            value = kind;
            return true;
        }
    }
    return false;
}

const char* ExecRecord::FilterFields()
{
    return "kind: rom, ram, cache, none; event: int, nmi, clock_change; numbers: pc, page, frame, t, idle_iterations, "
           "from_hz, clock_hz";
}

void ExecRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["t"] = int64_t(t);
    node["pc"] = int(pc);
    switch (kind)
    {
        case kInt:
        case kNmi:
            node["event"] = std::string(kind == kInt ? "int" : "nmi");
            return;
        case kClock:
            node["event"] = std::string("clock_change");
            node["from_hz"] = int64_t(page) * 1000000;
            node["clock_hz"] = int64_t(idle);
            return;
        default:
            break;
    }
    node["kind"] = std::string(KindName(kind));
    node["page"] = int(page);
    if (idle)
        node["idle_iterations"] = int64_t(idle);
}
