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
    if (!ddma)
        return;
    // zxnDDMA: the flags of the block, in physical mode the 22-bit addresses, the 2D rectangle, the fill / transparent value, Layer 2 X,Y
    const bool physical = flags & 0x01;
    node["physical"] = physical;
    if (physical)
    {
        if (!sourceIo)
            node["source"] = int64_t((uint32_t(sourceExt & 0x3F) << 16) | source);
        if (!destinationIo && !(flags & 0x10))  // a Layer 2 X,Y destination is worked out from X, Y and NR #12 / #13 (layer2_x / _y)
            node["destination"] = int64_t((uint32_t(destinationExt & 0x3F) << 16) | destination);
    }
    if (flags & 0x02)
    {
        node["two_d"] = true;
        node["row_length"] = int(rowLength);
        node["rows"] = int(rows);
    }
    if (flags & 0x08)
        node["fill"] = int(value);
    else if (flags & 0x04)
        node["transparent"] = int(value);
    if (flags & 0x10)
    {
        node["layer2_x"] = int(layer2X);
        node["layer2_y"] = int(layer2Y);
        node["hidden"] = (flags & 0x20) != 0;
    }
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
