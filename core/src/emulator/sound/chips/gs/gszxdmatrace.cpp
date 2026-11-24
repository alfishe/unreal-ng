#include "emulator/sound/chips/gs/gszxdmatrace.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/soundmanager.h"

namespace
{
bool ParseCount(const std::string& text, const char* name, uint64_t& out, std::string& error)
{
    if (text.empty())
        return true;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (text[0] == '-' || !end || *end != '\0')
    {
        error = std::string("'") + name + "' is a non-negative number";
        return false;
    }
    out = value;
    return true;
}

constexpr const char* kStartHint =
    "start the GS port trace to record the bytes: POST control/audio/gs/porttrace {action: start}, CLI "
    "'gsporttrace start'";
}  // namespace

bool GSZxDmaTraceQueryFromStrings(const std::string& last, const std::string& since, GSZxDmaTraceQuery& query,
                                  std::string& error)
{
    uint64_t count = query.last;
    if (!ParseCount(last, "last", count, error) || !ParseCount(since, "since", query.since, error))
        return false;
    query.last = static_cast<size_t>(count);
    return true;
}

namespace DeviceState
{
StateNode GsZxDmaTrace(EmulatorContext* context, const GSZxDmaTraceQuery& query)
{
    StateNode n = StateNode::Object();
    GeneralSoundCard* gs = context && context->pSoundManager ? context->pSoundManager->getGeneralSound() : nullptr;
    NeoGSStateInfo info;
    if (!gs || !gs->neogsState(info))
    {
        n["available"] = false;
        n["description"] = gs ? "the ZX-DMA trace needs a NeoGS (the " + gs->deviceDescription() + " has no ZX-DMA)"
                              : std::string("No NeoGS fitted (slots plug zxbus.next neogs)");
        return n;
    }

    // The newest `last` ZX-DMA bytes after `since`, copied out of the history in one walk (the DAC fetches sharing
    // the ring are skipped, not copied); a `since` past the newest event is a new session: from the start
    size_t keptCount = 0;
    uint64_t lastSeq = 0;
    uint64_t evicted = 0;
    std::vector<std::pair<uint64_t, GSTraceEvent>> rowsKept =
        gs->getPortTraceSide(GSTraceSide::ZxDma, query.since, query.last, keptCount, lastSeq, evicted);
    const bool restarted = query.since > lastSeq;
    if (restarted)
        rowsKept = gs->getPortTraceSide(GSTraceSide::ZxDma, 0, query.last, keptCount, lastSeq, evicted);

    const bool capturing = gs->isPortTraceCapturing();
    n["available"] = true;
    n["card"] = gs->deviceDescription();
    n["device"] = GSHistoryDevice(context);
    n["capturing"] = capturing;
    n["armed"] = gs->isPortTraceArmed();
    n["total_bytes_kept"] = uint64_t(keptCount);
    n["evicted"] = evicted;
    n["last_seq"] = lastSeq;
    n["restarted"] = restarted;
    StateNode& counters = n["counters"];
    counters = StateNode::Object();
    counters["bytes_read"] = info.zxBytesRead;
    counters["bytes_written"] = info.zxBytesWritten;
    counters["bytes_dropped"] = info.zxBytesDropped;
    counters["wait_t_states"] = info.zxWaitTStates;

    char line[256];
    std::string text;
    std::snprintf(line, sizeof line, "ZX-DMA trace (%s, %s): %zu byte(s) kept, %zu shown; module totals: read %llu, "
                  "written %llu, dropped %llu, wait %llu T\n",
                  gs->deviceDescription().c_str(), capturing ? "capturing" : "not capturing", keptCount,
                  rowsKept.size(), static_cast<unsigned long long>(info.zxBytesRead),
                  static_cast<unsigned long long>(info.zxBytesWritten),
                  static_cast<unsigned long long>(info.zxBytesDropped),
                  static_cast<unsigned long long>(info.zxWaitTStates));
    text += line;

    StateNode rows = StateNode::Array();
    for (const auto& [seq, e] : rowsKept)
    {
        const int64_t cardTime = gs->cardCycles(e.timestamp);
        StateNode r = StateNode::Object();
        r["seq"] = seq;
        r["frame"] = int64_t(e.frameNumber);
        r["host_t"] = int64_t(e.hostT());
        r["card_time"] = cardTime;
        r["direction"] = e.isOut() ? "write" : "read";
        r["card_address"] = int64_t(e.cardAddress());
        r["page"] = int64_t(e.cardAddress() >> 14);
        r["value"] = int(e.value);
        r["wait"] = int64_t(e.waitT());
        r["host_pc"] = int(e.pc);
        r["dropped"] = e.isDropped();
        rows.push(std::move(r));
        std::snprintf(line, sizeof line, "  #%llu f%u T%u c%lld %-5s #%06X p%02X #%02X wait %u PC #%04X%s\n",
                      static_cast<unsigned long long>(seq), e.frameNumber, e.hostT(), static_cast<long long>(cardTime),
                      e.isOut() ? "write" : "read", e.cardAddress(), e.cardAddress() >> 14, unsigned(e.value), e.waitT(), unsigned(e.pc),
                      e.isDropped() ? " DROPPED" : "");
        text += line;
    }
    n["rows"] = std::move(rows);

    if (evicted > 0)
        text += "note: the ring is full (" + std::to_string(evicted) +
                " oldest bytes evicted): read sooner, or stop the trace after the transfer\n";
    if (!capturing && keptCount == 0)
        text += std::string("not capturing: ") + kStartHint + "\n";
    if (!capturing)
        n["how_to_start"] = kStartHint;
    n["text"] = text;
    return n;
}
}  // namespace DeviceState
