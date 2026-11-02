#pragma once

/// @file l1valueflow.h
/// @brief Value provenance for L1 (l1builder.h): where each byte written to the screen came from. Internal to the
/// builder.
///
/// Every 8-bit register and every memory byte carries the sources of its value: the memory bytes an instruction read
/// it from, and through them the byte the value was first read from (a byte copied by LD r,(mem) / LD (mem),r, PUSH /
/// POP or LDI keeps the source it was copied from; one written with a computed value, or never written, is its own
/// source). An ALU operation merges its operands' sources, the latest first (AND (HL) with the screen, then OR with
/// the graphic byte). A write picks the latest source that is not the byte it overwrites: the sprite byte, not the
/// screen byte the sprite is merged with, and the graphic byte, not the mask byte read before it.
///
/// A write to the screen's pixels whose value came from a byte outside the screen is a draw of that source byte: its
/// position (byte column, pixel line) and the source bytes the screen shows next to it. How far in memory the byte
/// shown to the right and the one shown below lie is the graphics' geometry: right +1, below +w for a picture stored
/// row by row, w bytes a row (+2, +2w with a mask byte between); below +1, right +h for one stored in columns h bytes
/// high. A row step larger than the drawn width is a window of a wider picture (a scrolled landscape). A back buffer
/// copied to the screen carries its sources with it, so a sprite drawn into the buffer is a draw of the sprite's own
/// bytes. A copied byte whose value goes to more than two places before it is written again is a table (a font
/// unpacked from a pool of rows: each glyph printed all over the screen; the sprites a level unpacks), the source of
/// what is drawn from it; each value written into a buffer goes to one place (two: both screens).
///
/// Drawn frames: the screen writes with a source, in order, form a drawing pass until a screen byte is written again
/// (a rewrite of a byte one of the last few writes wrote is a shifted sprite's carry into the next column: it stays
/// in the pass, the later source counts), or after two screens' worth of writes; not at a frame's end (a back buffer
/// copied to the screen takes longer than a frame). In a pass, the bytes of one byte column on consecutive
/// pixel lines whose sources lie a constant step apart are a column piece; pieces in neighbouring columns join when
/// the sources lie a constant step apart on every line they share and the model base + line * rowStep + column *
/// colStep holds for every byte. What joins is one picture drawn as a whole (a sprite frame, a glyph, a tile), with
/// the mask byte merged into each graphic byte when there was one (AND with the screen before the OR).

#include <cstdint>
#include <memory>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/z80opinfo.h"

namespace analysis
{

class ValueFlow
{
public:
    struct Access
    {
        bool write = false;
        bool stack = false;  ///< PUSH / POP / CALL / RET / EX (SP),rr, or an interrupt's push
        bool push = false;   ///< an interrupt's or a call's push: no register value
        uint16_t z80 = 0;
        PhysKey key = 0;
    };

    ValueFlow();
    ~ValueFlow();
    ValueFlow(const ValueFlow&) = delete;
    ValueFlow& operator=(const ValueFlow&) = delete;

    /// One instruction's data accesses (opcode fetches excluded), in bus order; @p writer: the instruction's key
    void Instruction(const OpInfo& op, const Access* accesses, int count, PhysKey writer, uint64_t frame);

    /// The source bytes drawn to the screen, sorted by key
    std::vector<DrawnByte> Build() const;
    /// The pictures drawn as a whole, sorted by base then shape (ends the drawing pass in progress)
    std::vector<DrawnFrame> BuildFrames();
    size_t MemoryBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace analysis
