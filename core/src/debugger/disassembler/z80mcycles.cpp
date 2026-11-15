#include "z80mcycles.h"

namespace
{
using K = Z80BusCycle;

constexpr uint8_t kTimes[] = {4, 3, 3, 3, 4, 4, 0};  // per kind, Internal given by the caller

/// Builds both ways of an instruction at once: a step added to both, or to `met` / `notMet` only
class Builder
{
public:
    explicit Builder(Z80MCycles& out) : _out(out) {}

    void Both(K kind, uint8_t t = 0)
    {
        Add(_out.met, kind, t);
        Add(_out.notMet, kind, t);
    }
    void Met(K kind, uint8_t t = 0)
    {
        _out.conditional = true;
        Add(_out.met, kind, t);
    }
    void NotMet(K kind, uint8_t t = 0)
    {
        _out.conditional = true;
        Add(_out.notMet, kind, t);
    }

private:
    static void Add(Z80MCycleList& list, K kind, uint8_t t)
    {
        const uint8_t time = kind == K::Internal ? t : kTimes[static_cast<int>(kind)];
        list.total = static_cast<uint8_t>(list.total + time);
        // Internal T next to each other are one stretch with no bus cycle
        if (kind == K::Internal && list.count > 0 && list.cycles[list.count - 1].kind == K::Internal)
        {
            list.cycles[list.count - 1].t = static_cast<uint8_t>(list.cycles[list.count - 1].t + time);
            return;
        }
        if (list.count < Z80MCycleList::kMax)
            list.cycles[list.count++] = {kind, time};
    }

    Z80MCycles& _out;
};

/// The CB page after its opcode fetch(es); `indexed`: DD CB d op / FD CB d op, whose d and op are read as operands
void CbPage(Builder& b, uint8_t op, bool indexed)
{
    const bool bit = (op >> 6) == 1;
    if (indexed)
    {
        b.Both(K::OperandRead);  // d
        b.Both(K::OperandRead);  // op
        b.Both(K::Internal, 2);
        b.Both(K::MemoryRead);
        b.Both(K::Internal, 1);
        if (!bit)
            b.Both(K::MemoryWrite);
        return;
    }
    if ((op & 7) != 6)
        return;
    b.Both(K::MemoryRead);
    b.Both(K::Internal, 1);
    if (!bit)
        b.Both(K::MemoryWrite);
}

/// The ED page after its two opcode fetches; returns the instruction's operand bytes
int EdPage(Builder& b, uint8_t op)
{
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    if (x == 1)
    {
        switch (z)
        {
            case 0: b.Both(K::PortRead); return 0;   // IN r,(C)
            case 1: b.Both(K::PortWrite); return 0;  // OUT (C),r
            case 2: b.Both(K::Internal, 7); return 0;  // SBC / ADC HL,rp
            case 3:  // LD (nn),rp / LD rp,(nn)
                b.Both(K::OperandRead);
                b.Both(K::OperandRead);
                b.Both((y & 1) ? K::MemoryRead : K::MemoryWrite);
                b.Both((y & 1) ? K::MemoryRead : K::MemoryWrite);
                return 2;
            case 4: return 0;  // NEG
            case 5:            // RETN / RETI
                b.Both(K::MemoryRead);
                b.Both(K::MemoryRead);
                return 0;
            case 6: return 0;  // IM
            default:           // z == 7
                if (y < 4)
                    b.Both(K::Internal, 1);  // LD I,A / LD R,A / LD A,I / LD A,R
                else if (y < 6)
                {
                    b.Both(K::MemoryRead);  // RRD / RLD
                    b.Both(K::Internal, 4);
                    b.Both(K::MemoryWrite);
                }
                return 0;
        }
    }
    if (x == 2 && y >= 4 && z <= 3)
    {
        const bool repeat = y >= 6;
        switch (z)
        {
            case 0:  // LDI / LDD / LDIR / LDDR
                b.Both(K::MemoryRead);
                b.Both(K::MemoryWrite);
                b.Both(K::Internal, 2);
                break;
            case 1:  // CPI / CPD / CPIR / CPDR
                b.Both(K::MemoryRead);
                b.Both(K::Internal, 5);
                break;
            case 2:  // INI / IND / INIR / INDR
                b.Both(K::Internal, 1);
                b.Both(K::PortRead);
                b.Both(K::MemoryWrite);
                break;
            default:  // OUTI / OUTD / OTIR / OTDR
                b.Both(K::Internal, 1);
                b.Both(K::MemoryRead);
                b.Both(K::PortWrite);
                break;
        }
        if (repeat)
            b.Met(K::Internal, 5);
        return 0;
    }
    return 0;  // the rest of the page: a NOP of two fetches
}

/// The unprefixed page after the opcode fetch, or the DD / FD page (`indexed`: (HL) is (IX+d)). Returns the operand
/// bytes
int MainPage(Builder& b, uint8_t op, bool indexed)
{
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    // (IX+d) in place of (HL): the displacement and the 5 T that add it
    auto displaced = [&]() {
        b.Both(K::OperandRead);
        b.Both(K::Internal, 5);
    };

    if (x == 1)
    {
        if (y == 6 && z == 6)
            return 0;  // HALT
        if (z == 6 || y == 6)
        {
            if (indexed)
                displaced();
            b.Both(z == 6 ? K::MemoryRead : K::MemoryWrite);
            return indexed ? 1 : 0;
        }
        return 0;
    }
    if (x == 2)
    {
        if (z != 6)
            return 0;
        if (indexed)
            displaced();
        b.Both(K::MemoryRead);
        return indexed ? 1 : 0;
    }
    if (x == 0)
    {
        switch (z)
        {
            case 0:
                if (y == 2)  // DJNZ
                {
                    b.Both(K::Internal, 1);
                    b.Both(K::OperandRead);
                    b.Met(K::Internal, 5);
                    return 1;
                }
                if (y == 3)  // JR d
                {
                    b.Both(K::OperandRead);
                    b.Both(K::Internal, 5);
                    return 1;
                }
                if (y >= 4)  // JR cc,d
                {
                    b.Both(K::OperandRead);
                    b.Met(K::Internal, 5);
                    return 1;
                }
                return 0;  // NOP, EX AF,AF'
            case 1:
                if (q == 0)  // LD rp,nn
                {
                    b.Both(K::OperandRead);
                    b.Both(K::OperandRead);
                    return 2;
                }
                b.Both(K::Internal, 7);  // ADD HL,rp
                return 0;
            case 2:
            {
                const K data = q ? K::MemoryRead : K::MemoryWrite;
                if (p < 2)  // LD (BC),A / LD (DE),A / LD A,(BC) / LD A,(DE)
                {
                    b.Both(data);
                    return 0;
                }
                b.Both(K::OperandRead);
                b.Both(K::OperandRead);
                b.Both(data);
                if (p == 2)  // LD (nn),HL / LD HL,(nn)
                    b.Both(data);
                return 2;
            }
            case 3: b.Both(K::Internal, 2); return 0;  // INC / DEC rp
            case 4:
            case 5:  // INC / DEC r
                if (y != 6)
                    return 0;
                if (indexed)
                    displaced();
                b.Both(K::MemoryRead);
                b.Both(K::Internal, 1);
                b.Both(K::MemoryWrite);
                return indexed ? 1 : 0;
            case 6:  // LD r,n
                if (y != 6)
                {
                    b.Both(K::OperandRead);
                    return 1;
                }
                if (indexed)  // LD (IX+d),n: d, n, then the 2 T that add d
                {
                    b.Both(K::OperandRead);
                    b.Both(K::OperandRead);
                    b.Both(K::Internal, 2);
                    b.Both(K::MemoryWrite);
                    return 2;
                }
                b.Both(K::OperandRead);
                b.Both(K::MemoryWrite);
                return 1;
            default: return 0;  // the accumulator and flag group
        }
    }

    // x == 3
    switch (z)
    {
        case 0:  // RET cc
            b.Both(K::Internal, 1);
            b.Met(K::MemoryRead);
            b.Met(K::MemoryRead);
            return 0;
        case 1:
            if (q == 0 || p == 0)  // POP rp2, RET
            {
                b.Both(K::MemoryRead);
                b.Both(K::MemoryRead);
            }
            else if (p == 3)
                b.Both(K::Internal, 2);  // LD SP,HL
            return 0;                    // EXX, JP (HL)
        case 2:  // JP cc,nn
            b.Both(K::OperandRead);
            b.Both(K::OperandRead);
            return 2;
        case 3:
            switch (y)
            {
                case 0:  // JP nn
                    b.Both(K::OperandRead);
                    b.Both(K::OperandRead);
                    return 2;
                case 2:  // OUT (n),A
                case 3:  // IN A,(n)
                    b.Both(K::OperandRead);
                    b.Both(y == 2 ? K::PortWrite : K::PortRead);
                    return 1;
                case 4:  // EX (SP),HL
                    b.Both(K::MemoryRead);
                    b.Both(K::MemoryRead);
                    b.Both(K::Internal, 1);
                    b.Both(K::MemoryWrite);
                    b.Both(K::MemoryWrite);
                    b.Both(K::Internal, 2);
                    return 0;
                default: return 0;  // EX DE,HL, DI, EI (CB handled by the caller)
            }
        case 4:  // CALL cc,nn
            b.Both(K::OperandRead);
            b.Both(K::OperandRead);
            b.Met(K::Internal, 1);
            b.Met(K::MemoryWrite);
            b.Met(K::MemoryWrite);
            return 2;
        case 5:
            if (q == 0)  // PUSH rp2
            {
                b.Both(K::Internal, 1);
                b.Both(K::MemoryWrite);
                b.Both(K::MemoryWrite);
                return 0;
            }
            // CALL nn (the prefixes are handled by the caller)
            b.Both(K::OperandRead);
            b.Both(K::OperandRead);
            b.Both(K::Internal, 1);
            b.Both(K::MemoryWrite);
            b.Both(K::MemoryWrite);
            return 2;
        case 6:  // ALU A,n
            b.Both(K::OperandRead);
            return 1;
        default:  // RST
            b.Both(K::Internal, 1);
            b.Both(K::MemoryWrite);
            b.Both(K::MemoryWrite);
            return 0;
    }
}
}  // namespace

Z80MCycles Z80MCycleDecoder::Decode(const uint8_t* bytes, size_t size)
{
    Z80MCycles out;
    if (!bytes || size == 0)
        return out;
    Builder b(out);
    size_t needed = 1;
    uint8_t op = bytes[0];
    bool indexed = false;
    b.Both(K::OpcodeFetch);

    if (op == 0xDD || op == 0xFD)
    {
        if (size < 2)
            return out;
        const uint8_t next = bytes[1];
        // A prefix before another prefix does nothing but its fetch: an instruction of its own
        if (next == 0xDD || next == 0xFD)
        {
            out.length = 1;
            out.valid = true;
            return out;
        }
        indexed = true;
        needed = 2;
        op = next;
        b.Both(K::OpcodeFetch);
        if (op == 0xCB)
        {
            needed = 4;
            if (size < needed)
                return out;
            CbPage(b, bytes[3], true);
            out.length = 4;
            out.valid = true;
            return out;
        }
        if (op == 0xED)  // ED ignores the prefix: DD's fetch, then the ED instruction
        {
            indexed = false;
            if (size < 3)
                return out;
            b.Both(K::OpcodeFetch);
            needed = 3 + static_cast<size_t>(EdPage(b, bytes[2]));
            if (size < needed)
                return out;
            out.length = static_cast<uint8_t>(needed);
            out.valid = true;
            return out;
        }
    }
    else if (op == 0xCB || op == 0xED)
    {
        if (size < 2)
            return out;
        b.Both(K::OpcodeFetch);
        needed = 2;
        if (op == 0xCB)
            CbPage(b, bytes[1], false);
        else
            needed += static_cast<size_t>(EdPage(b, bytes[1]));
        if (size < needed)
            return out;
        out.length = static_cast<uint8_t>(needed);
        out.valid = true;
        return out;
    }

    needed += static_cast<size_t>(MainPage(b, op, indexed));
    if (size < needed)
        return out;
    out.length = static_cast<uint8_t>(needed);
    out.valid = true;
    return out;
}

Z80MCyclePosition Z80MCycleDecoder::Locate(const Z80MCycleList& list, uint32_t elapsedT)
{
    Z80MCyclePosition position;
    uint32_t start = 0;
    for (uint8_t i = 0; i < list.count; i++)
    {
        const uint32_t end = start + list.cycles[i].t;
        if (elapsedT < end)
        {
            position.inside = true;
            position.index = i;
            position.intoT = static_cast<uint8_t>(elapsedT - start);
            return position;
        }
        start = end;
    }
    position.index = list.count > 0 ? static_cast<uint8_t>(list.count - 1) : 0;
    return position;
}

const char* Z80MCycleDecoder::Name(Z80BusCycle kind)
{
    switch (kind)
    {
        case K::OpcodeFetch: return "ocf";
        case K::OperandRead: return "od";
        case K::MemoryRead: return "mr";
        case K::MemoryWrite: return "mw";
        case K::PortRead: return "pr";
        case K::PortWrite: return "pw";
        default: return "int";
    }
}

const char* Z80MCycleDecoder::Description(Z80BusCycle kind)
{
    switch (kind)
    {
        case K::OpcodeFetch: return "opcode fetch";
        case K::OperandRead: return "operand read";
        case K::MemoryRead: return "memory read";
        case K::MemoryWrite: return "memory write";
        case K::PortRead: return "port read";
        case K::PortWrite: return "port write";
        default: return "internal";
    }
}
