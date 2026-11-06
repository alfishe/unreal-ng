#pragma once

// DeZog breakpoint conditions (the text of CMD_ADD_BREAKPOINT) in the core breakpoint-condition language.
//
// DeZog evaluates a condition itself on every pause (dzrpremote.ts checkConditionAndLog: Utility.evalExpression,
// a JavaScript eval after the registers, labels and numbers were replaced by their values) and continues silently
// when it is false. The same condition in the core only skips the false hits without a pause, so it has to be true
// whenever DeZog's is. Only the part of JavaScript whose value the core's 64-bit integers give the same is taken:
//
//   numbers    decimal, 0x / $ hex, digit-led hex with h, binary with b (lower case suffixes, as DeZog's
//              tokenizer); DeZog masks each with & 0xFFFF
//   names      the Z80 registers DeZog knows, any case: PC SP AF BC DE HL IX IY AF' BC' DE' HL' IR IM
//              A F B C D E H L IXH IXL IYH IYL A' F' B' C' D' E' H' L' I R
//   operators  ( ) ! ~ unary - +, * %, + -, << >> >>>, < <= > >=, == != === !==, & ^ |, && ||
//
// Refused (the breakpoint is then set without a condition and DeZog alone decides): labels and any other name
// (DeZog resolves them from the project), '/' (a fraction), ?:, assignments, ++ / --, characters, letter-led hex
// (it could be a label), a remainder by a value that can be zero (NaN: true in DeZog), bitwise operands outside
// 32 bits (JavaScript truncates them), values beyond 2^53, a shift by a count outside 0..31, a right shift of a
// value that can be negative, && / || used as a number (JavaScript gives one of the operands, the core 0 / 1),
// and strict equality between a truth and a number.

#include <string>

namespace dzrp
{
/// The core form (Unreal dialect, every operation in brackets) of a DeZog condition. False with the reason when
/// the condition uses something the core would answer differently (see above)
bool translateCondition(const std::string& condition, std::string& coreOut, std::string& reasonOut);
} // namespace dzrp
