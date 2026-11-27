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
        case GSTraceSide::Idle:
            return "idle";
    }
    return "unknown";
}
}  // namespace

history::FieldGetter<GSTraceEvent> GSTraceEvent::FilterField(const std::string& name)
{
    using E = GSTraceEvent;
    // A field is there for the sides ToState shows it for
    if (name == "timestamp")
        return [](const E& e, int64_t& v) { v = e.timestamp; return true; };
    if (name == "frame")
        return [](const E& e, int64_t& v) { v = e.frameNumber; return true; };
    if (name == "side")
        return [](const E& e, int64_t& v) { v = static_cast<int64_t>(e.side); return true; };
    if (name == "port")
        return [](const E& e, int64_t& v) { v = e.port; return true; };
    if (name == "pc")
        return [](const E& e, int64_t& v) { v = e.pc; return true; };
    if (name == "value")
        return [](const E& e, int64_t& v) { v = e.value; return true; };
    if (name == "channel")
        return [](const E& e, int64_t& v) {
            v = e.channel;
            return e.side == GSTraceSide::DacFetch || e.side == GSTraceSide::ZxDma;
        };
    if (name == "card_address")
        return [](const E& e, int64_t& v) { v = e.cardAddress(); return e.side == GSTraceSide::ZxDma; };
    if (name == "host_t")
        return [](const E& e, int64_t& v) { v = e.hostT(); return e.side == GSTraceSide::ZxDma; };
    if (name == "wait")
        return [](const E& e, int64_t& v) { v = e.waitT(); return e.side == GSTraceSide::ZxDma; };
    if (name == "dropped")
        return [](const E& e, int64_t& v) { v = e.isDropped(); return e.side == GSTraceSide::ZxDma; };
    if (name == "iterations")
        return [](const E& e, int64_t& v) { v = e.iterations; return e.side == GSTraceSide::Idle; };
    if (name == "nmi")
        return [](const E& e, int64_t& v) { v = e.isNmi(); return e.side == GSTraceSide::Interrupt; };
    if (name == "out")
        return [](const E& e, int64_t& v) {
            v = e.isOut();
            return e.side != GSTraceSide::Interrupt && e.side != GSTraceSide::Idle;
        };
    return nullptr;
}

bool GSTraceEvent::FilterWord(const std::string& field, const std::string& word, int64_t& value)
{
    if (field == "side")
    {
        for (GSTraceSide side : { GSTraceSide::Host, GSTraceSide::GsInternal, GSTraceSide::DacFetch,
                                  GSTraceSide::Interrupt, GSTraceSide::ZxDma, GSTraceSide::Idle })
        {
            if (word == SideName(side))
            {
                value = static_cast<int64_t>(side);
                return true;
            }
        }
        return false;
    }
    bool flag = false;
    if ((field == "out" || field == "nmi" || field == "dropped") && history::RecordFilter::FlagWord(word, flag))
    {
        value = flag ? 1 : 0;
        return true;
    }
    return false;
}

const char* GSTraceEvent::FilterFields()
{
    return "side: host, gs, dac, interrupt, zx-dma, idle; out, nmi, dropped: true, false; numbers: port, pc, value, "
           "frame, timestamp, channel, card_address, host_t, wait, iterations";
}

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
    if (side == GSTraceSide::ZxDma)
    {
        node["card_address"] = int64_t(cardAddress());
        node["host_t"] = int64_t(hostT());
        node["wait"] = int64_t(waitT());
        node["dropped"] = isDropped();
    }
    if (side == GSTraceSide::Idle)
        node["iterations"] = int64_t(iterations);
    else if (side == GSTraceSide::Interrupt)
        node["nmi"] = isNmi();
    else
        node["out"] = isOut();
}
