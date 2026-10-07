#include "debugger/ttd/ttdv1events.h"

#include <cstring>

#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdinputjournal.h"

namespace ttd
{

size_t FeedV1Events(TimeTravelEngine& engine, const TTDInputJournal& input, const TTDExternalEventJournal& external,
                    TTDV1EventCursor& cursor, uint64_t throughFrame, size_t* refused,
                    const std::unordered_map<size_t, std::vector<uint8_t>>* editData,
                    const std::vector<TTDPendingFact>* facts)
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
        const bool haveFact = facts && cursor.facts < facts->size() && (*facts)[cursor.facts].at.frame <= throughFrame;
        if (!haveInput && !haveMarker && !haveFact)
            break;
        TTDEvent ev;
        TTDTimePoint at;
        const TTDPendingFact* fact = haveFact ? &(*facts)[cursor.facts] : nullptr;
        if (fact && (!haveInput || !before(inputs[cursor.input].time, fact->at)) &&
            (!haveMarker || !before(markers[cursor.external].time, fact->at)))
        {
            ++cursor.facts;
            at = fact->at;
            ev = fact->ev;
        }
        else if (haveInput && (!haveMarker || before(inputs[cursor.input].time, markers[cursor.external].time)))
        {
            const TTDInputEvent& in = inputs[cursor.input++];
            at = in.time;
            ev = TTDEventLog::FromInput(0, in);
            if (HasNetRecord(in.kind))   // a host event or an Ethernet frame: its bytes are the payload
                if (const TTDNetInput* net = input.NetOf(in))
                {
                    TTDEventLog::PackNet(*net, ev);
                    if (net->payloadLength)
                        ev.payload = engine.Payloads().Store(input.PayloadOf(*net), net->payloadLength);
                }
        }
        else
        {
            const size_t index = cursor.external++;
            const TTDExternalEvent& m = markers[index];
            at = m.time;
            ev.kind = static_cast<TTDEventKind>(0x0100 + static_cast<uint16_t>(m.kind));
            const auto edit = editData ? editData->find(index) : decltype(editData->end()){};
            if (ev.kind == TTDEventKind::DebuggerEdit && editData && edit != editData->end())
            {
                ev.args[0] = kEditCarriesData;   // the payload is the edit itself
                ev.payload = engine.Payloads().Store(edit->second.data(), edit->second.size());
            }
            else
            {
                const size_t length = strnlen(m.reason, sizeof(m.reason));
                if (length)
                    ev.payload = engine.Payloads().Store(reinterpret_cast<const uint8_t*>(m.reason), length);
            }
        }
        if (engine.AppendEvent(at.frame, at.tInFrame, ev))
            ++appended;
        else if (refused)
            ++*refused;
    }
    return appended;
}

}  // namespace ttd
