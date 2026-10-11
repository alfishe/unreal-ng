#pragma once

// A tool's NMI for every automation surface (WebAPI POST /next/nmi, MCP invoke_api, CLI `next nmi`, Lua / Python next_nmi_trigger): one
// implementation (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md R6, automation-gaps-2026-10-11.md P4).
//
// The sources are the board's own (PortDecoder_Next::TriggerNmi): "m1" is the Multiface button (what NR #02 bit 3 does: it needs NR #06
// bit 3, CONMEM off and no handler running), "drive" is the DRIVE button (NR #02 bit 2, NR #06 bit 4), "pulse" the bare /NMI line. A
// button that the board refuses is a reply with `accepted: false` and the reason, not an error. The request is taken with the CPU held
// (Emulator::CpuHold), as Emulator::RequestNMI does.

#include <string>

#include "emulator/state/statenode.h"

class Emulator;

namespace NextNmiControl
{
struct Result
{
    bool ok = false;        // the request was made (accepted or refused by the board)
    bool accepted = false;  // the board took it
    bool notNext = false;   // the machine is not a ZX Spectrum Next (WebAPI: 409)
    bool badSource = false; // an unknown source (WebAPI: 400)
    std::string source;
    std::string reason;     // refused or not ok: why
};

Result Trigger(Emulator* emulator, const std::string& source);

/// The reply every plane renders: ok, accepted, source, reason
StateNode ToState(const Result& result);
}  // namespace NextNmiControl
