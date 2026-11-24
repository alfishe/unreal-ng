/// @file ttdsegmentwindow.cpp
/// @brief A recording folder's segment file read for one stretch of its checkpoints (TTDSessionFile::
/// ReadSegmentWindow): the seek index says where each part's records lie, a table record gives every piece's
/// version at the stretch's first checkpoint, and only the versions that table and the stretch's checkpoints name,
/// with the versions they build on, are read and imported. A seek into evicted history reads a few hundred
/// kilobytes of a file of megabytes.

#include <algorithm>
#include <cstring>

#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/engine/ttdseekindex.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcompression.h"

namespace ttd
{

namespace
{
constexpr uint8_t kSeekIndexMagic = 0x53;   // 'S'
constexpr uint8_t kSeekIndexVersion = 1;
constexpr uint32_t kNoItem = 0xFFFFFFFFu;

/// A checkpoint record's fields up to its changed regions, skipped (ttdsessionfile.cpp ReadCheckpointFields)
bool SkipCheckpointFields(TTDByteReader& c, uint64_t& delta, uint64_t& changedRegions)
{
    uint8_t flags = 0;
    uint64_t start = 0, unclaimed = 0, cursor = 0;
    return c.Varint(delta) && c.U8(flags) && c.U64(start) && c.Skip(sizeof(TTDCpuState)) &&
           c.Skip(sizeof(TTDChipsetState)) && c.Varint(unclaimed) && unclaimed <= c.Left() &&
           c.Skip(static_cast<size_t>(unclaimed)) && c.Varint(cursor) && c.Varint(cursor) && c.Varint(cursor) &&
           c.Varint(cursor) && c.Varint(changedRegions);
}
}  // namespace

std::vector<uint8_t> TTDSeekIndex::Encode(uint64_t indexOffset) const
{
    TTDByteWriter w;
    w.U8(kSeekIndexMagic);
    w.U8(kSeekIndexVersion);
    w.U64(indexOffset);
    w.Varint(line);
    w.Varint(parts.size());
    for (const TTDSeekIndexPart& p : parts)
    {
        w.Varint(p.piecesOffset);
        w.Varint(p.piecesStored);
        w.Varint(p.checkpointsOffset);
        w.Varint(p.checkpointsStored);
        w.Varint(p.tablesOffset);
        w.Varint(p.tablesStored);
        w.Varint(p.firstItem);
        w.Varint(p.items);
        w.Varint(p.firstFrame);
        w.Varint(p.tableFrames.size());
        uint64_t previous = p.firstFrame;
        for (const uint64_t frame : p.tableFrames)
        {
            w.Varint(frame - previous);
            previous = frame;
        }
    }
    return std::move(w.bytes);
}

bool TTDSeekIndex::Decode(const uint8_t* data, size_t size, uint64_t indexOffset, TTDSeekIndex& out)
{
    TTDByteReader r(data, size);
    uint8_t magic = 0, version = 0;
    uint64_t at = 0, count = 0;
    if (!r.U8(magic) || !r.U8(version) || magic != kSeekIndexMagic || version != kSeekIndexVersion || !r.U64(at) ||
        at != indexOffset || !r.VarintAs(out.line) || !r.Varint(count) || count > r.Left())
        return false;
    out.parts.assign(static_cast<size_t>(count), {});
    uint64_t nextItem = 0;
    for (TTDSeekIndexPart& p : out.parts)
    {
        uint64_t tables = 0;
        if (!r.Varint(p.piecesOffset) || !r.VarintAs(p.piecesStored) || !r.Varint(p.checkpointsOffset) ||
            !r.VarintAs(p.checkpointsStored) || !r.Varint(p.tablesOffset) || !r.VarintAs(p.tablesStored) ||
            !r.Varint(p.firstItem) || !r.VarintAs(p.items) || !r.Varint(p.firstFrame) || !r.Varint(tables) ||
            tables > r.Left())
            return false;
        // The records lie before the index, the versions are numbered one part after another
        if (p.piecesOffset >= indexOffset || p.checkpointsOffset >= indexOffset || p.tablesOffset >= indexOffset ||
            p.firstItem != nextItem || p.firstItem + p.items > kNoItem)
            return false;
        nextItem = p.firstItem + p.items;
        uint64_t previous = p.firstFrame;
        p.tableFrames.resize(static_cast<size_t>(tables));
        for (uint64_t& frame : p.tableFrames)
        {
            uint64_t delta = 0;
            if (!r.Varint(delta))
                return false;
            previous += delta;
            frame = previous;
        }
    }
    return r.Left() == 0;
}

bool TTDSessionFile::ReadSegmentWindow(const ITTDByteSource& source, TTDPieceStore& store, TTDSegmentPieces& out,
                                       std::string& error, uint64_t fromFrame, uint64_t toFrame,
                                       TTDSegmentWindowScratch& scratch, bool withTable)
{
    using namespace sessionstream;
    out.byFrame.clear();
    out.table.clear();
    out.owned.clear();
    out.window = true;
    out.tableFrame = fromFrame;
    scratch.work = {};

    uint64_t indexOffset = 0;
    if (!TTDContainerReader::ReadIndexExtra(source, scratch.extra, indexOffset, error))
        return false;
    TTDSeekIndex& index = scratch.index;
    if (!TTDSeekIndex::Decode(scratch.extra.data(), scratch.extra.size(), indexOffset, index) || index.parts.empty())
    {
        error = "the file has no seek index";
        return false;
    }
    const uint32_t line = index.line;
    const uint16_t piecesId = line ? BranchTwin(kPieces) : kPieces;
    const uint16_t checkpointsId = line ? BranchTwin(kCheckpoints) : kCheckpoints;
    const uint16_t tablesId = line ? BranchTwin(kPieceTables) : kPieceTables;
    const std::vector<TTDSeekIndexPart>& parts = index.parts;

    // The part holding the first frame (and its table), and the last part with a checkpoint of the stretch
    size_t tablePart = SIZE_MAX, tableOrdinal = 0;
    for (size_t k = parts.size(); k-- > 0 && tablePart == SIZE_MAX;)
    {
        if (parts[k].firstFrame > fromFrame)
            continue;
        tablePart = k;
        if (!withTable)
            break;
        const auto at = std::find(parts[k].tableFrames.begin(), parts[k].tableFrames.end(), fromFrame);
        if (at == parts[k].tableFrames.end() || parts[k].tablesOffset == 0)
            tablePart = SIZE_MAX - 1;   // the frame lies in this part: it has no table of it
        else
            tableOrdinal = static_cast<size_t>(at - parts[k].tableFrames.begin());
    }
    if (tablePart >= parts.size())
    {
        error = withTable && tablePart == SIZE_MAX - 1 ? "the file has no piece table at frame " + std::to_string(fromFrame)
                                                       : "the file has no part of frame " + std::to_string(fromFrame);
        return false;
    }
    size_t lastPart = tablePart;
    while (lastPart + 1 < parts.size() && parts[lastPart + 1].firstFrame <= toFrame)
        ++lastPart;
    const uint64_t itemCount = parts[lastPart].firstItem + parts[lastPart].items;

    // Per version of the file up to the last part: wanted, and its id once imported
    std::vector<uint8_t>& wanted = scratch.wanted;
    wanted.assign(static_cast<size_t>(itemCount), 0);
    std::vector<uint32_t>& wantedInPart = scratch.wantedInPart;
    wantedInPart.assign(lastPart + 1, 0);
    auto partOf = [&](uint64_t item) {
        const auto at = std::upper_bound(parts.begin(), parts.begin() + static_cast<std::ptrdiff_t>(lastPart) + 1, item,
                                         [](uint64_t i, const TTDSeekIndexPart& p) { return i < p.firstItem; });
        return static_cast<size_t>(at - parts.begin()) - 1;
    };
    auto want = [&](uint64_t item) {
        if (item >= itemCount)
            return false;
        if (!wanted[static_cast<size_t>(item)])
        {
            wanted[static_cast<size_t>(item)] = 1;
            ++wantedInPart[partOf(item)];
        }
        return true;
    };
    auto fail = [&](const std::string& why) {
        error = why;
        ReleaseSegmentPieces(store, out);
        return false;
    };

    // The table: every piece's version at the first frame, as numbers in the file
    struct Named
    {
        uint64_t frame;
        uint32_t region, piece, item;
    };
    std::vector<Named> tableItems;
    if (withTable)
    {
        const TTDSeekIndexPart& p = parts[tablePart];
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (!TTDContainerReader::ReadRecordAt(source, p.tablesOffset, p.tablesStored, tablesId, scratch.stored,
                                              scratch.record, data, size, error))
            return fail("piece tables of part " + std::to_string(tablePart) + ": " + error);
        scratch.work.bytesRead += p.tablesStored;
        TTDByteReader r(data, size);
        uint64_t tables = 0;
        bool ok = r.Varint(tables) && tables == p.tableFrames.size();
        for (uint64_t t = 0; ok && t <= tableOrdinal; ++t)
        {
            uint64_t ordinal = 0, regions = 0;
            ok = r.Varint(ordinal) && r.Varint(regions) && regions <= r.Left();
            for (uint64_t g = 0; ok && g < regions; ++g)
            {
                uint32_t region = 0, pieces = 0;
                ok = r.VarintAs(region) && r.VarintAs(pieces) && pieces <= r.Left();
                for (uint32_t piece = 0; ok && piece < pieces; ++piece)
                {
                    uint64_t item = 0;
                    ok = r.Varint(item);
                    if (ok && item != 0 && t == tableOrdinal)
                    {
                        ok = want(item - 1);
                        tableItems.push_back({fromFrame, region, piece, static_cast<uint32_t>(item - 1)});
                    }
                }
            }
        }
        if (!ok)
            return fail("part " + std::to_string(tablePart) + ": damaged piece tables");
    }

    // The stretch's checkpoints: the versions each names, numbered as the parts' checkpoints list them
    std::vector<Named> changeItems;
    std::vector<uint64_t> frames;   // of the stretch, in order
    for (size_t k = tablePart; k <= lastPart; ++k)
    {
        const TTDSeekIndexPart& p = parts[k];
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (p.checkpointsOffset == 0 ||
            !TTDContainerReader::ReadRecordAt(source, p.checkpointsOffset, p.checkpointsStored, checkpointsId,
                                              scratch.stored, scratch.record, data, size, error))
            return fail("checkpoints of part " + std::to_string(k) + ": " + error);
        scratch.work.bytesRead += p.checkpointsStored;
        TTDByteReader c(data, size);
        uint64_t cps = 0, frame = 0, item = p.firstItem;
        bool ok = c.Varint(cps);
        for (uint64_t i = 0; ok && i < cps; ++i)
        {
            uint64_t delta = 0, changedRegions = 0;
            ok = SkipCheckpointFields(c, delta, changedRegions);
            frame += delta;
            const bool inside = frame >= fromFrame && frame <= toFrame;
            if (ok && inside)
                frames.push_back(frame);
            for (uint64_t g = 0; ok && g < changedRegions; ++g)
            {
                uint32_t region = 0, changeCount = 0;
                ok = c.VarintAs(region) && c.VarintAs(changeCount) && changeCount <= c.Left();
                for (uint32_t n = 0; ok && n < changeCount; ++n, ++item)
                {
                    uint32_t piece = 0;
                    ok = c.VarintAs(piece);
                    if (ok && inside)
                    {
                        ok = want(item);
                        changeItems.push_back({frame, region, piece, static_cast<uint32_t>(item)});
                    }
                }
            }
        }
        if (!ok || item != p.firstItem + p.items)
            return fail("part " + std::to_string(k) + ": damaged checkpoints");
    }

    // The versions wanted and, from the last part back, the ones they build on. A part's piece record is read when
    // one of its versions is wanted: its versions' places are found by walking their headers
    struct Item
    {
        const uint8_t* payload = nullptr;
        uint32_t size = 0;
        uint32_t base = kNoItem;
        uint32_t crc = 0;
        uint16_t depth = 0;
        uint8_t encoding = 0;
    };
    std::vector<std::vector<Item>> items(lastPart + 1);
    if (scratch.pieces.size() < lastPart + 1)
        scratch.pieces.resize(lastPart + 1);
    for (size_t k = lastPart + 1; k-- > 0;)
    {
        if (wantedInPart[k] == 0)
            continue;
        const TTDSeekIndexPart& p = parts[k];
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (p.piecesOffset == 0 || !TTDContainerReader::ReadRecordAt(source, p.piecesOffset, p.piecesStored, piecesId,
                                                                    scratch.stored, scratch.pieces[k], data, size, error))
            return fail("pieces of part " + std::to_string(k) + ": " + error);
        scratch.work.bytesRead += p.piecesStored;
        ++scratch.work.partsRead;
        TTDByteReader r(data, size);
        std::vector<Item>& list = items[k];
        list.resize(p.items);
        bool ok = true;
        for (uint32_t n = 0; ok && n < p.items; ++n)
        {
            Item& it = list[n];
            uint32_t depth = 0;
            uint64_t base = 0;
            ok = r.U8(it.encoding) && r.VarintAs(depth) && r.Varint(base) && depth <= 0xFFFF;
            it.depth = static_cast<uint16_t>(depth);
            if (ok && base != 0)
            {
                // A branch's file names the base's line first; a folder file holds one line (its own)
                uint64_t number = base - 1;
                if (line != 0)
                    ok = r.Varint(number) && base - 1 == line;
                ok = ok && number < p.firstItem + n;
                it.base = static_cast<uint32_t>(number);
            }
            ok = ok && r.U32(it.crc) && r.VarintAs(it.size) && r.View(it.payload, it.size);
        }
        if (!ok || r.Left() != 0)
            return fail("part " + std::to_string(k) + ": damaged pieces");
        for (uint32_t n = p.items; n-- > 0;)
            if (wanted[static_cast<size_t>(p.firstItem) + n] && list[n].base != kNoItem)
                want(list[n].base);
    }

    // Imported in the file's order: a base before the versions built on it
    std::vector<TTDPieceId>& idOf = scratch.idOf;
    idOf.assign(static_cast<size_t>(itemCount), TTDPieceStore::kNone);
    for (size_t k = 0; k <= lastPart; ++k)
    {
        if (wantedInPart[k] == 0)
            continue;
        const TTDSeekIndexPart& p = parts[k];
        for (uint32_t n = 0; n < p.items; ++n)
        {
            const size_t number = static_cast<size_t>(p.firstItem) + n;
            if (!wanted[number])
                continue;
            const Item& it = items[k][n];
            const TTDPieceId base = it.base == kNoItem ? TTDPieceStore::kNone : idOf[it.base];
            const TTDPieceId id = store.Import(static_cast<TTDPieceStore::Encoding>(it.encoding), base, it.depth, it.crc,
                                               it.payload, it.size);
            if (id == TTDPieceStore::kNone)
                return fail("part " + std::to_string(k) + ": a version does not fit its base");
            idOf[number] = id;
            out.owned.push_back(id);
        }
    }

    scratch.work.versionsImported = static_cast<uint32_t>(out.owned.size());
    out.table.reserve(tableItems.size());
    for (const Named& n : tableItems)
        out.table.push_back({n.region, n.piece, idOf[n.item]});
    for (const uint64_t frame : frames)
        out.byFrame[frame];   // a checkpoint without changes is of the stretch too
    for (const Named& n : changeItems)
        out.byFrame[n.frame].push_back({n.region, n.piece, idOf[n.item]});
    return true;
}

}  // namespace ttd
