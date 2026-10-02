#include "ttdv1feeder.h"

#include <cstring>

#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"

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
    if (!engine.BeginSession({ram}, error))
        return false;

    std::vector<uint8_t> image(size_t(pieces) * kTTDPieceSize, 0);
    std::vector<uint8_t> present(pieces, 0);
    std::vector<uint8_t> previous;           // the piece's content before this checkpoint, for the change test
    std::vector<uint32_t> prevSlots;
    std::vector<uint32_t> slots;
    std::vector<uint32_t> decoded;
    const uint64_t span = v1.FrameSpan();
    FeedStats local;

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
        for (const uint32_t p : decoded)
        {
            const uint8_t* now = image.data() + size_t(p) * kTTDPieceSize;
            if (i > 0 && wasPresent[p] &&
                std::memcmp(now, previous.data() + size_t(p) * kTTDPieceSize, kTTDPieceSize) == 0)
                continue;   // v1 stored it again (a key frame); the content did not change
            in.changed.push_back({0, p, now});
        }
        local.changedPieces += in.changed.size();

        if (!engine.CaptureFrame(in, error))
            return false;
        prevSlots.swap(slots);
        local.checkpoints++;
    }
    if (stats)
        *stats = local;
    return true;
}

}  // namespace ttd::bench
