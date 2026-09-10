#include "stdafx.h"
#include "pch.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

/// FUSE-derived full-opcode bus-phase tests (Phase 2/3 of the phase-test plan).
///
/// Data: testdata/z80/fuse/tests.in + tests.expected (FUSE emulator test
/// vectors, ~1300 cases covering every documented and undocumented opcode).
/// Each case provides initial CPU/memory state and the expected full bus
/// trace: every memory/port access with its T-state, plus final state.
///
/// Event mapping (verified against FUSE conventions):
///  - FUSE logs MC (cycle start) + MR/MW (data transfer). Memory data events
///    map to our busTraceHook 'R'/'W' at (MC time + 3) - rd()/wd() charge the
///    3T cycle then access, and FUSE M1 reads log at MC+4 while ours fire at
///    MC+3 (before the decode T) - both normalize to MC+3.
///  - FUSE PR/PW times equal our 'I'/'O' hook times exactly: both models put
///    the port access at the IORQ T-state (T2 of the IO cycle).
///
/// Port reads return the high byte of the port address (FUSE stub behavior),
/// so PR event values and final register state match the reference.

namespace
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

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

uint32_t hexVal(const std::string& s)
{
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

/// Parse tests.in
std::map<std::string, FuseCase> parseTestsIn(const std::string& path)
{
    std::map<std::string, FuseCase> cases;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        FuseCase tc;
        tc.name = tokenize(line)[0];

        // Registers line: 13 hex fields
        std::getline(file, line);
        auto regs = tokenize(line);
        for (int i = 0; i < 13; i++)
            tc.regs[i] = static_cast<uint16_t>(hexVal(regs[i]));

        // State line: I R IFF1 IFF2 IM halted tstates
        std::getline(file, line);
        auto state = tokenize(line);
        tc.i = static_cast<uint8_t>(hexVal(state[0]));
        tc.r = static_cast<uint8_t>(hexVal(state[1]));
        tc.iff1 = static_cast<uint8_t>(std::stoul(state[2]));
        tc.iff2 = static_cast<uint8_t>(std::stoul(state[3]));
        tc.im = static_cast<uint8_t>(std::stoul(state[4]));
        tc.halted = std::stoul(state[5]) != 0;

        // Memory blocks until standalone "-1"
        while (std::getline(file, line))
        {
            auto tokens = tokenize(line);
            if (tokens.empty() || tokens[0] == "-1")
                break;
            uint16_t addr = static_cast<uint16_t>(hexVal(tokens[0]));
            std::vector<uint8_t> bytes;
            for (size_t i = 1; i < tokens.size() && tokens[i] != "-1"; i++)
                bytes.push_back(static_cast<uint8_t>(hexVal(tokens[i])));
            tc.memory.push_back({addr, bytes});
        }

        cases[tc.name] = tc;
    }

    return cases;
}

/// Parse tests.expected, merging into cases
void parseTestsExpected(const std::string& path, std::map<std::string, FuseCase>& cases)
{
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::string name = tokenize(line)[0];
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
            auto tokens = tokenize(line);
            if (tokens.size() >= 3 &&
                (tokens[1] == "MC" || tokens[1] == "MR" || tokens[1] == "MW" || tokens[1] == "PR" ||
                 tokens[1] == "PW" || tokens[1] == "PC"))
            {
                uint32_t t = static_cast<uint32_t>(std::stoul(tokens[0]));
                uint16_t addr = static_cast<uint16_t>(hexVal(tokens[2]));

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
                    uint8_t value = static_cast<uint8_t>(hexVal(tokens[3]));
                    // Our R/W fires at cycle start + 3
                    uint32_t mcTime = lastMC.count(addr) ? lastMC[addr] : (t >= 3 ? t - 3 : 0);
                    unconsumedMC.erase(addr);
                    raw.push_back({tokens[1] == "MR" ? 'R' : 'W', addr, value, mcTime + 3});
                }
                else if (tokens[1] == "PR" || tokens[1] == "PW")
                {
                    uint8_t value = static_cast<uint8_t>(hexVal(tokens[3]));
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

        auto regs = tokenize(line);
        for (int i = 0; i < 13; i++)
            tc.expRegs[i] = static_cast<uint16_t>(hexVal(regs[i]));

        std::getline(file, line);
        auto state = tokenize(line);
        tc.expI = static_cast<uint8_t>(hexVal(state[0]));
        tc.expR = static_cast<uint8_t>(hexVal(state[1]));
        tc.expIff1 = static_cast<uint8_t>(std::stoul(state[2]));
        tc.expIff2 = static_cast<uint8_t>(std::stoul(state[3]));
        tc.expIm = static_cast<uint8_t>(std::stoul(state[4]));
        tc.expHalted = std::stoul(state[5]) != 0;
        tc.expTotal = static_cast<uint32_t>(std::stoul(state[6]));

        // Optional memory-change lines until blank line
        while (std::getline(file, line) && !line.empty())
        {
            auto tokens = tokenize(line);
            if (tokens.empty())
                break;
            uint16_t addr = static_cast<uint16_t>(hexVal(tokens[0]));
            std::vector<uint8_t> bytes;
            for (size_t i = 1; i < tokens.size() && tokens[i] != "-1"; i++)
                bytes.push_back(static_cast<uint8_t>(hexVal(tokens[i])));
            tc.expMemory.push_back({addr, bytes});
        }
    }
}
}  // namespace

class FusePhase_Test : public ::testing::Test
{
protected:
    // Resolve testdata anchored to the executable location (repo/build/bin/
    // core-tests -> repo/testdata) - cwd-relative paths break when earlier
    // tests in the suite change the working directory.
    static std::string findDataFile(const std::string& name)
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

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder* _originalDecoder = nullptr;
    FusePortDecoder* _fuseDecoder = nullptr;

    std::vector<BusEvent> _trace;
    uint32_t _t0 = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;

        // FUSE tests execute at arbitrary addresses incl. $0000 - map RAM
        // into bank 0 (page 6: unused by default banks 5/2/0)
        _memory->SetRAMPageToBank0(6);

        _fuseDecoder = new FusePortDecoder(_context);
        _originalDecoder = _context->pPortDecoder;
        _context->pPortDecoder = _fuseDecoder;

        _z80->busTraceHook = [this](char type, uint16_t addr, uint8_t value) {
            _trace.push_back({type, addr, value, _z80->t - _t0});
        };
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->busTraceHook = nullptr;
            _context->pPortDecoder = _originalDecoder;
            delete _fuseDecoder;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void loadState(const FuseCase& tc)
    {
        // Clear all four Z80 banks for deterministic reads
        for (uint8_t bank = 0; bank < 4; bank++)
        {
            uint8_t* page = _memory->GetPhysicalAddressForZ80Page(bank);
            ASSERT_NE(page, nullptr);
            memset(page, 0, 0x4000);
        }

        for (const auto& block : tc.memory)
        {
            uint16_t addr = block.first;
            for (uint8_t byte : block.second)
                _memory->DirectWriteToZ80Memory(addr++, byte);
        }

        _z80->af = tc.regs[0];
        _z80->bc = tc.regs[1];
        _z80->de = tc.regs[2];
        _z80->hl = tc.regs[3];
        _z80->alt.af = tc.regs[4];
        _z80->alt.bc = tc.regs[5];
        _z80->alt.de = tc.regs[6];
        _z80->alt.hl = tc.regs[7];
        _z80->ix = tc.regs[8];
        _z80->iy = tc.regs[9];
        _z80->sp = tc.regs[10];
        _z80->pc = tc.regs[11];
        _z80->memptr = tc.regs[12];
        _z80->i = tc.i;
        _z80->r_low = tc.r & 0x7F;
        _z80->r_hi = tc.r & 0x80;
        _z80->iff1 = tc.iff1;
        _z80->iff2 = tc.iff2;
        _z80->im = tc.im;
        _z80->halted = tc.halted ? 1 : 0;
        _z80->prefix = 0;
        _z80->eipos = 0;
    }

    /// Run one FUSE case; returns list of mismatch descriptions (empty = pass)
    std::vector<std::string> runCase(const FuseCase& tc, bool skipAFCompare = false)
    {
        std::vector<std::string> issues;

        loadState(tc);

        _trace.clear();
        _z80->t = 10000;  // Far from INT window, mid-frame
        _t0 = _z80->t;

        int steps = 0;
        while ((_z80->t - _t0) < tc.expTotal && steps++ < 64)
        {
            _z80->Z80Step();
        }

        uint32_t total = _z80->t - _t0;
        if (total != tc.expTotal)
            issues.push_back("total " + std::to_string(total) + " != " + std::to_string(tc.expTotal));

        // Structural rule for FUSE's contend-only convention: FUSE skips the
        // data read on paths where it doesn't need the value (not-taken JR cc
        // displacement, final DJNZ iteration) and logs a bare MC. Real
        // hardware - and our core - reads the byte. Drop our 'R' events that
        // pair exactly (addr, t == MC+3) with such a cycle before comparing.
        std::vector<BusEvent> trace;
        for (const BusEvent& ev : _trace)
        {
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
                trace.push_back(ev);
        }

        if (trace.size() != tc.expectedTrace.size())
        {
            issues.push_back("event count " + std::to_string(trace.size()) + " != " +
                             std::to_string(tc.expectedTrace.size()));
        }
        else
        {
            for (size_t i = 0; i < trace.size(); i++)
            {
                const BusEvent& got = trace[i];
                const BusEvent& exp = tc.expectedTrace[i];
                if (got.type != exp.type || got.addr != exp.addr || got.value != exp.value ||
                    got.tOffset != exp.tOffset)
                {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "event %zu: got %c@+%u %04X=%02X, exp %c@+%u %04X=%02X", i,
                             got.type, got.tOffset, got.addr, got.value, exp.type, exp.tOffset, exp.addr,
                             exp.value);
                    issues.push_back(buf);
                }
            }
        }

        // Final state
        uint16_t finalRegs[13] = {_z80->af,     _z80->bc,     _z80->de,     _z80->hl, _z80->alt.af,
                                  _z80->alt.bc, _z80->alt.de, _z80->alt.hl, _z80->ix, _z80->iy,
                                  _z80->sp,     _z80->pc,     _z80->memptr};
        static const char* regNames[13] = {"AF",  "BC",  "DE",  "HL", "AF'", "BC'", "DE'",
                                           "HL'", "IX",  "IY",  "SP", "PC",  "MEMPTR"};
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

        uint8_t finalR = (_z80->r_low & 0x7F) | (_z80->r_hi & 0x80);
        if (finalR != tc.expR)
            issues.push_back("R mismatch");
        if (_z80->i != tc.expI)
            issues.push_back("I mismatch");
        if ((_z80->iff1 ? 1 : 0) != tc.expIff1 || (_z80->iff2 ? 1 : 0) != tc.expIff2)
            issues.push_back("IFF mismatch");
        if (_z80->im != tc.expIm)
            issues.push_back("IM mismatch");
        if ((_z80->halted != 0) != tc.expHalted)
            issues.push_back("halted mismatch");

        for (const auto& block : tc.expMemory)
        {
            uint16_t addr = block.first;
            for (uint8_t expected : block.second)
            {
                uint8_t got = _z80->DirectRead(addr);
                if (got != expected)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "mem[%04X]=%02X exp %02X", addr, got, expected);
                    issues.push_back(buf);
                }
                addr++;
            }
        }

        return issues;
    }
};

TEST_F(FusePhase_Test, AllOpcodes)
{
    auto cases = parseTestsIn(findDataFile("tests.in"));
    ASSERT_GT(cases.size(), 1000u) << "FUSE tests.in not found or truncated";
    parseTestsExpected(findDataFile("tests.expected"), cases);

    // Documented divergence from the FUSE vectors (justified, not bugs):
    // interrupted block-op flags. Our core implements David Banks'
    // hardware-verified undocumented flag research (F5/F3 from PC.13/PC.11 on
    // repeat, adjusted PV) which postdates these FUSE vectors (classic flag
    // model - verified identical in FUSE official master as of 2026-08).
    // AF comparison skipped for these four; everything else verified.
    // (FUSE's contend-only logging for not-taken JR/DJNZ displacement reads
    // is handled structurally in runCase, not by exclusion.)
    static const std::set<std::string> skipAF = {"edb2_1", "edb3_1", "edb9_2", "edbb_1"};

    int passed = 0, failed = 0;
    std::string failureReport;

    for (const auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;  // No expected data merged

        auto issues = runCase(tc, skipAF.count(name) > 0);
        if (issues.empty())
        {
            passed++;
        }
        else
        {
            failed++;
            if (failureReport.size() < 8000)
            {
                failureReport += "\n[" + name + "] ";
                for (size_t i = 0; i < issues.size() && i < 4; i++)
                    failureReport += issues[i] + "; ";
            }
        }
    }

    EXPECT_EQ(failed, 0) << "FUSE phase cases: " << passed << " passed, " << failed << " failed"
                         << failureReport;
    std::cout << "[FUSE] " << passed << " passed, " << failed << " failed" << std::endl;
}
