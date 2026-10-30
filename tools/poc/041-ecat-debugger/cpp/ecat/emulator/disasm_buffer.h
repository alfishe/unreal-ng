// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Disassembled listing of the debugger, header

#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "emulator/disasm.h"

//A run of instructions one after another, kept while the debugger shows it:
//the window sees a part of it, scrolls through the rest, and the part that has
//left the window is still there when the program comes back. Lines are added
//below by plain decoding and above by the backward disassembly, which never
//changes the lines already there: the instruction it puts on top always ends
//exactly where the first line starts. Memory is read through the function
//given (CPU::peek_mem), so a processor that translates its addresses (the
//КМ1801ВМ3, the 8086's segments) is seen as it sees itself
class DisasmBuffer
{
public:
    struct Line {
        unsigned int address;
        std::vector<uint8_t> bytes;
        DisasmLine d;
    };

    //An arrow of a jump in the listing. Rows count from the first line of the
    //buffer; a target outside the buffer is a row just past the edge it lies
    //beyond (-1 or size()). Lanes count from the text: 0 is the innermost
    struct Arrow {
        int from;
        int to;
        int lane;
        bool conditional;
    };

    typedef std::function<uint8_t(unsigned int)> Peek;

    DisasmBuffer(DisAsm * disasm, const Peek &peek, unsigned int address_space);

    size_t size() const { return m_lines.size(); }
    const Line & at(size_t i) const { return m_lines[i]; }
    //The line that starts at the address, -1 when none does
    int find(unsigned int address) const;
    //The line the address lies in, -1 outside the buffer
    int find_containing(unsigned int address) const;

    //Starts again at the address, with count lines below it
    void reset(unsigned int address, size_t count);
    //Lines added below, or above; the number added
    size_t extend_down(size_t count);
    size_t extend_up(size_t count);
    //The address has a line of its own, the buffer built again from it when
    //it does not: below the first line, inside one or far away
    void ensure(unsigned int address, size_t below);
    //Reads the memory again. The lines whose bytes have changed are decoded
    //again with everything below them; true when anything changed
    bool refresh();

    //Addresses known to start instructions - where PC has been, breakpoints:
    //the backward disassembly prefers them when the bytes allow two readings
    void set_anchors(const std::set<unsigned int> &anchors) { m_anchors = anchors; }

    //The arrows of the jumps that cross the rows from..to (inclusive), lanes
    //assigned so that the shorter arrows lie nearer to the text and no two
    //arrows of a lane overlap; past max_lanes they share the outermost
    std::vector<Arrow> layout_jumps(int from, int to, int max_lanes) const;

    static const size_t MAX_LINES = 8192;

private:
    DisAsm * m_disasm;
    Peek m_peek;
    unsigned int m_space;
    std::vector<Line> m_lines;
    std::set<unsigned int> m_anchors;

    Line decode(unsigned int address) const;
    Line data_line(unsigned int address, unsigned int length) const;
    bool previous(unsigned int first, Line &out) const;
    void trim(bool keep_top);
};
