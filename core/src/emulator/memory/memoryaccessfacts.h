#pragma once

/// @file memoryaccessfacts.h
/// @brief L0 access facts of the analysis layers (docs/inprogress/2026-10-24-analysis-layers/design.md, "L0 Access
/// facts"): per byte, how it was used, by which instruction, when, and the range of the values written. Filled by
/// MemoryAccessTracker at its level `facts`, on top of the counters, from the same Track* calls.
///
/// Storage: one 16-byte ByteFacts per byte, so one access touches one cache line.
///   - Z80 address space mode: 64K entries (1 MB), allocated with the level.
///   - Physical mode: 16K entries (256 KB) per physical page, allocated when the page is first touched (a 128K game
///     touches 8-10 pages). The page table is atomic: the emulation thread publishes a new page, a reader on another
///     thread (explain, status) sees either nothing or the zeroed page.
///
/// The access context (stack / block / plain) comes from the Z80: after the opcode fetch of every instruction, while
/// the level is `facts`, it stores the instruction's class (a 256-entry table per prefix) into the tracker's context
/// byte; the INT / NMI acceptance sets it around its pushes. accesscontext::kBase / kEd are those tables.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// How a byte was accessed: a bitmask, accumulated over the session
enum class AccessKind : uint8_t
{
    OpcodeFetch = 0x01,      ///< first byte of an instruction (the M1 at the instruction's start)
    OperandFetch = 0x02,     ///< any other byte of an instruction (prefixed opcodes, displacements, immediates)
    DataRead = 0x04,         ///< a data read (with BlockAccess: by a block instruction)
    DataWrite = 0x08,        ///< a data write (with BlockAccess: by a block instruction)
    StackRead = 0x10,        ///< POP, RET, RETI, RETN, EX (SP) (instead of DataRead)
    StackWrite = 0x20,       ///< PUSH, CALL, RST, EX (SP), interrupt and NMI acceptance (instead of DataWrite)
    BlockAccess = 0x40,      ///< LDI / LDIR / LDD / LDDR / CPI.. / INI.. / OUTI.. source or destination
    WrittenWhileCode = 0x80  ///< written after it was fetched as code: self-modifying code
};

constexpr uint8_t operator|(AccessKind a, AccessKind b)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr uint8_t operator|(uint8_t a, AccessKind b)
{
    return static_cast<uint8_t>(a | static_cast<uint8_t>(b));
}
constexpr uint8_t operator&(uint8_t a, AccessKind b)
{
    return static_cast<uint8_t>(a & static_cast<uint8_t>(b));
}

/// The class of the running instruction, as the Z80 tells the tracker (one byte)
enum class AccessContext : uint8_t
{
    Plain = 0,      ///< data accesses are DataRead / DataWrite
    Stack = 1,      ///< data accesses are StackRead / StackWrite
    Block = 2,      ///< data accesses are DataRead / DataWrite plus BlockAccess
    HaltFetch = 3,  ///< the idle fetch of a halted CPU (the byte after the HALT): not an instruction byte
};

namespace accesscontext
{
using Table = std::array<uint8_t, 256>;

constexpr uint8_t Ctx(AccessContext context)
{
    return static_cast<uint8_t>(context);
}

/// No prefix, DD, FD: POP / PUSH rr, RET, RET cc, CALL nn, CALL cc,nn, RST n, EX (SP),HL. The stack instructions keep
/// their class behind DD / FD (PUSH IX, POP IX, EX (SP),IX; a DD before RET is ignored by the CPU)
constexpr Table MakeBaseTable()
{
    Table table{};
    for (uint8_t& entry : table)
        entry = Ctx(AccessContext::Plain);
    for (unsigned row = 0; row < 4; row++)
    {
        const unsigned base = 0xC0 + row * 0x10;
        table[base + 0x1] = Ctx(AccessContext::Stack);  // POP BC / DE / HL / AF
        table[base + 0x5] = Ctx(AccessContext::Stack);  // PUSH BC / DE / HL / AF
    }
    for (unsigned cc = 0; cc < 8; cc++)
    {
        table[0xC0 + cc * 8] = Ctx(AccessContext::Stack);  // RET cc
        table[0xC4 + cc * 8] = Ctx(AccessContext::Stack);  // CALL cc,nn
        table[0xC7 + cc * 8] = Ctx(AccessContext::Stack);  // RST n
    }
    table[0xC9] = Ctx(AccessContext::Stack);  // RET
    table[0xCD] = Ctx(AccessContext::Stack);  // CALL nn
    table[0xE3] = Ctx(AccessContext::Stack);  // EX (SP),HL
    return table;
}

/// ED: the block group (LDI.. / CPI.. / INI.. / OUTI.. and their repeats) and RETN / RETI (all eight encodings)
constexpr Table MakeEdTable()
{
    Table table{};
    for (uint8_t& entry : table)
        entry = Ctx(AccessContext::Plain);
    for (unsigned row = 0xA0; row <= 0xB8; row += 8)
        for (unsigned op = 0; op < 4; op++)
            table[row + op] = Ctx(AccessContext::Block);
    for (unsigned op = 0x45; op <= 0x7D; op += 8)
        table[op] = Ctx(AccessContext::Stack);
    return table;
}

/// The tables the Z80 reads after an opcode fetch (one load)
inline constexpr Table kBase = MakeBaseTable();
inline constexpr Table kEd = MakeEdTable();
}  // namespace accesscontext

/// The instruction class of an opcode after a prefix: 0x00 / 0xDD / 0xFD the base table, 0xED the ED table, anything
/// else (CB, DDCB, FDCB: no stack or block form) plain
inline AccessContext AccessContextFor(uint16_t prefix, uint8_t opcode)
{
    if (prefix == 0xED)
        return static_cast<AccessContext>(accesscontext::kEd[opcode]);
    if (prefix == 0x00 || prefix == 0xDD || prefix == 0xFD)
        return static_cast<AccessContext>(accesscontext::kBase[opcode]);
    return AccessContext::Plain;
}

/// Value class bits (ByteFacts::valueClass)
namespace valueclass
{
constexpr uint8_t kDistinctMask = 0x03;  ///< written values: 0 never written, 1 one, 2 two, 3 three or more
constexpr uint8_t kMultiReader = 0x04;   ///< data read by two or more instructions
constexpr uint8_t kMultiWriter = 0x08;   ///< written by two or more instructions
}  // namespace valueclass

/// The facts of one byte (16 bytes: one access touches one cache line)
struct ByteFacts
{
    uint32_t firstFrame;  ///< frame of the first access (valid when kinds != 0)
    uint32_t lastFrame;   ///< frame of the last access
    uint16_t lastReader;  ///< PC of the instruction of the last data / stack read
    uint16_t lastWriter;  ///< PC of the instruction of the last write
    uint8_t kinds;        ///< AccessKind bits
    uint8_t valueClass;   ///< valueclass bits
    uint8_t minWritten;   ///< smallest value written (valid when written)
    uint8_t maxWritten;   ///< largest value written
};
static_assert(sizeof(ByteFacts) == 16, "one access, one cache line");

class MemoryAccessFacts
{
public:
    static constexpr size_t kZ80Size = 0x10000;
    static constexpr size_t kPageSize = 0x4000;

    MemoryAccessFacts();
    ~MemoryAccessFacts();
    MemoryAccessFacts(const MemoryAccessFacts&) = delete;
    MemoryAccessFacts& operator=(const MemoryAccessFacts&) = delete;

    /// The Z80 view (64K) and the page table (pageCount pointers, no page yet). Callers hold the CPU
    void Allocate(size_t pageCount);
    /// Everything freed. Callers hold the CPU
    void Free();
    /// Every fact back to "never accessed"; touched pages are kept (zeroed). Callers hold the CPU
    void Clear();
    bool IsAllocated() const { return _z80 != nullptr; }

    /// The entry of a Z80 address (allocated)
    ByteFacts& Z80At(uint16_t address) { return _z80[address]; }
    /// The entry of a physical byte; the page is allocated on first touch (emulation thread). Null past the table
    ByteFacts* PhysicalAt(size_t physicalOffset)
    {
        const size_t page = physicalOffset / kPageSize;
        if (page >= _pageCount)
            return nullptr;
        ByteFacts* entries = _pages[page].load(std::memory_order_relaxed);
        if (entries == nullptr) [[unlikely]]
            entries = AllocatePage(page);
        return entries + (physicalOffset % kPageSize);
    }

    /// Copies for a reader on another thread: false when there is no such entry (not allocated, page not touched)
    bool ReadZ80(uint16_t address, ByteFacts& out) const;
    bool ReadPhysical(uint16_t page, uint16_t offset, ByteFacts& out) const;

    /// The pages that have facts (physical mode), ascending
    std::vector<uint16_t> TouchedPages() const;
    /// Bytes allocated now (the Z80 view plus the pages)
    size_t BytesAllocated() const;
    /// Z80 addresses with any fact
    size_t TouchedZ80Bytes() const;

    /// region <Updates (the emulation thread, inline: the tracker's hot path)>
    static void OnExecute(ByteFacts& f, AccessKind kind, uint32_t frame)
    {
        if (f.kinds == 0)
            f.firstFrame = frame;
        f.lastFrame = frame;
        f.kinds = f.kinds | kind;
    }

    static void OnRead(ByteFacts& f, uint8_t kind, uint16_t pc, uint32_t frame)
    {
        constexpr uint8_t kReadKinds = AccessKind::DataRead | AccessKind::StackRead;
        if (f.kinds == 0)
            f.firstFrame = frame;
        else if ((f.kinds & kReadKinds) && f.lastReader != pc)
            f.valueClass |= valueclass::kMultiReader;
        f.lastFrame = frame;
        f.kinds |= kind;
        f.lastReader = pc;
    }

    static void OnWrite(ByteFacts& f, uint8_t kind, uint8_t value, uint16_t pc, uint32_t frame)
    {
        constexpr uint8_t kCodeKinds = AccessKind::OpcodeFetch | AccessKind::OperandFetch;
        if (f.kinds == 0)
            f.firstFrame = frame;
        f.lastFrame = frame;
        uint8_t distinct = f.valueClass & valueclass::kDistinctMask;
        if (distinct == 0)
        {
            f.minWritten = value;
            f.maxWritten = value;
            distinct = 1;
        }
        else
        {
            if (f.lastWriter != pc)
                f.valueClass |= valueclass::kMultiWriter;
            // One value: min == max, another one makes two. Two: they are exactly min and max, a third is neither
            if (value != f.minWritten && value != f.maxWritten && distinct < 3)
                distinct++;
            if (value < f.minWritten)
                f.minWritten = value;
            if (value > f.maxWritten)
                f.maxWritten = value;
        }
        f.valueClass = static_cast<uint8_t>((f.valueClass & ~valueclass::kDistinctMask) | distinct);
        if (f.kinds & kCodeKinds)
            kind = kind | AccessKind::WrittenWhileCode;
        f.kinds |= kind;
        f.lastWriter = pc;
    }
    /// endregion </Updates>

    /// "OpcodeFetch, DataRead" (empty: none)
    static std::string KindNames(uint8_t kinds);
    /// One name per set bit, in bit order
    static std::vector<std::string> KindList(uint8_t kinds);

private:
    ByteFacts* AllocatePage(size_t page);

    std::unique_ptr<ByteFacts[]> _z80;
    std::unique_ptr<std::atomic<ByteFacts*>[]> _pages;
    size_t _pageCount = 0;
};
