// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Disassembled listing of the debugger

#include <algorithm>
#include <cstdlib>
#include <map>

#include "emulator/disasm_buffer.h"
#include "emulator/utils.h"

DisasmBuffer::DisasmBuffer(DisAsm * disasm, const Peek &peek, unsigned int address_space):
    m_disasm(disasm),
    m_peek(peek),
    m_space(address_space ? address_space : 0x10000)
{}

DisasmBuffer::Line DisasmBuffer::data_line(unsigned int address, unsigned int length) const
{
    Line l;
    l.address = address;
    for (unsigned int i = 0; i < length; i++)
        l.bytes.push_back(m_peek((address + i) % m_space));
    l.d.length = length;
    l.d.valid = false;
    if (length == 2 && m_disasm->unit_bytes() == 2)
        l.d.text = ".WORD " + oct_str(l.bytes[0] | (l.bytes[1] << 8), 6);
    else
        l.d.text = "DB " + hex_str(l.bytes[0], 2);
    return l;
}

DisasmBuffer::Line DisasmBuffer::decode(unsigned int address) const
{
    uint8_t b[15] = {0};
    unsigned int n = m_disasm->max_command_length;
    if (n > sizeof(b)) n = sizeof(b);
    for (unsigned int i = 0; i < n; i++) b[i] = m_peek((address + i) % m_space);

    Line l;
    l.address = address;
    const unsigned int len = m_disasm->decode(&b, address, n, &l.d);
    if (len == 0 || len > n) return data_line(address, m_disasm->unit_bytes());
    l.d.length = len;
    l.bytes.assign(b, b + len);
    return l;
}

int DisasmBuffer::find(unsigned int address) const
{
    const int i = find_containing(address);
    return (i >= 0 && m_lines[i].address == address) ? i : -1;
}

int DisasmBuffer::find_containing(unsigned int address) const
{
    auto it = std::upper_bound(m_lines.begin(), m_lines.end(), address,
                               [](unsigned int a, const Line &l) { return a < l.address; });
    if (it == m_lines.begin()) return -1;
    --it;
    if (address >= it->address + it->d.length) return -1;
    return (int)(it - m_lines.begin());
}

void DisasmBuffer::reset(unsigned int address, size_t count)
{
    m_lines.clear();
    m_lines.push_back(decode(address % m_space));
    if (count > 1) extend_down(count - 1);
}

size_t DisasmBuffer::extend_down(size_t count)
{
    if (m_lines.empty()) return 0;
    size_t added = 0;
    for (; added < count; added++) {
        const Line &last = m_lines.back();
        const unsigned int next = last.address + last.d.length;
        if (next >= m_space) break;
        m_lines.push_back(decode(next));
    }
    trim(true);
    return added;
}

size_t DisasmBuffer::extend_up(size_t count)
{
    if (m_lines.empty()) return 0;
    std::vector<Line> above;
    unsigned int first = m_lines.front().address;
    for (size_t i = 0; i < count; i++) {
        Line l;
        if (!previous(first, l)) break;
        first = l.address;
        above.push_back(l);
    }
    std::reverse(above.begin(), above.end());
    m_lines.insert(m_lines.begin(), above.begin(), above.end());
    trim(false);
    return above.size();
}

//The instruction that ends where first begins. Every reading of the bytes
//above that ends there is a candidate. With more than one, decoding is
//started at every address of a stretch above, and each chain of real
//instructions that lands exactly on first votes for the one it lands with:
//the chains fall in step with the program after an instruction or two, which
//is why a word of an operand (MOV #imm, MVI A,0) loses to the instruction it
//belongs to. An address the processor is known to have run from wins outright
bool DisasmBuffer::previous(unsigned int first, Line &out) const
{
    const unsigned int unit = m_disasm->unit_bytes();
    const unsigned int longest = std::max(m_disasm->max_command_length, unit);
    if (first < unit) return false;

    std::vector<unsigned int> candidates;
    for (unsigned int k = unit; k <= longest && k <= first; k += unit) {
        const Line l = decode(first - k);
        if (l.d.valid && l.d.length == k) candidates.push_back(first - k);
    }
    if (candidates.empty()) {
        out = data_line(first - unit, unit);
        return true;
    }
    if (candidates.size() > 1)
        for (unsigned int c : candidates)
            if (m_anchors.count(c)) {
                out = decode(c);
                return true;
            }
    if (candidates.size() == 1) {
        out = decode(candidates[0]);
        return true;
    }

    const unsigned int window = 16 * longest;
    const unsigned int low = (first > window) ? first - window : first % unit;
    std::map<unsigned int, std::pair<unsigned int, bool> > memo;
    std::map<unsigned int, unsigned int> votes;
    for (unsigned int s = low; s < first; s += unit) {
        unsigned int a = s;
        unsigned int last = s;
        bool ok = true;
        while (a < first) {
            auto it = memo.find(a);
            if (it == memo.end()) {
                const Line l = decode(a);
                it = memo.insert(std::make_pair(a, std::make_pair(l.d.length, l.d.valid))).first;
            }
            if (!it->second.second) { ok = false; break; }
            last = a;
            a += it->second.first;
        }
        if (ok && a == first) votes[last]++;
    }

    //The most votes; between equals the longer instruction
    unsigned int best = candidates.back();
    unsigned int best_votes = 0;
    for (unsigned int c : candidates) {
        const unsigned int v = votes.count(c) ? votes[c] : 0;
        if (v > best_votes || (v == best_votes && c < best)) {
            best = c;
            best_votes = v;
        }
    }
    out = decode(best);
    return true;
}

void DisasmBuffer::trim(bool keep_bottom)
{
    if (m_lines.size() <= MAX_LINES) return;
    const size_t extra = m_lines.size() - MAX_LINES;
    if (keep_bottom) m_lines.erase(m_lines.begin(), m_lines.begin() + extra);
    else m_lines.erase(m_lines.end() - extra, m_lines.end());
}

void DisasmBuffer::ensure(unsigned int address, size_t below)
{
    const int i = find(address);
    if (i >= 0) {
        const size_t after = m_lines.size() - (size_t)i;
        if (after < below) extend_down(below - after);
        return;
    }
    if (!m_lines.empty()) {
        const Line &last = m_lines.back();
        if (address == last.address + last.d.length) {
            extend_down(below);
            return;
        }
    }
    reset(address, below);
}

bool DisasmBuffer::refresh()
{
    for (size_t i = 0; i < m_lines.size(); i++) {
        const Line &l = m_lines[i];
        bool same = true;
        for (size_t j = 0; j < l.bytes.size() && same; j++)
            same = (l.bytes[j] == m_peek((l.address + (unsigned int)j) % m_space));
        if (same) continue;

        //The instruction and everything below it: a new length moves the rest
        const size_t count = m_lines.size() - i;
        const unsigned int address = l.address;
        m_lines.erase(m_lines.begin() + i, m_lines.end());
        m_lines.push_back(decode(address));
        extend_down(count - 1);
        return true;
    }
    return false;
}

std::vector<DisasmBuffer::Arrow> DisasmBuffer::layout_jumps(int from, int to, int max_lanes) const
{
    std::vector<Arrow> arrows;
    if (m_lines.empty()) return arrows;
    const unsigned int top = m_lines.front().address;
    const unsigned int end = m_lines.back().address + m_lines.back().d.length;

    for (size_t i = 0; i < m_lines.size(); i++) {
        const DisasmLine &d = m_lines[i].d;
        if (d.flow != FLOW_JUMP || !d.has_target) continue;
        int t = find(d.target);
        if (t < 0) {
            if (d.target < top) t = -1;
            else if (d.target >= end) t = (int)m_lines.size();
            else continue;      //Into the middle of an instruction: the listing is wrong there
        }
        const int lo = std::min((int)i, t);
        const int hi = std::max((int)i, t);
        if (hi < from || lo > to) continue;
        Arrow a;
        a.from = (int)i;
        a.to = t;
        a.lane = 0;
        a.conditional = d.conditional;
        arrows.push_back(a);
    }

    std::stable_sort(arrows.begin(), arrows.end(), [](const Arrow &a, const Arrow &b) {
        return std::abs(a.to - a.from) < std::abs(b.to - b.from);
    });

    if (max_lanes < 1) max_lanes = 1;
    std::vector<std::vector<std::pair<int, int> > > lanes(max_lanes);
    for (Arrow &a : arrows) {
        const int lo = std::min(a.from, a.to);
        const int hi = std::max(a.from, a.to);
        int lane = 0;
        for (; lane < max_lanes - 1; lane++) {
            bool free = true;
            for (const auto &o : lanes[lane])
                if (lo <= o.second && o.first <= hi) { free = false; break; }
            if (free) break;
        }
        lanes[lane].push_back(std::make_pair(lo, hi));
        a.lane = lane;
    }
    return arrows;
}
