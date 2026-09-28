#pragma once

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "common/filehelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "pch.h"

/// FUSE Z80 test vectors (testdata/z80/fuse/tests.in + tests.expected, GPL-2.0+): parsing, loading a case
/// into the CPU, and comparing the final state. Shared by the bus-phase suite (fuse_phase_test.cpp) and the
/// contended replay (memorycontended_test.cpp).
///
/// Event mapping (verified against FUSE conventions):
///  - FUSE logs MC (cycle start) + MR/MW (data transfer). Memory data events map to our busTraceHook 'R'/'W'
///    at (MC time + 3) - rd()/wd() charge the 3T cycle then access, and FUSE M1 reads log at MC+4 while ours
///    fire at MC+3 (before the decode T) - both normalize to MC+3.
///  - FUSE PR/PW times equal our 'I'/'O' hook times exactly: both models put the port access at the IORQ
///    T-state (T2 of the IO cycle).
namespace FuseVectors
{
struct BusEvent
{
    char type;  // 'R'/'W'/'I'/'O'
    uint16_t addr;
    uint8_t value;
    uint32_t tOffset;
};

struct FuseCase
{
    std::string name;
    uint16_t regs[13];  // AF BC DE HL AF' BC' DE' HL' IX IY SP PC MEMPTR
    uint8_t i, r, iff1, iff2, im;
    bool halted;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> memory;

    // From tests.expected:
    std::vector<BusEvent> expectedTrace;
    // FUSE contend-only memory cycles (MC without a paired MR/MW): FUSE's
    // implementation skips the data read on paths where it doesn't need the
    // value (not-taken JR cc displacement, final DJNZ iteration). Real
    // hardware - and our core - performs the read. Our extra 'R' events that
    // pair with one of these cycles (same addr, t == MC+3) are dropped
    // before comparison.
    std::vector<std::pair<uint16_t, uint32_t>> contendOnlyCycles;  // (addr, MC time)
    uint16_t expRegs[13];
    uint8_t expI, expR, expIff1, expIff2, expIm;
    bool expHalted;
    uint32_t expTotal;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> expMemory;
};

// FUSE-style port stub: IN returns high byte of port address
class FusePortDecoder : public PortDecoder
{
public:
    explicit FusePortDecoder(EmulatorContext* context) : PortDecoder(context) {}
    void reset() override {}

    uint8_t DecodePortIn(uint16_t addr, uint16_t pc) override
    {
        (void)pc;
        _lastPortDecoded = true;  // Suppress floating-bus override
        return static_cast<uint8_t>(addr >> 8);
    }

    void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc) override
    {
        (void)addr;
        (void)value;
        (void)pc;
        _lastPortDecoded = true;
    }
};

inline std::vector<std::string> Tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

inline uint32_t HexVal(const std::string& s)
{
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

/// Parse tests.in
inline std::map<std::string, FuseCase> ParseTestsIn(const std::string& path)
{
    std::map<std::string, FuseCase> cases;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        FuseCase tc;
        tc.name = Tokenize(line)[0];

        // Registers line: 13 hex fields
        std::getline(file, line);
        auto regs = Tokenize(line);
        for (int i = 0; i < 13; i++)
            tc.regs[i] = static_cast<uint16_t>(HexVal(regs[i]));

        // State line: I R IFF1 IFF2 IM halted tstates
        std::getline(file, line);
        auto state = Tokenize(line);
        tc.i = static_cast<uint8_t>(HexVal(state[0]));
        tc.r = static_cast<uint8_t>(HexVal(state[1]));
        tc.iff1 = static_cast<uint8_t>(std::stoul(state[2]));
        tc.iff2 = static_cast<uint8_t>(std::stoul(state[3]));
        tc.im = static_cast<uint8_t>(std::stoul(state[4]));
        tc.halted = std::stoul(state[5]) != 0;

        // Memory blocks until standalone "-1"
        while (std::getline(file, line))
        {
            auto tokens = Tokenize(line);
            if (tokens.empty() || tokens[0] == "-1")
                break;
            uint16_t addr = static_cast<uint16_t>(HexVal(tokens[0]));
            std::vector<uint8_t> bytes;
            for (size_t i = 1; i < tokens.size() && tokens[i] != "-1"; i++)
                bytes.push_back(static_cast<uint8_t>(HexVal(tokens[i])));
            tc.memory.push_back({addr, bytes});
        }

        cases[tc.name] = tc;
    }

    return cases;
}

/// Parse tests.expected, merging into cases
inline void ParseTestsExpected(const std::string& path, std::map<std::string, FuseCase>& cases)
{
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::string name = Tokenize(line)[0];
        auto it = cases.find(name);

        // Collect event lines (indented), then registers line (13 fields, not indented... both
        // are space-separated; event lines have 3-4 tokens with a known type in position 2)
        std::vector<std::vector<std::string>> pending;
        std::vector<BusEvent> raw;  // MR/MW/PR/PW with resolved times
        std::map<uint16_t, uint32_t> lastMC;  // addr -> cycle start time
        std::map<uint16_t, uint32_t> unconsumedMC;  // MC not (yet) paired with MR/MW
        std::vector<std::pair<uint16_t, uint32_t>> contendOnly;

        while (std::getline(file, line))
        {
            auto tokens = Tokenize(line);
            if (tokens.size() >= 3 &&
                (tokens[1] == "MC" || tokens[1] == "MR" || tokens[1] == "MW" || tokens[1] == "PR" ||
                 tokens[1] == "PW" || tokens[1] == "PC"))
            {
                uint32_t t = static_cast<uint32_t>(std::stoul(tokens[0]));
                uint16_t addr = static_cast<uint16_t>(HexVal(tokens[2]));

                if (tokens[1] == "MC")
                {
                    // Previous MC on this addr never got its data event: a
                    // contend-only cycle (or an internal T - filtered later
                    // by requiring exact t == MC+3 pairing with our reads)
                    if (unconsumedMC.count(addr))
                        contendOnly.push_back({addr, unconsumedMC[addr]});
                    lastMC[addr] = t;
                    unconsumedMC[addr] = t;
                }
                else if (tokens[1] == "MR" || tokens[1] == "MW")
                {
                    uint8_t value = static_cast<uint8_t>(HexVal(tokens[3]));
                    // Our R/W fires at cycle start + 3
                    uint32_t mcTime = lastMC.count(addr) ? lastMC[addr] : (t >= 3 ? t - 3 : 0);
                    unconsumedMC.erase(addr);
                    raw.push_back({tokens[1] == "MR" ? 'R' : 'W', addr, value, mcTime + 3});
                }
                else if (tokens[1] == "PR" || tokens[1] == "PW")
                {
                    uint8_t value = static_cast<uint8_t>(HexVal(tokens[3]));
                    raw.push_back({tokens[1] == "PR" ? 'I' : 'O', addr, value, t});
                }
                // "PC" (port contend) carries no data - ignore
                continue;
            }

            // First non-event line = final registers
            break;
        }

        if (it == cases.end())
            continue;  // Expected entry without input - skip

        FuseCase& tc = it->second;
        tc.expectedTrace = raw;

        // Flush trailing unpaired MCs (e.g., the not-taken displacement cycle
        // at the very end of a trace)
        for (const auto& [addr, t] : unconsumedMC)
            contendOnly.push_back({addr, t});
        tc.contendOnlyCycles = contendOnly;

        auto regs = Tokenize(line);
        for (int i = 0; i < 13; i++)
            tc.expRegs[i] = static_cast<uint16_t>(HexVal(regs[i]));

        std::getline(file, line);
        auto state = Tokenize(line);
        tc.expI = static_cast<uint8_t>(HexVal(state[0]));
        tc.expR = static_cast<uint8_t>(HexVal(state[1]));
        tc.expIff1 = static_cast<uint8_t>(std::stoul(state[2]));
        tc.expIff2 = static_cast<uint8_t>(std::stoul(state[3]));
        tc.expIm = static_cast<uint8_t>(std::stoul(state[4]));
        tc.expHalted = std::stoul(state[5]) != 0;
        tc.expTotal = static_cast<uint32_t>(std::stoul(state[6]));

        // Optional memory-change lines until blank line
        while (std::getline(file, line) && !line.empty())
        {
            auto tokens = Tokenize(line);
            if (tokens.empty())
                break;
            uint16_t addr = static_cast<uint16_t>(HexVal(tokens[0]));
            std::vector<uint8_t> bytes;
            for (size_t i = 1; i < tokens.size() && tokens[i] != "-1"; i++)
                bytes.push_back(static_cast<uint8_t>(HexVal(tokens[i])));
            tc.expMemory.push_back({addr, bytes});
        }
    }
}

/// testdata/z80/fuse/<name>, anchored to the executable (cwd-relative paths break when earlier tests change
/// the working directory)
inline std::string FindDataFile(const std::string& name)
{
    std::string exeDir = FileHelper::GetExecutablePath();
    for (const std::string& prefix :
         {exeDir + "/../../testdata/z80/fuse/", exeDir + "/../testdata/z80/fuse/",
          exeDir + "/../../../testdata/z80/fuse/", std::string("testdata/z80/fuse/"),
          std::string("../testdata/z80/fuse/")})
    {
        std::string path = prefix + name;
        if (std::ifstream(path).good())
            return path;
    }
    return name;  // Let the open fail with a clear assert
}

/// Both FUSE files, merged
inline std::map<std::string, FuseCase> LoadCases()
{
    auto cases = ParseTestsIn(FindDataFile("tests.in"));
    ParseTestsExpected(FindDataFile("tests.expected"), cases);
    return cases;
}

/// Documented divergence from the FUSE vectors: interrupted block-op flags (David Banks' hardware-verified
/// undocumented flag model postdates the vectors). AF is not compared for these four
inline const std::set<std::string>& SkipAFCases()
{
    static const std::set<std::string> skip = {"edb2_1", "edb3_1", "edb9_2", "edbb_1"};
    return skip;
}

/// Load a case's registers and memory (all four Z80 banks cleared first)
inline void LoadState(Z80* z80, Memory* memory, const FuseCase& tc)
{
    for (uint8_t bank = 0; bank < 4; bank++)
    {
        uint8_t* page = memory->GetPhysicalAddressForZ80Page(bank);
        ASSERT_NE(page, nullptr);
        memset(page, 0, 0x4000);
    }

    for (const auto& block : tc.memory)
    {
        uint16_t addr = block.first;
        for (uint8_t byte : block.second)
            memory->DirectWriteToZ80Memory(addr++, byte);
    }

    z80->af = tc.regs[0];
    z80->bc = tc.regs[1];
    z80->de = tc.regs[2];
    z80->hl = tc.regs[3];
    z80->alt.af = tc.regs[4];
    z80->alt.bc = tc.regs[5];
    z80->alt.de = tc.regs[6];
    z80->alt.hl = tc.regs[7];
    z80->ix = tc.regs[8];
    z80->iy = tc.regs[9];
    z80->sp = tc.regs[10];
    z80->pc = tc.regs[11];
    z80->memptr = tc.regs[12];
    z80->i = tc.i;
    z80->r_low = tc.r & 0x7F;
    z80->r_hi = tc.r & 0x80;
    z80->iff1 = tc.iff1;
    z80->iff2 = tc.iff2;
    z80->im = tc.im;
    z80->halted = tc.halted ? 1 : 0;
    z80->prefix = 0;
    z80->boundary = Z80_BOUNDARY_NONE;
    z80->q = 0;  // each vector starts from a fresh CPU: no flags changed by a previous instruction (SCF / CCF read Q)
}

/// Compare the CPU's final registers, flags, interrupt state and changed memory with the case's expectations
/// (skipAF: the documented block-flag divergence, see FusePhase_Test)
inline void CompareFinalState(Z80* z80, const FuseCase& tc, bool skipAFCompare, std::vector<std::string>& issues)
{
    uint16_t finalRegs[13] = { z80->af,     z80->bc,     z80->de,     z80->hl, z80->alt.af, z80->alt.bc, z80->alt.de,
                               z80->alt.hl, z80->ix,     z80->iy,     z80->sp, z80->pc,     z80->memptr };
    static const char* regNames[13] = { "AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'", "IX", "IY", "SP", "PC", "MEMPTR" };
    for (int i = 0; i < 13; i++)
    {
        if (i == 0 && skipAFCompare)
            continue;  // Documented Banks-vs-classic block-flag divergence
        if (finalRegs[i] != tc.expRegs[i])
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s=%04X exp %04X", regNames[i], finalRegs[i], tc.expRegs[i]);
            issues.push_back(buf);
        }
    }

    uint8_t finalR = (z80->r_low & 0x7F) | (z80->r_hi & 0x80);
    if (finalR != tc.expR)
        issues.push_back("R mismatch");
    if (z80->i != tc.expI)
        issues.push_back("I mismatch");
    if ((z80->iff1 ? 1 : 0) != tc.expIff1 || (z80->iff2 ? 1 : 0) != tc.expIff2)
        issues.push_back("IFF mismatch");
    if (z80->im != tc.expIm)
        issues.push_back("IM mismatch");
    if ((z80->halted != 0) != tc.expHalted)
        issues.push_back("halted mismatch");

    for (const auto& block : tc.expMemory)
    {
        uint16_t addr = block.first;
        for (uint8_t expected : block.second)
        {
            uint8_t got = z80->DirectRead(addr);
            if (got != expected)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "mem[%04X]=%02X exp %02X", addr, got, expected);
                issues.push_back(buf);
            }
            addr++;
        }
    }
}

/// FUSE's contend-only convention: FUSE skips the data read on paths where it doesn't need the value
/// (not-taken JR cc displacement, final DJNZ iteration) and logs a bare MC. Real hardware - and our core -
/// reads the byte. Drop our 'R' events that pair exactly (addr, t == MC+3) with such a cycle
inline std::vector<BusEvent> FilterContendOnly(const std::vector<BusEvent>& raw, const FuseCase& tc)
{
    std::vector<BusEvent> filtered;
    for (const BusEvent& ev : raw)
    {
        if (ev.type == 'N')
            continue;  // internal cycles: CompareIdle
        bool contendOnly = false;
        if (ev.type == 'R')
        {
            for (const auto& cycle : tc.contendOnlyCycles)
            {
                if (cycle.first == ev.addr && ev.tOffset == cycle.second + 3)
                {
                    contendOnly = true;
                    break;
                }
            }
        }
        if (!contendOnly)
            filtered.push_back(ev);
    }
    return filtered;
}

/// Event-by-event comparison: type, address, value and T-state offset
inline void CompareEvents(const std::vector<BusEvent>& got, const std::vector<BusEvent>& expected,
                          std::vector<std::string>& issues)
{
    if (got.size() != expected.size())
    {
        issues.push_back("event count " + std::to_string(got.size()) + " != " + std::to_string(expected.size()));
        return;
    }
    for (size_t i = 0; i < got.size(); i++)
    {
        const BusEvent& g = got[i];
        const BusEvent& e = expected[i];
        if (g.type != e.type || g.addr != e.addr || g.value != e.value || g.tOffset != e.tOffset)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "event %zu: got %c@+%u %04X=%02X, exp %c@+%u %04X=%02X", i, g.type, g.tOffset,
                     g.addr, g.value, e.type, e.tOffset, e.addr, e.value);
            issues.push_back(buf);
        }
    }
}

/// Our raw bus trace against the case's FUSE trace (contend-only reads filtered)
inline void CompareTrace(const std::vector<BusEvent>& raw, const FuseCase& tc, std::vector<std::string>& issues)
{
    CompareEvents(FilterContendOnly(raw, tc), tc.expectedTrace, issues);
}

/// FUSE's MC-only checkpoints that are internal (no-MREQ) T-states, in time order: the bare MCs minus the
/// displacement reads FUSE skips but our core performs (they pair with one of our 'R' events at MC+3)
inline std::vector<BusEvent> ExpectedIdleCycles(const std::vector<BusEvent>& raw, const FuseCase& tc)
{
    std::vector<BusEvent> idle;
    for (const auto& cycle : tc.contendOnlyCycles)
    {
        bool isRead = false;
        for (const BusEvent& ev : raw)
            isRead |= ev.type == 'R' && ev.addr == cycle.first && ev.tOffset == cycle.second + 3;
        if (!isRead)
            idle.push_back({ 'N', cycle.first, 0, cycle.second });
    }
    std::sort(idle.begin(), idle.end(), [](const BusEvent& a, const BusEvent& b) { return a.tOffset < b.tOffset; });
    return idle;
}

/// Our internal-cycle events ('N', one per T-state) against FUSE's no-MREQ checkpoints: address and T-state
inline void CompareIdle(const std::vector<BusEvent>& raw, const FuseCase& tc, std::vector<std::string>& issues)
{
    std::vector<BusEvent> got;
    for (const BusEvent& ev : raw)
        if (ev.type == 'N')
            got.push_back(ev);
    std::vector<std::string> idleIssues;
    CompareEvents(got, ExpectedIdleCycles(raw, tc), idleIssues);
    for (const std::string& issue : idleIssues)
        issues.push_back("idle " + issue);
}
}  // namespace FuseVectors
