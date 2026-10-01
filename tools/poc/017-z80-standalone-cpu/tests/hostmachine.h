// hostmachine.h - minimal host for the standalone Z80 library (PoC 017).
//
// Flat 64K RAM machine with:
//  - RST 0x10 character capture (the ZX ROM print entry): every call to
//    0x0010 captures A into an output string and behaves like RET
//  - TAP tape code-block loading (for ZEXALL/ZEXDOC verification runs)
//  - a simple run loop with a T-state budget and done-predicate

#ifndef Z80POC_HOSTMACHINE_H
#define Z80POC_HOSTMACHINE_H

#include <cstdint>
#include <string>
#include <vector>

#include "z80cpu.h"

struct TapCodeBlock
{
    std::string name;        // block name from the header
    uint16_t startAddress;   // param1 of the type-3 header
    std::vector<uint8_t> data;
};

class HostMachine
{
public:
    uint8_t mem[0x10000];
    Z80CPU* cpu;
    std::string output;  // characters captured from RST 0x10
    bool romErrorHit = false;  // RST 0x08 executed (ZEX programs: test failure)
    uint8_t romErrorCode = 0;
    uint64_t instructions = 0;

    HostMachine();
    ~HostMachine();
    HostMachine(const HostMachine&) = delete;
    HostMachine& operator=(const HostMachine&) = delete;

    // Zero RAM, reset CPU (register state = post-reset; T=3).
    void Reset();

    void LoadCode(uint16_t addr, const uint8_t* data, size_t len);

    // One instruction with soft-ROM interception: RST 0x10 captures the
    // character in A; CALL 0x1601 (ZX ROM "open channel") returns;
    // RST 0x08 records the error code byte that follows the opcode and
    // returns. Returns the instruction's T-states.
    int StepCapture();

private:
    // Emulate RET for a soft-ROM entry (pops PC from the emulated stack).
    void SoftRomReturn();

public:

    // Run until predicate() returns true (called at every instruction
    // boundary), PC == breakPc, or the T-state budget is exhausted.
    // Returns T-states consumed.
    uint64_t Run(uint64_t maxTstates, bool (*predicate)(HostMachine&, void*), void* userData,
                 uint16_t breakPc = 0xFFFF);

    // Convenience: run until the captured output contains marker or budget hit.
    uint64_t RunUntilOutput(const std::string& marker, uint64_t maxTstates);

    // --- TAP loading ---
    // Loads a .tap file and returns all type-3 (CODE) blocks.
    static bool LoadTapCodeBlocks(const std::string& path, std::vector<TapCodeBlock>& blocks);

    // Loads the first CODE block of the tape into RAM at its start address
    // and returns the execution entry (the start address).
    bool LoadTapProgram(const std::string& path, uint16_t& entry);

    // --- ZEXALL-family runner ---
    // Loads the given tape, runs it capturing output, and reports whether
    // the test program printed "SUCCEEDED" (prints the captured text).
    bool RunZex(const std::string& tapPath, uint64_t maxTstates, bool verbose);
};

#endif  // Z80POC_HOSTMACHINE_H
