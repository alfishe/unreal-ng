/// @file ttdcausechain.cpp
/// @brief The cause chain's names, its source rules (design §4 of docs/inprogress/2026-11-02-ttd-cause-chain) and
/// the text form of an answer. The replay and the walk are TimeTravelController::QueryCauseChain's
/// (ttdcontrollercausechain.cpp).

#include "stdafx.h"
#include "ttdcausechain.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "debugger/analyzers/analysis/z80opinfo.h"

namespace ttd
{

namespace
{
constexpr size_t kRegCount = static_cast<size_t>(TTDCauseReg::Count);
using RegSet = uint32_t;   // a bit per TTDCauseReg

constexpr RegSet Bit(TTDCauseReg r)
{
    return RegSet(1) << static_cast<unsigned>(r);
}

const char* const kRegNames[kRegCount] = {"A",  "F",  "B",  "C",  "D",  "E",  "H",  "L",  "IXH", "IXL", "IYH", "IYL",
                                          "I",  "R",  "A'", "F'", "B'", "C'", "D'", "E'", "H'", "L'",  "SPH", "SPL"};

/// The decoder's register bits as register bytes (the alternate-set flag dropped: the exchanges are handled apart)
RegSet FromOpRegs(uint16_t bits)
{
    namespace reg = analysis::reg;
    RegSet s = 0;
    const struct
    {
        uint16_t bit;
        RegSet set;
    } map[] = {{reg::kA, Bit(TTDCauseReg::A)},     {reg::kF, Bit(TTDCauseReg::F)},     {reg::kB, Bit(TTDCauseReg::B)},
               {reg::kC, Bit(TTDCauseReg::C)},     {reg::kD, Bit(TTDCauseReg::D)},     {reg::kE, Bit(TTDCauseReg::E)},
               {reg::kH, Bit(TTDCauseReg::H)},     {reg::kL, Bit(TTDCauseReg::L)},     {reg::kIXH, Bit(TTDCauseReg::IXH)},
               {reg::kIXL, Bit(TTDCauseReg::IXL)}, {reg::kIYH, Bit(TTDCauseReg::IYH)}, {reg::kIYL, Bit(TTDCauseReg::IYL)},
               {reg::kSP, Bit(TTDCauseReg::SPH) | Bit(TTDCauseReg::SPL)},              {reg::kI, Bit(TTDCauseReg::I)},
               {reg::kR, Bit(TTDCauseReg::R)}};
    for (const auto& m : map)
        if (bits & m.bit)
            s |= m.set;
    return s;
}

/// The registers that form the memory operand's address (not the value)
RegSet AddressRegs(const analysis::OpInfo& op)
{
    namespace reg = analysis::reg;
    switch (op.mem)
    {
        case analysis::MemForm::HL:
            return FromOpRegs(reg::kHL);
        case analysis::MemForm::BC:
            return FromOpRegs(reg::kBC);
        case analysis::MemForm::DE:
            return FromOpRegs(reg::kDE);
        case analysis::MemForm::IX:
            return FromOpRegs(reg::kIX);
        case analysis::MemForm::IY:
            return FromOpRegs(reg::kIY);
        case analysis::MemForm::Stack:
            return FromOpRegs(reg::kSP);
        case analysis::MemForm::Block:
            return FromOpRegs(reg::kHL | reg::kDE);
        default:
            return 0;
    }
}

/// The 16-bit pair a register byte belongs to: its high and low bytes
void PairOf(TTDCauseReg r, TTDCauseReg& high, TTDCauseReg& low)
{
    switch (r)
    {
        case TTDCauseReg::A: case TTDCauseReg::F: high = TTDCauseReg::A; low = TTDCauseReg::F; return;
        case TTDCauseReg::B: case TTDCauseReg::C: high = TTDCauseReg::B; low = TTDCauseReg::C; return;
        case TTDCauseReg::D: case TTDCauseReg::E: high = TTDCauseReg::D; low = TTDCauseReg::E; return;
        case TTDCauseReg::H: case TTDCauseReg::L: high = TTDCauseReg::H; low = TTDCauseReg::L; return;
        case TTDCauseReg::IXH: case TTDCauseReg::IXL: high = TTDCauseReg::IXH; low = TTDCauseReg::IXL; return;
        case TTDCauseReg::IYH: case TTDCauseReg::IYL: high = TTDCauseReg::IYH; low = TTDCauseReg::IYL; return;
        case TTDCauseReg::SPH: case TTDCauseReg::SPL: high = TTDCauseReg::SPH; low = TTDCauseReg::SPL; return;
        default: high = low = r; return;
    }
}

bool IsLow(TTDCauseReg r)
{
    TTDCauseReg high, low;
    PairOf(r, high, low);
    return r == low && high != low;
}

/// An instruction's first bytes as the decoder groups them
struct Decoded
{
    const analysis::OpInfo* op = nullptr;
    uint8_t index = 0;    ///< 0, #DD or #FD
    bool ed = false;
    bool cb = false;
    uint8_t opcode = 0;   ///< the byte after the prefixes (DDCB: the last byte)
};

Decoded Decode(const uint8_t* bytes)
{
    Decoded d;
    d.op = &analysis::DecodeOp(bytes);
    size_t k = 0;
    if (bytes[0] == 0xDD || bytes[0] == 0xFD)
    {
        d.index = bytes[0];
        k = 1;
    }
    if (bytes[k] == 0xED && d.index == 0)
    {
        d.ed = true;
        d.opcode = bytes[1];
    }
    else if (bytes[k] == 0xCB)
    {
        d.cb = true;
        d.opcode = d.index ? bytes[3] : bytes[1];
    }
    else
        d.opcode = bytes[k];
    return d;
}

/// HL, IX or IY as the prefix chooses: the high and low bytes
void IndexPair(uint8_t index, TTDCauseReg& high, TTDCauseReg& low)
{
    high = index == 0xDD ? TTDCauseReg::IXH : index == 0xFD ? TTDCauseReg::IYH : TTDCauseReg::H;
    low = index == 0xDD ? TTDCauseReg::IXL : index == 0xFD ? TTDCauseReg::IYL : TTDCauseReg::L;
}

/// The plain register of an r field (0 B ... 5 L, 7 A; 6 is (HL))
TTDCauseReg R8(uint8_t field)
{
    static const TTDCauseReg map[8] = {TTDCauseReg::B, TTDCauseReg::C, TTDCauseReg::D, TTDCauseReg::E,
                                       TTDCauseReg::H, TTDCauseReg::L, TTDCauseReg::A, TTDCauseReg::A};
    return map[field & 7];
}

/// The exchanges: each byte written and the byte it came from
size_t Exchanges(const Decoded& d, TTDCauseReg* to, TTDCauseReg* from)
{
    using R = TTDCauseReg;
    size_t n = 0;
    auto swap = [&](R a, R b) {
        to[n] = a;
        from[n++] = b;
        to[n] = b;
        from[n++] = a;
    };
    if (d.ed || d.cb)
        return 0;
    if (d.opcode == 0x08 && d.index == 0)
    {
        swap(R::A, R::A2);
        swap(R::F, R::F2);
    }
    else if (d.opcode == 0xD9 && d.index == 0)
    {
        swap(R::B, R::B2);
        swap(R::C, R::C2);
        swap(R::D, R::D2);
        swap(R::E, R::E2);
        swap(R::H, R::H2);
        swap(R::L, R::L2);
    }
    else if (d.opcode == 0xEB)   // EX DE,HL (a DD / FD in front changes nothing)
    {
        swap(R::D, R::H);
        swap(R::E, R::L);
    }
    return n;
}

struct Events
{
    const TTDTraceEvent* begin = nullptr;
    const TTDTraceEvent* end = nullptr;
};

Events EventsOf(const TTDFrameTrace& trace, const TTDTraceInsn& in)
{
    Events e;
    e.begin = trace.events.data() + in.firstEvent;
    e.end = e.begin + in.eventCount;
    return e;
}

/// A read of the instruction's own bytes (opcode, displacement, immediate): not data
bool IsInstructionByte(const TTDTraceInsn& in, uint8_t length, uint16_t address)
{
    return static_cast<uint16_t>(address - in.pc) < length;
}

/// The registers instruction @p in wrote (the decoder's, the exchanges', SP of a taken call / return only)
RegSet WrittenRegs(const TTDFrameTrace& trace, size_t index)
{
    const TTDTraceInsn& in = trace.insns[index];
    if (in.interrupt)
        return Bit(TTDCauseReg::SPH) | Bit(TTDCauseReg::SPL);
    const Decoded d = Decode(in.bytes);
    TTDCauseReg to[12], from[12];
    const size_t n = Exchanges(d, to, from);
    if (n > 0)
    {
        RegSet s = 0;
        for (size_t k = 0; k < n; ++k)
            s |= Bit(to[k]);
        return s;
    }
    RegSet s = FromOpRegs(d.op->regsWritten);
    // A conditional call or return not taken leaves SP alone
    if (d.op->cond != analysis::kAlways &&
        (d.op->flow == analysis::Flow::Call || d.op->flow == analysis::Flow::Ret))
    {
        const uint8_t* after = trace.RegsAfter(index);
        if (after[size_t(TTDCauseReg::SPH)] == in.regs[size_t(TTDCauseReg::SPH)] &&
            after[size_t(TTDCauseReg::SPL)] == in.regs[size_t(TTDCauseReg::SPL)])
            s &= ~(Bit(TTDCauseReg::SPH) | Bit(TTDCauseReg::SPL));
    }
    return s;
}

TTDCauseInput RegInput(const TTDTraceInsn& in, TTDCauseReg r, bool followFlags)
{
    TTDCauseInput i;
    i.name = TTDCauseName::Reg(r);
    i.value = in.regs[static_cast<size_t>(r)];
    i.followed = followFlags || (r != TTDCauseReg::F && r != TTDCauseReg::F2);
    return i;
}

TTDCauseInput MemInput(const TTDTraceEvent& e)
{
    TTDCauseInput i;
    i.name = TTDCauseName::Mem(e.address, e.page, e.Flip());
    i.value = e.value;
    return i;
}

void AddRegs(std::vector<TTDCauseInput>& out, const TTDTraceInsn& in, RegSet regs, bool followFlags)
{
    for (size_t r = 0; r < kRegCount; ++r)
        if (regs & (RegSet(1) << r))
            out.push_back(RegInput(in, static_cast<TTDCauseReg>(r), followFlags));
}
}  // namespace

const char* TTDCauseRegName(TTDCauseReg reg)
{
    const size_t i = static_cast<size_t>(reg);
    return i < kRegCount ? kRegNames[i] : "?";
}

bool TTDCauseRegsByName(const std::string& name, std::vector<TTDCauseReg>& out)
{
    std::string n;
    for (char c : name)
        if (!std::isspace(static_cast<unsigned char>(c)))
            n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    out.clear();
    using R = TTDCauseReg;
    const struct
    {
        const char* name;
        R high;
        R low;
    } pairs[] = {{"AF", R::A, R::F},       {"BC", R::B, R::C},       {"DE", R::D, R::E},       {"HL", R::H, R::L},
                 {"IX", R::IXH, R::IXL},   {"IY", R::IYH, R::IYL},   {"SP", R::SPH, R::SPL},   {"AF'", R::A2, R::F2},
                 {"BC'", R::B2, R::C2},    {"DE'", R::D2, R::E2},    {"HL'", R::H2, R::L2}};
    for (const auto& p : pairs)
        if (n == p.name)
        {
            out = {p.high, p.low};
            return true;
        }
    for (size_t r = 0; r < kRegCount; ++r)
        if (n == kRegNames[r])
        {
            out = {static_cast<R>(r)};
            return true;
        }
    return false;
}

std::string TTDCauseNameText(const TTDCauseName& name)
{
    if (!name.memory)
        return TTDCauseRegName(name.reg);
    char buf[48];
    if (name.page == kPhysPageNone)
        std::snprintf(buf, sizeof(buf), "(#%04X ROM)", name.address);
    else if (SpaceOfPage(name.page) != TTDMemorySpace::Ram)
        std::snprintf(buf, sizeof(buf), "(#%04X %s %u)", name.address, SpaceName(SpaceOfPage(name.page)),
                      static_cast<unsigned>(name.page - (SpaceOfPage(name.page) == TTDMemorySpace::Vram ? kVramPageBase
                                                                                                    : kCachePageBase)));
    else
        std::snprintf(buf, sizeof(buf), "(#%04X RAM %u)", name.address, static_cast<unsigned>(name.page));
    return buf;
}

const char* TTDCauseKindName(TTDCauseKind kind)
{
    switch (kind)
    {
        case TTDCauseKind::Store: return "store";
        case TTDCauseKind::Load: return "load";
        case TTDCauseKind::Copy: return "copy";
        case TTDCauseKind::Computed: return "computed";
        case TTDCauseKind::Constant: return "constant";
        case TTDCauseKind::Port: return "port";
        case TTDCauseKind::Block: return "block";
        case TTDCauseKind::Modify: return "modify";
        case TTDCauseKind::Return: return "return";
        case TTDCauseKind::Interrupt: return "interrupt";
        case TTDCauseKind::Refresh: return "refresh";
        case TTDCauseKind::Initial: return "initial";
        case TTDCauseKind::Rom: return "rom";
        case TTDCauseKind::Device: return "device";
        case TTDCauseKind::Stop: return "stop";
    }
    return "?";
}

bool TTDCauseWrites(const TTDFrameTrace& trace, size_t index, const TTDCauseName& name)
{
    const TTDTraceInsn& in = trace.insns[index];
    if (name.memory)
    {
        const Events ev = EventsOf(trace, in);
        for (const TTDTraceEvent* e = ev.begin; e != ev.end; ++e)
            if (e->type == 'W' && e->address == name.address && e->page == name.page && e->Flip() == name.flip)
                return true;
        return false;
    }
    return (WrittenRegs(trace, index) & Bit(name.reg)) != 0;
}

uint32_t TTDCauseWrittenRegs(const TTDFrameTrace& trace, size_t index)
{
    return WrittenRegs(trace, index);
}

std::vector<TTDCauseOutput> TTDExplainInstruction(const TTDFrameTrace& trace, size_t index, bool followFlags)
{
    using R = TTDCauseReg;
    std::vector<TTDCauseOutput> outs;
    const TTDTraceInsn& in = trace.insns[index];
    const uint8_t* after = trace.RegsAfter(index);
    const Events ev = EventsOf(trace, in);

    // An interrupt acknowledge: its pushes are the interrupted PC; SP moved by two
    if (in.interrupt)
    {
        for (const TTDTraceEvent* e = ev.begin; e != ev.end; ++e)
            if (e->type == 'W')
            {
                TTDCauseOutput o;
                o.name = TTDCauseName::Mem(e->address, e->page, e->Flip());
                o.value = e->value;
                o.kind = TTDCauseKind::Interrupt;
                o.leafValue = in.pc;
                outs.push_back(o);
            }
        for (R r : {R::SPH, R::SPL})
        {
            TTDCauseOutput o;
            o.name = TTDCauseName::Reg(r);
            o.value = after[size_t(r)];
            o.kind = TTDCauseKind::Computed;
            AddRegs(o.inputs, in, Bit(R::SPH) | Bit(R::SPL), followFlags);
            outs.push_back(o);
        }
        return outs;
    }

    const Decoded d = Decode(in.bytes);
    const analysis::OpInfo& op = *d.op;
    const uint8_t length = op.length;

    std::vector<const TTDTraceEvent*> reads, writes, portIns;
    for (const TTDTraceEvent* e = ev.begin; e != ev.end; ++e)
    {
        if (e->type == 'R' && (op.memRead || op.cls == analysis::OpClass::Block) && !IsInstructionByte(in, length, e->address))
            reads.push_back(e);
        else if (e->type == 'W')
            writes.push_back(e);
        else if (e->type == 'I')
            portIns.push_back(e);
    }
    const RegSet addressRegs = AddressRegs(op);
    const RegSet valueRegs = FromOpRegs(op.regsRead) & ~addressRegs & ~Bit(R::I) & ~Bit(R::R);
    const bool rld = d.ed && (d.opcode == 0x67 || d.opcode == 0x6F);
    const bool pushOp = !d.ed && !d.cb && (d.opcode & 0xCF) == 0xC5;
    const bool popOp = !d.ed && !d.cb && (d.opcode & 0xCF) == 0xC1;
    const bool exSp = !d.ed && !d.cb && d.opcode == 0xE3;

    // The register pair a 16-bit store or load moves (PUSH / POP / LD (nn),rr / LD rr,(nn) / EX (SP),rr)
    auto movedPair = [&](R& high, R& low) -> bool {
        if (pushOp || popOp)
        {
            switch ((d.opcode >> 4) & 3)
            {
                case 0: high = R::B; low = R::C; return true;
                case 1: high = R::D; low = R::E; return true;
                case 2: IndexPair(d.index, high, low); return true;
                default: high = R::A; low = R::F; return true;
            }
        }
        if (exSp || (!d.ed && !d.cb && (d.opcode == 0x22 || d.opcode == 0x2A)))
        {
            IndexPair(d.index, high, low);
            return true;
        }
        if (d.ed && (d.opcode & 0xC7) == 0x43)   // ED 43 / 4B / 53 / 5B / 63 / 6B / 73 / 7B
        {
            switch ((d.opcode >> 4) & 3)
            {
                case 0: high = R::B; low = R::C; return true;
                case 1: high = R::D; low = R::E; return true;
                case 2: high = R::H; low = R::L; return true;
                default: high = R::SPH; low = R::SPL; return true;
            }
        }
        return false;
    };
    // Two bytes of one 16-bit access: the low half at the lower address (a pair at #FFFF wraps to #0000)
    auto lowerOf = [](const TTDTraceEvent* a, const TTDTraceEvent* b) {
        return static_cast<uint16_t>(b->address - a->address) == 1 ? a : b;
    };

    // Memory bytes written
    for (const TTDTraceEvent* w : writes)
    {
        TTDCauseOutput o;
        o.name = TTDCauseName::Mem(w->address, w->page, w->Flip());
        o.value = w->value;
        R high, low;
        if (op.flow == analysis::Flow::Call || op.flow == analysis::Flow::Rst)
        {
            o.kind = TTDCauseKind::Return;
            o.leafValue = static_cast<uint16_t>(in.pc + length);
        }
        else if (op.cls == analysis::OpClass::Block && op.ioIn)
        {
            o.kind = TTDCauseKind::Port;
            if (!portIns.empty())
                o.port = portIns.back()->address;
        }
        else if (op.cls == analysis::OpClass::Block)
        {
            o.kind = TTDCauseKind::Block;
            const TTDTraceEvent* source = nullptr;
            for (const TTDTraceEvent* r : reads)
                if (r < w)
                    source = r;
            if (source)
                o.inputs.push_back(MemInput(*source));
        }
        else if (movedPair(high, low) && writes.size() == 2)
        {
            o.kind = TTDCauseKind::Store;
            const TTDTraceEvent* lower = lowerOf(writes[0], writes[1]);
            o.inputs.push_back(RegInput(in, w == lower ? low : high, followFlags));
        }
        else if (op.memRead && std::any_of(reads.begin(), reads.end(),
                                           [&](const TTDTraceEvent* r) { return r->address == w->address; }))
        {
            // Read, changed and written back in place
            o.kind = TTDCauseKind::Modify;
            for (const TTDTraceEvent* r : reads)
                if (r->address == w->address)
                {
                    o.inputs.push_back(MemInput(*r));
                    break;
                }
            if (rld)
                o.inputs.push_back(RegInput(in, R::A, followFlags));
            if (op.regsRead & analysis::reg::kF)
                o.inputs.push_back(RegInput(in, R::F, followFlags));
        }
        else if (op.immBytes == 1 && !op.immIsAddress && op.cls == analysis::OpClass::Load)
            o.kind = TTDCauseKind::Constant;   // LD (HL),n / LD (IX+d),n
        else if (!d.ed && !d.cb && d.opcode >= 0x70 && d.opcode <= 0x77)
        {
            o.kind = TTDCauseKind::Store;      // LD (HL),r / LD (IX+d),r: r is the plain register, H and L too
            o.inputs.push_back(RegInput(in, R8(d.opcode & 7), followFlags));
        }
        else
        {
            const RegSet source = valueRegs & ~Bit(R::F) & ~Bit(R::SPH) & ~Bit(R::SPL);
            o.kind = op.cls == analysis::OpClass::Load ? TTDCauseKind::Store : TTDCauseKind::Computed;
            AddRegs(o.inputs, in, source, followFlags);
            for (const TTDTraceEvent* r : reads)
                o.inputs.push_back(MemInput(*r));
        }
        outs.push_back(std::move(o));
    }

    // Registers written
    const RegSet written = WrittenRegs(trace, index);
    TTDCauseReg exTo[12], exFrom[12];
    const size_t exchanges = Exchanges(d, exTo, exFrom);
    for (size_t ri = 0; ri < kRegCount; ++ri)
    {
        if (!(written & (RegSet(1) << ri)))
            continue;
        const R r = static_cast<R>(ri);
        TTDCauseOutput o;
        o.name = TTDCauseName::Reg(r);
        o.value = after[ri];
        R high, low;
        PairOf(r, high, low);
        const RegSet rBit = Bit(r);

        bool done = false;
        for (size_t k = 0; k < exchanges && !done; ++k)
            if (exTo[k] == r)
            {
                o.kind = TTDCauseKind::Copy;
                o.inputs.push_back(RegInput(in, exFrom[k], true));
                done = true;
            }
        if (done)
        {
        }
        else if ((!d.ed && !d.cb && (d.opcode == 0xAF || d.opcode == 0x97) && d.index == 0))
            o.kind = TTDCauseKind::Constant;   // XOR A / SUB A: zero whatever A held
        else if (d.ed && d.opcode == 0x5F && r == R::A)
            o.kind = TTDCauseKind::Refresh;    // LD A,R
        else if (d.ed && d.opcode == 0x57 && r == R::A)
        {
            o.kind = TTDCauseKind::Copy;       // LD A,I
            o.inputs.push_back(RegInput(in, R::I, true));
        }
        else if (d.ed && (d.opcode == 0x47 || d.opcode == 0x4F))
        {
            o.kind = TTDCauseKind::Copy;       // LD I,A / LD R,A
            o.inputs.push_back(RegInput(in, R::A, true));
        }
        else if (!d.ed && !d.cb && d.opcode == 0xF9 && (r == R::SPH || r == R::SPL))
        {
            R ih, il;
            IndexPair(d.index, ih, il);
            o.kind = TTDCauseKind::Copy;       // LD SP,HL / IX / IY
            o.inputs.push_back(RegInput(in, r == R::SPH ? ih : il, true));
        }
        else if (op.copyDest && (FromOpRegs(op.copyDest) & rBit))
        {
            o.kind = TTDCauseKind::Copy;       // LD r,r'
            AddRegs(o.inputs, in, FromOpRegs(op.copySrc), true);
        }
        else if (op.immDest && (FromOpRegs(op.immDest) & rBit))
            o.kind = TTDCauseKind::Constant;   // LD r,n / LD rr,nn
        else if (op.ioIn && op.cls == analysis::OpClass::Io && op.loadDest && (FromOpRegs(op.loadDest) & rBit))
        {
            o.kind = TTDCauseKind::Port;       // IN r,(C) / IN A,(n)
            if (!portIns.empty())
                o.port = portIns.back()->address;
        }
        else if (op.loadDest && (FromOpRegs(op.loadDest) & rBit) && !reads.empty())
        {
            o.kind = TTDCauseKind::Load;       // LD r,(mem), LD rr,(nn), POP rr
            if (reads.size() >= 2 && high != low)
            {
                const TTDTraceEvent* lower = lowerOf(reads[0], reads[1]);
                const TTDTraceEvent* upper = lower == reads[0] ? reads[1] : reads[0];
                o.inputs.push_back(MemInput(*(IsLow(r) ? lower : upper)));
            }
            else
                o.inputs.push_back(MemInput(*reads.back()));
        }
        else if (exSp && reads.size() == 2)
        {
            o.kind = TTDCauseKind::Load;       // EX (SP),rr: the register half from the stack
            const TTDTraceEvent* lower = lowerOf(reads[0], reads[1]);
            const TTDTraceEvent* upper = lower == reads[0] ? reads[1] : reads[0];
            o.inputs.push_back(MemInput(*(IsLow(r) ? lower : upper)));
        }
        else if ((!d.ed && !d.cb && (d.opcode & 0xC7) == 0x03) || (addressRegs & rBit))
        {
            // INC rr / DEC rr, a pointer moved (LDI's HL / DE, POP's SP, CALL's SP): from itself; the carry goes up
            // only, so the low byte is from the low byte alone
            o.kind = TTDCauseKind::Computed;
            AddRegs(o.inputs, in, IsLow(r) ? Bit(low) : Bit(high) | Bit(low), followFlags);
        }
        else
        {
            o.kind = TTDCauseKind::Computed;   // ALU, INC / DEC, rotates, bits, DJNZ's B, a counter
            AddRegs(o.inputs, in, valueRegs, followFlags);
            for (const TTDTraceEvent* rd : reads)
                o.inputs.push_back(MemInput(*rd));
        }
        outs.push_back(std::move(o));
    }
    return outs;
}

std::string TTDCauseChainText(const TTDCauseResult& result)
{
    if (!result.error.empty())
        return "cause chain: " + result.error;
    std::string text;
    char buf[160];
    for (size_t i = 0; i < result.links.size(); ++i)
    {
        const TTDCauseLink& l = result.links[i];
        text.append(2 * l.depth, ' ');
        std::snprintf(buf, sizeof(buf), "%s = #%02X  %s", TTDCauseNameText(l.name).c_str(), l.value,
                      TTDCauseKindName(l.kind));
        text += buf;
        if (l.hasProducer)
        {
            std::snprintf(buf, sizeof(buf), "  f%llu+%u #%04X %s", static_cast<unsigned long long>(l.time.frame),
                          static_cast<unsigned>(l.time.tInFrame), l.pc, l.text.c_str());
            text += buf;
        }
        if (!l.inputs.empty())
        {
            text += "  <- ";
            for (size_t k = 0; k < l.inputs.size(); ++k)
                text += (k ? ", " : "") + l.inputs[k];
        }
        if (!l.detail.empty())
            text += "  (" + l.detail + ")";
        if (!l.stop.empty())
            text += "  [stop: " + l.stop + "]";
        text += "\n";
    }
    std::snprintf(buf, sizeof(buf), "%zu links, %u frames traced, %.1f ms\n", result.links.size(), result.framesTraced,
                  result.ms);
    text += buf;
    return text;
}

}  // namespace ttd
