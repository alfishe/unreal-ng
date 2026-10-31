#pragma once

/// @file media.h
/// @brief What the media detectors (graphics.cpp, levelmaps.cpp, screens.cpp) share: the display set (the screen and
/// the back buffers the code copies to it) and the blit loops (loops that move bytes read from memory into the
/// display set, one pixel row a pass). Internal to the detectors.
///
/// The display set: the screen, and back through chains of copies the sources code wrote of block copies (LDIR /
/// LDDR, LDI / LDD chains) whose destinations are display: copies of 32 bytes or more, or of 8 or more when their
/// copier moved 512 bytes in all and they go to the screen itself (a back buffer copied a row at a time: POP the
/// row's display address, 24 LDI).
///
/// A blit: a loop whose write strides land in display pixels and whose reads of one base register (one source
/// stream: (DE), (HL), (IX+d) ...) come from outside the display set and load A (or an ALU operand), never an
/// address register (a table of screen addresses walked by IX is not graphics). Its geometry: height = the loop's
/// dominant trip count, bytes a row = the source stride (half of it when the stream reads a mask byte before each
/// graphic byte: twice as many reads as display writes a pass, with an AND), frame size = bytes a row x height
/// (x 2 masked). The frames: where each instance of the loop began reading (Stride::starts), clustered within one
/// source stride.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis::media
{

/// Bytes of the display set: the screen and the back buffers copied to it (Z80 view, the start's paging)
enum : uint8_t
{
    kNotDisplay = 0,
    kPixels = 1,
    kAttributes = 2,
};

/// A block copy into the display set (aggregated over runs with the same source, destination and length)
struct DisplayCopy
{
    uint16_t src = 0;   ///< the lowest byte of each range
    uint16_t dst = 0;
    uint32_t length = 0;
    uint8_t type = kPixels;     ///< what the destination holds
    bool written = false;       ///< code (not a block copy) wrote the source: a back buffer, not a picture
    bool toScreen = false;      ///< the destination is the screen itself ($4000-$5AFF)
    std::vector<uint16_t> copiers;  ///< the LDx instructions (Z80, sorted)
    uint64_t runs = 0;
};

struct DisplayInfo
{
    std::vector<uint8_t> map;  ///< 0x10000 entries: kNotDisplay, kPixels, kAttributes
    std::vector<DisplayCopy> copies;

    uint8_t At(uint32_t z80) const { return z80 <= 0xFFFF ? map[z80] : kNotDisplay; }
};

DisplayInfo FindDisplay(const DetectorContext& context);

/// The bytes a stride's ranges cover in the Z80 view, how many of them are display bytes of @p type (0: any)
struct Coverage
{
    uint32_t bytes = 0;
    uint32_t display = 0;
};
Coverage Cover(const DetectorContext& context, const DisplayInfo& display, const Stride& s, uint8_t type);

struct Blit
{
    const Loop* loop = nullptr;
    uint16_t rowBytes = 0;     ///< bytes of one pixel row
    uint16_t height = 0;       ///< pixel rows (the dominant trip count)
    bool mask = false;
    uint32_t frameBytes = 0;
    int32_t srcStride = 0;
    uint32_t dominant = 0;     ///< instances that ran the dominant trip count
    uint64_t instances = 0;
    std::vector<uint16_t> starts;   ///< frame starts (Z80, sorted)
    bool startsMore = false;        ///< an instruction saw more starts than it kept
    std::vector<Interval> origins;  ///< where the source bytes were copied from (all stream reads)
    std::vector<uint16_t> readers;  ///< the source reads (Z80)
    std::vector<uint16_t> writers;  ///< the display writes (Z80)
    bool toPixels = false;          ///< the writes went to display pixels
    /// Tiles: the loop around the blit that walked a map one byte a pass (nullptr: none)
    const Loop* mapLoop = nullptr;
    const Stride* mapRead = nullptr;
};

/// Every blit loop, in loop order
std::vector<Blit> FindBlits(const DetectorContext& context, const DisplayInfo& display);

/// Sorted runs of [lo, hi] (Z80) merged across gaps of at most @p slack
std::vector<std::pair<uint16_t, uint16_t>> Runs(const DetectorContext& context, const std::vector<Interval>& ranges,
                                                uint32_t slack);

/// A buffer claim (the records detector's copy destinations), copied out of the store (claims added later move it)
struct BufferCopy
{
    uint16_t lo = 0;
    uint16_t hi = 0;
    std::vector<uint16_t> instances;
};
std::vector<BufferCopy> Buffers(const DetectorContext& context);

/// The routine (Z80 entry) owning a Z80 address's instruction through the start's paging (0xFFFFFFFF: none)
uint32_t RoutineOfZ80(const DetectorContext& context, uint16_t z80);

}  // namespace analysis::media
