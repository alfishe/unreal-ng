#include "gsporttrace.h"

#include "emulator/emulatorcontext.h"
#include "emulator/slots/portowners.h"

namespace
{
const char* SideName(GSTraceSide side)
{
    switch (side)
    {
        case GSTraceSide::Host:
            return "host";
        case GSTraceSide::GsInternal:
            return "gs";
        case GSTraceSide::DacFetch:
            return "dac";
        case GSTraceSide::Interrupt:
            return "interrupt";
        case GSTraceSide::ZxDma:
            return "zx-dma";
    }
    return "unknown";
}
}  // namespace

std::string GSHistoryDevice(const EmulatorContext* context)
{
    const std::string owner = slots::PortOwners::Of(context).Find(0x00BB, true, false);  // a copy: the owners are a temporary
    return owner.empty() ? std::string("gs") : owner;
}

bool GSPortTraceRecorder::ReplayActive(const EmulatorContext* context)
{
    return context->ttdReplayActive;
}

void GSTraceEvent::ToState(StateNode& node) const
{
    node["timestamp"] = timestamp;
    node["frame"] = int64_t(frameNumber);
    node["side"] = SideName(side);
    node["port"] = int(port);
    node["pc"] = int(pc);
    node["value"] = int(value);
    if (side == GSTraceSide::DacFetch || side == GSTraceSide::ZxDma)
        node["channel"] = int(channel);
    if (side == GSTraceSide::Interrupt)
        node["nmi"] = isNmi();
    else
        node["out"] = isOut();
}
