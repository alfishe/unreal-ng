#include "ttdv1feeder.h"

#include "debugger/ttd/ttdv1events.h"

#include <array>
#include <cstring>
#include <unordered_map>

#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace ttd::bench
{

namespace
{

constexpr uint32_t kSubPagesPerPage = 4;

/// Decode the pieces of @p cp whose slot differs from @p prevSlots (all of them
/// when @p prevSlots is empty) into @p ram; @p slots receives the checkpoint's slots
bool DecodeChangedSlots(const TimeTravelManager& v1, const TTDCheckpoint& cp, const std::vector<uint32_t>& prevSlots,
                        std::vector<uint32_t>& slots, std::vector<uint8_t>& ram, std::vector<uint8_t>& present,
                        std::vector<uint32_t>& decoded, std::string& error)
{
    const size_t pieces = cp.ramPages.size() * kSubPagesPerPage;
    slots.resize(pieces);
    decoded.clear();
    for (size_t page = 0; page < cp.ramPages.size(); ++page)
        for (uint32_t sub = 0; sub < kSubPagesPerPage; ++sub)
        {
            const size_t p = page * kSubPagesPerPage + sub;
            const uint32_t slot = cp.ramPages[page].pageSlots[sub];
            slots[p] = slot;
            if (!prevSlots.empty() && prevSlots[p] == slot)
                continue;
            if (slot == TTDPageRef::kNeverTouched)
            {
                present[p] = 0;
                continue;
            }
            if (!v1.GetPageStore().GetPage(slot, ram.data() + p * kTTDPieceSize))
            {
                error = "v1 page store slot " + std::to_string(slot) + " failed to decode (piece " +
                        std::to_string(p) + ")";
                return false;
            }
            present[p] = 1;
            decoded.push_back(static_cast<uint32_t>(p));
        }
    return true;
}

}  // namespace

bool SplitV1GeneralSound(const std::vector<uint8_t>& blob, std::vector<uint8_t>& fixedState, std::vector<uint8_t>& ram)
{
    constexpr uint8_t id = static_cast<uint8_t>(PeripheralId::GeneralSound);
    const std::vector<uint8_t> state = TTDPeripheralRegistry::DecodeBlob(id, blob);
    constexpr size_t fixed = SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE;
    if (state.size() <= fixed)
        return false;
    fixedState.assign(state.begin(), state.begin() + fixed);
    ram.assign(state.begin() + fixed, state.end());
    return true;
}

bool DecodeV1Ram(const TimeTravelManager& v1, size_t index, std::vector<uint8_t>& ram, std::vector<uint8_t>& present,
                 std::string& error)
{
    const TTDCheckpoint* cp = v1.GetCheckpoint(index);
    if (!cp)
    {
        error = "no v1 checkpoint " + std::to_string(index);
        return false;
    }
    const size_t pieces = cp->ramPages.size() * kSubPagesPerPage;
    ram.assign(pieces * kTTDPieceSize, 0);
    present.assign(pieces, 0);
    std::vector<uint32_t> slots;
    std::vector<uint32_t> decoded;
    return DecodeChangedSlots(v1, *cp, {}, slots, ram, present, decoded, error);
}

bool FeedV1Session(const TimeTravelManager& v1, TimeTravelEngine& engine, std::string& error, FeedStats* stats,
                   uint32_t blockPieces)
{
    const size_t count = v1.GetCheckpointCount();
    if (count == 0)
    {
        error = "the v1 session holds no checkpoint";
        return false;
    }
    const TTDCheckpoint* first = v1.GetCheckpoint(0);
    const uint32_t pieces = static_cast<uint32_t>(first->ramPages.size() * kSubPagesPerPage);

    TTDRegionDesc ram;
    ram.id = TTDRegionId::MachineRam;
    ram.name = "ram";
    ram.pieces = pieces;
    ram.bytes = pieces * kTTDPieceSize;
    ram.dirtyGranularity = kTTDPieceSize * kSubPagesPerPage;
    ram.blockPieces = blockPieces;
    std::vector<TTDRegionDesc> regions = {ram};

    // The General Sound RAM, which v1 keeps inside the card's blob, is region 1
    constexpr uint8_t gsId = static_cast<uint8_t>(PeripheralId::GeneralSound);
    std::vector<uint8_t> gsFixed;
    std::vector<uint8_t> gsRam;
    std::vector<uint8_t> gsPrev;
    const auto gsBlob0 = first->peripheralBlobs.find(gsId);
    const bool hasGs = gsBlob0 != first->peripheralBlobs.end() && SplitV1GeneralSound(gsBlob0->second, gsFixed, gsRam) &&
                       gsRam.size() % kTTDPieceSize == 0;
    if (hasGs)
    {
        TTDRegionDesc gs;
        gs.id = TTDRegionId::GeneralSoundRam;
        gs.name = "gs.ram";
        gs.ownerType = gsId;
        gs.pieces = static_cast<uint32_t>(gsRam.size() / kTTDPieceSize);
        gs.bytes = static_cast<uint32_t>(gsRam.size());
        regions.push_back(gs);
    }
    // The device table from the file: every device that has a blob, sized by
    // its largest state (the General Sound card without its RAM)
    std::array<uint32_t, 256> stateSize{};
    std::array<bool, 256> variable{};
    for (size_t i = 0; i < count; ++i)
        for (const auto& [id, blob] : v1.GetCheckpoint(i)->peripheralBlobs)
        {
            uint32_t size = 0;
            if (id == gsId && hasGs && SplitV1GeneralSound(blob, gsFixed, gsRam))
                size = static_cast<uint32_t>(gsFixed.size());
            else
                size = static_cast<uint32_t>(TTDPeripheralRegistry::DecodeBlob(id, blob).size());
            if (stateSize[id] != 0 && stateSize[id] != size)
                variable[id] = true;
            stateSize[id] = std::max(stateSize[id], size);
        }
    std::vector<TTDDeviceEntry> devices;
    for (uint32_t id = 0; id < 256; ++id)
        if (stateSize[id] != 0)
        {
            TTDDeviceEntry e;
            e.descriptor.legacyId = static_cast<PeripheralId>(id);
            e.descriptor.type = static_cast<TTDDeviceType>(id);
            e.descriptor.instance = "id" + std::to_string(id);
            e.descriptor.stateSize = stateSize[id];
            e.descriptor.variableSize = variable[id];
            devices.push_back(e);
        }
    if (!engine.BeginSession(regions, std::move(devices), error))
        return false;

    std::vector<uint8_t> image(size_t(pieces) * kTTDPieceSize, 0);
    std::vector<uint8_t> present(pieces, 0);
    std::vector<uint8_t> previous;           // the piece's content before this checkpoint, for the change test
    std::vector<uint32_t> prevSlots;
    std::vector<uint32_t> slots;
    std::vector<uint32_t> decoded;
    const uint64_t span = v1.FrameSpan();
    FeedStats local;
    uint64_t busReads = v1.GetPortReadJournal().FirstIndex();
    uint64_t busWrites = v1.GetPortWriteJournal().FirstIndex();

    for (size_t i = 0; i < count; ++i)
    {
        const TTDCheckpoint* cp = v1.GetCheckpoint(i);
        if (cp->ramPages.size() * kSubPagesPerPage != pieces)
        {
            error = "v1 checkpoint " + std::to_string(i) + " has a different RAM size";
            return false;
        }

        // Keep the old content of the pieces about to be decoded, to feed only real changes
        previous = image;
        const std::vector<uint8_t> wasPresent = present;
        if (!DecodeChangedSlots(v1, *cp, i == 0 ? std::vector<uint32_t>{} : prevSlots, slots, image, present,
                                decoded, error))
            return false;

        TTDFrameInput in;
        in.position.frame = cp->time.frame;
        in.start = cp->time.frame * span;
        in.cpu = cp->cpu;
        in.chipset = cp->chipset;
        in.deviceBlobs = &cp->peripheralBlobs;

        // General Sound: the blob without its RAM, the RAM as changed pieces of region 1
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        if (hasGs)
        {
            const auto it = cp->peripheralBlobs.find(gsId);
            if (it == cp->peripheralBlobs.end() || !SplitV1GeneralSound(it->second, gsFixed, gsRam) ||
                gsRam.size() != size_t(regions[1].pieces) * kTTDPieceSize)
            {
                error = "v1 checkpoint " + std::to_string(i) + " has no usable General Sound blob";
                return false;
            }
            blobs = cp->peripheralBlobs;
            blobs[gsId] = TTDPeripheralRegistry::EncodeBlob(gsId, gsFixed.data(), gsFixed.size());
            in.deviceBlobs = &blobs;
            for (uint32_t p = 0; p < regions[1].pieces; ++p)
                if (gsPrev.empty() || std::memcmp(gsRam.data() + size_t(p) * kTTDPieceSize,
                                                  gsPrev.data() + size_t(p) * kTTDPieceSize, kTTDPieceSize) != 0)
                    in.changed.push_back({1, p, gsRam.data() + size_t(p) * kTTDPieceSize});
        }
        for (const uint32_t p : decoded)
        {
            const uint8_t* now = image.data() + size_t(p) * kTTDPieceSize;
            if (i > 0 && wasPresent[p] &&
                std::memcmp(now, previous.data() + size_t(p) * kTTDPieceSize, kTTDPieceSize) == 0)
                continue;   // v1 stored it again (a key frame); the content did not change
            in.changed.push_back({0, p, now});
        }
        local.changedPieces += in.changed.size();
        for (const TTDChangedPiece& c : in.changed)
            local.changedRamPieces += c.region == 0 ? 1 : 0;

        // Bus data up to this checkpoint's cursors (a file's cursors are relative to its first record)
        {
            const TTDPortJournal& reads = v1.GetPortReadJournal();
            const TTDPortJournal& writes = v1.GetPortWriteJournal();
            TTDPortRecord r;
            for (; busReads < reads.FirstIndex() + cp->portReadCursor && reads.Get(busReads, r); ++busReads)
                engine.AppendBusRead(r);
            for (; busWrites < writes.FirstIndex() + cp->portWriteCursor && writes.Get(busWrites, r); ++busWrites)
                engine.AppendBusWrite(r);
        }
        if (!engine.CaptureFrame(in, error))
            return false;
        if (hasGs)
            gsPrev = gsRam;
        prevSlots.swap(slots);
        local.checkpoints++;
    }
    // Bus data after the last checkpoint: its frame's, which a replay from it reads
    {
        TTDPortRecord r;
        for (; busReads < v1.GetPortReadJournal().Size() && v1.GetPortReadJournal().Get(busReads, r); ++busReads)
            engine.AppendBusRead(r);
        for (; busWrites < v1.GetPortWriteJournal().Size() && v1.GetPortWriteJournal().Get(busWrites, r); ++busWrites)
            engine.AppendBusWrite(r);
    }
    // The write journal (D40): its memory writes and the spans it covers; v1
    // files written before D40 also journaled port OUTs (the port journal has them)
    if (const TTDWriteJournal* journal = v1.GetWriteJournal())
    {
        for (uint64_t seq = journal->SeqTail(); seq < journal->SeqHead(); ++seq)
            if (!journal->RecordAt(seq).isIo)
                engine.Writes().Append(journal->RecordAt(seq));
        engine.Writes().SetSegments(v1.GetSessionInfo().writeJournalSegments);
    }
    // The session's events: input, network, markers (Phase 3, Step 1)
    TTDV1EventCursor cursor;
    local.events = FeedV1Events(engine, v1.GetInputJournal(), v1.GetExternalEvents(), cursor, UINT64_MAX,
                                &local.eventsRefused);
    if (stats)
        *stats = local;
    return true;
}

}  // namespace ttd::bench
