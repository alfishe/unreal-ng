#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <string>
#include <vector>

#include "_helpers/fusevectors.h"
#include "debugger/disassembler/z80mcycles.h"

namespace
{
/// The instruction bytes at the case's PC (up to 4), from its memory blocks
std::vector<uint8_t> BytesAtPc(const FuseVectors::FuseCase& tc)
{
    const uint16_t pc = tc.regs[11];
    std::vector<uint8_t> bytes;
    for (uint16_t i = 0; i < 4; i++)
    {
        const uint16_t addr = static_cast<uint16_t>(pc + i);
        bool found = false;
        for (const auto& block : tc.memory)
        {
            if (addr >= block.first && addr < block.first + block.second.size())
            {
                bytes.push_back(block.second[addr - block.first]);
                found = true;
                break;
            }
        }
        if (!found)
            break;
    }
    return bytes;
}

/// The data events a list of machine cycles puts on the bus, timed as the FUSE vectors are (a memory cycle's byte
/// at its start + 3, a port cycle's at its IORQ T, start + 1)
std::vector<std::pair<char, uint32_t>> EventsOf(const Z80MCycleList& list)
{
    std::vector<std::pair<char, uint32_t>> events;
    uint32_t start = 0;
    for (uint8_t i = 0; i < list.count; i++)
    {
        const Z80MCycle& cycle = list.cycles[i];
        switch (cycle.kind)
        {
            case Z80BusCycle::OpcodeFetch:
            case Z80BusCycle::OperandRead:
            case Z80BusCycle::MemoryRead: events.push_back({'R', start + 3}); break;
            case Z80BusCycle::MemoryWrite: events.push_back({'W', start + 3}); break;
            case Z80BusCycle::PortRead: events.push_back({'I', start + 1}); break;
            case Z80BusCycle::PortWrite: events.push_back({'O', start + 1}); break;
            default: break;
        }
        start += cycle.t;
    }
    return events;
}

/// The FUSE case's data events up to `total` T
std::vector<std::pair<char, uint32_t>> FuseEvents(const FuseVectors::FuseCase& tc, uint32_t total)
{
    std::vector<std::pair<char, uint32_t>> events;
    for (const auto& ev : tc.expectedTrace)
        if (ev.tOffset <= total)
            events.push_back({ev.type, ev.tOffset});
    return events;
}

/// A decoded read FUSE logs as a cycle without its data (the not-taken JR displacement, the last DJNZ one): FUSE
/// skips the read, the CPU makes it. Dropped as FuseVectors::FilterContendOnly drops the core's
std::vector<std::pair<char, uint32_t>> WithoutContendOnly(std::vector<std::pair<char, uint32_t>> events,
                                                          const FuseVectors::FuseCase& tc)
{
    std::erase_if(events, [&](const auto& ev) {
        if (ev.first != 'R')
            return false;
        for (const auto& cycle : tc.contendOnlyCycles)
            if (ev.second == cycle.second + 3)
                return true;
        return false;
    });
    return events;
}
}  // namespace

// Every FUSE case (~1300, every documented and undocumented opcode): one way of the decoded instruction puts the
// case's data events on the bus at the case's T and ends at the case's total. Cases that run on past one
// instruction (a repeating block instruction, a prefix chain) are compared up to the decoded instruction's end
TEST(Z80MCycles_Test, EveryFuseCaseMatchesOneWayOfItsInstruction)
{
    const auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE vectors not found";

    size_t whole = 0, prefix = 0;
    std::vector<std::string> failures;
    for (const auto& [name, tc] : cases)
    {
        if (tc.halted)
            continue;  // a halted CPU runs no instruction
        const std::vector<uint8_t> bytes = BytesAtPc(tc);
        const Z80MCycles decoded = Z80MCycleDecoder::Decode(bytes.data(), bytes.size());
        if (!decoded.valid)
        {
            failures.push_back(name + ": not decoded");
            continue;
        }
        bool matched = false;
        for (const Z80MCycleList* list : {&decoded.met, &decoded.notMet})
        {
            if (list->total > tc.expTotal)
                continue;
            const auto events = EventsOf(*list);
            const auto kept = WithoutContendOnly(events, tc);
            // Only the not-taken way of a conditional loses its one skipped read
            const size_t dropped = events.size() - kept.size();
            if (dropped > (decoded.conditional && list == &decoded.notMet ? 1u : 0u))
                continue;
            if (kept != FuseEvents(tc, list->total))
                continue;
            matched = true;
            (list->total == tc.expTotal ? whole : prefix)++;
            break;
        }
        if (!matched)
        {
            std::string got;
            for (const Z80MCycleList* list : {&decoded.met, &decoded.notMet})
            {
                got += " [";
                for (uint8_t i = 0; i < list->count; i++)
                    got += std::string(Z80MCycleDecoder::Name(list->cycles[i].kind)) +
                           std::to_string(list->cycles[i].t) + " ";
                got += "= " + std::to_string(list->total) + "]";
            }
            failures.push_back(name + ": fuse total " + std::to_string(tc.expTotal) + ", decoded" + got);
        }
    }
    for (size_t i = 0; i < failures.size() && i < 40; i++)
        ADD_FAILURE() << failures[i];
    EXPECT_TRUE(failures.empty()) << failures.size() << " cases differ";
    EXPECT_GT(whole, 1000u) << "cases that are one instruction";
    std::cout << "whole " << whole << ", compared to the instruction's end " << prefix << std::endl;
}

// The two ways of a conditional instruction and the position of a T inside one
TEST(Z80MCycles_Test, ConditionalWaysAndLocate)
{
    const uint8_t jrnz[] = {0x20, 0x05};
    const Z80MCycles jr = Z80MCycleDecoder::Decode(jrnz, sizeof(jrnz));
    ASSERT_TRUE(jr.valid);
    EXPECT_TRUE(jr.conditional);
    EXPECT_EQ(jr.length, 2);
    EXPECT_EQ(jr.met.total, 12);
    EXPECT_EQ(jr.notMet.total, 7);

    const uint8_t out[] = {0xD3, 0xFE};  // OUT (n),A: ocf 4, od 3, pw 4
    const Z80MCycles o = Z80MCycleDecoder::Decode(out, sizeof(out));
    ASSERT_TRUE(o.valid);
    EXPECT_FALSE(o.conditional);
    ASSERT_EQ(o.met.count, 3);
    EXPECT_EQ(o.met.cycles[2].kind, Z80BusCycle::PortWrite);
    const Z80MCyclePosition at8 = Z80MCycleDecoder::Locate(o.met, 8);
    EXPECT_TRUE(at8.inside);
    EXPECT_EQ(at8.index, 2);
    EXPECT_EQ(at8.intoT, 1);
    EXPECT_FALSE(Z80MCycleDecoder::Locate(o.met, 11).inside);

    const uint8_t truncated[] = {0xDD, 0xCB, 0x05};
    EXPECT_FALSE(Z80MCycleDecoder::Decode(truncated, sizeof(truncated)).valid);
    const uint8_t chain[] = {0xDD, 0xFD, 0x21};
    const Z80MCycles c = Z80MCycleDecoder::Decode(chain, sizeof(chain));
    ASSERT_TRUE(c.valid);
    EXPECT_EQ(c.length, 1);
    EXPECT_EQ(c.met.total, 4);
}
