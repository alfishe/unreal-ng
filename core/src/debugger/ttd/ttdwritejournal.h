#pragma once

/// @file ttdwritejournal.h
/// @brief TTD write journal — fast-path accelerator for reverse watchpoints.
///
/// Per parent TDD §9.3 ("Write Journal (Fast Path)"):
///   "Appended from the same MemoryWriteDebug hook. Ring buffer, default
///    64 MB ≈ 5.5M writes ≈ ~50 seconds at max intensity (action game).
///    Measured: ~2300 writes/frame peak, ~130 writes/frame typical game.
///    FindLastWrite scans the journal backward (memory-bandwidth-fast);
///    only if the journal has wrapped past the target window does it
///    fall back to [two-pass silent replay]."
///
/// Record format (TDD §9.3): 12 bytes per write. Bit-packed globalT keeps
/// the record compact while still covering ~9 years of t-states at the
/// ZX Spectrum's 3.5 MHz clock. m1pc + value + physPage give the reverse-
/// watchpoint UI everything it needs to display "the byte was last written
/// by instruction at PC=X with value=V in physical page P" — no second
/// probe pass required when the journal hit is in-window.
///
/// Thread model: single-producer (emulator thread appends via Append),
/// multi-consumer (control thread reads via FindLast under pause, control
/// thread mutates via DropAfter / Clear under pause). The replay fallback
/// owns the emulator thread during its silent RunTStates passes, so no
/// concurrent append is possible while a query is in flight. No internal
/// lock — matches the page store's threading model.
///
/// Ring behavior: capacity is rounded up to a power-of-two record count.
/// When the ring fills, the oldest records are overwritten. _seqHead /
/// _seqTail are absolute append counts (not indices) — the delta gives the
/// live count, and an index in the live range is computed as
/// `_ring[(_seqTail + offset) & _mask]`. _seqHead monotonicity also lets a
/// deserialized journal preserve global ordering across save/load.

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <ostream>
#include <istream>


namespace ttd {

#pragma pack(push, 1)
/// @brief A single recorded memory or port write.
///
/// Per TDD §9.3. Bit fields are little-endian (matches x86 / Apple Silicon
/// memory layout) — the journal is not portable across endianness, matching
/// the rest of the .ttd format.
struct TTDWriteRecord
{
    uint64_t globalT  : 40;   ///< Absolute t-state since session start (~9 years max)
    uint64_t addr     : 16;   ///< Z80 address (or port number when isIo == 1)
    uint64_t isIo     : 1;    ///< 1 = port OUT, 0 = memory write
    uint64_t pad      : 7;    ///< Reserved (alignment / future flags)
    uint16_t m1pc;            ///< PC of the writing instruction
    uint8_t  value;           ///< Byte written
    uint8_t  physPage;        ///< Physical RAM page (disambiguates banked writes)
};
#pragma pack(pop)
static_assert(sizeof(TTDWriteRecord) == 8 + 2 + 1 + 1,
              "TTDWriteRecord must pack to 12 bytes per TDD §9.3");

/// @brief Ring-buffered journal of memory/port writes.
///
/// Append is O(1). FindLast is O(N) over the live ring — at ~5.5M records
/// that's ~25 ms memory-bandwidth-bound scan, well below interactive
/// latency. DropAfter and Clear are O(N) but rare (resume-from-past,
/// invalidate, session end).
class TTDWriteJournal
{
public:
    /// @brief Construct with a ring capacity in bytes (rounded up to the
    /// next power-of-two records). Default 64 MB — provides ~50 seconds at
    /// max-intensity workload (2300 writes/frame), several minutes typical.
    ///
    /// Memory is committed lazily, one chunk at a time as the ring fills (see
    /// LazyRing), so construction is cheap and a short session pays only for
    /// the records it writes.
    ///
    /// @param ringBytes  Desired ring size in bytes.
    /// @param asyncAlloc Kept for API compatibility: construction no longer
    ///                   allocates the ring, so there is nothing to offload.
    explicit TTDWriteJournal(size_t ringBytes = 64u * 1024 * 1024,
                             bool asyncAlloc = false);

    ~TTDWriteJournal() = default;

    TTDWriteJournal(const TTDWriteJournal&) = delete;
    TTDWriteJournal& operator=(const TTDWriteJournal&) = delete;

    /// @brief Kept for API compatibility: the journal is ready on construction.
    void WaitReady() {}

    /// @brief Kept for API compatibility: always true.
    bool IsReady() const { return true; }

    // -----------------------------------------------------------------------
    // Capture path (emulator thread only; called from MemoryWriteDebug /
    // DecodePortOut hooks)
    // -----------------------------------------------------------------------

    /// @brief Append a write record.
    ///
    /// Caller (TimeTravelManager::RecordMemoryWrite / RecordIoWrite) is
    /// responsible for monotonicity of `globalT`. This method just stores.
    void Append(const TTDWriteRecord& rec);

    // -----------------------------------------------------------------------
    // Query path (control thread; emulator must be paused)
    // -----------------------------------------------------------------------

    /// @brief Find the newest record with globalT <= beforeT matching pred.
    ///
    /// Scans backward from the most-recent append. Returns std::nullopt if
    /// no record in the live ring matches. Callers that need to verify
    /// "no match in the live ring" vs "no match in the entire session"
    /// should check OldestGlobalT() afterwards: if it's greater than the
    /// requested beforeT minus the query window, the ring may have wrapped
    /// past older matching records and the replay fallback is needed.
    std::optional<TTDWriteRecord> FindLast(
        uint64_t beforeT,
        const std::function<bool(const TTDWriteRecord&)>& pred) const;

    /// @brief Oldest globalT currently held in the ring.
    ///
    /// Returns 0 when the ring is empty. Used by FindLastAccess to decide
    /// whether to fall back to two-pass replay.
    uint64_t OldestGlobalT() const;

    /// @brief Newest globalT currently held in the ring (or 0 if empty).
    uint64_t NewestGlobalT() const;

    // -----------------------------------------------------------------------
    // Lifecycle (control thread; emulator paused)
    // -----------------------------------------------------------------------

    /// @brief Drop every record with globalT strictly greater than @p globalT.
    ///
    /// Used by Resume-from-past: when the user resumes from a Detached
    /// position T, history > T is discarded, and so are writes recorded
    /// after T. Writes exactly at T are kept.
    ///
    /// Implementation note: drops the records by advancing _seqTail past
    /// them and zeroing the slots. Capacity is unchanged.
    void DropAfter(uint64_t globalT);

    /// @brief Drop all records. Called by InvalidateSession / StartRecording.
    void Clear();

    // -----------------------------------------------------------------------
    // Serialization (.ttd v3 — see ttddumpformat.h)
    // -----------------------------------------------------------------------

    /// @brief Write the live records as a length-prefixed block.
    ///
    /// Format: u8 count, then count * sizeof(TTDWriteRecord) bytes.
    /// Called by TimeTravelManager::SerializeSession under pause.
    bool Serialize(std::ostream& out) const;

    /// @brief Read records written by Serialize.
    ///
    /// Replaces the current contents. Caller passes the count read from
    /// the header; this method reads exactly that many records.
    bool Deserialize(std::istream& in, uint64_t count);

    // -----------------------------------------------------------------------
    // Stats
    // -----------------------------------------------------------------------

    /// @brief Number of records currently in the ring (live count).
    inline size_t Size() const { return static_cast<size_t>(_seqHead - _seqTail); }

    /// @brief Ring capacity in records (NOT bytes).
    inline size_t Capacity() const { return _ring.size(); }

    /// @brief True iff Size() == 0.
    inline bool IsEmpty() const { return _seqHead == _seqTail; }

    /// @brief Read-only access to the sequence cursors and to a live record by
    /// its sequence number, SeqTail() <= seq < SeqHead() (for tests and for
    /// the analyzer tool — not used by the engine itself).
    inline const TTDWriteRecord& RecordAt(uint64_t seq) const { return _ring[SeqToIdx(seq)]; }
    inline uint64_t SeqHead() const { return _seqHead; }
    inline uint64_t SeqTail() const { return _seqTail; }

private:
    /// @brief Resolve a sequence number to a ring index.
    inline size_t SeqToIdx(uint64_t seq) const { return static_cast<size_t>(seq & _mask); }

    /// Power-of-two ring of records whose memory is committed a chunk at a
    /// time on first write. Allocating (and zero-filling) the whole 64 MiB
    /// ring up front cost every recording start ~10% of a full test run in
    /// memory fills, even when it was offloaded to a background thread
    /// (StartRecording waits for it) - while a typical session only ever
    /// touches a small part of it. A slot in a chunk not yet written reads as
    /// a zero record, exactly like the zero-filled ring did.
    class LazyRing
    {
    public:
        static constexpr size_t kMaxChunkShift = 16;  // 64K records = 768 KiB per chunk

        void Configure(size_t capacity)
        {
            _capacity = capacity;
            _chunkShift = 0;
            while ((size_t(1) << _chunkShift) < capacity && _chunkShift < kMaxChunkShift)
                _chunkShift++;
            _chunkMask = (size_t(1) << _chunkShift) - 1;
            _chunks.clear();
            _chunks.resize((capacity + _chunkMask) >> _chunkShift);
        }

        size_t size() const { return _capacity; }

        /// Writable slot: commits (zero-initialized) its chunk on first use
        TTDWriteRecord& operator[](size_t idx)
        {
            std::unique_ptr<TTDWriteRecord[]>& chunk = _chunks[idx >> _chunkShift];
            if (!chunk)
                chunk.reset(new TTDWriteRecord[_chunkMask + 1]());
            return chunk[idx & _chunkMask];
        }

        /// Read-only slot: a never-written chunk reads as zero records
        const TTDWriteRecord& operator[](size_t idx) const
        {
            static const TTDWriteRecord kZero{};
            const std::unique_ptr<TTDWriteRecord[]>& chunk = _chunks[idx >> _chunkShift];
            return chunk ? chunk[idx & _chunkMask] : kZero;
        }

    private:
        std::vector<std::unique_ptr<TTDWriteRecord[]>> _chunks;
        size_t _capacity = 0;
        size_t _chunkShift = 0;
        size_t _chunkMask = 0;
    };

    LazyRing _ring;                      // power-of-two size
    size_t   _mask = 0;                  // capacity - 1, for fast modulo
    uint64_t _seqHead = 0;               // absolute count of appends
    uint64_t _seqTail = 0;               // absolute seq of oldest live record
};

} // namespace ttd
