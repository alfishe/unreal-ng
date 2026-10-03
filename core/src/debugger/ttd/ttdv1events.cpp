#include "debugger/ttd/ttdv1events.h"

#include <cstring>

#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdinputjournal.h"

namespace ttd
{

size_t FeedV1Events(TimeTravelEngine& engine, const TTDInputJournal& input, const TTDExternalEventJournal& external,
                    TTDV1EventCursor& cursor, uint64_t throughFrame, size_t* refused)
{
    const std::vector<TTDInputEvent>& inputs = input.Events();
    const std::vector<TTDExternalEvent> markers = external.SnapshotEvents();
    auto before = [](const TTDTimePoint& a, const TTDTimePoint& b) {
        return a.frame < b.frame || (a.frame == b.frame && a.tInFrame <= b.tInFrame);
    };
    size_t appended = 0;
    for (;;)
    {
        const bool haveInput = cursor.input < inputs.size() && inputs[cursor.input].time.frame <= throughFrame;
        const bool haveMarker = cursor.external < markers.size() && markers[cursor.external].time.frame <= throughFrame;
        if (!haveInput && !haveMarker)
            break;
        TTDEvent ev;
        TTDTimePoint at;
        if (haveInput && (!haveMarker || before(inputs[cursor.input].time, markers[cursor.external].time)))
        {
            const TTDInputEvent& in = inputs[cursor.input++];
            at = in.time;
            ev = TTDEventLog::FromInput(0, in);
            if (in.kind == TTDInputKind::NetEvent)
                if (const TTDNetInput* net = input.NetOf(in))
                {
                    TTDEventLog::PackNet(*net, ev);
                    if (net->payloadLength)
                        ev.payload = engine.Payloads().Store(input.PayloadOf(*net), net->payloadLength);
                }
        }
        else
        {
            const TTDExternalEvent& m = markers[cursor.external++];
            at = m.time;
            ev.kind = static_cast<TTDEventKind>(0x0100 + static_cast<uint16_t>(m.kind));
            const size_t length = strnlen(m.reason, sizeof(m.reason));
            if (length)
                ev.payload = engine.Payloads().Store(reinterpret_cast<const uint8_t*>(m.reason), length);
        }
        if (engine.AppendEvent(at.frame, at.tInFrame, ev))
            ++appended;
        else if (refused)
            ++*refused;
    }
    return appended;
}

}  // namespace ttd
