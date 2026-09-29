#include "stdafx.h"
#include "pch.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/assembler/z80textassembler.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

/// ctprobe (testdata/contention/ctprobe; design docs/inprogress/2026-09-28-m1-contention/test-programs.md §3):
/// the emulated-side contention probe. The program times code fragments at exact frame T-states with the
/// Bobrowski / Rak engine and stores the durations; this suite boots a machine to 48 BASIC, assembles the
/// probe with the in-tree assembler, runs it, and compares every duration with an independent oracle: the
/// contention pattern tables, the raster geometry and the per-instruction bus cycles (FUSE's tables).

namespace
{
enum class Rule
{
    Ula48,      // Ferranti ULA, 48K
    Ula128,     // Ferranti ULA, 128K / +2
    GateArray,  // +2A / +3
    None        // the clones
};

struct Machine
{
    const char* editor;  // RomEditorFixture::BootEditor name
    Rule rule;
    uint32_t onset;      // first contended T-state (INT-relative)
    uint32_t lineT;
    uint8_t caps;        // the probe's CAPS: bit 0 #7FFD paging
    uint16_t turboOff;   // a port whose read selects 3.5 MHz (0: no hardware turbo)
};

const std::vector<Machine>& Machines()
{
    static const std::vector<Machine> machines = {
        { "48K", Rule::Ula48, 14335, 224, 0, 0 },
        { "128K-48BASIC", Rule::Ula128, 14361, 228, 1, 0 },
        { "Plus3-48BASIC", Rule::GateArray, 14361, 228, 1, 0 },
        { "Pentagon-48BASIC", Rule::None, 14361, 224, 1, 0 },
        { "Scorpion-48BASIC", Rule::None, 14335, 224, 1, 0x1FFD },
    };
    return machines;
}

void PrintTo(const Machine& m, std::ostream* os)
{
    *os << m.editor;
}

std::string MachineName(const ::testing::TestParamInfo<Machine>& info)
{
    std::string name = info.param.editor;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return name;
}

/// One bus cycle of a fragment: a memory cycle (M1 4 T, read / write 3 T), an internal T-state with an
/// address on the bus, or an I/O cycle
struct Cycle
{
    enum Kind { Mem, Internal, Io } kind;
    uint16_t address;
    uint8_t length;
};

Cycle M1(uint16_t a) { return { Cycle::Mem, a, 4 }; }
Cycle Rd(uint16_t a) { return { Cycle::Mem, a, 3 }; }
Cycle Wr(uint16_t a) { return { Cycle::Mem, a, 3 }; }
Cycle Io(uint16_t port) { return { Cycle::Io, port, 4 }; }
void Internal(std::vector<Cycle>& c, uint16_t a, int n)
{
    for (int i = 0; i < n; i++)
        c.push_back({ Cycle::Internal, a, 1 });
}

/// The independent oracle: the pattern tables and the raster, nothing from the emulator
class Oracle
{
public:
    Oracle(const Machine& m, uint8_t pageAtC000) : _m(m), _page(pageAtC000) {}

    bool Contended(uint16_t a) const
    {
        switch (_m.rule)
        {
            case Rule::Ula48:
                return a >= 0x4000 && a < 0x8000;
            case Rule::Ula128:
                return (a >= 0x4000 && a < 0x8000) || (a >= 0xC000 && (_page & 1));
            case Rule::GateArray:
                if (a < 0x4000)
                    return false;  // ROM
                if (a < 0x8000)
                    return true;   // page 5
                if (a < 0xC000)
                    return false;  // page 2
                return _page >= 4;
            case Rule::None:
                return false;
        }
        return false;
    }

    uint32_t Delay(uint32_t t) const
    {
        static const uint8_t ula[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };
        static const uint8_t gateArray[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
        if (_m.rule == Rule::None || t < _m.onset)
            return 0;
        const uint32_t d = t - _m.onset;
        if (d / _m.lineT >= 192)
            return 0;
        const uint32_t pos = d % _m.lineT;
        if (pos < 128)
            return _m.rule == Rule::GateArray ? gateArray[pos & 7] : ula[pos & 7];
        return (_m.rule == Rule::GateArray && pos == 128) ? 1 : 0;  // the gate array holds one T more
    }

    /// The fragment's duration from `t0`, plus the wait of the RET the probe places after it (its 10 T are
    /// not part of the result; the return address is popped from the uncontended probe stack)
    uint32_t Duration(const std::vector<Cycle>& cycles, uint16_t retAddress, uint32_t t0) const
    {
        const bool ula = _m.rule == Rule::Ula48 || _m.rule == Rule::Ula128;
        uint32_t t = t0;
        auto contend = [&](uint16_t a) {
            if (Contended(a))
                t += Delay(t);
        };
        for (const Cycle& c : cycles)
        {
            if (c.kind == Cycle::Mem)
            {
                contend(c.address);
            }
            else if (c.kind == Cycle::Internal)
            {
                if (ula)
                    contend(c.address);
            }
            else if (ula)  // I/O on the Ferranti ULA: FUSE's four patterns
            {
                const bool high = Contended(c.address);
                const bool ulaPort = (c.address & 1) == 0;
                if (high)
                {
                    t += Delay(t);
                    t += 1;
                    for (int k = 0; k < (ulaPort ? 1 : 3); k++)
                    {
                        t += Delay(t);
                        t += ulaPort ? 3 : 1;
                    }
                    continue;
                }
                t += 1;
                if (ulaPort)
                {
                    t += Delay(t);
                    t += 3;
                }
                else
                {
                    t += 3;
                }
                continue;
            }
            t += c.length;
        }
        contend(retAddress);  // the RET's opcode fetch
        return t - t0;
    }

private:
    Machine _m;
    uint8_t _page;
};

constexpr uint16_t kIr = 0xBE00;  // I = #BE (the engine's IM2 table) on the internal cycles that show IR

/// The bus cycles of each case's fragment, placed at `pc`; `sym` resolves the probe's labels
std::vector<Cycle> Fragment(uint8_t id, uint16_t pc, const std::function<uint16_t(const char*)>& sym)
{
    std::vector<Cycle> c;
    switch (id)
    {
        case 1:
        case 2:
        case 3:  // NOP
            c = { M1(pc) };
            break;
        case 4:  // LD A,n
            c = { M1(pc), Rd(pc + 1) };
            break;
        case 5:  // RLC B
        case 6:  // NEG
            c = { M1(pc), M1(pc + 1) };
            break;
        case 7:  // INC IX ; DEC IX
            c = { M1(pc), M1(pc + 1) };
            Internal(c, kIr, 2);
            c.push_back(M1(pc + 2));
            c.push_back(M1(pc + 3));
            Internal(c, kIr, 2);
            break;
        case 8:  // RLC (IX+0), IX = IXDATA
        {
            const uint16_t ix = sym("IXDATA");
            c = { M1(pc), M1(pc + 1), Rd(pc + 2), Rd(pc + 3) };
            Internal(c, pc + 3, 2);
            c.push_back(Rd(ix));
            Internal(c, ix, 1);
            c.push_back(Wr(ix));
            break;
        }
        case 9:  // DD DD DD NOP
            c = { M1(pc), M1(pc + 1), M1(pc + 2), M1(pc + 3) };
            break;
        case 20:  // LD A,(#4000)
        case 21:  // LD (#4000),A
        case 22:
            c = { M1(pc), Rd(pc + 1), Rd(pc + 2), Rd(0x4000) };
            break;
        case 23:  // LD (SPSAVE),SP ; LD SP,#4100 ; PUSH BC ; POP BC ; LD SP,(SPSAVE)
        {
            const uint16_t save = sym("SPSAVE");
            c = { M1(pc), M1(pc + 1), Rd(pc + 2), Rd(pc + 3), Wr(save), Wr(save + 1) };
            c.insert(c.end(), { M1(pc + 4), Rd(pc + 5), Rd(pc + 6) });
            c.push_back(M1(pc + 7));
            Internal(c, kIr, 1);
            c.insert(c.end(), { Wr(0x40FF), Wr(0x40FE) });
            c.insert(c.end(), { M1(pc + 8), Rd(0x40FE), Rd(0x40FF) });
            c.insert(c.end(), { M1(pc + 9), M1(pc + 10), Rd(pc + 11), Rd(pc + 12), Rd(save), Rd(save + 1) });
            break;
        }
        case 30:  // LD HL,#4000 ; INC (HL)
            c = { M1(pc), Rd(pc + 1), Rd(pc + 2), M1(pc + 3), Rd(0x4000) };
            Internal(c, 0x4000, 1);
            c.push_back(Wr(0x4000));
            break;
        case 31:  // JR +0
            c = { M1(pc), Rd(pc + 1) };
            Internal(c, pc + 1, 5);
            break;
        case 32:  // LD A,#40 ; LD I,A ; ADD HL,BC ; LD A,#BE ; LD I,A
            c = { M1(pc), Rd(pc + 1), M1(pc + 2), M1(pc + 3) };
            Internal(c, kIr, 1);  // IR still shows the old I (#BE)
            c.push_back(M1(pc + 4));
            Internal(c, 0x4000, 7);
            c.insert(c.end(), { M1(pc + 5), Rd(pc + 6), M1(pc + 7), M1(pc + 8) });
            Internal(c, 0x4000, 1);
            break;
        case 40:
        case 41:
        case 42:
        case 43:  // LD BC,port ; IN A,(C)
        {
            static const uint16_t ports[4] = { 0x00FE, 0x40FE, 0x00FF, 0x40FF };
            c = { M1(pc), Rd(pc + 1), Rd(pc + 2), M1(pc + 3), M1(pc + 4), Io(ports[id - 40]) };
            break;
        }
        case 44:  // XOR A ; LD BC,#40FE ; OUT (C),A
            c = { M1(pc), M1(pc + 1), Rd(pc + 2), Rd(pc + 3), M1(pc + 4), M1(pc + 5), Io(0x40FE) };
            break;
        default:  // 10-17: the RET alone
            break;
    }
    return c;
}

std::string ReadText(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

class CtProbe_Test : public RomEditorFixture
{
protected:
    struct CaseResult
    {
        uint8_t id;
        std::vector<uint32_t> measured;
        std::vector<uint32_t> expected;
        uint32_t firstT;
    };

    AsmResult _probe;
    std::vector<CaseResult> _results;
    uint32_t _onset = 0;

    uint16_t Sym(const char* name) const
    {
        auto it = _probe.symbols.find(name);
        EXPECT_NE(it, _probe.symbols.end()) << name;
        return it == _probe.symbols.end() ? 0 : static_cast<uint16_t>(it->second);
    }

    uint8_t Peek(uint16_t a) const { return _context->pMemory->DirectReadFromZ80Memory(a); }
    uint16_t PeekW(uint16_t a) const { return static_cast<uint16_t>(Peek(a) | (Peek(a + 1) << 8)); }
    void Poke(uint16_t a, uint8_t v) { _context->pMemory->DirectWriteToZ80Memory(a, v); }

    /// Boots `m`, runs the probe (every case, or only case `only`) and fills _results
    void RunProbe(const Machine& m, uint8_t only)
    {
        BootEditor(m.editor);
        ASSERT_FALSE(HasFatalFailure());

        Z80TextAssembler assembler;
        _probe = assembler.Assemble(ReadText(TestPathHelper::GetTestDataPath("contention/ctprobe/ctprobe.asm")) + "\n" +
                                        ReadText(TestPathHelper::GetTestDataPath("contention/ctprobe/engine.asm")),
                                    40000);
        ASSERT_TRUE(_probe.ok) << _probe.error.line << ": " << _probe.error.message << " | " << _probe.error.sourceLine;
        ASSERT_LT(_probe.endAddress, 0xBE00) << "the probe runs into the engine's IM2 table";

        for (size_t i = 0; i < _probe.bytes.size(); i++)
            Poke(static_cast<uint16_t>(40000 + i), _probe.bytes[i]);
        Poke(Sym("CAPS"), m.caps);
        Poke(Sym("ONLY"), only);
        // The Scorpion's ROM leaves the CPU at 7 MHz; the engine needs the INT pulse over before its handler
        // returns, as at 3.5 MHz (IN #1FFD selects it on the hardware too)
        if (m.turboOff != 0)
        {
            _context->pPortDecoder->DecodePortIn(m.turboOff, 0);
            RunFrames(1);  // the clock changes at the frame boundary
            ASSERT_EQ(_context->emulatorState.hw_turbo_shift, 0) << "still in turbo";
        }

        // The 48 BASIC boot locks #7FFD paging on the +2A / +3 (a reset into the menu leaves it open); the
        // paging cases need it open, as when the probe runs from 128 BASIC
        _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20);
        Poke(Sym("DEF7FFD"), _context->emulatorState.p7FFD);

        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = Sym("HOSTENTRY");
        z80->sp = 0xBDFE;
        z80->iff1 = z80->iff2 = 1;
        ASSERT_TRUE(RunUntil([&] { return Peek(Sym("DONE")) == 1; }, 40000))
            << "the probe never finished: case " << int(Peek(Sym("CURID"))) << ", T " << PeekW(Sym("CURT"))
            << ", PC #" << std::hex << z80->pc;

        _onset = PeekW(Sym("ONSET"));
        for (uint16_t rec = Sym("CASES"); Peek(rec) != 0; rec += 14)
        {
            const uint8_t id = Peek(rec);
            const uint8_t flags = Peek(rec + 1);
            if ((only != 0 && id != only) || (flags & ~m.caps) != 0)
                continue;
            const uint8_t page = Peek(rec + 2);
            uint16_t target = PeekW(rec + 4);
            if (target == 0)
                target = Sym("FRAGBUF");
            const uint8_t length = Peek(rec + 8);
            const int16_t offset = static_cast<int16_t>(PeekW(rec + 9));
            const uint8_t count = Peek(rec + 11);
            const uint16_t resultsAt = PeekW(rec + 12);

            const Oracle oracle(m, page == 0xFF ? 0 : page);
            const std::vector<Cycle> cycles = Fragment(id, target, [&](const char* s) { return Sym(s); });
            CaseResult r{ id, {}, {}, static_cast<uint32_t>(static_cast<int32_t>(m.onset) + offset) };
            for (uint8_t i = 0; i < count; i++)
            {
                r.measured.push_back(Peek(static_cast<uint16_t>(resultsAt + i)));
                r.expected.push_back(oracle.Duration(cycles, static_cast<uint16_t>(target + length), r.firstT + i));
            }
            _results.push_back(r);
        }
    }

    static std::string Row(const std::vector<uint32_t>& v)
    {
        std::string s;
        for (uint32_t x : v)
            s += (s.empty() ? "" : " ") + std::to_string(x);
        return s;
    }
};

/// The contended NOP (M1-01) on the 48K in the default run: one case, ~20 engine calls after the ROM boot
TEST_F(CtProbe_Test, ContendedNop48K)
{
    RunProbe(Machines().front(), 1);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(_onset, 14335u) << "the probe detects the 48K frame";
    ASSERT_EQ(_results.size(), 1u);
    EXPECT_EQ(Row(_results[0].measured), Row(_results[0].expected)) << "from T " << _results[0].firstT;
}

/// Every case on every machine: opt-in with UNREAL_TIMING_SUITES=1 (several thousand frames per machine)
class CtProbeSweep_Test : public CtProbe_Test, public ::testing::WithParamInterface<Machine>
{
};

TEST_P(CtProbeSweep_Test, EveryCaseMatchesTheOracle)
{
    const Machine& m = GetParam();
    RunProbe(m, 0);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(_onset, m.onset) << "the probe's frame-length detection";
    for (const CaseResult& r : _results)
    {
        const bool match = r.measured == r.expected;
        std::cout << "[ctprobe " << m.editor << "] case " << int(r.id) << " from T " << r.firstT << ": "
                  << Row(r.measured) << (match ? "  ok" : "  expected " + Row(r.expected)) << std::endl;
        EXPECT_TRUE(match) << "case " << int(r.id);
    }
}

INSTANTIATE_TEST_SUITE_P(Opt, CtProbeSweep_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? Machines() : std::vector<Machine>{}),
                         MachineName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CtProbeSweep_Test);

