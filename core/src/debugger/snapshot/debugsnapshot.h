#pragma once

// The debugger snapshot (docs/inprogress/2026-10-04-debugger-snapshot/tdd.md §4): one coherent picture of the machine
// for a debugger front end, and the builders its parts share with the separate WebAPI endpoints, so GET /registers,
// GET /disasm and the snapshot cannot drift apart.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
struct Z80State;

namespace DebugSnapshot
{
/// The main Z80's registers: the GET /registers object (main, alternate, index, special, interrupt, flags)
StateNode Registers(EmulatorContext* context);
/// The same object for a saved register set (the snapshot's prev_regs)
StateNode RegistersOf(const Z80State& state);

/// `count` (1..100) instructions from `address` in the CPU view: the GET /disasm object (address, count,
/// instructions[] with address, bytes, mnemonic, size, label, target, targetLabel, displacement, effectiveAddress,
/// effectiveAddressLabel). Stops at the 64K wrap. {available: false, description} without a disassembler
StateNode Disasm(EmulatorContext* context, uint16_t address, size_t count);

/// What a snapshot request asks for beyond the small part (seq ... time)
struct Options
{
    unsigned disasm = 0;               ///< lines from PC (0 = none, at most 100)
    unsigned stack = 8;                ///< words from SP (0 = none, at most 128)
    std::vector<std::string> memory;   ///< windows "<space>:<address>:<length>", at most 8, each at most 65536
    bool rawBytes = false;             ///< memory windows carry "bytes" (the raw bytes as a string) instead of "base64"
};
constexpr unsigned kMaxStackWords = 128;
constexpr size_t kMaxWindows = 8;

struct Result
{
    StateNode snapshot;   ///< the answer (tdd §4.1)
    std::string error;    ///< non-empty: refused (`busy` false: a bad request; true: no coherent moment in time)
    bool busy = false;
};

/// Check the options without taking a snapshot: empty = valid, else the reason
std::string Validate(const Options& options);

/// Standard base64 (RFC 4648, with padding): the snapshot's memory windows
std::string Base64(const std::vector<uint8_t>& bytes);

/// The whole snapshot at one moment: read while the emulator stays parked ("paused"), at the next frame boundary of a
/// running machine ("frame"), or directly when it does not run ("stopped"). Memory windows carry their bytes as base64
Result Build(Emulator* emulator, const Options& options);
}  // namespace DebugSnapshot
