#pragma once

/// @file z80mcycles.h
/// @brief The machine cycles of a Z80 instruction: what the bus does at each T of it (GS debugger design §5.4).
///
/// A debugger that stops one CPU inside another CPU's instruction (a card's breakpoint while the main CPU runs)
/// shows how far that instruction is and which bus cycle is under way. The disassembler's opcode records give an
/// instruction's total T only; this decoder gives its machine cycles, uncontended, as the Zilog manual times them:
/// the opcode fetches (4 T), operand and memory reads and writes (3 T), port cycles (4 T, the wait state included)
/// and the internal T in between, where the bus has no memory or port cycle.
///
/// No allocation and no context: the instruction's bytes in, a fixed array out. Checked opcode by opcode against
/// the FUSE test vectors' bus traces (z80mcycles_test.cpp).

#include <cstddef>
#include <cstdint>

enum class Z80BusCycle : uint8_t
{
    OpcodeFetch,  ///< M1: the opcode byte (a prefix, the byte after it), 4 T
    OperandRead,  ///< an instruction byte after the opcode: n, nn, d, 3 T
    MemoryRead,   ///< a data read, 3 T
    MemoryWrite,  ///< a data write, 3 T
    PortRead,     ///< an IN cycle, 4 T
    PortWrite,    ///< an OUT cycle, 4 T
    Internal,     ///< T with no memory or port cycle on the bus
};

struct Z80MCycle
{
    Z80BusCycle kind = Z80BusCycle::Internal;
    uint8_t t = 0;
};

/// One way through an instruction: its machine cycles in order and their total
struct Z80MCycleList
{
    static constexpr size_t kMax = 12;
    /// No `{}` on the array: Z80MCycle's member initialisers already set every element, and g++ 14 -O3 -flto dies
    /// (internal compiler error in gimple_add_tmp_var) value-initialising a struct holding it (Emulator's BreakpointStop)
    Z80MCycle cycles[kMax];
    uint8_t count = 0;
    uint8_t total = 0;
};

/// An instruction's machine cycles. A conditional instruction (JR / CALL / RET cc, DJNZ, a repeating block
/// instruction) has two ways: `met` (the jump taken, the block repeated) and `notMet`; an unconditional one has
/// `conditional == false` and both the same
struct Z80MCycles
{
    bool valid = false;
    bool conditional = false;
    uint8_t length = 0;  ///< the instruction's bytes
    Z80MCycleList met;
    Z80MCycleList notMet;
};

struct Z80MCyclePosition
{
    bool inside = false;  ///< `elapsedT` falls inside the list (false: at or past its end)
    uint8_t index = 0;    ///< the machine cycle under way
    uint8_t intoT = 0;    ///< T of it already run
};

class Z80MCycleDecoder
{
public:
    /// The machine cycles of the instruction starting at `bytes` (up to 4 of them are read; fewer given: invalid
    /// when the instruction needs more). A DD / FD prefix followed by another prefix is an instruction of its own
    /// (4 T), as the CPU runs it
    static Z80MCycles Decode(const uint8_t* bytes, size_t size);

    /// The machine cycle `elapsedT` falls in
    static Z80MCyclePosition Locate(const Z80MCycleList& list, uint32_t elapsedT);

    /// A short name of a kind: "ocf", "od", "mr", "mw", "pr", "pw", "int"
    static const char* Name(Z80BusCycle kind);
    /// The kind in words: "opcode fetch", "operand read", "memory read", "memory write", "port read", "port write",
    /// "internal"
    static const char* Description(Z80BusCycle kind);
};
