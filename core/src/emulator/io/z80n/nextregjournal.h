#pragma once

/// @file nextregjournal.h
/// @brief The NextREG write journal (docs/inprogress/2026-10-07-zx-next/design-nextreg-journal.md): a bounded ring of every write to
/// the Next's register file with who wrote it (NEXTREG instruction / port #253B / copper / the board itself), when (frame, T, PC)
/// and the value before. The NEXTREG instruction and the copper make no port cycle, so the port trace and the TTD I/O journal
/// never see them; this is the one place that does.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The door a register write came through
enum class NextRegSource : uint8_t
{
    Internal = 0,  ///< the board itself: reset, the NEX loader's set-up, a device writing its own mirror
    NextReg = 1,   ///< the NEXTREG n,v / NEXTREG n,A instructions (no bus cycle)
    Port = 2,      ///< OUT (#253B) after OUT (#243B)
    Copper = 3,    ///< a copper MOVE
};

const char* NextRegSourceName(NextRegSource source);

struct NextRegWriteEvent
{
    uint64_t seq = 0;
    uint32_t frame = 0;
    uint32_t t = 0;  ///< T-state in the frame
    uint16_t pc = 0;
    NextRegSource source = NextRegSource::Internal;
    uint8_t reg = 0;
    uint8_t value = 0;
    uint8_t previous = 0;
};

/// "07,02" / "0x07" / "#07": register numbers; "nextreg,copper": source names. Empty = no filter
struct NextRegJournalQuery
{
    std::vector<uint8_t> regs;
    std::vector<NextRegSource> sources;
    uint64_t since = 0;  ///< only events with seq > since
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;  ///< the newest N matches (0 = all)
};

bool NextRegJournalQueryFromStrings(const std::string& regs, const std::string& sources, const std::string& since,
                                    const std::string& from, const std::string& to, const std::string& limit,
                                    NextRegJournalQuery& query, std::string& error);

class NextRegJournal
{
public:
    static constexpr size_t kDefaultCapacity = 65536;

    explicit NextRegJournal(size_t capacity = kDefaultCapacity) { SetCapacity(capacity); }

    bool Enabled() const { return _enabled; }
    void SetEnabled(bool on) { _enabled = on; }
    /// Resizes the ring (the content is dropped)
    void SetCapacity(size_t capacity);
    size_t Capacity() const { return _ring.size(); }
    size_t Size() const { return _count; }
    uint64_t Evicted() const { return _evicted; }
    /// The seq of the newest event (0: none yet)
    uint64_t LastSeq() const { return _seq; }
    void Clear();

    void Record(uint32_t frame, uint32_t t, uint16_t pc, NextRegSource source, uint8_t reg, uint8_t value, uint8_t previous);

    /// The matches, oldest first, the newest `limit` of them
    std::vector<NextRegWriteEvent> Query(const NextRegJournalQuery& query) const;

private:
    std::vector<NextRegWriteEvent> _ring;
    size_t _head = 0;   ///< where the next event goes
    size_t _count = 0;
    uint64_t _seq = 0;
    uint64_t _evicted = 0;
    bool _enabled = false;
};
