#include "stdafx.h"
#include "pch.h"

#include <algorithm>
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
#include "_helpers/zxprogramfiles.h"
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
    Scorpion = 4,   // no contention, 69888 T frame, and any unused port reads the fetched attribute
    ScorpionEvenM1 = 5, // as Scorpion, and opcode fetches from RAM start on even T-states (Even M1)
    NoneEvenM1 = 6      // no contention, Even M1, unused ports without the attribute bus (MAME's Scorpion)
};

/// Even M1 (the Scorpion's WAIT logic, docs/emulator/design/core/memory-contention.md): an opcode fetch from RAM
/// that would start on an odd T-state waits one; ROM, data, I/O and the interrupt acknowledge never wait
uint32_t EvenM1Align(uint32_t t)
{
    return t + (t & 1);
}

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
        case Rule::ScorpionEvenM1:
        case Rule::NoneEvenM1:
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
    bool evenM1Off = false;  // a Scorpion board without Even M1 (the 2007 GAL re-creation; most emulators)
};

const std::vector<Machine>& Machines()
{
    static const std::vector<Machine> machines = {
        { "48K", Rule::Ula48, 14335, 0 },
        { "128K-48BASIC", Rule::Ula128, 14361, 1 },
        { "Plus3-48BASIC", Rule::GateArray, 14361, 3 },
        { "Pentagon-48BASIC", Rule::None, 14361, 1 },
        { "Scorpion-48BASIC", Rule::ScorpionEvenM1, 14335, 1 },
        { "Scorpion-48BASIC", Rule::Scorpion, 14335, 1, true },
    };
    return machines;
}

void PrintTo(const Machine& m, std::ostream* os)
{
    *os << m.editor << (m.evenM1Off ? " without Even M1" : "");
}

std::string MachineName(const ::testing::TestParamInfo<Machine>& info)
{
    std::string name = std::string(info.param.editor) + (info.param.evenM1Off ? "-NoEvenM1" : "");
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
            case Rule::ScorpionEvenM1:
            case Rule::NoneEvenM1:
                return false;
        }
        return false;
    }

    uint32_t Delay(uint32_t t) const
    {
        static const uint8_t ula[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };
        static const uint8_t gateArray[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
        if (_rule == Rule::None || _rule == Rule::Scorpion || _rule == Rule::ScorpionEvenM1 ||
            _rule == Rule::NoneEvenM1 || t < _t.onset)
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
    /// not part of the result; the return address is popped from the uncontended probe stack). With Even M1
    /// each opcode fetch from RAM that would start on an odd T-state waits one, the fragment's first one
    /// included (the WAIT is inside that fetch's cycle), and a RET in ROM returns to the engine, whose next
    /// fetch aligns
    uint32_t Duration(const std::vector<Cycle>& cycles, uint16_t retAddress, uint32_t t0) const
    {
        const bool ula = _rule == Rule::Ula48 || _rule == Rule::Ula128;
        const bool evenM1 = _rule == Rule::ScorpionEvenM1 || _rule == Rule::NoneEvenM1;
        uint32_t t = t0;
        auto contend = [&](const Cycle& c) {
            if (c.force >= 0 ? c.force == 1 : Contended(c.address))
                t += Delay(t);
        };
        for (const Cycle& c : cycles)
        {
            if (c.kind == Cycle::Mem)
            {
                if (evenM1 && c.length == 4 && c.address >= 0x4000)
                    t = EvenM1Align(t);
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
        if (evenM1 && retAddress >= 0x4000)
            t = EvenM1Align(t);
        if (evenM1 && retAddress < 0x4000 && ((t + 10) & 1))
            t++;  // back in the engine, in RAM
        return t - t0;
    }

    /// The byte IN A,(#FF) reads with its I/O cycle starting at `s`, the first 32 screen cells holding the
    /// probe's pattern (bitmap #10+n, attribute #40+n): on the Ferranti ULA an I/O cycle starting on the onset
    /// reads the bitmap byte, then the attribute, the next pair, #FF for the 4 T without a fetch (FUSE, Zero,
    /// ZXMAK2, MAME, pico-spec; Butler's floating-bus tests 36/37). The Scorpion's unused ports read the
    /// attribute of the cell being fetched (programmer's manual, port #FF): the cell advances every 4 T from
    /// 4 T before the paper (INT + 14336, Xpeccy / ZXMAK2); the T grid is this project's model, not a hardware
    /// measurement. #FF elsewhere
    /// An IN from a port whose high byte is in contended memory, its I/O cycle starting at `t1`: the Ferranti ULA
    /// holds T1, T2, TW and T3 (C:1 four times), and the CPU takes the data at the end of T3. Returns the T1 of an
    /// unstretched cycle with the same T3, the point FloatingBus is calibrated to
    /// (docs/inprogress/2026-09-30-fusetest-core-defects/research.md claim 1)
    uint32_t LateIoSampleT1(uint32_t t1) const
    {
        if (_rule != Rule::Ula48 && _rule != Rule::Ula128)
            return t1;
        uint32_t t = t1;
        for (int k = 0; k < 4; k++)
        {
            t += Delay(t);
            if (k < 3)
                t += 1;
        }
        return t - 3;  // t: the start of T3
    }

    uint8_t FloatingBus(uint32_t s) const
    {
        if (_rule == Rule::Scorpion || _rule == Rule::ScorpionEvenM1)
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
        // ---- P-03 (ids 61-65): more port instructions; fragments FrOut00FE .. FrOutAn in ctprobe.asm ----
        case 61:
        case 62:
        case 63:  // XOR A ; LD BC,port ; OUT (C),A
        {
            static const uint16_t ports[3] = { 0x00FE, 0x00FF, 0x40FF };
            c.push_back(M1(pc));
            ldRrNn(pc + 1);
            c.insert(c.end(), { M1(pc + 4), M1(pc + 5), Io(ports[id - 61]) });
            break;
        }
        case 64:  // LD A,#40 ; IN A,(#FE): the port is A:n = #40FE
        case 65:  // LD A,#40 ; OUT (#FE),A
            c = { M1(pc), Rd(pc + 1), M1(pc + 2), Rd(pc + 3), Io(0x40FE) };
            break;

        // ---- P-04 (ids 66-69): block I/O, FUSE's cycle order; fragments FrIni .. FrOtir ----
        case 66:  // LD HL,INBUF ; LD BC,#40FE ; INI: IR tick, port, then the write
            ldRrNn(pc);
            ldRrNn(pc + 3);
            c.insert(c.end(), { M1(pc + 6), M1(pc + 7) });
            Internal(c, kIr, 1);
            c.insert(c.end(), { Io(0x40FE), Wr(sym("INBUF")) });
            break;
        case 67:  // LD HL,OUTBUF ; LD BC,#41FE ; OUTI: the read, B-- (#40), then the port #40FE
            ldRrNn(pc);
            ldRrNn(pc + 3);
            c.insert(c.end(), { M1(pc + 6), M1(pc + 7) });
            Internal(c, kIr, 1);
            c.insert(c.end(), { Rd(sym("OUTBUF")), Io(0x40FE) });
            break;
        case 68:  // LD HL,INBUF ; LD BC,#02FE ; INIR: two rounds, the repeat's 5 ticks on HL
        {
            const uint16_t buf = sym("INBUF");
            ldRrNn(pc);
            ldRrNn(pc + 3);
            for (int round = 0; round < 2; round++)
            {
                c.insert(c.end(), { M1(pc + 6), M1(pc + 7) });
                Internal(c, kIr, 1);
                c.insert(c.end(), { Io(static_cast<uint16_t>(0x02FE - round * 0x100)), Wr(static_cast<uint16_t>(buf + round)) });
                if (round == 0)
                    Internal(c, buf, 5);
            }
            break;
        }
        case 69:  // LD HL,OUTBUF ; LD BC,#03FE ; OTIR: three rounds, the repeats' 5 ticks on the new BC
        {
            const uint16_t buf = sym("OUTBUF");
            ldRrNn(pc);
            ldRrNn(pc + 3);
            for (int round = 0; round < 3; round++)
            {
                const uint16_t port = static_cast<uint16_t>(0x02FE - round * 0x100);
                c.insert(c.end(), { M1(pc + 6), M1(pc + 7) });
                Internal(c, kIr, 1);
                c.insert(c.end(), { Rd(static_cast<uint16_t>(buf + round)), Io(port) });
                if (round < 2)
                    Internal(c, port, 5);
            }
            break;
        }

        // ---- P-05 (ids 70-73): the port's high byte in the page at #C000; fragments FrInC0FE, FrInC0FF ----
        // The page comes from the case record (Oracle::Contended); only the 128K / +2 contend it
        case 70:
        case 71:
        case 72:
        case 73:  // LD BC,#C0FE / #C0FF ; IN A,(C)
            ldRrNn(pc);
            c.insert(c.end(), { M1(pc + 3), M1(pc + 4), Io(id < 72 ? 0xC0FE : 0xC0FF) });
            break;

        default:  // 10-17: the RET alone; 55, 56: value cases; 60: RET in ROM
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
        const uint32_t start = rule == Rule::ScorpionEvenM1 ? EvenM1Align(t) : t;  // Even M1 aligns the first fetch
        if ((r.flags & 4) && r.id == 56)  // P-02B: LD BC,#40FF / IN A,(C): the I/O cycle 18 T in
            values.push_back(oracle.FloatingBus(oracle.LateIoSampleT1(start + 18)));
        else if (r.flags & 4)  // XOR A, then IN A,(#FF): its I/O cycle 11 T in
            values.push_back(oracle.FloatingBus(start + 11));
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

using ZxProgramFiles::ReadBinary;
using ZxProgramFiles::ReadText;

constexpr uint8_t kClasses = 7;
constexpr uint16_t kOrg = 36000;  // where the probe loads and runs (org in ctprobe.asm)

/// The probe: assembled at kOrg and its expected tables filled for every class
struct Probe
{
    AsmResult asmResult;
    std::vector<uint8_t> bytes;  // kOrg .. PROBEEND
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
                                     kOrg);
    if (!p.asmResult.ok)
    {
        ADD_FAILURE() << p.asmResult.error.line << ": " << p.asmResult.error.message << " | "
                      << p.asmResult.error.sourceLine;
        return p;
    }
    p.bytes = p.asmResult.bytes;
    auto peek = [&](uint16_t a) { return p.bytes[a - kOrg]; };
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
                p.bytes[expected + cls * size + (r.results - results) + i - kOrg] = values[i];
        }
    return p;
}

// ---- the reference files (tools of core/tests/_helpers/zxprogramfiles.h) ----

std::vector<uint8_t> BuildTap(const Probe& p)
{
    return ZxProgramFiles::BuildTap("ctprobe", kOrg, p.bytes);
}

std::vector<uint8_t> BuildTrd(const Probe& p)
{
    return ZxProgramFiles::BuildTrd("ctprobe", kOrg, p.bytes);
}

/// ctprobe.sym: every label and constant (for debuggers, other emulators' harnesses)
std::vector<uint8_t> BuildSym(const Probe& p)
{
    return ZxProgramFiles::BuildSym("; ctprobe symbols (generated by ctprobe_test.cpp)", p.asmResult.symbols);
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
        if (m.evenM1Off)
            _context->config.even_M1 = 0;  // a Scorpion board without Even M1
        _probe = BuildProbe();
        ASSERT_FALSE(HasFailure());
        ASSERT_LT(kOrg + _probe.bytes.size(), 0xBE00u) << "the probe runs into the engine's IM2 table";

        for (size_t i = 0; i < _probe.bytes.size(); i++)
            Poke(static_cast<uint16_t>(kOrg + i), _probe.bytes[i]);
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

        std::printf("[ctprobe %s] frames to DONE: %d, measured frame %u T, class %d\n", m.editor, frames,
                    PeekW(_probe.Sym("FRAMET")) + PeekW(_probe.Sym("FRAMEADJ")) + 32768u, Peek(_probe.Sym("CLASS")));
        // The Scorpion aligns opcode fetches from RAM to even T-states (Even M1): the probe finds it and
        // switches to its 2 T-state delays
        const bool evenM1 = _context->config.even_M1 != 0;
        EXPECT_EQ(Peek(_probe.Sym("EVENM1")), evenM1 ? 1 : 0) << "the probe's Even M1 check\n" << Screen();
        EXPECT_EQ(Peek(_probe.Sym("CLASS")), static_cast<uint8_t>(m.rule)) << "the probe's class detection";
        EXPECT_EQ(PeekW(_probe.Sym("ONSET")), m.onset) << "the probe's frame-length detection";
        EXPECT_EQ(Peek(_probe.Sym("CAPS")), m.caps) << "the probe's paging detection";

        const SymbolFn sym = [&](const char* s) { return _probe.Sym(s); };
        const uint8_t caps = Peek(_probe.Sym("CAPS"));
        for (const CaseRecord& r : _probe.cases)
        {
            if ((only != 0 && r.id != only) || (r.flags & 3 & ~caps) != 0)
                continue;
            if ((r.flags & 32) && (m.rule == Rule::None || m.rule == Rule::NoneEvenM1))  // not on the plain clones
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

/// UNREAL_CTPROBE_EXPORT=1 writes the reference files from the source (into UNREAL_CTPROBE_EXPORT_DIR if set)
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
        const char* dir = std::getenv("UNREAL_CTPROBE_EXPORT_DIR");  // elsewhere, e.g. to try a change out
        std::ofstream out(dir ? std::string(dir) + "/" + file : ProbePath(file), std::ios::binary);
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
        { "ATM710-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
        { "ATM3-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
        { "Profi-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
        { "ProfScorp-TRDOS", "ctprobe.trd", { "RUN" }, "Paging yes" },
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




/// X-04 (test-programs.md §3.4): contention changes time only. Every case's fragment runs from one state twice,
/// with the `contention` feature on and off, from the case's first T-state; the registers, the RAM and the
/// paging latches must come out equal, and only the clock may differ. What an unused port reads is the byte
/// the video fetches at that moment (the floating bus), so it depends on time by definition: cases marked as
/// such (flags 4 and 32) are left out, and a case whose IN no device answers is checked for its port reads
/// only. The RET timed in ROM (flag 8) has no fragment. A boot per contended machine, then ~70 fragments of a few
/// instructions each: ~0.1 s per machine
class CtProbeTimeOnly_Test : public CtProbe_Test, public ::testing::WithParamInterface<Machine>
{
protected:
    struct Outcome
    {
        uint16_t af, bc, de, hl, af2, bc2, de2, hl2, ix, iy, sp, pc, ir, memptr;
        uint8_t rHi, iff1, iff2, im, p7FFD, p1FFD;
        std::vector<uint8_t> ram;
        uint32_t clocks;
        std::vector<std::pair<uint16_t, bool>> portReads;  // (port, answered by a device) of every IN
    };

    static constexpr uint16_t kSentinel = 0xFFF0;  // the fragment's RET returns here: the run ends

    Outcome RunFragment(const CaseRecord& r, uint16_t record, bool contention, const std::vector<uint8_t>& ram,
                        const Z80Registers& regs, uint8_t p7FFD, uint8_t p1FFD, uint32_t startT)
    {
        Z80* z80 = _context->pCore->GetZ80();
        Memory* memory = _context->pMemory;
        PortDecoder* ports = _context->pPortDecoder;
        const size_t ramBytes = ram.size();

        // The same machine state for both runs
        std::copy(ram.begin(), ram.end(), memory->RAMBase());
        static_cast<Z80Registers&>(*z80) = regs;
        ports->DecodePortOut(0x7FFD, p7FFD, 0);
        if (_context->config.mem_model == MM_PLUS3)
            ports->DecodePortOut(0x1FFD, p1FFD, 0);
        _context->emulatorState.p7FFD = p7FFD;
        _context->pCore->SetContentionSwitch(contention);

        // The fragment as the probe places it: its page at #C000, the bytes and a RET at the target, a mirror
        const uint16_t base7FFD = static_cast<uint16_t>(p7FFD & 0xD8);
        if (r.page != 0xFF)
            ports->DecodePortOut(0x7FFD, static_cast<uint8_t>(base7FFD | r.page), 0);
        const uint16_t target = r.target == 0 ? _probe.Sym("FRAGBUF") : r.target;
        const uint16_t src = PeekW(static_cast<uint16_t>(record + 6));
        for (uint8_t i = 0; i < r.length; i++)
            Poke(static_cast<uint16_t>(target + i), Peek(static_cast<uint16_t>(src + i)));
        Poke(static_cast<uint16_t>(target + r.length), 0xC9);
        const uint8_t mirror = Peek(static_cast<uint16_t>(record + 3));
        if (mirror != 0xFF)
        {
            const uint8_t mapped = _context->emulatorState.p7FFD;
            ports->DecodePortOut(0x7FFD, static_cast<uint8_t>(base7FFD | mirror), 0);
            for (uint16_t i = 0; i <= r.length; i++)
                Poke(static_cast<uint16_t>(_probe.Sym("FRAGBUF") + 0x4000 + i),
                     Peek(static_cast<uint16_t>(_probe.Sym("FRAGBUF") + i)));
            ports->DecodePortOut(0x7FFD, mapped, 0);
        }

        z80->ix = _probe.Sym("IXDATA");
        z80->sp = 0xBDFE;
        Poke(0xBDFE, kSentinel & 0xFF);
        Poke(0xBDFF, kSentinel >> 8);
        z80->pc = target;
        z80->iff1 = z80->iff2 = 0;
        z80->t = startT;
        // Every IN, and whether a device answered it (the hook runs right after the decoder; an unanswered
        // read gets the floating bus)
        std::vector<std::pair<uint16_t, bool>> portReads;
        z80->busTraceHook = [&](char kind, uint16_t port, uint8_t) {
            if (kind == 'I')
                portReads.emplace_back(port, ports->WasLastPortDecoded());
        };
        for (int steps = 0; z80->pc != kSentinel && steps < 10000; steps++)
            z80->Z80Step();
        z80->busTraceHook = nullptr;
        EXPECT_EQ(z80->pc, kSentinel) << r.name << ": the fragment did not return";

        Outcome o{ z80->af, z80->bc, z80->de, z80->hl, z80->alt.af, z80->alt.bc, z80->alt.de, z80->alt.hl,
                   z80->ix, z80->iy, z80->sp, z80->pc, z80->ir_, z80->memptr, z80->r_hi, z80->iff1, z80->iff2,
                   z80->im, _context->emulatorState.p7FFD, _context->emulatorState.p1FFD,
                   std::vector<uint8_t>(memory->RAMBase(), memory->RAMBase() + ramBytes), z80->t - startT,
                   portReads };
        return o;
    }
};

TEST_P(CtProbeTimeOnly_Test, ContentionChangesTimeOnly)
{
    const Machine& m = GetParam();
    BootEditor(m.editor);
    ASSERT_FALSE(HasFatalFailure());
    _probe = BuildProbe();
    ASSERT_FALSE(HasFailure());
    for (size_t i = 0; i < _probe.bytes.size(); i++)
        Poke(static_cast<uint16_t>(kOrg + i), _probe.bytes[i]);
    OpenPaging();
    // The mapping BASIC runs with, as the probe's detection stores it: the paging and layout fragments use it
    Poke(_probe.Sym("DEF7FFD"), _context->emulatorState.p7FFD);
    Poke(_probe.Sym("DEF1FFD"), _context->emulatorState.p1FFD);

    const uint8_t caps = m.caps;
    const size_t ramBytes = static_cast<size_t>(std::max<uint32_t>(_context->config.ramsize, 128)) * 1024;
    const std::vector<uint8_t> ram(_context->pMemory->RAMBase(), _context->pMemory->RAMBase() + ramBytes);
    const Z80Registers regs = *_context->pCore->GetZ80();
    const uint8_t p7FFD = _context->emulatorState.p7FFD;
    const uint8_t p1FFD = _context->emulatorState.p1FFD;
    const uint32_t intT = _context->config.intstart + 1;

    int compared = 0;
    int slower = 0;
    int floating = 0;
    uint16_t record = _probe.Sym("CASES");
    for (const CaseRecord& r : _probe.cases)
    {
        const uint16_t at = record;
        record = static_cast<uint16_t>(record + kRecord);
        if ((r.flags & (4 | 8 | 32)) != 0 || (r.flags & 3 & ~caps) != 0)
            continue;
        SCOPED_TRACE(r.name);
        // The case's first timed T-state (INT-relative), in the frame
        const uint32_t startT = intT + static_cast<uint32_t>(static_cast<int32_t>(m.onset) + r.offset);
        const Outcome on = RunFragment(r, at, true, ram, regs, p7FFD, p1FFD, startT);
        const Outcome off = RunFragment(r, at, false, ram, regs, p7FFD, p1FFD, startT);

        EXPECT_TRUE(on.portReads == off.portReads) << "the same ports, answered by the same devices";
        bool busRead = false;
        for (const auto& read : on.portReads)
            busRead = busRead || !read.second;
        if (busRead)
        {
            floating++;
            continue;
        }

        EXPECT_EQ(on.af, off.af);
        EXPECT_EQ(on.bc, off.bc);
        EXPECT_EQ(on.de, off.de);
        EXPECT_EQ(on.hl, off.hl);
        EXPECT_EQ(on.af2, off.af2);
        EXPECT_EQ(on.bc2, off.bc2);
        EXPECT_EQ(on.de2, off.de2);
        EXPECT_EQ(on.hl2, off.hl2);
        EXPECT_EQ(on.ix, off.ix);
        EXPECT_EQ(on.iy, off.iy);
        EXPECT_EQ(on.sp, off.sp);
        EXPECT_EQ(on.ir, off.ir);
        EXPECT_EQ(on.rHi, off.rHi);
        EXPECT_EQ(on.memptr, off.memptr);
        EXPECT_EQ(on.iff1, off.iff1);
        EXPECT_EQ(on.iff2, off.iff2);
        EXPECT_EQ(on.im, off.im);
        EXPECT_EQ(on.p7FFD, off.p7FFD);
        EXPECT_EQ(on.p1FFD, off.p1FFD);
        EXPECT_TRUE(on.ram == off.ram) << "RAM differs";
        EXPECT_GE(on.clocks, off.clocks) << "contention never makes code faster";
        compared++;
        slower += on.clocks > off.clocks ? 1 : 0;
    }
    std::printf("[ctprobe X-04 %s] %d fragments compared, %d slower with contention, %d read the floating bus\n",
                m.editor, compared, slower, floating);
    EXPECT_GT(compared, 30);
    EXPECT_GT(slower, 10) << "the contention feature did not act: the comparison proves nothing";
    _context->pCore->SetContentionSwitch(true);
}

INSTANTIATE_TEST_SUITE_P(Contended, CtProbeTimeOnly_Test,
                         ::testing::Values(Machines()[0], Machines()[1], Machines()[2]), MachineName);
