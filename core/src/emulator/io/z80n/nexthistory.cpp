#include "stdafx.h"

#include "nexthistory.h"

#include "emulator/state/statenode.h"

void NextCopperRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["pc"] = int(pc);
    node["kind"] = move ? (reg ? "move" : "nop") : "wait";
    if (move && reg)
    {
        node["reg"] = int(reg);
        node["value"] = int(value);
    }
    node["line"] = int(line);
    node["hc"] = int(hc);
}

void NextDmaRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int64_t(tacts);
    node["pc"] = int(pc);
    node["source"] = int(source);
    node["source_space"] = sourceIo ? "io" : "memory";
    node["destination"] = int(destination);
    node["destination_space"] = destinationIo ? "io" : "memory";
    node["length"] = int(length);
    node["mode"] = mode == 0 ? "byte" : mode == 1 ? "continuous" : "burst";
    node["restart"] = restart;
}

void NextUartRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["at"] = int64_t(at);
    node["channel"] = channel ? "pi" : "esp";
    node["dir"] = tx ? "tx" : "rx";
    node["value"] = int(value);
    if (error)
        node["error"] = true;
}
