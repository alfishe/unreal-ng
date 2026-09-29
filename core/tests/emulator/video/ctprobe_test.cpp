#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/debugmanager.h"
#include "debugger/assembler/z80textassembler.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

/// ctprobe (tools/verification/contention/ctprobe; design docs/inprogress/2026-09-28-m1-contention/test-programs.md §3):
/// the emulated-side contention probe. The program times code fragments at exact frame T-states with the
/// Bobrowski / Rak engine, compares them with expected tables and prints a report. This suite
/// - builds the probe: assembles it with the in-tree assembler and fills its expected tables from an oracle that
///   owes nothing to the emulator (the contention pattern tables, the raster geometry, the bus cycles of each
///   instruction from FUSE's tables);
/// - runs it on each machine and checks every value against the oracle, and that the probe's own verdict and
///   detection agree;
/// - keeps the committed reference files (ctprobe.tap, ctprobe.trd) equal to the source, and loads them the way
///   a user would.

namespace
{
enum class Rule : uint8_t
{
    Ula48 = 0,      // Ferranti ULA, 48K
    Ula128 = 1,     // Ferranti ULA, 128K / +2
    GateArray = 2,  // +2A / +3
    None = 3,       // the clones
    Scorpion = 4    // no contention, 69888 T frame, and any unused port reads the fetched attribute
};

struct ClassTiming
{
    uint32_t onset;  // first contended T-state (INT-relative)
    uint32_t lineT;
};

ClassTiming Timing(Rule rule)
{
    switch (rule)
    {
        case Rule::Ula48:
            return { 14335, 224 };
        case Rule::Ula128:
        case Rule::GateArray:
            return { 14361, 228 };
        case Rule::Scorpion:
            return { 14335, 224 };
        case Rule::None:
            break;
    }
    return { 14361, 224 };
}

struct Machine
{
    const char* editor;  // RomEditorFixture::BootEditor name
    Rule rule;
    uint32_t onset;      // what the probe detects
    uint8_t caps;        // what the probe detects (the host opens the +3's paging first)
};

const std::vector<Machine>& Machines()
{
    static const std::vector<Machine> machines = {
        { "48K", Rule::Ula48, 14335, 0 },
        { "128K-48BASIC", Rule::Ula128, 14361, 1 },
        { "Plus3-48BASIC", Rule::GateArray, 14361, 3 },
        { "Pentagon-48BASIC", Rule::None, 14361, 1 },
        { "Scorpion-48BASIC", Rule::Scorpion, 14335, 1 },
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
/// address on the bus, or an I/O cycle. `force` overrides the address map (the +3 layout cases, whose code
/// switches the map under itself)
struct Cycle
{
    enum Kind { Mem, Internal, Io } kind;
    uint16_t address;
    uint8_t length;
    int8_t force = -1;
};

Cycle M1(uint16_t a, int8_t force = -1) { return { Cycle::Mem, a, 4, force }; }
Cycle Rd(uint16_t a, int8_t force = -1) { return { Cycle::Mem, a, 3, force }; }
Cycle Wr(uint16_t a, int8_t force = -1) { return { Cycle::Mem, a, 3, force }; }
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
    Oracle(Rule rule, uint8_t pageAtC000) : _rule(rule), _t(Timing(rule)), _page(pageAtC000) {}

    bool Contended(uint16_t a) const
    {
        switch (_rule)
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
            case Rule::Scorpion:
                return false;
        }
        return false;
    }

    uint32_t Delay(uint32_t t) const
    {
        static const uint8_t ula[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };
        static const uint8_t gateArray[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
        if (_rule == Rule::None || _rule == Rule::Scorpion || t < _t.onset)
            return 0;
        const uint32_t d = t - _t.onset;
        if (d / _t.lineT >= 192)
            return 0;
        const uint32_t pos = d % _t.lineT;
        if (pos < 128)
            return _rule == Rule::GateArray ? gateArray[pos & 7] : ula[pos & 7];
        return (_rule == Rule::GateArray && pos == 128) ? 1 : 0;  // the gate array holds one T more
    }

    /// The fragment's duration from `t0`, plus the wait of the RET the probe places after it (its 10 T are
    /// not part of the result; the return address is popped from the uncontended probe stack)
    uint32_t Duration(const std::vector<Cycle>& cycles, uint16_t retAddress, uint32_t t0) const
    {
        const bool ula = _rule == Rule::Ula48 || _rule == Rule::Ula128;
        uint32_t t = t0;
        auto contend = [&](const Cycle& c) {
            if (c.force >= 0 ? c.force == 1 : Contended(c.address))
                t += Delay(t);
        };
        for (const Cycle& c : cycles)
        {
            if (c.kind == Cycle::Mem)
            {
                contend(c);
            }
            else if (c.kind == Cycle::Internal)
            {
                if (ula)
                    contend(c);
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
                    t += Delay(t);
                t += 3;
                continue;
            }
            t += c.length;
        }
        contend(M1(retAddress));  // the RET's opcode fetch
        return t - t0;
    }

    /// The byte IN A,(#FF) reads with its I/O cycle starting at `s`, the first 32 screen cells holding the
    /// probe's pattern (bitmap #10+n, attribute #40+n): on the Ferranti ULA an I/O cycle starting on the onset
    /// reads the bitmap byte, then the attribute, the next pair, #FF for the 4 T without a fetch (FUSE, Zero,
    /// ZXMAK2, MAME, pico-spec; Butler's floating-bus tests 36/37). The Scorpion's unused ports read the
    /// attribute of the cell being fetched (programmer's manual, port #FF): the cell advances every 4 T from
    /// 4 T before the paper (INT + 14336, Xpeccy / ZXMAK2); the T grid is this project's model, not a hardware
    /// measurement. #FF elsewhere
    uint8_t FloatingBus(uint32_t s) const
    {
        if (_rule == Rule::Scorpion)
        {
            const uint32_t t = s + 1;  // the port is read at IORQ
            const uint32_t fetchStart = 14336 - 4;
            if (t < fetchStart || t >= fetchStart + 128)
                return 0xFF;
            return static_cast<uint8_t>(0x40 + (t - fetchStart) / 4);
        }
        if ((_rule != Rule::Ula48 && _rule != Rule::Ula128) || s < _t.onset)
            return 0xFF;
        const uint32_t d = s - _t.onset;
        if (d / _t.lineT != 0)  // the pattern is on the first line only
            return 0xFF;
        const uint32_t pos = d % _t.lineT;
        if (pos >= 128 || (pos & 7) >= 4)
            return 0xFF;
        const uint32_t cell = (pos >> 3) * 2 + ((pos & 7) >= 2 ? 1 : 0);
        return static_cast<uint8_t>(((pos & 1) ? 0x40 : 0x10) + cell);
    }

private:
    Rule _rule;
    ClassTiming _t;
    uint8_t _page;
};

constexpr uint16_t kIr = 0xBE00;     // I = #BE (the engine's IM2 table) on the internal cycles that show IR
constexpr uint16_t kStack = 0xBD00;  // the probe's stack, uncontended

using SymbolFn = std::function<uint16_t(const char*)>;

/// The bus cycles of each case's fragment, placed at `pc`
std::vector<Cycle> Fragment(uint8_t id, uint16_t pc, const SymbolFn& sym)
{
    std::vector<Cycle> c;
    auto ldRrNn = [&](uint16_t at) { c.insert(c.end(), { M1(at), Rd(at + 1), Rd(at + 2) }); };
    auto pushDe = [&](uint16_t at) {
        c.push_back(M1(at));
        Internal(c, kIr, 1);
        c.insert(c.end(), { Wr(kStack - 1), Wr(kStack - 2) });
    };
    auto popDe = [&](uint16_t at) { c.insert(c.end(), { M1(at), Rd(kStack - 2), Rd(kStack - 1) }); };
    auto ldNnSp = [&](uint16_t at, bool store) {
        const uint16_t save = sym("SPSAVE");
        c.insert(c.end(), { M1(at), M1(at + 1), Rd(at + 2), Rd(at + 3) });
        c.insert(c.end(), { store ? Wr(save) : Rd(save), store ? Wr(save + 1) : Rd(save + 1) });
    };

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
        case 18:
        case 19:
        case 20:
        case 21:  // +3 layout 0..3, switched by the fragment itself
        {
            const int layout = id - 18;
            const int8_t slot2 = layout >= 1 ? 1 : 0;  // page 2, or page 6
            const int8_t slot0 = layout >= 1 ? 1 : 0;  // page 0, or page 4
            const int8_t slot3 = layout == 1 ? 1 : 0;  // page 3, or page 7 in layout 1
            c = { M1(pc), Rd(pc + 1), Rd(pc + 2), Rd(sym("DEF1FFD")) };                          // LD A,(nn)
            c.insert(c.end(), { M1(pc + 3), M1(pc + 4), Rd(pc + 5), M1(pc + 6), Rd(pc + 7) });  // LD L,A; AND; OR
            ldRrNn(pc + 8);
            c.insert(c.end(), { M1(pc + 11), M1(pc + 12), Io(0x1FFD) });  // OUT (C),A: the layout is on
            c.push_back(M1(pc + 13, slot2));                               // NOP
            c.insert(c.end(), { M1(pc + 14, slot2), Rd(pc + 15, slot2), Rd(pc + 16, slot2), Rd(0x0000, slot0) });
            c.insert(c.end(), { M1(pc + 17, slot2), Rd(pc + 18, slot2), Rd(pc + 19, slot2), Rd(0xC000, slot3) });
            c.push_back(M1(pc + 20, slot2));                                            // LD A,L
            c.insert(c.end(), { M1(pc + 21, slot2), M1(pc + 22, slot2), Io(0x1FFD) });  // OUT (C),A: back
            break;
        }
        case 30:  // LD A,(#4000)
        case 31:  // LD (#4000),A
        case 32:
            c = { M1(pc), Rd(pc + 1), Rd(pc + 2), Rd(0x4000) };
            break;
        case 33:  // LD (SPSAVE),SP ; LD SP,#4100 ; PUSH BC ; POP BC ; LD SP,(SPSAVE)
            ldNnSp(pc, true);
            ldRrNn(pc + 4);
            c.push_back(M1(pc + 7));
            Internal(c, kIr, 1);
            c.insert(c.end(), { Wr(0x40FF), Wr(0x40FE) });
            c.insert(c.end(), { M1(pc + 8), Rd(0x40FE), Rd(0x40FF) });
            ldNnSp(pc + 9, false);
            break;
        case 34:  // PUSH DE ; LD HL,#4000 ; LD DE,#4100 ; LD BC,1 ; LDI ; POP DE
            pushDe(pc);
            ldRrNn(pc + 1);
            ldRrNn(pc + 4);
            ldRrNn(pc + 7);
            c.insert(c.end(), { M1(pc + 10), M1(pc + 11), Rd(0x4000), Wr(0x4100) });
            Internal(c, 0x4100, 2);
            popDe(pc + 12);
            break;
        case 40:  // LD HL,#4000 ; INC (HL)
            ldRrNn(pc);
            c.insert(c.end(), { M1(pc + 3), Rd(0x4000) });
            Internal(c, 0x4000, 1);
            c.push_back(Wr(0x4000));
            break;
        case 41:  // JR +0
            c = { M1(pc), Rd(pc + 1) };
            Internal(c, pc + 1, 5);
            break;
        case 42:  // LD (SPSAVE),SP ; LD SP,#4100 ; EX (SP),HL ; LD SP,(SPSAVE)
            ldNnSp(pc, true);
            ldRrNn(pc + 4);
            c.insert(c.end(), { M1(pc + 7), Rd(0x4100), Rd(0x4101) });
            Internal(c, 0x4101, 1);
            c.insert(c.end(), { Wr(0x4101), Wr(0x4100) });
            Internal(c, 0x4100, 2);
            ldNnSp(pc + 8, false);
            break;
        case 43:  // LD A,#40 ; LD I,A ; ADD HL,BC ; LD A,#BE ; LD I,A
            c = { M1(pc), Rd(pc + 1), M1(pc + 2), M1(pc + 3) };
            Internal(c, kIr, 1);  // IR still shows the old I (#BE)
            c.push_back(M1(pc + 4));
            Internal(c, 0x4000, 7);
            c.insert(c.end(), { M1(pc + 5), Rd(pc + 6), M1(pc + 7), M1(pc + 8) });
            Internal(c, 0x4000, 1);
            break;
        case 44:  // PUSH DE ; LD HL,#4000 ; LD DE,#4100 ; LD BC,2 ; LDIR ; POP DE
            pushDe(pc);
            ldRrNn(pc + 1);
            ldRrNn(pc + 4);
            ldRrNn(pc + 7);
            c.insert(c.end(), { M1(pc + 10), M1(pc + 11), Rd(0x4000), Wr(0x4100) });
            Internal(c, 0x4100, 2);
            Internal(c, 0x4100, 5);  // the repeat
            c.insert(c.end(), { M1(pc + 10), M1(pc + 11), Rd(0x4001), Wr(0x4101) });
            Internal(c, 0x4101, 2);
            popDe(pc + 12);
            break;
        case 45:  // LD HL,0 ; LD (#4000),HL ; LD HL,#4000 ; LD BC,2 ; LD A,#AA ; CPIR
            ldRrNn(pc);
            c.insert(c.end(), { M1(pc + 3), Rd(pc + 4), Rd(pc + 5), Wr(0x4000), Wr(0x4001) });
            ldRrNn(pc + 6);
            ldRrNn(pc + 9);
            c.insert(c.end(), { M1(pc + 12), Rd(pc + 13) });
            c.insert(c.end(), { M1(pc + 14), M1(pc + 15), Rd(0x4000) });
            Internal(c, 0x4000, 5);
            Internal(c, 0x4000, 5);  // the repeat
            c.insert(c.end(), { M1(pc + 14), M1(pc + 15), Rd(0x4001) });
            Internal(c, 0x4001, 5);
            break;
        case 46:  // LD A,(IX+0) at #4000
            c = { M1(pc), M1(pc + 1), Rd(pc + 2) };
            Internal(c, pc + 2, 5);
            c.push_back(Rd(sym("IXDATA")));
            break;
        case 50:
        case 51:
        case 52:
        case 53:  // LD BC,port ; IN A,(C)
        {
            static const uint16_t ports[4] = { 0x00FE, 0x40FE, 0x00FF, 0x40FF };
            ldRrNn(pc);
            c.insert(c.end(), { M1(pc + 3), M1(pc + 4), Io(ports[id - 50]) });
            break;
        }
        case 54:  // XOR A ; LD BC,#40FE ; OUT (C),A
            c.push_back(M1(pc));
            ldRrNn(pc + 1);
            c.insert(c.end(), { M1(pc + 4), M1(pc + 5), Io(0x40FE) });
            break;
        default:  // 10-17: the RET alone; 55: a value case; 60: RET in ROM
            break;
    }
    return c;
}

constexpr size_t kRecord = 19;  // the probe's case record

struct CaseRecord
{
    uint8_t id;
    uint8_t flags;
    uint8_t page;
    uint16_t target;
    uint8_t length;
    int16_t offset;
    uint8_t count;
    uint16_t results;
    std::string name;
};

using PeekFn = std::function<uint8_t(uint16_t)>;

std::vector<CaseRecord> ReadCases(uint16_t at, const PeekFn& peek)
{
    std::vector<CaseRecord> cases;
    auto word = [&](uint16_t a) { return static_cast<uint16_t>(peek(a) | (peek(a + 1) << 8)); };
    for (; peek(at) != 0; at += kRecord)
    {
        CaseRecord r;
        r.id = peek(at);
        r.flags = peek(at + 1);
        r.page = peek(at + 2);
        r.target = word(at + 4);
        r.length = peek(at + 8);
        r.offset = static_cast<int16_t>(word(at + 9));
        r.count = peek(at + 11);
        r.results = word(at + 12);
        for (int i = 0; i < 5; i++)
            r.name += static_cast<char>(peek(at + 14 + i));
        cases.push_back(r);
    }
    return cases;
}

/// What the oracle expects for one case on a machine of `rule`, one value per timed T-state
std::vector<uint8_t> Expected(Rule rule, const CaseRecord& r, const SymbolFn& sym)
{
    const Oracle oracle(rule, r.page == 0xFF ? 0 : r.page);
    const uint32_t first = static_cast<uint32_t>(static_cast<int32_t>(Timing(rule).onset) + r.offset);
    const uint16_t target = r.target == 0 ? sym("FRAGBUF") : r.target;
    const bool romRet = (r.flags & 8) != 0;
    const std::vector<Cycle> cycles = romRet ? std::vector<Cycle>{} : Fragment(r.id, target, sym);
    std::vector<uint8_t> values;
    for (uint8_t i = 0; i < r.count; i++)
    {
        const uint32_t t = first + i;
        if (r.flags & 4)
            values.push_back(oracle.FloatingBus(t + 11));  // XOR A, then IN A,(#FF): its I/O cycle 11 T in
        else
            values.push_back(static_cast<uint8_t>(
                oracle.Duration(cycles, romRet ? 0x0000 : static_cast<uint16_t>(target + r.length), t)));
    }
    return values;
}

/// The probe's folder: tools/verification/contention/ctprobe
std::string ProbePath(const std::string& file)
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "ctprobe" / file)
        .make_preferred()
        .string();
}

std::string ReadText(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<uint8_t> ReadBinary(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

constexpr uint8_t kClasses = 5;

/// The probe: assembled at 40000 and its expected tables filled for every class
struct Probe
{
    AsmResult asmResult;
    std::vector<uint8_t> bytes;  // 40000 .. PROBEEND
    std::vector<CaseRecord> cases;

    uint16_t Sym(const char* name) const
    {
        auto it = asmResult.symbols.find(name);
        EXPECT_NE(it, asmResult.symbols.end()) << name;
        return it == asmResult.symbols.end() ? 0 : static_cast<uint16_t>(it->second);
    }
};

Probe BuildProbe()
{
    Probe p;
    Z80TextAssembler assembler;
    p.asmResult = assembler.Assemble(ReadText(ProbePath("ctprobe.asm")) + "\n" +
                                         ReadText(ProbePath("engine.asm")),
                                     40000);
    if (!p.asmResult.ok)
    {
        ADD_FAILURE() << p.asmResult.error.line << ": " << p.asmResult.error.message << " | "
                      << p.asmResult.error.sourceLine;
        return p;
    }
    p.bytes = p.asmResult.bytes;
    auto peek = [&](uint16_t a) { return p.bytes[a - 40000]; };
    p.cases = ReadCases(p.Sym("CASES"), peek);

    const SymbolFn sym = [&](const char* s) { return p.Sym(s); };
    const uint16_t results = p.Sym("RESULTS");
    const uint16_t size = static_cast<uint16_t>(p.Sym("RESULTSEND") - results);
    const uint16_t expected = p.Sym("EXPECTED");
    for (uint8_t cls = 0; cls < kClasses; cls++)
        for (const CaseRecord& r : p.cases)
        {
            const std::vector<uint8_t> values = Expected(static_cast<Rule>(cls), r, sym);
            for (size_t i = 0; i < values.size(); i++)
                p.bytes[expected + cls * size + (r.results - results) + i - 40000] = values[i];
        }
    return p;
}

// ---- the reference files ----

/// A tokenized BASIC line (BasicEncoder::tokenize writes neither the hidden numbers nor keywords after a
/// statement's first, so the loaders are spelled out here, as TrdosBootInjector does)
std::vector<uint8_t> BasicLine(uint16_t number, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> line{ static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number & 0xFF) };
    const size_t length = body.size() + 1;
    line.push_back(static_cast<uint8_t>(length & 0xFF));
    line.push_back(static_cast<uint8_t>(length >> 8));
    line.insert(line.end(), body.begin(), body.end());
    line.push_back(0x0D);
    return line;
}

/// A number as BASIC stores it: the digits, then the hidden 5-byte small integer
std::vector<uint8_t> BasicNumber(uint16_t value)
{
    const std::string digits = std::to_string(value);
    std::vector<uint8_t> n(digits.begin(), digits.end());
    n.insert(n.end(), { 0x0E, 0x00, 0x00, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8), 0x00 });
    return n;
}

std::vector<uint8_t> Concat(std::initializer_list<std::vector<uint8_t>> parts)
{
    std::vector<uint8_t> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

constexpr uint8_t kClear = 0xFD, kLoad = 0xEF, kCode = 0xAF, kRandomize = 0xF9, kUsr = 0xC0, kRem = 0xEA;

/// 10 CLEAR 39999 / 20 <load> / 30 RANDOMIZE USR 40000
std::vector<uint8_t> Loader(const std::vector<uint8_t>& loadStatement)
{
    return Concat({ BasicLine(10, Concat({ { kClear }, BasicNumber(39999) })), BasicLine(20, loadStatement),
                    BasicLine(30, Concat({ { kRandomize, kUsr }, BasicNumber(40000) })) });
}

std::vector<uint8_t> TapBlock(uint8_t flag, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> block;
    const size_t length = data.size() + 2;
    block.push_back(static_cast<uint8_t>(length & 0xFF));
    block.push_back(static_cast<uint8_t>(length >> 8));
    block.push_back(flag);
    uint8_t sum = flag;
    for (uint8_t b : data)
    {
        block.push_back(b);
        sum ^= b;
    }
    block.push_back(sum);
    return block;
}

std::vector<uint8_t> TapHeader(uint8_t type, const std::string& name, uint16_t length, uint16_t param1, uint16_t param2)
{
    std::vector<uint8_t> h{ type };
    std::string padded = name;
    padded.resize(10, ' ');
    h.insert(h.end(), padded.begin(), padded.end());
    for (uint16_t w : { length, param1, param2 })
    {
        h.push_back(static_cast<uint8_t>(w & 0xFF));
        h.push_back(static_cast<uint8_t>(w >> 8));
    }
    return h;
}

/// ctprobe.tap: a BASIC loader (auto-runs line 10) and the code at 40000
std::vector<uint8_t> BuildTap(const Probe& p)
{
    const std::vector<uint8_t> basic = Loader({ kLoad, '"', '"', kCode });  // LOAD "" CODE
    std::vector<uint8_t> tap;
    auto append = [&](const std::vector<uint8_t>& block) { tap.insert(tap.end(), block.begin(), block.end()); };
    const uint16_t basicLength = static_cast<uint16_t>(basic.size());
    append(TapBlock(0x00, TapHeader(0, "ctprobe", basicLength, 10, basicLength)));
    append(TapBlock(0xFF, basic));
    append(TapBlock(0x00, TapHeader(3, "ctprobe", static_cast<uint16_t>(p.bytes.size()), 40000, 32768)));
    append(TapBlock(0xFF, p.bytes));
    return tap;
}

/// ctprobe.sym: every label and constant, "NAME equ #ADDR" (for debuggers, other emulators' harnesses)
std::vector<uint8_t> BuildSym(const Probe& p)
{
    std::string text = "; ctprobe symbols (generated by ctprobe_test.cpp)\n";
    char line[80];
    for (const auto& [name, value] : p.asmResult.symbols)
    {
        std::snprintf(line, sizeof line, "%s equ #%04X\n", name.c_str(), static_cast<unsigned>(value & 0xFFFF));
        text += line;
    }
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// ctprobe.trd: TR-DOS 80 tracks, two sides; "boot" (BASIC, auto-runs line 10) loads "ctprobe" (CODE, 40000)
std::vector<uint8_t> BuildTrd(const Probe& p)
{
    constexpr size_t sector = 256;
    std::vector<uint8_t> trd(160 * 16 * sector, 0);

    // RANDOMIZE USR 15619: REM : LOAD "ctprobe" CODE - TR-DOS runs the command after the REM
    const std::string name = "\"ctprobe\"";
    std::vector<uint8_t> basic = Loader(Concat({ { kRandomize, kUsr }, BasicNumber(15619), { ':', kRem, ':', kLoad },
                                                 std::vector<uint8_t>(name.begin(), name.end()), { kCode } }));
    const uint16_t basicLength = static_cast<uint16_t>(basic.size());
    basic.insert(basic.end(), { 0x80, 0xAA, 10, 0 });  // autostart line 10, after the program

    size_t next = 16;  // logical sector: track 1, sector 0
    int files = 0;
    auto addFile = [&](const std::string& name, char type, uint16_t start, uint16_t length,
                       const std::vector<uint8_t>& data) {
        const uint8_t sectors = static_cast<uint8_t>((data.size() + sector - 1) / sector);
        uint8_t* e = &trd[static_cast<size_t>(files) * 16];
        std::string padded = name;
        padded.resize(8, ' ');
        std::copy(padded.begin(), padded.end(), e);
        e[8] = static_cast<uint8_t>(type);
        e[9] = static_cast<uint8_t>(start & 0xFF);
        e[10] = static_cast<uint8_t>(start >> 8);
        e[11] = static_cast<uint8_t>(length & 0xFF);
        e[12] = static_cast<uint8_t>(length >> 8);
        e[13] = sectors;
        e[14] = static_cast<uint8_t>(next % 16);
        e[15] = static_cast<uint8_t>(next / 16);
        std::copy(data.begin(), data.end(), trd.begin() + static_cast<std::ptrdiff_t>(next * sector));
        next += sectors;
        files++;
    };
    addFile("boot", 'B', basicLength, basicLength, basic);
    addFile("ctprobe", 'C', 40000, static_cast<uint16_t>(p.bytes.size()), p.bytes);

    uint8_t* info = &trd[8 * sector];
    const uint16_t freeSectors = static_cast<uint16_t>(160 * 16 - next);
    info[0xE1] = static_cast<uint8_t>(next % 16);
    info[0xE2] = static_cast<uint8_t>(next / 16);
    info[0xE3] = 0x16;  // 80 tracks, two sides
    info[0xE4] = static_cast<uint8_t>(files);
    info[0xE5] = static_cast<uint8_t>(freeSectors & 0xFF);
    info[0xE6] = static_cast<uint8_t>(freeSectors >> 8);
    info[0xE7] = 0x10;  // TR-DOS
    const std::string label = "ctprobe ";
    std::copy(label.begin(), label.end(), info + 0xF5);
    return trd;
}
}  // namespace

class CtProbe_Test : public RomEditorFixture
{
protected:
    struct CaseResult
    {
        CaseRecord record;
        std::vector<uint8_t> measured;
        std::vector<uint8_t> expected;
    };

    Probe _probe;
    std::vector<CaseResult> _results;

    uint8_t Peek(uint16_t a) const { return _context->pMemory->DirectReadFromZ80Memory(a); }
    uint16_t PeekW(uint16_t a) const { return static_cast<uint16_t>(Peek(a) | (Peek(a + 1) << 8)); }
    void Poke(uint16_t a, uint8_t v) { _context->pMemory->DirectWriteToZ80Memory(a, v); }

    /// Opens the +3's #7FFD paging (the 48 BASIC boot locks it; a reset into the menu leaves it open), so the
    /// paging and layout cases run, as when the probe runs from +3 BASIC
    void OpenPaging() { _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20); }

    /// Boots `m`, runs the probe with everything detected (or only case `only`) and collects the values of the
    /// cases that ran, with the oracle's
    void RunProbe(const Machine& m, uint8_t only)
    {
        BootEditor(m.editor);
        ASSERT_FALSE(HasFatalFailure());
        _probe = BuildProbe();
        ASSERT_FALSE(HasFailure());
        ASSERT_LT(40000 + _probe.bytes.size(), 0xBE00u) << "the probe runs into the engine's IM2 table";

        for (size_t i = 0; i < _probe.bytes.size(); i++)
            Poke(static_cast<uint16_t>(40000 + i), _probe.bytes[i]);
        Poke(_probe.Sym("ONLY"), only);
        OpenPaging();

        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = _probe.Sym("HOSTENTRY");
        z80->sp = 0xBDFE;
        z80->iff1 = z80->iff2 = 1;
        int frames = 0;
        ASSERT_TRUE(RunUntil([&] { frames++; return Peek(_probe.Sym("DONE")) == 1; }, 40000))
            << "the probe never finished: case " << int(Peek(_probe.Sym("CURID"))) << ", T "
            << PeekW(_probe.Sym("CURT")) << ", PC #" << std::hex << z80->pc << " IY #" << z80->iy << " SCR_CT "
            << std::dec << int(Peek(0x5C8C)) << " DF_SZ " << int(Peek(0x5C6B)) << " iff1 " << int(z80->iff1)
            << "\n" << Screen();

        std::printf("[ctprobe %s] frames to DONE: %d\n", m.editor, frames);
        EXPECT_EQ(Peek(_probe.Sym("CLASS")), static_cast<uint8_t>(m.rule)) << "the probe's class detection";
        EXPECT_EQ(PeekW(_probe.Sym("ONSET")), m.onset) << "the probe's frame-length detection";
        EXPECT_EQ(Peek(_probe.Sym("CAPS")), m.caps) << "the probe's paging detection";

        const SymbolFn sym = [&](const char* s) { return _probe.Sym(s); };
        const uint8_t caps = Peek(_probe.Sym("CAPS"));
        for (const CaseRecord& r : _probe.cases)
        {
            if ((only != 0 && r.id != only) || (r.flags & 3 & ~caps) != 0)
                continue;
            CaseResult result{ r, {}, Expected(m.rule, r, sym) };
            for (uint8_t i = 0; i < r.count; i++)
                result.measured.push_back(Peek(static_cast<uint16_t>(r.results + i)));
            _results.push_back(result);
        }
    }

    static std::string Row(const std::vector<uint8_t>& v)
    {
        std::string s;
        for (uint8_t x : v)
            s += (s.empty() ? "" : " ") + std::to_string(x);
        return s;
    }
};

/// The contended NOP (M1-01) on the 48K in the default run: one case, ~20 engine calls after the ROM boot
TEST_F(CtProbe_Test, ContendedNop48K)
{
    RunProbe(Machines().front(), 1);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(_results.size(), 1u);
    EXPECT_EQ(Row(_results[0].measured), Row(_results[0].expected));
    EXPECT_EQ(PeekW(_probe.Sym("FAILS")), 0) << "the probe's own verdict:\n" << Screen();
}

/// The committed reference files are what the source builds (no drift)
TEST(CtProbeFiles_Test, CommittedFilesMatchTheSource)
{
    const Probe p = BuildProbe();
    ASSERT_FALSE(p.bytes.empty());
    EXPECT_TRUE(ReadBinary(ProbePath("ctprobe.tap")) == BuildTap(p))
        << "rebuild with UNREAL_CTPROBE_EXPORT=1";
    EXPECT_TRUE(ReadBinary(ProbePath("ctprobe.trd")) == BuildTrd(p))
        << "rebuild with UNREAL_CTPROBE_EXPORT=1";
    EXPECT_TRUE(ReadBinary(ProbePath("ctprobe.sym")) == BuildSym(p))
        << "rebuild with UNREAL_CTPROBE_EXPORT=1";
}

/// UNREAL_CTPROBE_EXPORT=1 writes the reference files from the source
TEST(CtProbeFiles_Test, Export)
{
    if (!std::getenv("UNREAL_CTPROBE_EXPORT"))
        GTEST_SKIP() << "set UNREAL_CTPROBE_EXPORT=1 to write ctprobe.tap / ctprobe.trd";
    const Probe p = BuildProbe();
    ASSERT_FALSE(p.bytes.empty());
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> files = { { "ctprobe.tap", BuildTap(p) },
                                                                              { "ctprobe.trd", BuildTrd(p) },
                                                                              { "ctprobe.sym", BuildSym(p) } };
    for (const auto& [file, data] : files)
    {
        std::ofstream out(ProbePath(file), std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(out.good()) << file;
    }
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
    for (const CaseResult& r : _results)
    {
        const bool match = r.measured == r.expected;
        std::cout << "[ctprobe " << m.editor << "] " << r.record.name << " from T "
                  << static_cast<int32_t>(m.onset) + r.record.offset << ": " << Row(r.measured)
                  << (match ? "  ok" : "  expected " + Row(r.expected)) << std::endl;
        EXPECT_TRUE(match) << r.record.name;
    }
    EXPECT_EQ(PeekW(_probe.Sym("FAILS")), 0) << "the probe's own verdict:\n" << Screen();
    EXPECT_TRUE(ScreenHas("ALL VALUES AS EXPECTED")) << Screen();
}

INSTANTIATE_TEST_SUITE_P(Opt, CtProbeSweep_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? Machines() : std::vector<Machine>{}),
                         MachineName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CtProbeSweep_Test);


/// The reference files loaded the way a user would, the probe detecting everything itself (opt-in with
/// UNREAL_TIMING_SUITES=1; a tape or disk load and a full run per machine)
struct Loading
{
    const char* editor;
    const char* file;                   // ctprobe.tap or ctprobe.trd
    std::vector<std::string> commands;  // typed in order
    const char* paging;                 // the report's paging line
};

void PrintTo(const Loading& l, std::ostream* os)
{
    *os << l.editor;
}

class CtProbeLoad_Test : public CtProbe_Test, public ::testing::WithParamInterface<Loading>
{
};

TEST_P(CtProbeLoad_Test, ReportsAllValuesAsExpected)
{
    const Loading& l = GetParam();
    BootEditor(l.editor);
    ASSERT_FALSE(HasFatalFailure());
    _context->pFeatureManager->setFeature(Features::kFastTape, true);
    const std::string path = ProbePath(l.file);
    if (std::string(l.file).find(".tap") != std::string::npos)
        ASSERT_TRUE(_emulator->LoadTape(path));
    else
        ASSERT_TRUE(_emulator->LoadDisk(path));

    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
    for (const std::string& command : l.commands)
    {
        ASSERT_TRUE(typer->Request(command, CommandTyper::Options{})) << command;
        ASSERT_TRUE(RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000)) << command;
    }
    ASSERT_TRUE(RunUntil([&] { return ScreenHas("AS EXPECTED") || ScreenHas("VALUES WRONG"); }, 30000)) << Screen();
    EXPECT_TRUE(ScreenHas("ALL VALUES AS EXPECTED")) << Screen();
    EXPECT_TRUE(ScreenHas(l.paging)) << Screen();
}

std::vector<Loading> Loadings()
{
    return {
        { "48K", "ctprobe.tap", { "LOAD \"\"" }, "Paging no" },
        { "128K-128BASIC", "ctprobe.tap", { "LOAD \"\"" }, "Paging yes, +3 layouts no" },
        { "Plus3-3BASIC", "ctprobe.tap", { "LOAD \"t:\"", "LOAD \"\"" }, "Paging yes, +3 layouts yes" },
        { "Pentagon-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
        { "Scorpion-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
    };
}

std::string LoadingName(const ::testing::TestParamInfo<Loading>& info)
{
    std::string name = info.param.editor;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return name;
}

INSTANTIATE_TEST_SUITE_P(Opt, CtProbeLoad_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? Loadings() : std::vector<Loading>{}),
                         LoadingName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CtProbeLoad_Test);


