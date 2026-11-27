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
    if (name == "frame")
        return [](const R& r, int64_t& v) { v = static_cast<int64_t>(r.frame); return true; };
    if (name == "pc")
        return [](const R& r, int64_t& v) { v = r.pc; return true; };
    // The kinds as ToState names them: everything past cache is "none"
    if (name == "kind")
        return [](const R& r, int64_t& v) { v = r.kind < 3 ? r.kind : 3; return true; };
    if (name == "page")
        return [](const R& r, int64_t& v) { v = r.page; return true; };
    if (name == "idle_iterations")
        return [](const R& r, int64_t& v) { v = r.idle; return r.idle != 0; };
    return nullptr;
}

bool ExecRecord::FilterWord(const std::string& field, const std::string& word, int64_t& value)
{
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
    return "kind: rom, ram, cache, none; numbers: pc, page, frame, idle_iterations";
}

void ExecRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["pc"] = int(pc);
    node["kind"] = std::string(KindName(kind));
    node["page"] = int(page);
    if (idle)
        node["idle_iterations"] = int64_t(idle);
}
