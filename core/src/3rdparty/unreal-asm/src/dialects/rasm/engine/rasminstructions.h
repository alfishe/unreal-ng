#pragma once

// rasm's Z80 instruction layer as pure functions: a port of the instruction functions of rasm.c (_LD, _IN, _OUT, _EX,
// _ADD ... _SET, the `instruction[]` keyword table), of PushExpression (where a deferred operand goes and what `$` is for
// it), of the write rules of PopAllExpression (range checks, errors, warnings) and of the timing functions __GETNOP,
// __GETTICK, __GETSIZE and __DURATION. Byte-exact with the rasm binary: tests/rasminstructions_test.cpp checks every
// instruction form rasm accepts or rejects against testdata/rasm/instructions.tsv and timings.tsv, which
// tools/verification/unreal-asm/checks/rasminstrtable.py writes by assembling each form with rasm. Spec:
// docs/inprogress/2026-10-05-unreal-asm/research-rasm-instructions.md.
//
// Operand words: exactly what rasm's preprocessor hands to an instruction function. Upper case outside quotes, every
// blank outside quotes removed, split at the commas outside parentheses and quotes, `AF'` after a comma already turned
// into `AF`, `<<` / `>>` turned into `[` / `]`. Example: `ld a , ( ix + 5 )` arrives as "LD" {"A", "(IX+5)"}.
//
// Changes to the interface agreed in the brief (minimal, all additive):
// - Encoded::nops / Encoded::ticks: the NOP / T-state counters rasm's instruction function adds (ae->nop, ae->tick);
//   COUNTNOPS and TICKER read them. InstructionTiming() is a convenience over them.
// - EncoderOptions: rough (-m), noWarning (-w), insideLoopOrMacro (rasm's ae->ir || ae->iw || ae->imacro, changes the
//   `$` of the second half of LD rr,(IX+n) / LD (IX+n),rr).
// - ApplyFixup(): the write rule of each fixup kind (rasm's PopAllExpression), so the engine and the tests share it.
// - Duration(): rasm's DURATION() (GETNOP, or GETTICK once BUILDZX made the output a ZX one).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::rasm
{
/// Where and how the engine writes a deferred operand's value (mirrors rasm's enum e_expression)
enum class FixupKind : uint8_t
{
    J8,             ///< E_EXPRESSION_J8: relative jump, byte = value - $ - 2; error when outside -128..127
    V8At0,          ///< E_EXPRESSION_0V8: 8-bit data (DEFB); same write rule as V8
    V8,             ///< E_EXPRESSION_V8: 8-bit immediate; warning when outside -128..255, low byte written
    J16,            ///< E_EXPRESSION_J16: JP / CALL target; same write rule as V16
    V16,            ///< E_EXPRESSION_V16: 16-bit value, little endian; warning when outside -32768..65535
    V16At0,         ///< E_EXPRESSION_0V16: 16-bit data (DEFW); same write rule as V16
    V32At0,         ///< E_EXPRESSION_0V32: 32-bit data; warning when outside -2^31..2^32-1
    RealAmsdos,     ///< E_EXPRESSION_0VR: 5-byte Amstrad BASIC real (data directives: not written by ApplyFixup)
    RealMicrosoft,  ///< E_EXPRESSION_0VRMike: 5-byte Microsoft real (data directives: not written by ApplyFixup)
    Float24,        ///< E_EXPRESSION_F24: 3-byte float (DF24)
    IV8,            ///< E_EXPRESSION_IV8: index displacement d of (IX+d) / (IY+d); error when outside -128..127
    IV81,           ///< E_EXPRESSION_IV81: displacement + 1 (high half of LD rr,(IX+n) / LD (IX+n),rr)
    V8At3,          ///< E_EXPRESSION_3V8: the n of LD (IX+d),n; same write rule as V8
    IV16,           ///< E_EXPRESSION_IV16: 16-bit value after a prefix (ED 4B nn nn, DD 21 nn nn); rule of V16
    Rst,            ///< E_EXPRESSION_RST: the value picks the RST opcode (#00..#38 step 8), else error
    RstConditional, ///< E_EXPRESSION_RSTC: RST cc,#38 = JR cc,$+1: value must be #38, byte becomes #FF
    Im,             ///< E_EXPRESSION_IM: IM 0/1/2 -> second byte #46/#56/#5E, else error
    BitResSet       ///< E_EXPRESSION_BRS: bit number 0..7 added as value*8 to the opcode byte, else error
};

struct Fixup
{
    FixupKind kind = FixupKind::V8;
    /// Index in Encoded::bytes of the (first) byte the value goes to; for Rst / RstConditional / Im / BitResSet the
    /// byte the value modifies (the RST opcode, the JR displacement, the IM second byte, the CB-page opcode)
    uint32_t offset = 0;
    /// rasm's `$` for this expression, as an index in Encoded::bytes: the start of the (sub-)instruction the operand
    /// belongs to, with rasm's quirks kept (RstConditional: 1; BitResSet: the CB-page opcode position; the second
    /// half of LD rr,(IX+n) / LD (IX+n),rr: 0 unless EncoderOptions::insideLoopOrMacro). The J8 jump base is the same
    /// address.
    uint32_t instructionOffset = 0;
    /// The expression text to evaluate, as rasm evaluates it. The operand word unchanged, except for IV8 / IV81 where
    /// rasm patches the register away: "(IX+5)" -> "(0+5)", "(IY-3)" -> "(0-3)", "(IX)" -> "(0)" (rasm writes "%0", a
    /// binary zero, at the place of "IX"; "0" is the same number). Proximity references `_+` / `_-` (J8 / J16) are
    /// left to the engine.
    std::string expression;
    /// IV8 / IV81: the index prefix (#DD or #FD, for rasm's error text "(IX+n)" / "(IY+n)"); BitResSet: the base
    /// opcode (#40 BIT, #80 RES, #C0 SET plus the register); 0 otherwise
    uint8_t extra = 0;
};

struct Encoded
{
    std::vector<uint8_t> bytes;  ///< opcodes with the deferred operand bytes as 0
    std::vector<Fixup> fixups;
    std::string error;           ///< non-empty: rasm reports this error (its message text, no location, no newline)
    std::vector<std::string> warnings;
    int nops = 0;                ///< CPC NOPs rasm adds to its counter (ae->nop) for this statement
    int ticks = 0;               ///< Z80 T-states rasm adds to its counter (ae->tick) for this statement
};

/// Assembler state and command-line options the instruction layer depends on (rasm's defaults otherwise)
struct EncoderOptions
{
    /// Added before flooring a value (rasm's ae->rough): 0.5 by default, 0.0 with -m (Maxam style calculations).
    /// Used for the repetition counts (NOP n ...) and by ApplyFixup.
    double rough = 0.5;
    /// -w: no warnings (ApplyFixup truncation warnings)
    bool noWarning = false;
    /// The statement is assembled inside REPEAT / WHILE / a macro expansion (rasm's ae->ir || ae->iw || ae->imacro).
    /// Outside them rasm translates an operand word in place at its first PushExpression, so the second half of
    /// LD rr,(IX+n) / LD (IX+n),rr sees the `$` of the first half; inside them it gets its own.
    bool insideLoopOrMacro = false;
};

/// True when rasm treats the (upper-case) word as a Z80 instruction mnemonic (including the aliases and
/// super-instructions EXA, SL1, SRL8, SHL, SHR, OTD, OTI); directives (DEFB, API_SEND ...) are not instructions
bool IsInstruction(std::string_view mnemonic);

/// One statement as rasm encodes it. `mnemonic` upper case; `operands` the operand words (see the top of this file).
/// Super-instructions (PUSH BC,DE,HL; NOP 5; LD BC,DE; LD HL,(IX+n); SRL HL; RST Z,#38 ...) yield all their bytes and
/// fixups. Repetition counts (NOP n, LDI n, HALT n ...) are evaluated at once through `evaluateNow` (rasm's `$` for it:
/// the address of the statement); std::nullopt is reported as an error.
Encoded EncodeInstruction(std::string_view mnemonic, const std::vector<std::string>& operands, const EncoderOptions& options,
                          const std::function<std::optional<double>(const std::string& expression)>& evaluateNow);

struct FixupResult
{
    std::string error;    ///< rasm's error text, empty when none
    std::string warning;  ///< rasm's warning text without the "Warning: " prefix, empty when none
};

/// Writes a fixup's value into `bytes` the way rasm's PopAllExpression does: value = floor(value + rough),
/// `$` = instructionAddress + fixup.instructionOffset (instructionAddress: the address of bytes[0]).
/// `shownExpression`: the expression as rasm quotes it in the J8 / IV8 error texts, after its in-place translation of
/// the word (`$` -> the decimal address, 'c' -> #hex ...); empty: fixup.expression
FixupResult ApplyFixup(std::vector<uint8_t>& bytes, const Fixup& fixup, double value, uint32_t instructionAddress,
                       const EncoderOptions& options, std::string_view shownExpression = {});

/// Timing of one statement (for COUNTNOPS / TICKER): CPC NOPs and ZX T-states as the instruction function counts them,
/// and its size. Repetition counts must be plain numbers; known = false when rasm reports an error.
struct Timing
{
    int nops = 0;
    int ticks = 0;
    int size = 0;
    bool known = false;
};
Timing InstructionTiming(std::string_view mnemonic, const std::vector<std::string>& operands);

/// rasm's GETNOP('code') / GETTICK('code') / GETSIZE('code') / DURATION('code'): rasm's own simplified parser of the
/// code string (':' separated instructions), not the encoder; value as rasm returns it, first error text on failure
struct TimingSum
{
    int value = 0;
    std::string error;
};
TimingSum GetNop(std::string_view code);
TimingSum GetTick(std::string_view code);
TimingSum GetSize(std::string_view code);
/// DURATION: GETNOP, or GETTICK when the assembly is a ZX one (rasm's ae->forcezx, set by BUILDZX)
TimingSum Duration(std::string_view code, bool zxTicks);
}  // namespace unrealasm::rasm
