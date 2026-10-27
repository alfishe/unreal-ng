#pragma once

/// @file nextsdjournal.h
/// @brief The Next's SD card journal (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P1): a bounded ring of what a
/// card was asked and answered over SPI - every command with its argument and R1, every block read and written - with when (frame, T)
/// and who asked (PC). The port trace shows bytes; this shows the SD protocol, so "the card answered as SDSC", "CMD17 of sector N
/// came back with an error" and "esxDOS never reads past the root" are one query.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class NextSdKind : uint8_t
{
    Command = 0,
    ReadBlock = 1,
    WriteBlock = 2,
};

const char* NextSdKindName(NextSdKind kind);
/// "CMD17", "ACMD41" ... for the report
std::string NextSdCommandName(uint8_t index, bool app);

struct NextSdEvent
{
    uint64_t seq = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    uint8_t card = 0;  ///< 0 or 1 (the two SD lines; the swap bit of NR #0A is already applied)
    NextSdKind kind = NextSdKind::Command;
    uint8_t index = 0;
    bool app = false;
    bool crcValid = true;
    uint32_t argument = 0;
    uint8_t r1 = 0xFF;  ///< Command: the response byte queued (0xFF: none)
    uint64_t block = 0; ///< ReadBlock / WriteBlock
    bool ok = true;     ///< WriteBlock: stored
};

/// "command,read": kinds; "0" / "1": card. Empty = no filter
struct NextSdJournalQuery
{
    std::vector<NextSdKind> kinds;
    int card = -1;
    uint64_t since = 0;
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;  ///< the newest N matches (0 = all)
};

bool NextSdJournalQueryFromStrings(const std::string& kinds, const std::string& card, const std::string& since, const std::string& from,
                                   const std::string& to, const std::string& limit, NextSdJournalQuery& query, std::string& error);

class NextSdJournal
{
public:
    static constexpr size_t kDefaultCapacity = 16384;

    explicit NextSdJournal(size_t capacity = kDefaultCapacity) { SetCapacity(capacity); }

    bool Enabled() const { return _enabled; }
    void SetEnabled(bool on) { _enabled = on; }
    /// Resizes the ring (the content is dropped)
    void SetCapacity(size_t capacity);
    size_t Capacity() const { return _ring.size(); }
    size_t Size() const { return _count; }
    uint64_t Evicted() const { return _evicted; }
    uint64_t LastSeq() const { return _seq; }
    void Clear();

    /// `event.seq` is assigned here
    void Record(NextSdEvent event);

    /// The matches, oldest first, the newest `limit` of them
    std::vector<NextSdEvent> Query(const NextSdJournalQuery& query) const;

private:
    std::vector<NextSdEvent> _ring;
    size_t _head = 0;
    size_t _count = 0;
    uint64_t _seq = 0;
    uint64_t _evicted = 0;
    bool _enabled = false;
};
