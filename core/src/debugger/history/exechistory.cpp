#include "exechistory.h"

#include <string>

#include "emulator/state/statenode.h"

void ExecRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["pc"] = int(pc);
    node["kind"] = std::string(kind == 0 ? "rom" : kind == 1 ? "ram" : kind == 2 ? "cache" : "none");
    node["page"] = int(page);
    if (idle)
        node["idle_iterations"] = int64_t(idle);
}
