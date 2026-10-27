#pragma once

/// @file nextmagicport.h
/// @brief The magic debug port (jnext --magic-port): a program writes bytes to one chosen port and the emulator shows them as text -
/// the debug console of a program under test (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P6, G9).
/// The port is an exact 16-bit address (no mask); the write is also seen by the machine's own decoding as usual. Output is kept
/// in a bounded ring that every automation plane reads (jnext prints to stderr). Off by default; one branch per OUT when off.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class NextMagicPortMode : uint8_t
{
    Hex = 0,    ///< one item per byte: "5A"
    Dec = 1,    ///< one item per byte: "90"
    Ascii = 2,  ///< one item per byte: the character (non-printable as \xNN)
    Line = 3,   ///< one item per line: bytes up to CR or LF (an empty line is dropped)
};

const char* NextMagicPortModeName(NextMagicPortMode mode);
bool NextMagicPortModeFromString(const std::string& text, NextMagicPortMode& mode);

struct NextMagicPortItem
{
    uint64_t seq = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    std::string text;
};

class NextMagicPort
{
public:
    static constexpr size_t kDefaultCapacity = 4096;
    static constexpr size_t kMaxLineLength = 1024;  ///< a line mode line longer than this is cut here

    bool Enabled() const { return _enabled; }
    uint16_t Port() const { return _port; }
    NextMagicPortMode Mode() const { return _mode; }
    void Configure(bool enabled, uint16_t port, NextMagicPortMode mode);
    void Clear();

    /// An OUT to `port`: false when it is not the magic port (nothing recorded)
    bool Write(uint16_t port, uint8_t value, uint32_t frame, uint32_t t, uint16_t pc);

    uint64_t Bytes() const { return _bytes; }
    uint64_t LastSeq() const { return _seq; }
    uint64_t Evicted() const { return _evicted; }
    size_t Size() const { return _count; }
    const std::string& Partial() const { return _line; }  ///< line mode: bytes not yet ended by CR / LF
    /// Items after `since` (a seq), oldest first, at most `limit` (the newest when there are more)
    std::vector<NextMagicPortItem> Query(uint64_t since, size_t limit) const;

private:
    void Push(std::string text, uint32_t frame, uint32_t t, uint16_t pc);

    bool _enabled = false;
    uint16_t _port = 0;
    NextMagicPortMode _mode = NextMagicPortMode::Hex;
    std::vector<NextMagicPortItem> _ring = std::vector<NextMagicPortItem>(kDefaultCapacity);
    size_t _head = 0;
    size_t _count = 0;
    uint64_t _seq = 0;
    uint64_t _evicted = 0;
    uint64_t _bytes = 0;
    std::string _line;
};
