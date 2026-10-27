#include "portreadwatch.h"

namespace
{
/// The length of the instruction at @p address (the documented and undocumented opcodes; DD / FD take the HL forms'
/// lengths plus the displacement of an (IX+d) operand)
unsigned InstructionLength(PortReadCodeReader read, const void* context, uint16_t address)
{
    const uint8_t op = read(context, address);
    auto plain = [](uint8_t code) -> unsigned {
        switch (code)
        {
            case 0x01: case 0x11: case 0x21: case 0x31:  // LD rr,nn
            case 0x22: case 0x2A: case 0x32: case 0x3A:  // LD (nn),HL / A and back
            case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
            case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
                return 3;
            case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:  // LD r,n
            case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:                        // DJNZ, JR
            case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:  // ALU n
            case 0xD3: case 0xDB: case 0xCB:
                return 2;
            default:
                return 1;
        }
    };
    if (op == 0xED)
    {
        const uint8_t next = read(context, static_cast<uint16_t>(address + 1));
        return (next & 0xC7) == 0x43 ? 4 : 2;  // LD (nn),rr / LD rr,(nn)
    }
    if (op == 0xDD || op == 0xFD)
    {
        const uint8_t next = read(context, static_cast<uint16_t>(address + 1));
        if (next == 0xCB)
            return 4;
        if (next == 0xDD || next == 0xFD || next == 0xED)
            return 1;  // a prefix that does nothing: the next one starts over
        const bool indexed = next == 0x34 || next == 0x35 || next == 0x36 ||
                             (next >= 0x40 && next < 0xC0 && next != 0x76 &&
                              ((next & 0x07) == 0x06 || (next & 0xF8) == 0x70));
        return 1 + plain(next) + (indexed ? 1u : 0u);
    }
    return plain(op);
}
}  // namespace

uint8_t TestedBitsAfterIn(PortReadCodeReader read, const void* context, uint16_t next)
{
    constexpr unsigned kA = 7;
    constexpr unsigned kNone = 8;
    // The register the IN loaded: IN A,(n) is DB n; IN r,(C) is ED 40 + 8r (r 6 is IN F,(C): flags only)
    unsigned target = kNone;
    if (read(context, static_cast<uint16_t>(next - 2)) == 0xDB)
        target = kA;
    else if (read(context, static_cast<uint16_t>(next - 2)) == 0xED)
    {
        const uint8_t op = read(context, static_cast<uint16_t>(next - 1));
        if ((op & 0xC7) == 0x40 && ((op >> 3) & 7) != 6)
            target = (op >> 3) & 7;
    }
    if (target == kNone)
        return 0;

    uint8_t bits = 0;
    unsigned shift = 0;          // right shifts of the value in A since the IN
    bool carryFromShift = false; // the last instruction shifted bit (shift - 1) into the carry
    unsigned held = target;      // the register that holds the value (A after LD A,r)
    uint16_t address = next;
    for (unsigned count = 0; count < 8; ++count)
    {
        const uint8_t op = read(context, address);
        const uint16_t operand = static_cast<uint16_t>(address + 1);
        const unsigned length = InstructionLength(read, context, address);
        bool shifted = false;
        if (op == 0xE6 && held == kA)  // AND n
            bits |= static_cast<uint8_t>(read(context, operand) << shift);
        else if (op == 0xCB)
        {
            const uint8_t cb = read(context, operand);
            const unsigned reg = cb & 7;
            if ((cb & 0xC0) == 0x40 && reg == held)  // BIT b,r
                bits |= static_cast<uint8_t>(1u << (((cb >> 3) & 7) + (held == kA ? shift : 0)));
            else if (reg == held && held == kA && (cb == 0x0F || cb == 0x1F || cb == 0x3F))  // RRC A, RR A, SRL A
                shifted = true;
            else if (reg == held)
                break;  // another shift or a change of the value: what follows tests something else
        }
        else if ((op == 0x0F || op == 0x1F) && held == kA)  // RRCA, RRA
            shifted = true;
        else if ((op & 0xF8) == 0x78 && (op & 7) == held && held != kA)  // LD A,r: the value goes to A
        {
            held = kA;
            shift = 0;
        }
        else if (((op & 0xC7) == 0x06 && ((op >> 3) & 7) == held) ||
                 (op >= 0x40 && op < 0x80 && op != 0x76 && ((op >> 3) & 7) == held && (op & 7) != held) ||
                 (held == kA && ((op >= 0x80 && op < 0xB8) || op == 0xC6 || op == 0xCE || op == 0xD6 || op == 0xDE ||
                                 op == 0xEE || op == 0xF6)))
            break;  // the register is loaded again or A computed: what follows tests something else
        else if (carryFromShift && (op == 0x30 || op == 0x38 || op == 0xD2 || op == 0xDA || op == 0xD0 ||
                                    op == 0xD8 || op == 0xD4 || op == 0xDC))  // JR / JP / RET / CALL on the carry
            bits |= static_cast<uint8_t>(1u << (shift - 1));
        // The scan follows the fall-through path: it stops where the code surely goes elsewhere, and at another IN
        if (op == 0x18 || op == 0xC3 || op == 0xE9 || op == 0xC9 || op == 0xCD || op == 0x76 || op == 0xDB ||
            (op & 0xC7) == 0xC7)
            break;
        if (op == 0xED)
        {
            const uint8_t ed = read(context, operand);
            if ((ed & 0xC7) == 0x40 || ed == 0x4D || ed == 0x45)
                break;
        }
        if (shifted)
            ++shift;
        carryFromShift = shifted;
        if (shift >= 8)
            break;
        address = static_cast<uint16_t>(address + length);
    }
    return bits;
}

const char* PortReadClassName(PortReadClass cls)
{
    static const char* const names[kPortReadClasses] = {"kempston", "kempston2", "fuller", "kmouse",
                                                        "row.fe",   "row.fd",    "row.fb", "row.f7",
                                                        "row.ef",   "row.df",    "row.bf", "row.7f",
                                                        "rows.multi", "ay.r14",  "nr.b2"};
    const size_t index = static_cast<size_t>(cls);
    return index < kPortReadClasses ? names[index] : "";
}

void PortReadWatch::SampleRowBits(unsigned row, uint16_t next)
{
    if (_sampledAt[row] != next)
    {
        _sampledAt[row] = next;
        _sampledBits[row] = TestedBitsAfterIn(_read, _readContext, next);
    }
    _frameRowBits[row] |= _sampledBits[row];
}

void PortReadWatch::EndFrame()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _ring[_frames % kPortReadWindow] = _frame;
    _ringRowBits[_frames % kPortReadWindow] = _frameRowBits;
    ++_frames;
    _frame.fill(0);
    _frameRowBits.fill(0);
}

void PortReadWatch::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& frame : _ring)
        frame.fill(0);
    for (auto& frame : _ringRowBits)
        frame.fill(0);
    _frames = 0;
    _frame.fill(0);
    _frameRowBits.fill(0);
    _sampledAt.fill(kNotSampled);  // a new program: its code is somewhere else
}

PortReadEvidence PortReadWatch::Evidence() const
{
    PortReadEvidence evidence;
    std::lock_guard<std::mutex> lock(_mutex);
    evidence.framesObserved = _frames;
    evidence.window = _frames < kPortReadWindow ? _frames : kPortReadWindow;
    std::array<uint32_t, kPortReadClasses> reads{};
    for (uint32_t i = 0; i < evidence.window; ++i)
    {
        for (size_t cls = 0; cls < kPortReadClasses; ++cls)
            if (const uint16_t count = _ring[i][cls]; count != 0)
            {
                ++evidence.framesWithReads[cls];
                reads[cls] += count;
            }
        for (size_t row = 0; row < evidence.rowBits.size(); ++row)
            evidence.rowBits[row] |= _ringRowBits[i][row];
    }
    if (evidence.window != 0)
        for (size_t cls = 0; cls < kPortReadClasses; ++cls)
            evidence.readsPerFrame[cls] = static_cast<float>(reads[cls]) / static_cast<float>(evidence.window);
    return evidence;
}
