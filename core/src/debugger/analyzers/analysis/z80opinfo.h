#pragma once

/// @file z80opinfo.h
/// @brief What one Z80 instruction does, for the L1 replay tracer (docs/emulator/analysis/analysis-layers.md): its
/// length, its control flow, the registers it reads and writes, its memory operand and its immediate. One table per
/// prefix group, built once from the x/y/z/p/q fields of the opcode (the "Decoding Z80 opcodes" scheme), so a lookup
/// is two array reads. Undocumented forms (IXH / IXL, the DDCB register copy, SLL) are decoded; ED holes are 2-byte
/// NOPs, a DD / FD in front of another prefix is a 1-byte instruction of its own (as the emulator runs it).
///
/// @code
///   const analysis::OpInfo& op = analysis::DecodeOp(bytes);   // bytes: the 1..4 bytes at the instruction start
///   if (op.flow == analysis::Flow::Call) ...;
///   if (op.regsRead & analysis::reg::kA) ...;
/// @endcode

#include <cstdint>

namespace analysis
{

/// Register bits of OpInfo::regsRead / regsWritten
namespace reg
{
constexpr uint16_t kA = 1u << 0;
constexpr uint16_t kF = 1u << 1;
constexpr uint16_t kB = 1u << 2;
constexpr uint16_t kC = 1u << 3;
constexpr uint16_t kD = 1u << 4;
constexpr uint16_t kE = 1u << 5;
constexpr uint16_t kH = 1u << 6;
constexpr uint16_t kL = 1u << 7;
constexpr uint16_t kIXH = 1u << 8;
constexpr uint16_t kIXL = 1u << 9;
constexpr uint16_t kIYH = 1u << 10;
constexpr uint16_t kIYL = 1u << 11;
constexpr uint16_t kSP = 1u << 12;
constexpr uint16_t kI = 1u << 13;
constexpr uint16_t kR = 1u << 14;
constexpr uint16_t kAlt = 1u << 15;  ///< the alternate set (EX AF,AF' / EXX)

constexpr uint16_t kBC = kB | kC;
constexpr uint16_t kDE = kD | kE;
constexpr uint16_t kHL = kH | kL;
constexpr uint16_t kAF = kA | kF;
constexpr uint16_t kIX = kIXH | kIXL;
constexpr uint16_t kIY = kIYH | kIYL;
/// The registers a routine passes values in (live-in / live-out): everything but SP, I, R and the alternate set
constexpr uint16_t kValueRegs = kAF | kBC | kDE | kHL | kIX | kIY;

/// "A F B C D E H L IXH IXL IYH IYL SP I R" order; pairs are shown as pairs when both halves are set
const char* Name(int bit);
}  // namespace reg

enum class Flow : uint8_t
{
    None,        ///< falls through
    Jp,          ///< JP nn / JP cc,nn
    Jr,          ///< JR e / JR cc,e
    Djnz,        ///< DJNZ e
    Call,        ///< CALL nn / CALL cc,nn
    Rst,         ///< RST p
    Ret,         ///< RET / RET cc
    Reti,        ///< RETI
    Retn,        ///< RETN (and the ED RETN mirrors)
    JpIndirect,  ///< JP (HL) / JP (IX) / JP (IY)
    Halt,        ///< HALT
};

/// The memory operand's addressing
enum class MemForm : uint8_t
{
    None,
    HL,     ///< (HL)
    BC,     ///< (BC)
    DE,     ///< (DE)
    IX,     ///< (IX+d)
    IY,     ///< (IY+d)
    Abs,    ///< (nn)
    Stack,  ///< PUSH / POP / CALL / RET / EX (SP),rr
    Block,  ///< LDI / CPI / INI / OUTI and their repeats: (HL), and (DE) for LDx
};

enum class OpClass : uint8_t
{
    Nop,
    Load,      ///< LD of any kind
    Alu,       ///< ADD ADC SUB SBC AND XOR OR, NEG, DAA, CPL, SCF, CCF, 16-bit ADD / ADC / SBC
    Compare,   ///< CP r / CP n / CP (HL)
    IncDec,    ///< INC / DEC
    Rotate,    ///< RLCA ... RRA, the CB rotates and shifts, RLD / RRD
    Bit,       ///< BIT / RES / SET
    Jump,      ///< JP / JR / DJNZ / JP (rr)
    Call,      ///< CALL / RST
    Ret,       ///< RET / RETI / RETN
    Stack,     ///< PUSH / POP / EX (SP),rr / LD SP,rr
    Io,        ///< IN / OUT
    Block,     ///< LDI ... OTDR
    Exchange,  ///< EX AF,AF' / EX DE,HL / EXX
    Control,   ///< DI / EI / IM / HALT
    Prefix,    ///< a DD / FD that is an instruction of its own (followed by another prefix)
};

/// Condition codes of OpInfo::cond (kAlways: unconditional)
constexpr uint8_t kAlways = 0xFF;

struct OpInfo
{
    uint8_t length = 1;               ///< bytes, prefixes and displacement included
    Flow flow = Flow::None;
    uint8_t cond = kAlways;           ///< 0 NZ, 1 Z, 2 NC, 3 C, 4 PO, 5 PE, 6 P, 7 M
    OpClass cls = OpClass::Nop;
    uint16_t regsRead = 0;
    uint16_t regsWritten = 0;
    MemForm mem = MemForm::None;
    bool memRead = false;
    bool memWrite = false;
    bool ioIn = false;
    bool ioOut = false;
    bool repeats = false;             ///< LDIR / CPIR / INIR / OTIR / LDDR / CPDR / INDR / OTDR
    uint8_t immBytes = 0;             ///< 1: n, 2: nn (an address operand of JP / CALL / LD (nn) included)
    uint8_t immOffset = 0;            ///< where the immediate starts in the instruction
    bool immIsAddress = false;        ///< the immediate is a jump / call target or an (nn) operand
    uint16_t immDest = 0;             ///< LD r,n / LD rr,nn: the register(s) the immediate goes to
    uint8_t dispOffset = 0;           ///< (IX+d) / (IY+d): where d is (0: none)
    uint8_t aluOp = 0xFF;             ///< 8-bit ALU: 0 ADD 1 ADC 2 SUB 3 SBC 4 AND 5 XOR 6 OR 7 CP (0xFF: none)
    uint16_t loadDest = 0;            ///< LD r,(mem) / LD rr,(nn) / POP rr / IN r,(C): the register(s) loaded
    uint16_t copyDest = 0;            ///< LD r,r': the destination register
    uint16_t copySrc = 0;             ///< LD r,r': the source register
    uint8_t rstTarget = 0;            ///< RST p: p
};

/// The prefix group of the instruction starting with @p bytes (4 bytes: what is there, pad with anything)
const OpInfo& DecodeOp(const uint8_t* bytes);

/// "load", "alu", "compare", ...
const char* OpClassName(OpClass cls);
const char* FlowName(Flow flow);

}  // namespace analysis
