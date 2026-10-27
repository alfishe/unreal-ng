#include "debugger/ttd/ttdsessionfacts.h"

#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/ttdbookmarks.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralmask.h"
#include "debugger/ttd/ttdperipheralregistry.h"

namespace ttd
{

namespace
{
/// 3 (D23): the not-recorded devices as keys, no mask of the recorded ones (the session's device table lists them);
/// 2: two masks of v1 ids as word lists (u8 count, u64 words: any u8 id); 1: one u64 each (ids 0..63). The masks of
/// versions 1 and 2 are read: the recorded one dropped, the not-recorded one as keys {type of the id, ""}
constexpr uint16_t kFactsVersion = 3;
constexpr uint16_t kFactsVersionFirst = 1;
constexpr uint16_t kFactsVersionKeys = 3;

bool ReadMask(TTDByteReader& r, uint16_t version, PeripheralMask& mask)
{
    mask = PeripheralMask{};
    if (version < 2)
    {
        uint64_t low = 0;
        if (!r.U64(low))
            return false;
        mask = PeripheralMask::FromLow(low);
        return true;
    }
    uint8_t words = 0;
    if (!r.U8(words) || words == 0 || words > PeripheralMask::kWords)
        return false;
    for (uint8_t i = 0; i < words; ++i)
    {
        uint64_t value = 0;
        if (!r.U64(value))
            return false;
        mask.SetWord(i, value);
    }
    return true;
}
constexpr uint16_t kBookmarksVersion = 1;
}  // namespace

std::vector<uint8_t> EncodeSessionFacts(const TTDSessionFacts& facts)
{
    TTDByteWriter w;
    w.U16(kFactsVersion);
    w.U8(facts.modelId);
    w.U16(facts.modelRamPages);
    w.U64(facts.romSignature);
    w.U64(facts.capturedAtUnixMs);
    w.Str(facts.recordedBy);
    w.Varint(facts.notRecorded.size());
    for (const TTDDeviceKey& key : facts.notRecorded)
    {
        w.U16(static_cast<uint16_t>(key.type));
        w.Str(key.instance);
    }
    w.U8(facts.inputHistoryComplete ? 1 : 0);
    w.U8(facts.portJournalValid ? 1 : 0);
    w.Str(facts.portJournalOffReason);
    return std::move(w.bytes);
}

bool DecodeSessionFacts(const std::vector<uint8_t>& bytes, TTDSessionFacts& facts)
{
    TTDByteReader r(bytes);
    uint16_t version = 0;
    uint8_t input = 0, port = 0;
    if (!r.U16(version) || version < kFactsVersionFirst || version > kFactsVersion)
        return false;
    TTDSessionFacts f;
    if (!r.U8(f.modelId) || !r.U16(f.modelRamPages) || !r.U64(f.romSignature) || !r.U64(f.capturedAtUnixMs) ||
        !r.Str(f.recordedBy))
        return false;
    if (version < kFactsVersionKeys)
    {
        PeripheralMask recorded, notRecorded;
        if (!ReadMask(r, version, recorded) || !ReadMask(r, version, notRecorded))
            return false;
        f.notRecorded = TTDPeripheralRegistry::KeysOfV1Mask(notRecorded);
    }
    else
    {
        uint64_t count = 0;
        if (!r.Varint(count) || count > r.Left())
            return false;
        for (uint64_t i = 0; i < count; ++i)
        {
            uint16_t type = 0;
            TTDDeviceKey key;
            if (!r.U16(type) || !r.Str(key.instance))
                return false;
            key.type = static_cast<TTDDeviceType>(type);
            f.notRecorded.push_back(std::move(key));
        }
    }
    if (!r.U8(input) || !r.U8(port) || !r.Str(f.portJournalOffReason))
        return false;
    f.inputHistoryComplete = input != 0;
    f.portJournalValid = port != 0;
    facts = std::move(f);
    return true;
}

std::vector<uint8_t> EncodeBookmarks(const TTDBookmarkJournal& bookmarks)
{
    const std::vector<TTDBookmark> all = bookmarks.Snapshot();
    TTDByteWriter w;
    w.U16(kBookmarksVersion);
    w.Varint(all.size());
    for (const TTDBookmark& b : all)
    {
        w.U64(b.time.frame);
        w.U32(b.time.tInFrame);
        w.Str(b.label);
    }
    return std::move(w.bytes);
}

bool DecodeBookmarks(const std::vector<uint8_t>& bytes, TTDBookmarkJournal& bookmarks)
{
    TTDByteReader r(bytes);
    uint16_t version = 0;
    uint64_t count = 0;
    if (!r.U16(version) || version != kBookmarksVersion || !r.Varint(count) || count > r.Left())
        return false;
    TTDBookmarkJournal read;
    for (uint64_t i = 0; i < count; ++i)
    {
        TTDBookmark b;
        if (!r.U64(b.time.frame) || !r.U32(b.time.tInFrame) || !r.Str(b.label))
            return false;
        read.Add(b);
    }
    bookmarks.Clear();
    for (const TTDBookmark& b : read.Snapshot())
        bookmarks.Add(b);
    return true;
}

bool ReadHolderStream(const ITTDByteSource& source, uint16_t id, std::vector<uint8_t>& out, std::string& error)
{
    TTDContainerReader reader;
    if (!reader.Open(source, error, [](uint16_t stream) { return TTDSessionFile::KnownStream(stream); }))
        return false;
    for (auto part = reader.Parts().rbegin(); part != reader.Parts().rend(); ++part)
        for (auto record = part->records.rbegin(); record != part->records.rend(); ++record)
            if (record->streamId == id)
                return reader.ReadRecord(*record, out, &error);
    error = "no holder stream " + std::to_string(id);
    return false;
}

bool ReadEngineFileInfo(const ITTDByteSource& source, TTDFileInfo& info, std::string& error)
{
    info = TTDFileInfo{};
    TTDContainerReader reader;
    if (!reader.Open(source, error, [](uint16_t stream) { return TTDSessionFile::KnownStream(stream); }))
        return false;
    info.schemaVersion = 2;
    info.flags = reader.Header().flags;
    info.topClockTime = true;
    info.peripheralsFromHeader = true;
    const std::vector<TTDPartRef>& parts = reader.Parts();
    if (!parts.empty())
    {
        info.startFrame = parts.front().firstFrame;
        info.endFrame = parts.back().firstFrame + (parts.back().frameCount ? parts.back().frameCount - 1 : 0);
    }
    bool bookmarks = false;
    std::vector<uint8_t> factBytes;
    for (const TTDPartRef& part : parts)
    {
        info.checkpointCount += part.frameCount;   // one checkpoint per recorded frame
        for (const TTDRecordRef& record : part.records)
        {
            info.hasWriteJournal |= record.streamId == sessionstream::kWriteJournal;
            info.hasCoverageIndex |= record.streamId == holderstream::kCoverage;
            bookmarks |= record.streamId == holderstream::kBookmarks;
        }
    }
    TTDSessionFacts facts;
    if (!ReadHolderStream(source, holderstream::kFacts, factBytes, error) || !DecodeSessionFacts(factBytes, facts))
    {
        error = "the file does not say which machine recorded it (no controller facts)";
        return false;
    }
    if (bookmarks)
    {
        std::vector<uint8_t> bytes;
        TTDBookmarkJournal read;
        std::string why;
        info.hasBookmarks = ReadHolderStream(source, holderstream::kBookmarks, bytes, why) &&
                            DecodeBookmarks(bytes, read) && read.Size() > 0;
    }
    info.capturedAtUnixMs = facts.capturedAtUnixMs;
    info.emulatorId = facts.recordedBy;
    info.hasInputJournal = facts.inputHistoryComplete;
    info.hasExternalEvents = facts.inputHistoryComplete;
    info.hasPortJournals = facts.portJournalValid;
    info.machine.modelId = facts.modelId;
    info.machine.ramPageBound = facts.modelRamPages;
    info.machine.romSignature = facts.romSignature;
    std::vector<TTDDeviceDescriptor> devices;
    if (!TTDSessionFile::ReadDevices(source, devices, error))
        return false;
    for (const TTDDeviceDescriptor& d : devices)
        info.machine.devices.push_back({d.Key(), d.legacyId});
    for (const TTDDeviceKey& key : facts.notRecorded)
        info.machine.notRecordedDevices.push_back({key, PeripheralId::None});
    DescribeRecordedMachine(info.machine);
    return true;
}

}  // namespace ttd
