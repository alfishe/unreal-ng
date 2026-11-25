#pragma once

/// @file ttdcontainer.h
/// @brief The TTD v2 session container (Phase 4, Step 2; design:
/// docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-session-file-tdd.md
/// §5.1-§5.2): records of numbered streams, grouped into parts, written only
/// at the end of the file, each record checked by CRC32C. It knows nothing
/// of what the streams hold; the session layer puts the engine's data in.
///
/// Layout:
///
///   header   magic "TTDD", schema 2, flags, size, CRC, session UUID,
///            creation time, zstd version, stream table, session tables
///   part 0   [record][record]...[part-end record]
///   part 1   ...
///   index    part table, stream totals, session index data   (finalize)
///   trailer  24 bytes: index offset, size, CRC, magic "TTDX", CRC
///
/// A record is a 32-byte header (sync "TREC", stream, flags, part, sequence,
/// stored and raw size, payload CRC, header CRC) and its payload, zstd-
/// compressed when that is smaller. A part ends with a part-end record
/// (stream 0) naming its frames, its records and the earlier parts it
/// depends on.
///
/// Integrity (owner decision 2026-10-04, "open with holes"): the header, the
/// index and every part-end record are checked when the file opens; a
/// record's payload when it is first read. A file without a trailer (a crash)
/// opens by scanning its records: every complete part is kept, an incomplete
/// last part is dropped. A damaged part is a hole: it, and every part that
/// depends on it, is unreachable; the others stay readable. An unknown
/// required stream refuses the file, naming the stream; an unknown ancillary
/// one is skipped.
///
/// Chunked streams (F3, 2026-11-16): a record holds at most kMaxRecordRaw bytes
/// raw (the readers' allocation bound). A larger payload of stream S is
/// written as several records of S's chunked twin (ChunkedTwin(S) = S | 0x8000),
/// one after another in the part, every one but the last flagged "continued";
/// the reader joins them back into one payload of S. The twin is declared in
/// the header with S's kind, so a reader that does not know chunks skips an
/// ancillary one (it reads the file as one without that stream) and refuses a
/// required one, naming it; it never reads a piece of a payload as the whole.
/// A payload over the limit of a stream whose twin is not declared is refused
/// with the sizes (TTDWriteFailure::TooLarge).
///
/// Worked example: a writer begins a file with streams 1 (pieces, required)
/// and 7 (write journal, ancillary), adds two records, ends part 0 (frames
/// 0-49), adds one record, ends part 1 (frames 50-99, depends on part 0) and
/// finalizes. A reader opening it sees two parts. Flip a byte inside part 0's
/// first payload: the file still opens; reading that record fails its CRC,
/// the caller marks part 0 damaged, and part 1 becomes unreachable too.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace platform
{
class AppendFile;
class RandomAccessFile;
}  // namespace platform

namespace ttd
{

constexpr uint16_t kContainerSchema = 2;
constexpr size_t kContainerFixedHeader = 64;
constexpr size_t kRecordHeaderSize = 32;
constexpr size_t kTrailerSize = 24;
constexpr uint16_t kContainerStream = 0;   ///< part-end records
/// A record never holds more raw bytes (the readers' allocation bound per record): larger payloads are chunked
constexpr uint32_t kMaxRecordRaw = 1u << 30;
/// The chunked twin of a stream: its records, joined in order, are one payload of the stream
constexpr uint16_t kChunkedStreamBit = 0x8000;
inline uint16_t ChunkedTwin(uint16_t id) { return static_cast<uint16_t>(id | kChunkedStreamBit); }
inline bool IsChunkedTwin(uint16_t id) { return (id & kChunkedStreamBit) != 0; }
inline uint16_t StreamOfChunkedTwin(uint16_t id) { return static_cast<uint16_t>(id & ~kChunkedStreamBit); }

/// Why a write failed (TTDContainerWriter::Error): what the caller tells its user, and the HTTP status it picks
enum class TTDWriteFailure : uint8_t
{
    None,
    Io,         ///< the file system refused a write or a sync (the system's reason in the text)
    NoSpace,    ///< ENOSPC / EDQUOT (ERROR_DISK_FULL): the disk or the quota is full
    TooLarge,   ///< a record passes the format's limit (the sizes in the text)
    Layout,     ///< the writer was used out of order, or a part could not be laid out
};

struct TTDWriteError
{
    TTDWriteFailure kind = TTDWriteFailure::None;
    std::string text;
};

enum class TTDStreamKind : uint8_t
{
    Required = 0,    ///< needed to restore exactly: a reader that does not know it refuses the file
    Ancillary = 1,   ///< useful, not needed (journals, coverage, screenshots): skipped when unknown
};

struct TTDStreamDesc
{
    uint16_t id = 0;
    uint16_t layoutVersion = 1;
    TTDStreamKind kind = TTDStreamKind::Required;
    std::string name;
};

struct TTDContainerHeader
{
    uint16_t flags = 0;
    std::array<uint8_t, 16> uuid{};
    uint64_t createdMicros = 0;   ///< microseconds since 1970, UTC
    uint32_t zstdVersion = 0;     ///< filled by the writer
    std::vector<TTDStreamDesc> streams;
    std::vector<uint8_t> sessionTables;   ///< the session layer's tables (regions, devices, configuration)

    const TTDStreamDesc* Stream(uint16_t id) const;
};

/// What the writer is told when a part ends
struct TTDPartEnd
{
    uint64_t firstFrame = 0;
    uint32_t frameCount = 0;
    uint16_t branch = 0;
    std::vector<uint32_t> dependencies;   ///< earlier parts this one needs (D6)
    std::vector<uint8_t> extra;           ///< the session layer's part data (item numbers)
};

/// Where the bytes go: a platform file, or memory (tests)
class ITTDByteSink
{
public:
    virtual ~ITTDByteSink() = default;
    virtual bool Write(const uint8_t* data, size_t size) = 0;
    virtual bool Sync() = 0;
    virtual uint64_t Size() const = 0;
    /// Why the last Write or Sync failed (the system's words); empty when the sink cannot say
    virtual std::string LastError() const { return {}; }
    /// The last failure was a full disk or quota (the only one a caller may call "disk full")
    virtual bool LastErrorNoSpace() const { return false; }
};

/// Where the bytes come from
class ITTDByteSource
{
public:
    virtual ~ITTDByteSource() = default;
    virtual uint64_t Size() const = 0;
    virtual bool ReadAt(uint64_t offset, uint8_t* out, size_t size) const = 0;
};

class TTDMemorySink : public ITTDByteSink
{
public:
    bool Write(const uint8_t* data, size_t size) override;
    bool Sync() override { return true; }
    uint64_t Size() const override { return bytes.size(); }
    std::vector<uint8_t> bytes;
};

class TTDMemorySource : public ITTDByteSource
{
public:
    explicit TTDMemorySource(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    uint64_t Size() const override { return bytes.size(); }
    bool ReadAt(uint64_t offset, uint8_t* out, size_t size) const override;
    std::vector<uint8_t> bytes;
};

class TTDFileSink : public ITTDByteSink
{
public:
    /// Create the file (it must not exist); Valid() false with Error() when it cannot be created
    explicit TTDFileSink(const std::string& utf8Path);
    ~TTDFileSink() override;
    bool Valid() const { return _file != nullptr; }
    const std::string& Error() const { return _error; }
    bool Write(const uint8_t* data, size_t size) override;
    bool Sync() override;
    uint64_t Size() const override;
    std::string LastError() const override;
    bool LastErrorNoSpace() const override;
    void Close();

private:
    std::string _error;  // declared before _file: _file's initializer writes into it
    std::unique_ptr<platform::AppendFile> _file;
};

class TTDFileSource : public ITTDByteSource
{
public:
    explicit TTDFileSource(const std::string& utf8Path);
    ~TTDFileSource() override;
    bool Valid() const { return _file != nullptr; }
    const std::string& Error() const { return _error; }
    uint64_t Size() const override;
    bool ReadAt(uint64_t offset, uint8_t* out, size_t size) const override;

private:
    std::string _error;  // declared before _file: _file's initializer writes into it
    std::unique_ptr<platform::RandomAccessFile> _file;
};

class TTDContainerWriter
{
public:
    /// Write the header. False with the reason when the sink fails
    bool Begin(ITTDByteSink& sink, const TTDContainerHeader& header, std::string* error = nullptr);
    /// Append one payload of the current part: one record, compressed when that is smaller and @p compress is set;
    /// over the record limit, several records of the stream's chunked twin when the header declares it
    bool AddRecord(uint16_t streamId, const uint8_t* data, size_t size, bool compress = true);
    bool AddRecord(uint16_t streamId, const std::vector<uint8_t>& data, bool compress = true)
    {
        return AddRecord(streamId, data.data(), data.size(), compress);
    }
    /// Append a record as another file stored it (a join): payload, flags and raw size kept
    bool AddStoredRecord(uint16_t streamId, uint16_t flags, const uint8_t* stored, size_t storedSize, size_t rawSize);
    /// Close the current part with its part-end record; @p sync makes it durable
    bool EndPart(const TTDPartEnd& part, bool sync = false);
    /// Write the index and the trailer (an open part is ended first only by the caller). @p indexExtra: the
    /// session layer's index data (the frame table)
    bool Finalize(const std::vector<uint8_t>& indexExtra = {});

    uint32_t PartCount() const { return _partIndex; }
    bool Failed() const { return _failed; }
    /// Why the writer failed (kind None while it has not)
    const TTDWriteError& Error() const { return _error; }
    /// The raw bytes one record may hold (kMaxRecordRaw by default); tests set a small one so a chunked stream
    /// takes milliseconds. Set before Begin
    void SetRecordLimit(uint32_t maxRaw) { _recordLimit = maxRaw == 0 ? kMaxRecordRaw : std::min(maxRaw, kMaxRecordRaw); }
    uint32_t RecordLimit() const { return _recordLimit; }

private:
    struct PartEntry
    {
        uint64_t partEndOffset = 0;
        uint64_t firstFrame = 0;
        uint32_t frameCount = 0;
        uint16_t branch = 0;
    };
    struct StreamTotal
    {
        uint64_t records = 0, stored = 0, raw = 0;
    };

    bool WriteRecord(uint16_t streamId, uint16_t flags, const uint8_t* stored, size_t storedSize, size_t rawSize);
    bool WritePayload(uint16_t streamId, uint16_t flags, const uint8_t* data, size_t size, bool compress);
    void Count(uint16_t streamId, size_t stored, size_t raw);
    bool Fail(TTDWriteFailure kind, std::string text);
    /// False when the writer failed before (its first reason stays) or was not begun
    bool Usable();
    /// A failed sink write or sync: Io, or NoSpace when the sink says so, with its reason after @p what
    bool FailSink(const std::string& what);
    std::string StreamName(uint16_t id) const;

    ITTDByteSink* _sink = nullptr;
    bool _failed = false;
    TTDWriteError _error;
    uint32_t _recordLimit = kMaxRecordRaw;
    std::vector<TTDStreamDesc> _streams;   ///< the header's, for the reasons and the chunked twins
    uint32_t _partIndex = 0;
    uint32_t _sequence = 0;
    std::vector<uint64_t> _partRecords;   ///< offsets of the current part's records
    std::vector<PartEntry> _parts;
    std::vector<std::pair<uint16_t, StreamTotal>> _totals;
};

/// A record as the part lists it. TTDPartRef::records joins a chunked stream's records into one (chunks > 1):
/// streamId is the stream (not its twin), offset, flags, sequence and sizes those of its first record, chunkedRaw
/// the whole payload's raw size; ReadRecord reads it whole
struct TTDRecordRef
{
    uint64_t offset = 0;   ///< of the record header
    uint16_t streamId = 0;
    uint16_t flags = 0;
    uint32_t partIndex = 0;
    uint32_t sequence = 0;
    uint32_t storedSize = 0;
    uint32_t rawSize = 0;
    uint32_t payloadCrc = 0;
    uint32_t chunks = 1;        ///< the records holding the payload (more than one: a chunked stream)
    uint64_t chunkedRaw = 0;    ///< chunks > 1: the payload's raw size, every chunk together
};

struct TTDPartRef
{
    uint32_t index = 0;
    uint64_t firstFrame = 0;
    uint32_t frameCount = 0;
    uint16_t branch = 0;
    std::vector<uint32_t> dependencies;
    std::vector<uint8_t> extra;
    std::vector<TTDRecordRef> records;   ///< one per payload, chunked streams joined; without the part-end record
    std::vector<TTDRecordRef> physical;  ///< every record as stored (a chunked stream's one by one): for copies
    bool damaged = false;
    std::string damage;                  ///< why
};

class TTDContainerReader
{
public:
    /// Open: header, then the index from the trailer, or a scan when there is none (a crash). @p knownStream
    /// says which stream ids the caller understands; an unknown required one refuses the file
    bool Open(const ITTDByteSource& source, std::string& error,
              const std::function<bool(uint16_t)>& knownStream = nullptr);

    const TTDContainerHeader& Header() const { return _header; }
    const std::vector<TTDPartRef>& Parts() const { return _parts; }
    /// The trailer and index were valid (a finished file); false: opened by scanning
    bool Finalized() const { return _finalized; }
    const std::vector<uint8_t>& IndexExtra() const { return _indexExtra; }
    /// What the open found and left out: an incomplete last part, damaged part-end records, unknown streams
    const std::vector<std::string>& Notes() const { return _notes; }

    /// Read one record's payload, checking its CRC and decompressing it. On a damaged payload the record's
    /// part is marked damaged (MarkDamaged) and false is returned with the reason
    bool ReadRecord(const TTDRecordRef& record, std::vector<uint8_t>& out, std::string* error = nullptr);
    /// The record's payload as stored (CRC checked, not decompressed); one physical record (TTDPartRef::physical)
    bool ReadStored(const TTDRecordRef& record, std::vector<uint8_t>& out, std::string* error = nullptr);

    /// A finished file's index extra and the offset its index starts at, from the trailer and the index alone (two
    /// reads; no part-end record, no record header). False with the reason for a file without a valid trailer
    static bool ReadIndexExtra(const ITTDByteSource& source, std::vector<uint8_t>& extra, uint64_t& indexOffset,
                               std::string& error);
    /// One record whose place is known (a session layer's own index): the header at @p offset checked (its CRC,
    /// stream @p streamId, part @p partIndex, @p storedSize bytes stored), the payload's CRC checked, the payload
    /// decompressed. The part is checked so that an index naming another part's record of the same stream is
    /// refused. Read in one read into @p buffer (@p stored: scratch for a compressed payload); @p data / @p size
    /// name the payload inside @p buffer. False with the reason
    static bool ReadRecordAt(const ITTDByteSource& source, uint64_t offset, uint32_t storedSize, uint16_t streamId,
                             uint32_t partIndex, std::vector<uint8_t>& stored, std::vector<uint8_t>& buffer,
                             const uint8_t*& data, size_t& size, std::string& error);

    void MarkDamaged(uint32_t partIndex, const std::string& reason);
    /// A part is reachable when neither it nor any part it depends on (transitively) is damaged
    bool IsReachable(uint32_t partIndex) const;

private:
    bool ReadHeader(std::string& error);
    bool ReadIndex(std::string& error);
    void Scan();
    bool ReadRecordHeader(uint64_t offset, TTDRecordRef& out) const;
    bool ParsePartEnd(const TTDRecordRef& partEnd, TTDPartRef& out, std::string& why) const;
    /// TTDPartRef::records from its physical records: a chunked stream's joined. False with the reason when a
    /// chunked stream is cut or a record claims a continuation it has not
    static bool JoinChunks(TTDPartRef& part, std::string& why);

    const ITTDByteSource* _source = nullptr;
    TTDContainerHeader _header;
    uint64_t _dataStart = 0;
    std::vector<TTDPartRef> _parts;
    std::vector<uint8_t> _indexExtra;
    std::vector<std::string> _notes;
    bool _finalized = false;
};

}  // namespace ttd
