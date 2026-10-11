#pragma once

/// @file nextdivmmcjournal.h
/// @brief The Next's DivMMC journal (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P2): a bounded ring of what
/// paged the DivMMC in and out - the automap entry and its address, the #1FF8 exit, RETN, the NR #0A / #83 disable, reset, the #E3
/// writes, the DRIVE button - and, for an entry at #0008, the esxDOS call (RST $08 + the byte after it: F_OPEN, F_READ ...) with the
/// registers the program set. "Why is the DivMMC in at this PC" and "which esxDOS calls did the program make" are one query.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class NextDivMmcKind : uint8_t
{
    MapIn = 0,         ///< the automap latch set by a fetch at `address`
    MapOut = 1,        ///< the latch cleared by a fetch in #1FF8-#1FFF
    Retn = 2,          ///< ED 45 cleared the latch
    Disabled = 3,      ///< NR #0A bit 4 or NR #83 bit 0 cleared it
    Reset = 4,
    ProgramStart = 5,  ///< the NEX loader released the DivMMC
    Port = 6,          ///< a write to #E3 (`control` = the value now)
    MapramClear = 7,   ///< NR #09 bit 3
    Button = 8,        ///< the DRIVE button armed the NMI entry
    EsxCall = 9,       ///< an entry at #0008: `call` is the byte after the RST
};

const char* NextDivMmcKindName(NextDivMmcKind kind);
/// "F_OPEN" ... for an esxDOS call code (the tbblue firmware's esxapi.def); nullptr when the table does not know it
const char* NextEsxCallName(uint8_t code);
/// Why an address maps the DivMMC in: rst, nmi, 3dxx, rom3_routine
const char* NextDivMmcEntryName(uint16_t address);

struct NextDivMmcEvent
{
    uint64_t seq = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    NextDivMmcKind kind = NextDivMmcKind::MapIn;
    uint16_t address = 0;  ///< MapIn / MapOut: the fetch address
    bool instant = false;  ///< MapIn: the opcode of that fetch already came from the DivMMC (NR #BA / #BB)
    bool mapped = false;   ///< the DivMMC memory is in after the event
    uint8_t control = 0;   ///< #E3 after the event
    // EsxCall
    uint8_t call = 0;
    bool basicRom = false;  ///< the 48K BASIC ROM is paged: RST 8 is an error report there, the byte is an error code
    uint16_t returnAddress = 0;
    uint16_t af = 0, bc = 0, de = 0, hl = 0;
};

struct NextDivMmcJournalQuery
{
    std::vector<NextDivMmcKind> kinds;
    uint64_t since = 0;
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;
};

bool NextDivMmcJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from, const std::string& to,
                                       const std::string& limit, NextDivMmcJournalQuery& query, std::string& error);

class NextDivMmcJournal
{
public:
    static constexpr size_t kDefaultCapacity = 16384;

    explicit NextDivMmcJournal(size_t capacity = kDefaultCapacity) { SetCapacity(capacity); }

    bool Enabled() const { return _enabled; }
    void SetEnabled(bool on) { _enabled = on; }
    void SetCapacity(size_t capacity);
    size_t Capacity() const { return _ring.size(); }
    size_t Size() const { return _count; }
    uint64_t Evicted() const { return _evicted; }
    uint64_t LastSeq() const { return _seq; }
    void Clear();

    void Record(NextDivMmcEvent event);
    std::vector<NextDivMmcEvent> Query(const NextDivMmcJournalQuery& query) const;

private:
    std::vector<NextDivMmcEvent> _ring;
    size_t _head = 0;
    size_t _count = 0;
    uint64_t _seq = 0;
    uint64_t _evicted = 0;
    bool _enabled = false;
};
