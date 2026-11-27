#include "exechistory.h"

#include <string>

#include "emulator/state/statenode.h"

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
    node["kind"] = std::string(kind == 0 ? "rom" : kind == 1 ? "ram" : kind == 2 ? "cache" : "none");
    node["page"] = int(page);
    if (idle)
        node["idle_iterations"] = int64_t(idle);
}
